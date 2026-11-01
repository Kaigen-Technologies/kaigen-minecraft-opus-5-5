#include "voxaria/vx_game.h"

// the world map: one texel per column, revealed as chunks load (Terraria's explored map), kept for the whole world

#define VX_MAP_CHUNKS (VX_MAP_SIZE / VX_CHUNK)
#define VX_MAP_UPLOADS 12 // chunk regions refreshed per frame at most

void vx_map_init(VxMap *m, Allocator *alloc, const VxTextureData *tex) {
  m->px = ALLOC_ARRAY(alloc, u8, VX_MAP_SIZE * VX_MAP_SIZE * 4);
  m->stamp = ALLOC_ARRAY(alloc, u32, VX_MAP_CHUNKS * VX_MAP_CHUNKS);
  m->colors = ALLOC_ARRAY(alloc, u32, B_COUNT);
  m->tex = gpu_make_texture(&(GpuTextureDesc){.data = m->px, .size = VX_MAP_SIZE * VX_MAP_SIZE * 4,
                                              .data_type = GPU_TEXTURE_DATA_RGBA8, .width = VX_MAP_SIZE,
                                              .height = VX_MAP_SIZE, .is_srgb = true});
  // each block's colour is the mean of its top face's first variant
  for (u32 id = 1; id < B_COUNT; id++) {
    const u8 *p = tex->pixels + vx_tex_layer[vx_face_tex[id][VX_FACE_PY]] * 256 * 4;
    u32 r = 0, g = 0, b = 0, n = 0;
    for (u32 i = 0; i < 256; i++) {
      if (p[i * 4 + 3] < 128) continue;
      r += p[i * 4], g += p[i * 4 + 1], b += p[i * 4 + 2], n++;
    }
    if (n) m->colors[id] = (r / n) << 16 | (g / n) << 8 | (b / n);
  }
}

void vx_map_clear(VxMap *m) {
  mem_zero(m->px, VX_MAP_SIZE * VX_MAP_SIZE * 4);
  mem_zero(m->stamp, sizeof(u32) * VX_MAP_CHUNKS * VX_MAP_CHUNKS);
  m->upload_all = true;
}

// the column's surface: the first block under its full sky light
force_inline i32 vx_map_surface(const VxChunk *c, i32 lx, i32 lz) { return MAX((i32)c->h15[(lz << 4) | lx] - 1, 0); }

// the surface of any loaded column, or fallback where it is not loaded
hz_internal i32 vx_map_height(const VxWorld *w, i32 x, i32 z, i32 fallback) {
  const VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  return c && c->state == VX_CHUNK_LIT ? vx_map_surface(c, x & 15, z & 15) : fallback;
}

hz_internal void vx_map_chunk(VxMap *m, const VxWorld *w, const VxChunk *c) {
  i32 x0 = c->cx * VX_CHUNK, z0 = c->cz * VX_CHUNK;
  u32 mx = (u32)(x0 + VX_WORLD_HALF), mz = (u32)(z0 + VX_WORLD_HALF);
  for (i32 lz = 0; lz < VX_CHUNK; lz++)
    for (i32 lx = 0; lx < VX_CHUNK; lx++) {
      i32 x = x0 + lx, z = z0 + lz, y = vx_map_surface(c, lx, lz);
      u8 id = vx_world_block(w, x, y, z);
      // plants and torches show the ground they stand on
      while (y > 0 && (id == B_AIR || vx_shape[id] == VX_SHAPE_CROSS || vx_shape[id] == VX_SHAPE_TORCH))
        id = vx_world_block(w, x, --y, z);
      u32 col = m->colors[id < B_COUNT ? id : B_STONE];
      // relief: lit from the north-west, by the height step to that neighbour
      i32 hn = lz > 0 ? vx_map_surface(c, lx, lz - 1) : vx_map_height(w, x, z - 1, y);
      i32 hw = lx > 0 ? vx_map_surface(c, lx - 1, lz) : vx_map_height(w, x - 1, z, y);
      f32 k = 1.0f + m_clampf((f32)((y - hn) + (y - hw)) * 0.08f, -0.3f, 0.3f);
      if (vx_shape[id] == VX_SHAPE_LIQUID) {
        // deeper water reads darker
        i32 d = 0;
        while (d < 12 && vx_shape[vx_world_block(w, x, y - d - 1, z)] == VX_SHAPE_LIQUID) d++;
        k = 1.05f - (f32)d * 0.04f;
      }
      u8 *o = m->px + ((mz + (u32)lz) * VX_MAP_SIZE + mx + (u32)lx) * 4;
      o[0] = (u8)m_minf(255, ((col >> 16) & 255) * k);
      o[1] = (u8)m_minf(255, ((col >> 8) & 255) * k);
      o[2] = (u8)m_minf(255, (col & 255) * k);
      o[3] = 255;
    }
}

void vx_map_update(VxMap *m, const VxWorld *w, VxV3d around) {
  i32 pcx = vx_floor_i(around.x) >> 4, pcz = vx_floor_i(around.z) >> 4, r = w->render_distance + 1;
  u32 uploads = 0;
  // a cleared map goes up whole, from inside the frame
  if (m->upload_all) {
    gpu_update_texture_region(m->tex, 0, 0, VX_MAP_SIZE, VX_MAP_SIZE, m->px);
    m->upload_all = false;
  }
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u8 *region = ALLOC_ARRAY(&tmp.allocator, u8, VX_CHUNK * VX_CHUNK * 4);
  // nearest rings first, so the minimap fills in from the player outwards
  for (i32 ring = 0; ring <= r && uploads < VX_MAP_UPLOADS; ring++)
    for (i32 dz = -ring; dz <= ring && uploads < VX_MAP_UPLOADS; dz++)
      for (i32 dx = -ring; dx <= ring && uploads < VX_MAP_UPLOADS; dx++) {
        if (MAX(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != ring) continue;
        i32 cx = pcx + dx, cz = pcz + dz;
        if (!vx_in_world(cx * VX_CHUNK, cz * VX_CHUNK)) continue;
        const VxChunk *c = vx_world_chunk(w, cx, cz);
        if (!c || c->state != VX_CHUNK_LIT) continue;
        u32 stamp = 1;
        for (u32 s = 0; s < VX_SECTIONS; s++) stamp = stamp * 31u + (u32)c->sec_version[s] * 7u + c->light_version[s];
        u32 *slot = &m->stamp[(cz + VX_MAP_CHUNKS / 2) * VX_MAP_CHUNKS + cx + VX_MAP_CHUNKS / 2];
        if (*slot == stamp) continue;
        *slot = stamp;
        vx_map_chunk(m, w, c);
        u32 mx = (u32)(cx * VX_CHUNK + VX_WORLD_HALF), mz = (u32)(cz * VX_CHUNK + VX_WORLD_HALF);
        for (u32 row = 0; row < VX_CHUNK; row++)
          mem_cpy(region + row * VX_CHUNK * 4, m->px + ((mz + row) * VX_MAP_SIZE + mx) * 4, VX_CHUNK * 4);
        gpu_update_texture_region(m->tex, mx, mz, VX_CHUNK, VX_CHUNK, region);
        uploads++;
      }
  tctx_temp_allocator_end(tmp);
}

#undef VX_MAP_CHUNKS
#undef VX_MAP_UPLOADS

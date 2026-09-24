#include "webcraft/wc.h"

// one WC_QUAD_U32-word record per quad (wc_quad); shaders/wc_quad.glsl derives its corners

#define WC_FACES 6
#define WC_CORNERS 4
hz_internal const i32 WC_FACE_OFF[WC_FACES] = {1, -1, 324, -324, 18, -18};
// per face corner: side1, side2, diagonal neighbour offsets in the padded volume
hz_internal const i32 WC_AO_OFF[WC_FACES][WC_CORNERS][3] = {
    {{-323, 19, -305}, {-323, -17, -341}, {325, -17, 307}, {325, 19, 343}},
    {{-325, -19, -343}, {-325, 17, -307}, {323, 17, 341}, {323, -19, 305}},
    {{342, 323, 341}, {342, 325, 343}, {306, 325, 307}, {306, 323, 305}},
    {{-306, -323, -305}, {-306, -325, -307}, {-342, -325, -343}, {-342, -323, -341}},
    {{17, -306, -307}, {19, -306, -305}, {19, 342, 343}, {17, 342, 341}},
    {{-17, -342, -341}, {-19, -342, -343}, {-19, 306, 305}, {-17, 306, 307}},
};

#define WC_WHITE 0xFFFFFFu
#define WC_BIRCH_TINT (128u | (168u << 8) | (87u << 16))
#define WC_SPRUCE_TINT (92u | (143u << 8) | (97u << 16))

void wc_mesh_scratch_init(WcMeshScratch *m, Allocator *alloc) {
  m->col_grass = ALLOC_ARRAY(alloc, u32, 256);
  m->col_foliage = ALLOC_ARRAY(alloc, u32, 256);
  m->col_water = ALLOC_ARRAY(alloc, u32, 256);
  m->opaque = ALLOC_ARRAY_NO_ZERO(alloc, u32, WC_MAX_SECTION_QUADS * WC_QUAD_U32);
  m->trans = ALLOC_ARRAY_NO_ZERO(alloc, u32, WC_MAX_SECTION_QUADS * WC_QUAD_U32);
  m->sort = ALLOC_ARRAY_NO_ZERO(alloc, u32, WC_MAX_SECTION_QUADS * WC_QUAD_U32);
}


typedef struct {
  u32 *data;
  u32 count; // quads
} WcVerts;

typedef enum { WC_QUAD_CUBE, WC_QUAD_CROSS, WC_QUAD_TORCH, WC_QUAD_LIQUID } WcQuadShape;

#define WC_Q_CROSS_DIAG (1u << 21)
#define WC_Q_CROSS_BACK (1u << 22)
#define WC_Q_CROSS_WAVE (1u << 23)
#define WC_Q_CROSS_CENTRED (1u << 24)
#define WC_Q_LIQUID_FULL (1u << 21)

// ao, sky and bl hold one value per corner in corner order (ao 2 bits, light 8 bits)
force_inline void wc_quad(WcVerts *mb, i32 x, i32 y, i32 z, u32 face, u32 shape, u32 bits, u32 tex_flags, u32 ao,
                          u32 sky, u32 bl, u32 tint, u32 extra1, u32 extra4) {
  u32 *d = mb->data + mb->count * WC_QUAD_U32;
  d[0] = (u32)x | ((u32)z << 4) | ((u32)y << 8) | (face << 16) | (shape << 19) | bits;
  d[1] = tex_flags | (ao << 16) | (extra1 << 24);
  d[2] = sky;
  d[3] = bl;
  d[4] = tint | (extra4 << 24);
  mb->count++;
}

force_inline u32 wc_splat4(u32 v) { return v * 0x01010101u; }

force_inline u32 wc_quad_bucket(const u32 *q) {
  u32 face = (q[0] >> 16) & 7;
  if (((q[0] >> 19) & 3) != WC_QUAD_CUBE) return WC_BUCKET_OTHER;
  return ((q[1] >> 10) & 63) ? WC_BUCKET_FLAGGED + face : face;
}

// stable counting sort of n quads by bucket into tmp, which it returns
hz_internal u32 *wc_sort_buckets(u32 *data, u32 *tmp, u32 n, u16 *counts) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u32 *off = ALLOC_ARRAY_NO_ZERO(&ta.allocator, u32, WC_BUCKETS);
  for (u32 b = 0; b < WC_BUCKETS; b++) counts[b] = 0;
  for (u32 q = 0; q < n; q++) counts[wc_quad_bucket(data + q * WC_QUAD_U32)]++;
  u32 sum = 0;
  for (u32 b = 0; b < WC_BUCKETS; b++) {
    off[b] = sum;
    sum += counts[b];
  }
  for (u32 q = 0; q < n; q++) {
    const u32 *src = data + q * WC_QUAD_U32;
    u32 *dst = tmp + off[wc_quad_bucket(src)]++ * WC_QUAD_U32;
    for (u32 i = 0; i < WC_QUAD_U32; i++) dst[i] = src[i];
  }
  tctx_temp_allocator_end(ta);
  return tmp;
}

hz_internal u32 wc_pack_rgb(v3 c) {
  i32 r = (i32)(m_clampf(c.x, 0, 1) * 255.0f + 0.5f);
  i32 g = (i32)(m_clampf(c.y, 0, 1) * 255.0f + 0.5f);
  i32 b = (i32)(m_clampf(c.z, 0, 1) * 255.0f + 0.5f);
  return (u32)r | ((u32)g << 8) | ((u32)b << 16);
}

hz_internal void wc_column_tints(WcMeshScratch *m) {
  for (i32 z = 0; z < 16; z++) {
    for (i32 x = 0; x < 16; x++) {
      u32 t = 0, h = 0;
      for (i32 dz = 0; dz < 3; dz++) {
        for (i32 dx = 0; dx < 3; dx++) {
          i32 i = ((z + dz) * WC_PAD + (x + dx)) * 2;
          t += m->climate[i];
          h += m->climate[i + 1];
        }
      }
      f64 tf = t / (9.0 * 255.0), hf = h / (9.0 * 255.0);
      i32 k = z * 16 + x;
      m->col_grass[k] = wc_pack_rgb(wc_grass_color(tf, hf));
      m->col_foliage[k] = wc_pack_rgb(wc_foliage_color(tf, hf));
      m->col_water[k] = wc_pack_rgb(wc_water_color(tf, hf));
    }
  }
}

force_inline u32 wc_tint_for(const WcMeshScratch *m, u8 id, i32 x, i32 z) {
  switch (wc_tint[id]) {
  case WC_TINT_GRASS: return m->col_grass[z * 16 + x];
  case WC_TINT_FOLIAGE: return m->col_foliage[z * 16 + x];
  case WC_TINT_WATER: return m->col_water[z * 16 + x];
  case WC_TINT_BIRCH: return WC_BIRCH_TINT;
  case WC_TINT_SPRUCE: return WC_SPRUCE_TINT;
  default: return WC_WHITE;
  }
}

force_inline b32 wc_cull_same(u8 id) {
  return id == B_GLASS || id == B_ICE || id == B_WATER || id == B_LAVA;
}

hz_internal void wc_emit_cross(WcMeshScratch *m, WcVerts *mb, u8 id, i32 x, i32 y,
                               i32 z, i32 p, i32 ox, i32 oz) {
  u8 l = m->light[p];
  u32 sky = (u32)(l >> 4) * 17, blk = (u32)(l & 15) * 17;
  u32 tint = wc_tint_for(m, id, x, z);
  u32 bits = wc_wave[id] == WC_WAVE_PLANT ? WC_Q_CROSS_WAVE : 0;
  u32 hx = 0, hz = 0;
  if (id == B_SUGAR_CANE) {
    bits |= WC_Q_CROSS_CENTRED;
  } else {
    u32 h = wc_hash2i(ox + x, oz + z, 0x51f3);
    hx = h & 255;
    hz = (h >> 8) & 255;
  }
  u32 tb = wc_face_tex[id][0] | (WC_VF_CUTOUT << 10);
  // bottom corners ao 2, top corners ao 3
  u32 ao = 2u | (2u << 2) | (3u << 4) | (3u << 6);
  for (u32 q = 0; q < 2; q++) {
    u32 diag = q ? WC_Q_CROSS_DIAG : 0;
    wc_quad(mb, x, y, z, WC_FACE_PLANT, WC_QUAD_CROSS, bits | diag, tb, ao, wc_splat4(sky), wc_splat4(blk), tint,
            hx, hz);
    wc_quad(mb, x, y, z, WC_FACE_PLANT, WC_QUAD_CROSS, bits | diag | WC_Q_CROSS_BACK, tb, ao, wc_splat4(sky),
            wc_splat4(blk), tint, hx, hz);
  }
}

hz_internal void wc_emit_torch(WcMeshScratch *m, WcVerts *mb, u8 id, i32 x, i32 y,
                               i32 z, i32 p) {
  u8 l = m->light[p];
  u32 sky = (u32)(l >> 4) * 17, blk = (u32)(l & 15) * 17;
  u32 tex = wc_face_tex[id][0] | (WC_VF_CUTOUT << 10);
  for (u32 f = 0; f < WC_FACES; f++) {
    if (f == WC_FACE_NY) continue; // bottom is never seen
    wc_quad(mb, x, y, z, f, WC_QUAD_TORCH, 0, tex, 0xff, wc_splat4(sky), wc_splat4(blk), WC_WHITE, 0, 0);
  }
}

hz_internal void wc_emit_liquid(WcMeshScratch *m, WcVerts *opaque, WcVerts *trans,
                                u8 id, i32 x, i32 y, i32 z, i32 p) {
  b32 is_water = id == B_WATER;
  WcVerts *mb = is_water ? trans : opaque;
  u32 flags = is_water ? WC_VF_WATER : WC_VF_LAVA;
  u8 above = m->blocks[p + WC_PAD2];
  u32 bits = above == id ? WC_Q_LIQUID_FULL : 0;
  u32 tint = wc_tint_for(m, id, x, z);
  for (i32 f = 0; f < WC_FACES; f++) {
    i32 np = p + WC_FACE_OFF[f];
    u8 n = m->blocks[np];
    if (n == id) continue;
    if (wc_occludes[n]) continue;
    if (f == WC_FACE_PY && n == B_ICE) continue;
    if (f != WC_FACE_PY && f != WC_FACE_NY && is_water && n == B_ICE) continue;
    u8 l = m->light[np];
    u32 sky = (u32)(l >> 4) * 17, blk = (u32)(l & 15) * 17;
    u32 tex_flags = wc_face_tex[id][f] | (flags << 10);
    wc_quad(mb, x, y, z, (u32)f, WC_QUAD_LIQUID, bits, tex_flags, 0xff, wc_splat4(sky), wc_splat4(blk), tint, 0,
            0);
  }
}

void wc_mesh_section(WcMeshScratch *m, i32 ox, i32 oy, i32 oz) {
  WcVerts opaque = {.data = m->opaque};
  WcVerts trans = {.data = m->trans};
  wc_column_tints(m);
  const u8 *blocks = m->blocks;
  const u8 *light = m->light;
  u32 ao[WC_CORNERS], sk[WC_CORNERS], bl[WC_CORNERS];

  for (i32 y = 0; y < 16; y++) {
    for (i32 z = 0; z < 16; z++) {
      i32 p = (y + 1) * WC_PAD2 + (z + 1) * WC_PAD + 1;
      for (i32 x = 0; x < 16; x++, p++) {
        u8 id = blocks[p];
        if (id == B_AIR) continue;
        u8 shape = wc_shape[id];
        if (shape == WC_SHAPE_CUBE) {
          u8 layer = wc_layer[id];
          WcVerts *mb = layer == WC_LAYER_TRANSLUCENT ? &trans : &opaque;
          u32 flags = 0;
          if (layer == WC_LAYER_CUTOUT) flags |= WC_VF_CUTOUT;
          if (wc_wave[id] == WC_WAVE_LEAVES) flags |= WC_VF_WAVE_LEAF;
          if (id == B_ICE) flags |= WC_VF_ICE;
          u32 tint = wc_tint_for(m, id, x, z);
          for (i32 f = 0; f < WC_FACES; f++) {
            i32 np = p + WC_FACE_OFF[f];
            u8 n = blocks[np];
            if (wc_occludes[n]) continue;
            if (n == id && wc_cull_same(id)) continue;
            u8 fl = light[np];
            for (i32 c = 0; c < 4; c++) {
              const i32 *o = WC_AO_OFF[f][c];
              u32 s1 = wc_occludes[blocks[p + o[0]]];
              u32 s2 = wc_occludes[blocks[p + o[1]]];
              u32 cc = (s1 && s2) ? 1 : wc_occludes[blocks[p + o[2]]];
              ao[c] = (s1 && s2) ? 0 : 3 - (s1 + s2 + cc);
              u32 ss = fl >> 4, bb = fl & 15, cnt = 1;
              if (!s1) {
                u8 lv = light[p + o[0]];
                ss += lv >> 4;
                bb += lv & 15;
                cnt++;
              }
              if (!s2) {
                u8 lv = light[p + o[1]];
                ss += lv >> 4;
                bb += lv & 15;
                cnt++;
              }
              if (!cc) {
                u8 lv = light[p + o[2]];
                ss += lv >> 4;
                bb += lv & 15;
                cnt++;
              }
              // round(avg * 17), halves up
              sk[c] = (ss * 34 + cnt) / (2 * cnt);
              bl[c] = (bb * 34 + cnt) / (2 * cnt);
            }
            u32 tex_flags = wc_face_tex[id][f] | (flags << 10);
            wc_quad(mb, x, y, z, (u32)f, WC_QUAD_CUBE, 0, tex_flags,
                    ao[0] | (ao[1] << 2) | (ao[2] << 4) | (ao[3] << 6),
                    sk[0] | (sk[1] << 8) | (sk[2] << 16) | (sk[3] << 24),
                    bl[0] | (bl[1] << 8) | (bl[2] << 16) | (bl[3] << 24), tint, 0, 0);
          }
        } else if (shape == WC_SHAPE_CROSS) {
          wc_emit_cross(m, &opaque, id, x, y, z, p, ox, oz);
        } else if (shape == WC_SHAPE_LIQUID) {
          wc_emit_liquid(m, &opaque, &trans, id, x, y, z, p);
        } else if (shape == WC_SHAPE_TORCH) {
          wc_emit_torch(m, &opaque, id, x, y, z, p);
        }
      }
    }
  }
  // positions become relative to the column base so a column draws in one call
  u32 add = (u32)oy << 8;
  for (u32 q = 0; q < opaque.count; q++) opaque.data[q * WC_QUAD_U32] += add;
  for (u32 q = 0; q < trans.count; q++) trans.data[q * WC_QUAD_U32] += add;
  m->opaque_quads = opaque.count;
  m->trans_quads = trans.count;
  u32 *sorted = wc_sort_buckets(m->opaque, m->sort, m->opaque_quads, m->buckets[0]);
  m->sort = m->opaque;
  m->opaque = sorted;
  sorted = wc_sort_buckets(m->trans, m->sort, m->trans_quads, m->buckets[1]);
  m->sort = m->trans;
  m->trans = sorted;
}

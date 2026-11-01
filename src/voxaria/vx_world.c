#include "voxaria/vx.h"

#define VX_NO_RENDER 0xFFFFFFFFu
#define VX_MAX_JOBS 4096
#define VX_WANTED_SPAN (2 * (VX_MAX_RENDER_DISTANCE + 3) + 1)
#define VX_WANTED_MAX (VX_WANTED_SPAN * VX_WANTED_SPAN)
#define VX_MIN_ROUND_MS 0.5f
#define VX_ROUND_MS 4.0f      // near-first rounds stay short so light and mesh jobs follow generation quickly
#define VX_LOOKAHEAD 3.0f     // near-first streaming runs this many chunks ahead of the nearest unmeshed one
#define VX_EST_ALPHA (1.0f / 32.0f)

force_inline u32 vx_cell(i32 cx, i32 cz) {
  return (u32)((cz & VX_GRID_MASK) * VX_GRID + (cx & VX_GRID_MASK));
}

VxChunk *vx_world_chunk(const VxWorld *w, i32 cx, i32 cz) {
  u16 slot = w->grid[vx_cell(cx, cz)];
  if (slot == VX_NO_SLOT) return NULL;
  VxChunk *c = &w->chunks[slot];
  return (c->cx == cx && c->cz == cz && c->in_use) ? c : NULL;
}

// the chunk's pylon cells, for the gems the entity pass floats over them
hz_internal void vx_chunk_find_pylons(VxChunk *c) {
  c->pylon_count = 0;
  for (u32 i = 0; i < VX_CHUNK_VOLUME && c->pylon_count < VX_CHUNK_PYLONS; i++)
    if (c->blocks[i] == B_PYLON) c->pylons[c->pylon_count++] = (u16)i;
}

force_inline u16 vx_slot_of(const VxWorld *w, const VxChunk *c) {
  return (u16)(c - w->chunks);
}

// ---- chunk slots ----

hz_internal VxChunk *vx_chunk_alloc(VxWorld *w, i32 cx, i32 cz) {
  if (w->free_count == 0) return NULL;
  u16 slot = w->free_slots[--w->free_count];
  VxChunk *c = &w->chunks[slot];
  u8 *blocks = c->blocks;
  u16 *light = c->light;
  u16 *meta0 = c->parts[0].meta, *meta1 = c->parts[1].meta;
  VxGenState *gen_states = c->gen_states;
  if (!blocks) {
    gen_states = ALLOC_ARRAY_NO_ZERO(&w->alloc, VxGenState, VX_GEN_STATES);
    blocks = ALLOC_ARRAY_NO_ZERO(&w->alloc, u8, VX_CHUNK_VOLUME);
    light = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, VX_CHUNK_VOLUME);
    meta0 = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, VX_SECTIONS * VX_SECTION_META);
    meta1 = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, VX_SECTIONS * VX_SECTION_META);
  }
  memzero_struct(c);
  c->blocks = blocks;
  c->light = light;
  c->gen_states = gen_states;
  c->parts[0].meta = meta0;
  c->parts[1].meta = meta1;
  c->cx = cx;
  c->cz = cz;
  c->uid = ++w->next_uid;
  c->in_use = true;
  c->state = VX_CHUNK_GENERATING;
  for (i32 s = 0; s < VX_SECTIONS; s++) {
    c->sec_meshed[s] = -1;
    c->sec_pending[s] = -1;
  }
  c->parts[0].pool = VX_NO_POOL;
  c->parts[1].pool = VX_NO_POOL;
  c->render_index = VX_NO_RENDER;
  w->grid[vx_cell(cx, cz)] = slot;
  w->stats.chunks++;
  return c;
}

hz_internal void vx_renderable_update(VxWorld *w, VxChunk *c) {
  b32 has_mesh = c->parts[0].pool != VX_NO_POOL || c->parts[1].pool != VX_NO_POOL;
  if (has_mesh && c->render_index == VX_NO_RENDER) {
    c->render_index = w->renderable_count;
    w->renderables[w->renderable_count++] = vx_slot_of(w, c);
  } else if (!has_mesh && c->render_index != VX_NO_RENDER) {
    u32 last = --w->renderable_count;
    u16 moved = w->renderables[last];
    w->renderables[c->render_index] = moved;
    w->chunks[moved].render_index = c->render_index;
    c->render_index = VX_NO_RENDER;
  }
}

hz_internal void vx_note_mesh_change(VxWorld *w, const VxChunk *c) {
  if (w->mesh_change_count >= VX_MAX_CHUNKS) {
    w->mesh_changes_overflow = true;
    return;
  }
  w->mesh_changes[w->mesh_change_count * 2] = c->cx;
  w->mesh_changes[w->mesh_change_count * 2 + 1] = c->cz;
  w->mesh_change_count++;
}

void vx_world_clear_mesh_changes(VxWorld *w) {
  w->mesh_change_count = 0;
  w->mesh_changes_overflow = false;
}

// ---- per-voxel state pages (main lane) ----

hz_internal u8 *vx_state_page(const VxWorld *w, const VxChunk *c, i32 sy) {
  return c->state_page[sy] ? w->state_pages[c->state_page[sy] - 1] : NULL;
}

hz_internal u8 *vx_state_page_make(VxWorld *w, VxChunk *c, i32 sy) {
  if (c->state_page[sy]) return w->state_pages[c->state_page[sy] - 1];
  u16 idx;
  if (w->state_free_count) {
    idx = w->state_free[--w->state_free_count];
  } else if (w->state_page_count < VX_STATE_PAGES) {
    idx = (u16)w->state_page_count++;
    w->state_pages[idx] = ALLOC_ARRAY_NO_ZERO(&w->state_alloc, u8, VX_SECTION_VOLUME);
  } else {
    log_warn("vx_world: all % state pages in use; a state edit is dropped", fmt_u32(VX_STATE_PAGES));
    return NULL;
  }
  u8 *page = w->state_pages[idx];
  mem_zero(page, VX_SECTION_VOLUME);
  c->state_page[sy] = (u16)(idx + 1);
  return page;
}

hz_internal void vx_state_pages_release(VxWorld *w, VxChunk *c) {
  for (i32 sy = 0; sy < VX_SECTIONS; sy++) {
    if (!c->state_page[sy]) continue;
    w->state_free[w->state_free_count++] = (u16)(c->state_page[sy] - 1);
    c->state_page[sy] = 0;
  }
}

hz_internal void vx_chunk_free(VxWorld *w, VxChunk *c) {
  if (c->render_index != VX_NO_RENDER) vx_note_mesh_change(w, c);
  vx_mesh_store_free(w->meshes, &c->parts[0]);
  vx_mesh_store_free(w->meshes, &c->parts[1]);
  vx_renderable_update(w, c);
  u32 cell = vx_cell(c->cx, c->cz);
  if (w->grid[cell] == vx_slot_of(w, c)) w->grid[cell] = VX_NO_SLOT;
  vx_state_pages_release(w, c);
  c->in_use = false;
  c->urgent_mask = 0;
  w->free_slots[w->free_count++] = vx_slot_of(w, c);
  w->stats.chunks--;
}

hz_internal void vx_lanes_init(VxWorld *w) {
  w->lane_count = tctx_current()->thread_count;
  w->lanes = ALLOC_ARRAY(&w->alloc, VxLane, w->lane_count);
  for (u32 i = 0; i < w->lane_count; i++) {
    VxLane *l = &w->lanes[i];
    l->arena = arena_create(MB(64), KB(256));
    l->alloc = make_arena_allocator(l->arena);
    l->gen = ALLOC(&l->alloc, VxGenScratch);
    l->mesh = ALLOC(&l->alloc, VxMeshScratch);
    vx_mesh_scratch_init(l->mesh, &l->alloc);
    vx_light_init(&l->light, &l->alloc);
    l->far_blocks = ALLOC_ARRAY_NO_ZERO(&l->alloc, u8, VX_CHUNK_VOLUME);
    l->results = ALLOC_ARRAY_NO_ZERO(&l->alloc, u32, VX_LANE_RESULT_U32);
  }
}

// the world arena's lifetime bound: tables, noise, lanes and every touched slot's storage (ios cannot reserve 2 GB)
hz_internal u64 vx_world_arena_bytes(u32 lane_count) {
  u64 slot_storage = 3 * (u64)VX_CHUNK_VOLUME + 2 * sizeof(u16) * VX_SECTIONS * VX_SECTION_META;
  u64 per_slot = sizeof(VxChunk) + 3 * sizeof(u16) + sizeof(u32) + sizeof(u16) + 2 * sizeof(i32);
  u64 tables = (u64)VX_MAX_CHUNKS * (per_slot + slot_storage) + VX_WANTED_MAX * sizeof(VxWanted) +
               2 * VX_MAX_JOBS * sizeof(VxJob) + lane_count * sizeof(VxLane) +
               VX_STATE_PAGES * (sizeof(u8 *) + sizeof(u16)) + 16 * 1024;
  // alignment padding of every allocation above, rounded to whole megabytes
  return (tables + (4 * (u64)VX_MAX_CHUNKS + 64) * 64 + MB(1) - 1) & ~(u64)(MB(1) - 1);
}

void vx_world_init(VxWorld *w, u32 seed, VxEditStore *edits, VxMeshStore *meshes) {
  memzero_struct(w);
  w->arena = arena_create(vx_world_arena_bytes(tctx_current()->thread_count), MB(4));
  w->alloc = make_arena_allocator(w->arena);
  w->edits = edits;
  w->meshes = meshes;
  w->render_distance = 12;
  w->chunks = ALLOC_ARRAY(&w->alloc, VxChunk, VX_MAX_CHUNKS);
  w->grid = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, VX_MAX_CHUNKS);
  w->free_slots = ALLOC_ARRAY(&w->alloc, u16, VX_MAX_CHUNKS);
  w->claims = ALLOC_ARRAY(&w->alloc, u32, VX_MAX_CHUNKS);
  w->urgent = ALLOC_ARRAY(&w->alloc, u16, VX_MAX_CHUNKS);
  w->renderables = ALLOC_ARRAY(&w->alloc, u16, VX_MAX_CHUNKS);
  w->mesh_changes = ALLOC_ARRAY(&w->alloc, i32, VX_MAX_CHUNKS * 2);
  w->wanted = ALLOC_ARRAY(&w->alloc, VxWanted, VX_WANTED_MAX);
  w->jobs = ALLOC_ARRAY(&w->alloc, VxJob, VX_MAX_JOBS);
  w->results = ALLOC_ARRAY(&w->alloc, VxJob, VX_MAX_JOBS);
  w->state_arena = arena_create((u64)VX_STATE_PAGES * VX_SECTION_VOLUME, KB(64));
  w->state_alloc = make_arena_allocator(w->state_arena);
  w->state_pages = ALLOC_ARRAY(&w->alloc, u8 *, VX_STATE_PAGES);
  w->state_free = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, VX_STATE_PAGES);
  vx_terrain_init(&w->terrain, seed, &w->alloc);
  vx_lanes_init(w);
  VxFar *f = &w->far_field;
  f->arena = arena_create(MB(4), KB(256));
  f->alloc = make_arena_allocator(f->arena);
  f->tiles = ALLOC_ARRAY_NO_ZERO(&f->alloc, VxFarTile, VX_FAR_TILES);
  f->tile_flags = ALLOC_ARRAY(&f->alloc, u8, VX_FAR_TILES);
  f->order = ALLOC_ARRAY_NO_ZERO(&f->alloc, u16, VX_FAR_TILES);
  for (u32 i = 0; i < VX_FAR_REGION_COUNT; i++) {
    f->regions[i].meta = ALLOC_ARRAY(&f->alloc, u16, VX_SECTIONS * VX_SECTION_META);
    f->regions[i].pool = VX_NO_POOL;
  }
  // first-round guesses (contended wasm costs); measured costs replace them
  w->job_est_ms[VX_JOB_GEN] = 2.0f;
  w->job_est_ms[VX_JOB_LIGHT] = 0.5f;
  w->job_est_ms[VX_JOB_MESH] = 0.2f;
  w->job_est_ms[VX_JOB_FAR_GEN] = 1.0f;
  w->job_est_ms[VX_JOB_FAR_MESH] = 2.0f;
  vx_world_reset(w, seed);
}

void vx_world_reset(VxWorld *w, u32 seed) {
  for (u32 i = 0; i < VX_MAX_CHUNKS; i++) {
    VxChunk *c = &w->chunks[i];
    if (c->in_use) {
      vx_mesh_store_free(w->meshes, &c->parts[0]);
      vx_mesh_store_free(w->meshes, &c->parts[1]);
      c->in_use = false;
    }
    mem_zero(c->state_page, sizeof(c->state_page));
    w->grid[i] = VX_NO_SLOT;
    w->free_slots[i] = (u16)(VX_MAX_CHUNKS - 1 - i);
  }
  w->free_count = VX_MAX_CHUNKS;
  // every page goes back on the free list
  for (u32 i = 0; i < w->state_page_count; i++) w->state_free[i] = (u16)i;
  w->state_free_count = w->state_page_count;
  w->renderable_count = 0;
  VxFar *f = &w->far_field;
  for (u32 i = 0; i < VX_FAR_REGION_COUNT; i++) {
    vx_mesh_store_free(w->meshes, &f->regions[i]);
    f->region_dirty[i] = 0;
    f->region_queued[i] = 0;
  }
  mem_zero(f->tile_flags, VX_FAR_TILES);
  f->ready_count = 0;
  f->has_order = false;
  f->version++;
  w->mesh_changes_overflow = true;
  w->urgent_count = 0;
  w->job_count = 0;
  w->result_count = 0;
  w->has_center = false;
  w->wanted_count = 0;
  w->stats = (VxWorldStats){0};
  for (u32 i = 0; i < w->lane_count; i++) w->lanes[i].results_used = 0;
  if (seed != w->terrain.seed) vx_terrain_reseed(&w->terrain, seed);
}

// ---- block access (main lane) ----

u8 vx_world_block(const VxWorld *w, i32 x, i32 y, i32 z) {
  if (y < 0 || y >= VX_HEIGHT) return B_AIR;
  VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state == VX_CHUNK_GENERATING) return B_AIR;
  return c->blocks[(y << 8) | ((z & 15) << 4) | (x & 15)];
}

u16 vx_world_light(const VxWorld *w, i32 x, i32 y, i32 z) {
  if (y >= VX_HEIGHT) return VX_LIGHT_SKY_FULL;
  if (y < 0) return 0;
  VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state != VX_CHUNK_LIT) return VX_LIGHT_SKY_FULL;
  return c->light[(y << 8) | ((z & 15) << 4) | (x & 15)];
}

b32 vx_world_is_ready(const VxWorld *w, f64 x, f64 z) {
  VxChunk *c = vx_world_chunk(w, vx_floor_i(x) >> 4, vx_floor_i(z) >> 4);
  if (!c || c->state != VX_CHUNK_LIT) return false;
  for (i32 s = 0; s < VX_SECTIONS; s++)
    if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) return false;
  return true;
}

b32 vx_world_idle(const VxWorld *w) {
  return w->stats.pending_gen == 0 && w->stats.pending_mesh == 0 &&
         w->result_count == 0;
}

f32 vx_world_load_progress(const VxWorld *w, i32 radius) {
  u32 total = 0, done = 0;
  for (u32 i = 0; i < w->wanted_count; i++) {
    const VxWanted *wa = &w->wanted[i];
    if (wa->d > radius) break;
    total++;
    VxChunk *c = vx_world_chunk(w, wa->cx, wa->cz);
    if (!c || c->state != VX_CHUNK_LIT) continue;
    b32 ok = true;
    for (i32 s = 0; s < VX_SECTIONS; s++)
      if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) ok = false;
    if (ok) done++;
  }
  return total ? (f32)done / (f32)total : 0.0f;
}

hz_internal void vx_light_window(const VxWorld *w, VxLightEngine *e, i32 ccx, i32 ccz) {
  for (i32 dz = -1; dz <= 1; dz++) {
    for (i32 dx = -1; dx <= 1; dx++) {
      VxChunk *c = vx_world_chunk(w, ccx + dx, ccz + dz);
      e->win[(dx + 1) + (dz + 1) * 3] =
          (c && c->state != VX_CHUNK_GENERATING) ? c : NULL;
    }
  }
}

hz_internal void vx_mark_urgent(VxWorld *w, VxChunk *c, i32 sy) {
  if (c->urgent_mask == 0) w->urgent[w->urgent_count++] = vx_slot_of(w, c);
  c->urgent_mask |= (u16)(1u << sy);
}

// bumps mesh versions of every section whose padded volume holds the cell
hz_internal void vx_mark_block_dirty(VxWorld *w, i32 x, i32 y, i32 z) {
  i32 cx0 = (x - 1) >> 4, cx1 = (x + 1) >> 4;
  i32 cz0 = (z - 1) >> 4, cz1 = (z + 1) >> 4;
  i32 sy0 = (y - 1) >> 4, sy1 = (y + 1) >> 4;
  if (sy0 < 0) sy0 = 0;
  if (sy1 > VX_SECTIONS - 1) sy1 = VX_SECTIONS - 1;
  w->stats.dirty_sections = 0;
  for (i32 cz = cz0; cz <= cz1; cz++) {
    for (i32 cx = cx0; cx <= cx1; cx++) {
      VxChunk *c = vx_world_chunk(w, cx, cz);
      if (!c) continue;
      for (i32 sy = sy0; sy <= sy1; sy++) {
        c->sec_version[sy]++;
        w->stats.dirty_sections++;
        if (c->state == VX_CHUNK_LIT) vx_mark_urgent(w, c, sy);
      }
    }
  }
}

u8 vx_world_state(const VxWorld *w, i32 x, i32 y, i32 z) {
  if (y < 0 || y >= VX_HEIGHT) return 0;
  VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state == VX_CHUNK_GENERATING) return 0;
  const u8 *page = vx_state_page(w, c, y >> 4);
  return page ? page[((y & 15) << 8) | ((z & 15) << 4) | (x & 15)] : 0;
}

void vx_world_set_cut(VxWorld *w, b32 on, i32 z) {
  if (w->cut_on == on && (!on || w->cut_z == z)) return;
  i32 old_cz = w->cut_on ? w->cut_z >> 4 : INT32_MIN, new_cz = on ? z >> 4 : INT32_MIN;
  w->cut_on = on;
  w->cut_z = z;
  for (u32 i = 0; i < VX_MAX_CHUNKS; i++) {
    VxChunk *c = &w->chunks[i];
    if (!c->in_use || (c->cz != old_cz && c->cz != new_cz)) continue;
    for (i32 sy = 0; sy < VX_SECTIONS; sy++) {
      if (c->section_count[sy] == 0) continue;
      c->sec_version[sy]++;
      if (c->state == VX_CHUNK_LIT) vx_mark_urgent(w, c, sy);
    }
  }
}

b32 vx_world_set_block(VxWorld *w, i32 x, i32 y, i32 z, u8 id, b32 record) {
  return vx_world_set_block_state(w, x, y, z, id, 0, record);
}

b32 vx_world_set_block_state(VxWorld *w, i32 x, i32 y, i32 z, u8 id, u8 state, b32 record) {
  if (y < 0 || y >= VX_HEIGHT || !vx_in_world(x, z)) return false;
  VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state != VX_CHUNK_LIT) return false;
  i32 lx = x & 15, lz = z & 15;
  i32 i = (y << 8) | (lz << 4) | lx;
  i32 si = ((y & 15) << 8) | (lz << 4) | lx;
  u8 old = c->blocks[i];
  u8 *page = vx_state_page(w, c, y >> 4);
  u8 old_state = page ? page[si] : 0;
  if (old == id && old_state == state) return false;
  if (state != old_state) {
    if (!page) page = vx_state_page_make(w, c, y >> 4);
    if (!page) return false;
    page[si] = state;
  }
  if (old == id) {
    // only the state moved: no light change, the geometry around it remeshes
    c->modified = true;
    w->edit_serial++;
    if (record) vx_edits_record(w->edits, c->cx, c->cz, (u16)i, id, state);
    vx_mark_block_dirty(w, x, y, z);
    return true;
  }
  c->blocks[i] = id;
  if (old == B_PYLON || id == B_PYLON) vx_chunk_find_pylons(c);
  i32 ti = vx_far_tile_of(c->cx, c->cz);
  if (ti >= 0) w->far_field.tile_flags[ti] |= VX_FAR_DIRTY;
  i32 sy = y >> 4;
  if (old == B_AIR) c->section_count[sy]++;
  else if (id == B_AIR) c->section_count[sy]--;
  c->modified = true;
  w->edit_serial++;
  if (record) vx_edits_record(w->edits, c->cx, c->cz, (u16)i, id, state);
  // keep h15 in sync for neighbours that are lit later
  i32 yy = VX_HEIGHT - 1;
  for (; yy >= 0; yy--) {
    if (vx_light_opacity[c->blocks[(yy << 8) | (lz << 4) | lx]] > 0) break;
  }
  c->h15[(lz << 4) | lx] = (u8)(yy + 1);
  u64 t0 = os_time_now();
  VxLightEngine *e = &w->lanes[0].light;
  vx_light_window(w, e, c->cx, c->cz);
  vx_light_block_changed(e, 16 + lx, y, 16 + lz, id);
  w->stats.light_ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), t0));
  w->stats.light_writes = e->writes;
  vx_mark_block_dirty(w, x, y, z);
  return true;
}

// ---- streaming ----

hz_internal void vx_rebuild_wanted(VxWorld *w, i32 pcx, i32 pcz, i32 r) {
  i32 g = r + 3;
  // counting sort by squared distance keeps generation order stable
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u32 bins = (u32)(g * g + 1);
  u32 *counts = ALLOC_ARRAY(&ta.allocator, u32, bins + 1);
  for (i32 dz = -g; dz <= g; dz++)
    for (i32 dx = -g; dx <= g; dx++) {
      i32 d2 = dx * dx + dz * dz;
      if (d2 <= g * g) counts[d2 + 1]++;
    }
  for (u32 i = 1; i <= bins; i++) counts[i] += counts[i - 1];
  u32 n = counts[bins];
  for (i32 dz = -g; dz <= g; dz++)
    for (i32 dx = -g; dx <= g; dx++) {
      i32 d2 = dx * dx + dz * dz;
      if (d2 > g * g) continue;
      w->wanted[counts[d2]++] =
          (VxWanted){pcx + dx, pcz + dz, m_sqrtf((f32)d2)};
    }
  tctx_temp_allocator_end(ta);
  w->wanted_count = n;

  f32 unload = (f32)r + 4.5f;
  for (u32 i = 0; i < VX_MAX_CHUNKS; i++) {
    VxChunk *c = &w->chunks[i];
    if (!c->in_use) continue;
    f32 dx = (f32)(c->cx - pcx), dz = (f32)(c->cz - pcz);
    if (m_sqrtf(dx * dx + dz * dz) > unload) vx_chunk_free(w, c);
  }
  w->center_cx = pcx;
  w->center_cz = pcz;
  w->center_r = r;
  w->has_center = true;
}

hz_internal b32 vx_neighbors_at_least(const VxWorld *w, const VxChunk *c, u8 state) {
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dz == 0) continue;
      VxChunk *n = vx_world_chunk(w, c->cx + dx, c->cz + dz);
      if (!n || n->state < state) return false;
    }
  return true;
}

// jobs whose 3x3 windows overlap may not run in the same frame
hz_internal b32 vx_claim_window(VxWorld *w, i32 cx, i32 cz, b32 write) {
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      u32 v = w->claims[vx_cell(cx + dx, cz + dz)];
      if ((v >> 1) == w->claim_stamp && (write || (v & 1))) return false;
    }
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++)
      w->claims[vx_cell(cx + dx, cz + dz)] = (w->claim_stamp << 1) | (write ? 1u : 0u);
  return true;
}

hz_internal VxJob *vx_add_job(VxWorld *w, VxJobKind kind, VxChunk *c) {
  if (w->job_count >= VX_MAX_JOBS) return NULL;
  VxJob *j = &w->jobs[w->job_count++];
  memzero_struct(j);
  j->kind = (u8)kind;
  j->slot = vx_slot_of(w, c);
  j->uid = c->uid;
  return j;
}

// ---- applying meshes ----

hz_internal void vx_apply_sections(VxWorld *w, VxChunk *c,
                                   const VxSectionUpdate *opaque,
                                   const VxSectionUpdate *trans) {
  vx_mesh_store_rebuild(w->meshes, &c->parts[0], opaque);
  vx_mesh_store_rebuild(w->meshes, &c->parts[1], trans);
  vx_note_mesh_change(w, c);
  i32 min_h = 255;
  for (i32 i = 0; i < 256; i++)
    if (c->h15[i] < min_h) min_h = c->h15[i];
  c->surface_min = min_h;
  vx_renderable_update(w, c);
}

hz_internal void vx_apply_empty_section(VxWorld *w, VxChunk *c, i32 sy) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  VxSectionUpdate *up = ALLOC_ARRAY(&ta.allocator, VxSectionUpdate, VX_SECTIONS * 2);
  up[sy].changed = true;
  up[VX_SECTIONS + sy].changed = true;
  vx_apply_sections(w, c, up, up + VX_SECTIONS);
  tctx_temp_allocator_end(ta);
}

hz_internal void vx_apply_results(VxWorld *w) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  VxSectionUpdate *up = ALLOC_ARRAY(&ta.allocator, VxSectionUpdate, VX_SECTIONS * 2);
  u8 *consumed = ALLOC_ARRAY(&ta.allocator, u8, w->result_count);
  for (u32 i = 0; i < w->result_count; i++) {
    if (consumed[i]) continue;
    VxJob *r = &w->results[i];
    VxChunk *c = &w->chunks[r->slot];
    if (!c->in_use || c->uid != r->uid) {
      consumed[i] = 1;
      continue;
    }
    mem_zero(up, sizeof(VxSectionUpdate) * VX_SECTIONS * 2);
    u32 upload = 0;
    b32 any = false;
    for (u32 k = i; k < w->result_count; k++) {
      VxJob *rk = &w->results[k];
      if (consumed[k] || rk->slot != r->slot || rk->uid != r->uid) continue;
      i32 sy = rk->sy;
      if (rk->version < c->sec_meshed[sy] || rk->version > c->sec_version[sy]) {
        consumed[k] = 2;
        continue;
      }
      const u32 *base = w->lanes[rk->lane].results + rk->out_offset;
      // a later result for the same section supersedes the earlier one
      if (up[sy].changed) upload -= (up[sy].quads + up[VX_SECTIONS + sy].quads) * VX_QUAD_BYTES;
      up[sy] = (VxSectionUpdate){base, rk->opaque_quads, rk->buckets[0], true};
      up[VX_SECTIONS + sy] = (VxSectionUpdate){
          base + rk->opaque_quads * VX_QUAD_U32, rk->trans_quads, rk->buckets[1], true};
      upload += (rk->opaque_quads + rk->trans_quads) * VX_QUAD_BYTES;
      any = true;
    }
    if (!any) {
      for (u32 k = i; k < w->result_count; k++)
        if (w->results[k].slot == r->slot && w->results[k].uid == r->uid) consumed[k] = 1;
      continue;
    }
    if (upload > vx_mesh_store_upload_space(w->meshes)) break;
    vx_apply_sections(w, c, up, up + VX_SECTIONS);
    for (u32 k = i; k < w->result_count; k++) {
      VxJob *rk = &w->results[k];
      if (rk->slot != r->slot || rk->uid != r->uid || consumed[k]) continue;
      consumed[k] = 1;
      i32 sy = rk->sy;
      if (c->sec_meshed[sy] < 0) w->stats.meshed_sections++;
      if (rk->version > c->sec_meshed[sy]) c->sec_meshed[sy] = rk->version;
      if (c->sec_pending[sy] == rk->version) c->sec_pending[sy] = -1;
    }
  }
  u32 kept = 0;
  for (u32 i = 0; i < w->result_count; i++)
    if (!consumed[i]) w->results[kept++] = w->results[i];
  w->result_count = kept;
  tctx_temp_allocator_end(ta);
  if (kept == 0)
    for (u32 i = 0; i < w->lane_count; i++) w->lanes[i].results_used = 0;
}

// ---- far terrain (main) ----

hz_internal void vx_far_mark_regions(VxFar *f, u32 ti) {
  i32 tx = (i32)(ti % VX_FAR_CHUNKS), tz = (i32)(ti / VX_FAR_CHUNKS);
  // a tile's runs shape its neighbours' sides too, which may sit in the next region
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      i32 x = tx + dx, z = tz + dz;
      if (x < 0 || z < 0 || x >= VX_FAR_CHUNKS || z >= VX_FAR_CHUNKS) continue;
      f->region_dirty[(z / VX_FAR_REGION) * VX_FAR_REGIONS + x / VX_FAR_REGION] = 1;
    }
}

hz_internal void vx_far_integrate(VxWorld *w, VxJob *j) {
  VxFar *f = &w->far_field;
  if (j->kind == VX_JOB_FAR_GEN) {
    f->tile_flags[j->slot] &= (u8)~VX_FAR_QUEUED;
    if (!j->done) return;
    if (!(f->tile_flags[j->slot] & VX_FAR_HAS)) f->ready_count++;
    f->tile_flags[j->slot] |= VX_FAR_HAS;
    vx_far_mark_regions(f, j->slot);
    return;
  }
  f->region_queued[j->slot] = 0;
  if (!j->done) {
    f->region_dirty[j->slot] = 1;
    return;
  }
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  VxSectionUpdate *up = ALLOC_ARRAY(&ta.allocator, VxSectionUpdate, VX_SECTIONS);
  up[0] = (VxSectionUpdate){w->lanes[j->lane].results + j->out_offset, j->opaque_quads, NULL, true};
  b32 ok = vx_mesh_store_rebuild(w->meshes, &f->regions[j->slot], up);
  tctx_temp_allocator_end(ta);
  if (!ok) {
    f->region_dirty[j->slot] = 1;
    return;
  }
  f->region_lo[j->slot] = (u8)(j->version & 255);
  f->region_hi[j->slot] = (u8)(j->version >> 8);
  f->version++;
}

// returns how many of the round's jobs ran to completion
// a generated chunk's state bytes: the generator's where its block still stands, then its edits' over them
hz_internal void vx_apply_gen_states(VxWorld *w, VxChunk *c) {
  for (u32 i = 0; i < c->gen_state_count; i++) {
    const VxGenState *g = &c->gen_states[i];
    if (c->blocks[g->index] != g->id) continue;
    u8 *page = vx_state_page_make(w, c, g->index >> 12);
    if (page) page[g->index & 4095] = g->state;
  }
  const VxChunkEdits *e = vx_edits_for_chunk(w->edits, c->cx, c->cz);
  if (!e) return;
  for (u32 i = 0; i < e->count; i++) {
    u8 st = e->items[i].state;
    u16 idx = e->items[i].index;
    u8 *page = st ? vx_state_page_make(w, c, idx >> 12) : vx_state_page(w, c, idx >> 12);
    if (page) page[idx & 4095] = st;
  }
}

hz_internal u32 vx_integrate_jobs(VxWorld *w) {
  u32 done = 0;
  for (u32 i = 0; i < w->job_count; i++) {
    VxJob *j = &w->jobs[i];
    if (j->kind == VX_JOB_FAR_GEN || j->kind == VX_JOB_FAR_MESH) {
      if (j->done) {
        w->job_est_ms[j->kind] += (j->ms - w->job_est_ms[j->kind]) * VX_EST_ALPHA;
        done++;
      }
      vx_far_integrate(w, j);
      continue;
    }
    VxChunk *c = &w->chunks[j->slot];
    b32 valid = c->in_use && c->uid == j->uid;
    if (j->done) {
      w->job_est_ms[j->kind] += (j->ms - w->job_est_ms[j->kind]) * VX_EST_ALPHA;
      done++;
    }
    switch (j->kind) {
    case VX_JOB_GEN:
      w->stats.pending_gen--;
      // an unrun generation frees its slot; streaming asks for it again
      if (!j->done && valid) vx_chunk_free(w, c);
      if (j->done && valid) vx_apply_gen_states(w, c);
      break;
    case VX_JOB_LIGHT:
      break;
    default:
      w->stats.pending_mesh--;
      if (!j->done) {
        if (valid && c->sec_pending[j->sy] == j->version) c->sec_pending[j->sy] = -1;
      } else if (valid) {
        w->results[w->result_count++] = *j;
      }
      break;
    }
  }
  w->stats.jobs_last_frame = w->job_count;
  w->job_count = 0;
  return done;
}

// ---- scheduling ----

// distance of the nearest chunk in view range that is not fully meshed
hz_internal f32 vx_mesh_frontier(const VxWorld *w) {
  for (u32 i = 0; i < w->wanted_count; i++) {
    const VxWanted *wa = &w->wanted[i];
    if (wa->d > (f32)w->render_distance) break;
    const VxChunk *c = vx_world_chunk(w, wa->cx, wa->cz);
    if (!c || c->state != VX_CHUNK_LIT) return wa->d;
    for (i32 s = 0; s < VX_SECTIONS; s++)
      if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) return wa->d;
  }
  return 1e9f;
}

// jobs of a kind the lanes can finish in left_ms, at least one per lane
hz_internal u32 vx_round_cap(const VxWorld *w, VxJobKind k, f32 left_ms) {
  f32 n = m_ceilf((f32)w->lane_count * left_ms / m_maxf(w->job_est_ms[k], 0.01f));
  return (u32)m_clampf(n, (f32)w->lane_count, (f32)VX_MAX_JOBS);
}

hz_internal void vx_far_schedule(VxWorld *w, f32 left_ms);

hz_internal void vx_schedule(VxWorld *w, f32 left_ms) {
  w->claim_stamp++;
  i32 r = w->render_distance;
  u32 max_gen = vx_round_cap(w, VX_JOB_GEN, left_ms);
  u32 max_mesh = vx_round_cap(w, VX_JOB_MESH, left_ms);
  u32 gen_count = 0, mesh_count = 0;
  b32 can_mesh = w->result_count == 0;

  if (can_mesh) {
    u32 kept = 0;
    for (u32 u = 0; u < w->urgent_count; u++) {
      VxChunk *c = &w->chunks[w->urgent[u]];
      u16 mask = c->urgent_mask;
      if (!c->in_use || mask == 0) continue;
      c->urgent_mask = 0;
      if (c->state != VX_CHUNK_LIT) continue;
      b32 claimed = false;
      for (i32 sy = 0; sy < VX_SECTIONS; sy++) {
        if (!(mask & (1u << sy))) continue;
        if (c->sec_meshed[sy] == c->sec_version[sy]) continue;
        if (c->section_count[sy] == 0) {
          if (c->sec_meshed[sy] >= 0) vx_apply_empty_section(w, c, sy);
          c->sec_meshed[sy] = c->sec_version[sy];
          continue;
        }
        if (!claimed && !(claimed = vx_claim_window(w, c->cx, c->cz, false))) {
          c->urgent_mask |= (u16)(1u << sy);
          continue;
        }
        VxJob *j = vx_add_job(w, VX_JOB_MESH, c);
        if (!j) {
          c->urgent_mask |= (u16)(1u << sy);
          continue;
        }
        j->urgent = 1;
        j->sy = (u8)sy;
        j->version = c->sec_version[sy];
        c->sec_pending[sy] = c->sec_version[sy];
        c->mesh_started = true;
        w->stats.pending_mesh++;
      }
      if (c->urgent_mask) w->urgent[kept++] = w->urgent[u];
    }
    w->urgent_count = kept;
  }

  // near-first: work stays within a few rings of the meshing frontier so near chunks finish before far ones start
  f32 horizon = w->job_near_first ? vx_mesh_frontier(w) + VX_LOOKAHEAD : 1e9f;
  for (u32 wi = 0; wi < w->wanted_count; wi++) {
    const VxWanted *wa = &w->wanted[wi];
    if (wa->d > horizon) break;
    VxChunk *c = vx_world_chunk(w, wa->cx, wa->cz);
    if (!c) {
      if (gen_count >= max_gen) continue;
      c = vx_chunk_alloc(w, wa->cx, wa->cz);
      if (!c) continue;
      if (!vx_add_job(w, VX_JOB_GEN, c)) {
        vx_chunk_free(w, c);
        continue;
      }
      gen_count++;
      w->stats.pending_gen++;
      continue;
    }
    if (c->state == VX_CHUNK_GENERATING) continue;
    if (c->state == VX_CHUNK_GENERATED) {
      if (wa->d <= r + 1.5f && vx_neighbors_at_least(w, c, VX_CHUNK_GENERATED) &&
          vx_claim_window(w, c->cx, c->cz, true)) {
        vx_add_job(w, VX_JOB_LIGHT, c);
      }
      continue;
    }
    if (wa->d > r || !vx_neighbors_at_least(w, c, VX_CHUNK_LIT)) continue;
    b32 claimed = false;
    for (i32 sy = 0; sy < VX_SECTIONS; sy++) {
      if (c->sec_meshed[sy] == c->sec_version[sy] ||
          c->sec_pending[sy] == c->sec_version[sy])
        continue;
      if (c->section_count[sy] == 0) {
        if (c->sec_meshed[sy] >= 0) vx_apply_empty_section(w, c, sy);
        c->sec_meshed[sy] = c->sec_version[sy];
        continue;
      }
      if (!can_mesh || mesh_count >= max_mesh) break;
      if (!claimed && !(claimed = vx_claim_window(w, c->cx, c->cz, false))) break;
      VxJob *j = vx_add_job(w, VX_JOB_MESH, c);
      if (!j) break;
      j->sy = (u8)sy;
      j->version = c->sec_version[sy];
      c->sec_pending[sy] = c->sec_version[sy];
      c->mesh_started = true;
      mesh_count++;
      w->stats.pending_mesh++;
    }
  }
  // far terrain: once everything in view range is meshed
  if (can_mesh && vx_mesh_frontier(w) > (f32)r) vx_far_schedule(w, left_ms);
}

hz_internal VxJob *vx_add_far_job(VxWorld *w, VxJobKind kind, u32 index) {
  if (w->job_count >= VX_MAX_JOBS) return NULL;
  VxJob *j = &w->jobs[w->job_count++];
  memzero_struct(j);
  j->kind = (u8)kind;
  j->slot = (u16)index;
  return j;
}

// counting sort of the far tiles by squared chunk distance from (pcx, pcz)
hz_internal void vx_far_order(VxFar *f, i32 pcx, i32 pcz) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u32 bins = 2 * VX_FAR_CHUNKS * VX_FAR_CHUNKS + 1;
  u32 *counts = ALLOC_ARRAY(&ta.allocator, u32, bins + 1);
  for (u32 pass = 0; pass < 2; pass++)
    for (u32 ti = 0; ti < VX_FAR_TILES; ti++) {
      i32 dx = (i32)(ti % VX_FAR_CHUNKS) + VX_FAR_C0 - pcx, dz = (i32)(ti / VX_FAR_CHUNKS) + VX_FAR_C0 - pcz;
      u32 d2 = (u32)(dx * dx + dz * dz);
      if (pass == 0) counts[d2 + 1]++;
      else f->order[counts[d2]++] = (u16)ti;
      if (pass == 0 && ti == VX_FAR_TILES - 1)
        for (u32 i = 1; i <= bins; i++) counts[i] += counts[i - 1];
    }
  tctx_temp_allocator_end(ta);
  f->order_cx = pcx;
  f->order_cz = pcz;
  f->has_order = true;
}

// every far tile and its neighbours hold runs and none is being written
hz_internal b32 vx_far_region_ready(const VxFar *f, u32 r) {
  i32 rx = (i32)(r % VX_FAR_REGIONS) * VX_FAR_REGION, rz = (i32)(r / VX_FAR_REGIONS) * VX_FAR_REGION;
  for (i32 z = rz - 1; z <= rz + VX_FAR_REGION; z++)
    for (i32 x = rx - 1; x <= rx + VX_FAR_REGION; x++) {
      if (x < 0 || z < 0 || x >= VX_FAR_CHUNKS || z >= VX_FAR_CHUNKS) continue;
      u8 fl = f->tile_flags[z * VX_FAR_CHUNKS + x];
      if (!(fl & VX_FAR_HAS) || (fl & VX_FAR_QUEUED)) return false;
    }
  return true;
}

// what is left of the round after the view range: the island's far tiles, then meshes of the regions they changed
hz_internal void vx_far_schedule(VxWorld *w, f32 left_ms) {
  VxFar *f = &w->far_field;
  i32 pcx = MIN(MAX(w->center_cx, VX_FAR_C0), VX_FAR_C0 + VX_FAR_CHUNKS - 1);
  i32 pcz = MIN(MAX(w->center_cz, VX_FAR_C0), VX_FAR_C0 + VX_FAR_CHUNKS - 1);
  if (!f->has_order || f->order_cx != pcx || f->order_cz != pcz) vx_far_order(f, pcx, pcz);
  u32 max_gen = vx_round_cap(w, VX_JOB_FAR_GEN, left_ms), n = 0;
  f32 r2 = (f32)(w->render_distance * w->render_distance);
  // missing tiles first; a changed tile waits until the view range has moved off it
  for (u32 pass = 0; pass < 2 && n < max_gen; pass++)
    for (u32 i = 0; i < VX_FAR_TILES && n < max_gen; i++) {
      u32 ti = f->order[i];
      u8 fl = f->tile_flags[ti];
      if (fl & VX_FAR_QUEUED) continue;
      if (pass == 0 ? (fl & VX_FAR_HAS) != 0 : (fl & VX_FAR_DIRTY) == 0) continue;
      i32 cx = (i32)(ti % VX_FAR_CHUNKS) + VX_FAR_C0, cz = (i32)(ti / VX_FAR_CHUNKS) + VX_FAR_C0;
      f32 dx = (f32)(cx - w->center_cx), dz = (f32)(cz - w->center_cz);
      if (pass == 1 && dx * dx + dz * dz <= r2) continue;
      VxJob *j = vx_add_far_job(w, VX_JOB_FAR_GEN, ti);
      if (!j) return;
      // a chunk generated before the round is stable through it: reduce its blocks instead of generating them again
      const VxChunk *c = vx_world_chunk(w, cx, cz);
      j->sy = c && c->state != VX_CHUNK_GENERATING;
      f->tile_flags[ti] = (u8)((fl | VX_FAR_QUEUED) & ~VX_FAR_DIRTY);
      n++;
    }
  u32 max_mesh = vx_round_cap(w, VX_JOB_FAR_MESH, left_ms);
  n = 0;
  for (u32 r = 0; r < VX_FAR_REGION_COUNT && n < max_mesh; r++) {
    if (!f->region_dirty[r] || f->region_queued[r] || !vx_far_region_ready(f, r)) continue;
    if (!vx_add_far_job(w, VX_JOB_FAR_MESH, r)) return;
    f->region_dirty[r] = 0;
    f->region_queued[r] = 1;
    n++;
  }
}

void vx_world_near_mask(const VxWorld *w, i32 pcx, i32 pcz, u32 *mask) {
  i32 ox = pcx - VX_FAR_MASK_SIDE / 2, oz = pcz - VX_FAR_MASK_SIDE / 2;
  mem_zero(mask, VX_FAR_MASK_U32 * sizeof(u32));
  mask[0] = (u32)ox;
  mask[1] = (u32)oz;
  for (u32 i = 0; i < w->renderable_count; i++) {
    const VxChunk *c = &w->chunks[w->renderables[i]];
    i32 x = c->cx - ox, z = c->cz - oz;
    if (x < 0 || z < 0 || x >= VX_FAR_MASK_SIDE || z >= VX_FAR_MASK_SIDE || c->state != VX_CHUNK_LIT) continue;
    // a column part way through meshing still shows the far terrain
    b32 whole = true;
    for (i32 s = 0; s < VX_SECTIONS && whole; s++) whole = c->section_count[s] == 0 || c->sec_meshed[s] >= 0;
    if (!whole) continue;
    u32 bit = (u32)(z * VX_FAR_MASK_SIDE + x);
    mask[2 + (bit >> 5)] |= 1u << (bit & 31);
  }
}

void vx_world_integrate(VxWorld *w) {
  vx_integrate_jobs(w);
  vx_apply_results(w);
}

void vx_world_update(VxWorld *w, f64 px, f64 pz, f32 budget_ms, b32 near_first) {
  i32 pcx = vx_floor_i(px) >> 4, pcz = vx_floor_i(pz) >> 4;
  i32 r = w->render_distance;
  if (!w->has_center || pcx != w->center_cx || pcz != w->center_cz ||
      r != w->center_r)
    vx_rebuild_wanted(w, pcx, pcz, r);
  w->job_budget_ms = budget_ms;
  w->job_near_first = near_first;
  w->job_start = os_time_now();
  w->job_rounds = 0;
  // short rounds buy latency near the player; otherwise a round spans the budget, as barriers idle lanes
  w->job_round_end_ms = near_first ? m_minf(budget_ms, VX_ROUND_MS) : budget_ms;
  vx_schedule(w, w->job_round_end_ms);
  w->job_cursor = 0;
}

// main, between rounds: integrate the finished round, upload its meshes, schedule the next while budget remains
hz_internal b32 vx_next_round(VxWorld *w) {
  u32 done = vx_integrate_jobs(w);
  vx_apply_results(w);
  if (done == 0) return false;
  f32 used = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), w->job_start));
  f32 left = w->job_budget_ms - used;
  if (left < VX_MIN_ROUND_MS) return false;
  f32 round = w->job_near_first ? m_minf(left, VX_ROUND_MS) : left;
  w->job_round_end_ms = used + round;
  vx_schedule(w, round);
  w->job_cursor = 0;
  return w->job_count > 0;
}

// ---- jobs (any lane) ----

hz_internal void vx_build_mesh_input(const VxWorld *w, VxMeshScratch *m, const VxChunk *c, i32 sy) {
  const VxChunk *nbr[9];
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      VxChunk *n = vx_world_chunk(w, c->cx + dx, c->cz + dz);
      nbr[(dx + 1) + (dz + 1) * 3] = (n && n->state != VX_CHUNK_GENERATING) ? n : NULL;
    }
  i32 oy = sy * 16;
  for (i32 dz = -1; dz <= 16; dz++) {
    i32 ncz = dz < 0 ? 0 : dz > 15 ? 2 : 1;
    i32 lz = (dz + 16) & 15;
    for (i32 dx = -1; dx <= 16; dx++) {
      i32 ncx = dx < 0 ? 0 : dx > 15 ? 2 : 1;
      i32 lx = (dx + 16) & 15;
      const VxChunk *n = nbr[ncx + ncz * 3];
      i32 col = (lz << 4) | lx;
      i32 p_base = (dz + 1) * VX_PAD + (dx + 1);
      for (i32 dy = -1; dy <= 16; dy++) {
        i32 y = oy + dy;
        i32 pi = (dy + 1) * VX_PAD2 + p_base;
        m->state[pi] = 0;
        if (y < 0) m->blocks[pi] = B_ASH;
        else if (y >= VX_HEIGHT) m->blocks[pi] = B_AIR;
        else if (!n) m->blocks[pi] = B_STONE;
        else {
          m->blocks[pi] = n->blocks[(y << 8) | col];
          const u8 *page = vx_state_page(w, n, y >> 4);
          if (page) m->state[pi] = page[((y & 15) << 8) | col];
        }
      }
    }
  }
}

hz_internal void vx_run_gen(VxWorld *w, VxLane *l, VxChunk *c) {
  VxGenStates gs = {.items = c->gen_states};
  vx_generate_chunk(&w->terrain, l->gen, c->cx, c->cz, c->blocks, c->biome, &gs, false);
  c->gen_state_count = gs.count;
  const VxChunkEdits *e = vx_edits_for_chunk(w->edits, c->cx, c->cz);
  if (e) {
    for (u32 i = 0; i < e->count; i++) c->blocks[e->items[i].index] = e->items[i].id;
  }
  mem_zero(c->light, VX_CHUNK_VOLUME * sizeof(u16));
  vx_chunk_recount_sections(c);
  vx_chunk_find_pylons(c);
  vx_chunk_column_fill(c);
  c->state = VX_CHUNK_GENERATED;
}

hz_internal void vx_run_far_gen(VxWorld *w, VxLane *l, VxJob *j) {
  i32 cx = (i32)(j->slot % VX_FAR_CHUNKS) + VX_FAR_C0, cz = (i32)(j->slot / VX_FAR_CHUNKS) + VX_FAR_C0;
  const VxChunk *c = j->sy ? vx_world_chunk(w, cx, cz) : NULL;
  const u8 *blocks = c ? c->blocks : l->far_blocks;
  if (!c) {
    TempAllocator ta = tctx_temp_allocator_begin(NULL);
    u8 *biome = ALLOC_ARRAY_NO_ZERO(&ta.allocator, u8, VX_CHUNK * VX_CHUNK);
    vx_generate_chunk(&w->terrain, l->gen, cx, cz, l->far_blocks, biome, NULL, true);
    tctx_temp_allocator_end(ta);
    const VxChunkEdits *e = vx_edits_for_chunk(w->edits, cx, cz);
    if (e)
      for (u32 i = 0; i < e->count; i++) l->far_blocks[e->items[i].index] = e->items[i].id;
  }
  vx_far_reduce(blocks, &w->far_field.tiles[j->slot]);
}

hz_internal b32 vx_run_far_mesh(VxWorld *w, VxLane *l, VxJob *j) {
  u32 cap = (VX_LANE_RESULT_U32 - l->results_used) / VX_QUAD_U32;
  u8 lo, hi;
  u32 n = vx_far_mesh_region(&w->far_field, j->slot, l->results + l->results_used, cap, &lo, &hi);
  if (n > cap) return false;
  j->out_offset = l->results_used;
  j->opaque_quads = n;
  j->version = (i32)(lo | (u32)hi << 8);
  j->lane = (u16)(l - w->lanes);
  l->results_used += n * VX_QUAD_U32;
  return true;
}

hz_internal b32 vx_run_mesh(VxWorld *w, VxLane *l, VxJob *j) {
  VxChunk *c = &w->chunks[j->slot];
  VxMeshScratch *m = l->mesh;
  vx_build_mesh_input(w, m, c, j->sy);
  m->cut_lz = w->cut_on && (w->cut_z >> 4) == c->cz ? (w->cut_z & 15) : -1;
  vx_mesh_section(m, c->cx * 16, j->sy * 16, c->cz * 16);
  u32 need = (m->opaque_quads + m->trans_quads) * VX_QUAD_U32;
  if (l->results_used + need > VX_LANE_RESULT_U32) return false;
  u32 *dst = l->results + l->results_used;
  mem_cpy(dst, m->opaque, m->opaque_quads * VX_QUAD_BYTES);
  mem_cpy(dst + m->opaque_quads * VX_QUAD_U32, m->trans, m->trans_quads * VX_QUAD_BYTES);
  j->out_offset = l->results_used;
  j->opaque_quads = m->opaque_quads;
  j->trans_quads = m->trans_quads;
  mem_cpy(j->buckets, m->buckets, sizeof(j->buckets));
  j->lane = (u16)(l - w->lanes);
  l->results_used += need;
  return true;
}

hz_internal void vx_run_round(VxWorld *w, VxLane *l) {
  for (;;) {
    u32 i = (u32)atomic_u32_inc_eval(&w->job_cursor) - 1;
    if (i >= w->job_count) break;
    VxJob *j = &w->jobs[i];
    u64 t0 = os_time_now();
    f32 elapsed = (f32)os_ticks_to_ms(os_time_diff(t0, w->job_start));
    // a job starts only if it is expected to finish in its round; a lane's first job only needs time left
    b32 fits = elapsed + w->job_est_ms[j->kind] <= w->job_round_end_ms ||
               (l->phase_jobs == 0 && elapsed < w->job_round_end_ms);
    if (!j->urgent && !fits) continue;
    l->phase_jobs++;
    VxChunk *c = &w->chunks[j->slot];
    switch (j->kind) {
    case VX_JOB_GEN:
      vx_run_gen(w, l, c);
      j->done = 1;
      break;
    case VX_JOB_LIGHT:
      vx_light_window(w, &l->light, c->cx, c->cz);
      vx_light_chunk(&l->light, c);
      c->state = VX_CHUNK_LIT;
      j->done = 1;
      break;
    case VX_JOB_MESH:
      j->done = (u8)vx_run_mesh(w, l, j);
      break;
    case VX_JOB_FAR_GEN:
      vx_run_far_gen(w, l, j);
      j->done = 1;
      break;
    case VX_JOB_FAR_MESH:
      j->done = (u8)vx_run_far_mesh(w, l, j);
      break;
    }
    u64 ran = os_time_diff(os_time_now(), t0);
    j->ms = (f32)os_ticks_to_ms(ran);
  }
}

void vx_world_run_jobs(VxWorld *w) {
  // main scheduled before the frame's lane sync, so every lane agrees there is nothing to run
  if (w->job_count == 0) return;
  VxLane *l = &w->lanes[tctx_current()->thread_idx];
  l->phase_jobs = 0;
  for (;;) {
    vx_run_round(w, l);
    lane_sync();
    if (is_main_thread()) {
      w->job_rounds++;
      w->job_more = vx_next_round(w);
    }
    lane_sync();
    if (!w->job_more) break;
  }
}

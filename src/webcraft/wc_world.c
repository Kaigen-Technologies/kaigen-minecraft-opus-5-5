#include "webcraft/wc.h"

#define WC_NO_RENDER 0xFFFFFFFFu
#define WC_MAX_JOBS 4096
#define WC_WANTED_SPAN (2 * (WC_MAX_RENDER_DISTANCE + 3) + 1)
#define WC_WANTED_MAX (WC_WANTED_SPAN * WC_WANTED_SPAN)
#define WC_MIN_ROUND_MS 0.5f
#define WC_ROUND_MS 4.0f      // near-first rounds stay short so light and mesh jobs follow generation quickly
#define WC_LOOKAHEAD 3.0f     // near-first streaming runs this many chunks ahead of the nearest unmeshed one
#define WC_EST_ALPHA (1.0f / 32.0f)

force_inline u32 wc_cell(i32 cx, i32 cz) {
  return (u32)((cz & WC_GRID_MASK) * WC_GRID + (cx & WC_GRID_MASK));
}

WcChunk *wc_world_chunk(const WcWorld *w, i32 cx, i32 cz) {
  u16 slot = w->grid[wc_cell(cx, cz)];
  if (slot == WC_NO_SLOT) return NULL;
  WcChunk *c = &w->chunks[slot];
  return (c->cx == cx && c->cz == cz && c->in_use) ? c : NULL;
}

force_inline u16 wc_slot_of(const WcWorld *w, const WcChunk *c) {
  return (u16)(c - w->chunks);
}

// ---- chunk slots ----

hz_internal WcChunk *wc_chunk_alloc(WcWorld *w, i32 cx, i32 cz) {
  if (w->free_count == 0) return NULL;
  u16 slot = w->free_slots[--w->free_count];
  WcChunk *c = &w->chunks[slot];
  u8 *blocks = c->blocks, *light = c->light;
  u16 *meta0 = c->parts[0].meta, *meta1 = c->parts[1].meta;
  if (!blocks) {
    blocks = ALLOC_ARRAY_NO_ZERO(&w->alloc, u8, WC_CHUNK_VOLUME);
    light = ALLOC_ARRAY_NO_ZERO(&w->alloc, u8, WC_CHUNK_VOLUME);
    meta0 = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, WC_SECTIONS * WC_SECTION_META);
    meta1 = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, WC_SECTIONS * WC_SECTION_META);
  }
  memzero_struct(c);
  c->blocks = blocks;
  c->light = light;
  c->parts[0].meta = meta0;
  c->parts[1].meta = meta1;
  c->cx = cx;
  c->cz = cz;
  c->uid = ++w->next_uid;
  c->in_use = true;
  c->state = WC_CHUNK_GENERATING;
  for (i32 s = 0; s < WC_SECTIONS; s++) {
    c->sec_meshed[s] = -1;
    c->sec_pending[s] = -1;
  }
  c->parts[0].pool = WC_NO_POOL;
  c->parts[1].pool = WC_NO_POOL;
  c->render_index = WC_NO_RENDER;
  w->grid[wc_cell(cx, cz)] = slot;
  w->stats.chunks++;
  return c;
}

hz_internal void wc_renderable_update(WcWorld *w, WcChunk *c) {
  b32 has_mesh = c->parts[0].pool != WC_NO_POOL || c->parts[1].pool != WC_NO_POOL;
  if (has_mesh && c->render_index == WC_NO_RENDER) {
    c->render_index = w->renderable_count;
    w->renderables[w->renderable_count++] = wc_slot_of(w, c);
  } else if (!has_mesh && c->render_index != WC_NO_RENDER) {
    u32 last = --w->renderable_count;
    u16 moved = w->renderables[last];
    w->renderables[c->render_index] = moved;
    w->chunks[moved].render_index = c->render_index;
    c->render_index = WC_NO_RENDER;
  }
}

hz_internal void wc_note_mesh_change(WcWorld *w, const WcChunk *c) {
  if (w->mesh_change_count >= WC_MAX_CHUNKS) {
    w->mesh_changes_overflow = true;
    return;
  }
  w->mesh_changes[w->mesh_change_count * 2] = c->cx;
  w->mesh_changes[w->mesh_change_count * 2 + 1] = c->cz;
  w->mesh_change_count++;
}

void wc_world_clear_mesh_changes(WcWorld *w) {
  w->mesh_change_count = 0;
  w->mesh_changes_overflow = false;
}

hz_internal void wc_chunk_free(WcWorld *w, WcChunk *c) {
  if (c->render_index != WC_NO_RENDER) wc_note_mesh_change(w, c);
  wc_mesh_store_free(w->meshes, &c->parts[0]);
  wc_mesh_store_free(w->meshes, &c->parts[1]);
  wc_renderable_update(w, c);
  u32 cell = wc_cell(c->cx, c->cz);
  if (w->grid[cell] == wc_slot_of(w, c)) w->grid[cell] = WC_NO_SLOT;
  c->in_use = false;
  c->urgent_mask = 0;
  w->free_slots[w->free_count++] = wc_slot_of(w, c);
  w->stats.chunks--;
}

hz_internal void wc_lanes_init(WcWorld *w) {
  w->lane_count = tctx_current()->thread_count;
  w->lanes = ALLOC_ARRAY(&w->alloc, WcLane, w->lane_count);
  for (u32 i = 0; i < w->lane_count; i++) {
    WcLane *l = &w->lanes[i];
    l->arena = arena_create(MB(64), KB(256));
    l->alloc = make_arena_allocator(l->arena);
    l->gen = ALLOC(&l->alloc, WcGenScratch);
    l->mesh = ALLOC(&l->alloc, WcMeshScratch);
    wc_mesh_scratch_init(l->mesh, &l->alloc);
    wc_light_init(&l->light, &l->alloc);
    l->results = ALLOC_ARRAY_NO_ZERO(&l->alloc, u32, WC_LANE_RESULT_U32);
  }
}

// the world arena's lifetime bound: tables, noise, lanes and every touched slot's storage (ios cannot reserve 2 GB)
hz_internal u64 wc_world_arena_bytes(u32 lane_count) {
  u64 slot_storage = 2 * (u64)WC_CHUNK_VOLUME + 2 * sizeof(u16) * WC_SECTIONS * WC_SECTION_META;
  u64 per_slot = sizeof(WcChunk) + 3 * sizeof(u16) + sizeof(u32) + sizeof(u16) + 2 * sizeof(i32);
  u64 tables = (u64)WC_MAX_CHUNKS * (per_slot + slot_storage) + WC_WANTED_MAX * sizeof(WcWanted) +
               2 * WC_MAX_JOBS * sizeof(WcJob) + lane_count * sizeof(WcLane) + 16 * 1024;
  // alignment padding of every allocation above, rounded to whole megabytes
  return (tables + (4 * (u64)WC_MAX_CHUNKS + 64) * 64 + MB(1) - 1) & ~(u64)(MB(1) - 1);
}

void wc_world_init(WcWorld *w, u32 seed, WcEditStore *edits, WcMeshStore *meshes) {
  memzero_struct(w);
  w->arena = arena_create(wc_world_arena_bytes(tctx_current()->thread_count), MB(4));
  w->alloc = make_arena_allocator(w->arena);
  w->edits = edits;
  w->meshes = meshes;
  w->render_distance = 12;
  w->chunks = ALLOC_ARRAY(&w->alloc, WcChunk, WC_MAX_CHUNKS);
  w->grid = ALLOC_ARRAY_NO_ZERO(&w->alloc, u16, WC_MAX_CHUNKS);
  w->free_slots = ALLOC_ARRAY(&w->alloc, u16, WC_MAX_CHUNKS);
  w->claims = ALLOC_ARRAY(&w->alloc, u32, WC_MAX_CHUNKS);
  w->urgent = ALLOC_ARRAY(&w->alloc, u16, WC_MAX_CHUNKS);
  w->renderables = ALLOC_ARRAY(&w->alloc, u16, WC_MAX_CHUNKS);
  w->mesh_changes = ALLOC_ARRAY(&w->alloc, i32, WC_MAX_CHUNKS * 2);
  w->wanted = ALLOC_ARRAY(&w->alloc, WcWanted, WC_WANTED_MAX);
  w->jobs = ALLOC_ARRAY(&w->alloc, WcJob, WC_MAX_JOBS);
  w->results = ALLOC_ARRAY(&w->alloc, WcJob, WC_MAX_JOBS);
  wc_terrain_init(&w->terrain, seed, &w->alloc);
  wc_lanes_init(w);
  // first-round guesses (contended wasm costs); measured costs replace them
  w->job_est_ms[WC_JOB_GEN] = 2.0f;
  w->job_est_ms[WC_JOB_LIGHT] = 0.5f;
  w->job_est_ms[WC_JOB_MESH] = 0.2f;
  wc_world_reset(w, seed);
}

void wc_world_reset(WcWorld *w, u32 seed) {
  for (u32 i = 0; i < WC_MAX_CHUNKS; i++) {
    WcChunk *c = &w->chunks[i];
    if (c->in_use) {
      wc_mesh_store_free(w->meshes, &c->parts[0]);
      wc_mesh_store_free(w->meshes, &c->parts[1]);
      c->in_use = false;
    }
    w->grid[i] = WC_NO_SLOT;
    w->free_slots[i] = (u16)(WC_MAX_CHUNKS - 1 - i);
  }
  w->free_count = WC_MAX_CHUNKS;
  w->renderable_count = 0;
  w->mesh_changes_overflow = true;
  w->urgent_count = 0;
  w->job_count = 0;
  w->result_count = 0;
  w->has_center = false;
  w->wanted_count = 0;
  w->stats = (WcWorldStats){0};
  for (u32 i = 0; i < w->lane_count; i++) w->lanes[i].results_used = 0;
  if (seed != w->terrain.seed) {
    // re-seed in place: the permutation tables keep their storage
    WcTerrain *t = &w->terrain;
    u32 s = seed;
    t->seed = seed;
    WcSimplex *all[16] = {&t->warp_x, &t->warp_z, &t->cont,   &t->ero,
                          &t->peaks,  &t->hills,  &t->detail, &t->river,
                          &t->temp,   &t->humid,  &t->variant, &t->cave_a,
                          &t->cave_b, &t->cave_c, &t->cave_mask, &t->patch};
    for (u32 i = 0; i < 16; i++) {
      s = s * 1664525u + 1013904223u;
      WcSimplex tmp;
      TempAllocator ta = tctx_temp_allocator_begin(NULL);
      wc_simplex_init(&tmp, s, &ta.allocator);
      mem_cpy(all[i]->perm, tmp.perm, 512);
      mem_cpy(all[i]->perm_mod12, tmp.perm_mod12, 512);
      tctx_temp_allocator_end(ta);
    }
  }
}

// ---- block access (main lane) ----

u8 wc_world_block(const WcWorld *w, i32 x, i32 y, i32 z) {
  if (y < 0 || y >= WC_HEIGHT) return B_AIR;
  WcChunk *c = wc_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state == WC_CHUNK_GENERATING) return B_AIR;
  return c->blocks[(y << 8) | ((z & 15) << 4) | (x & 15)];
}

u8 wc_world_light(const WcWorld *w, i32 x, i32 y, i32 z) {
  if (y >= WC_HEIGHT) return 0xf0;
  if (y < 0) return 0;
  WcChunk *c = wc_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state != WC_CHUNK_LIT) return 0xf0;
  return c->light[(y << 8) | ((z & 15) << 4) | (x & 15)];
}

b32 wc_world_is_ready(const WcWorld *w, f64 x, f64 z) {
  WcChunk *c = wc_world_chunk(w, wc_floor_i(x) >> 4, wc_floor_i(z) >> 4);
  if (!c || c->state != WC_CHUNK_LIT) return false;
  for (i32 s = 0; s < WC_SECTIONS; s++)
    if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) return false;
  return true;
}

b32 wc_world_idle(const WcWorld *w) {
  return w->stats.pending_gen == 0 && w->stats.pending_mesh == 0 &&
         w->result_count == 0;
}

f32 wc_world_load_progress(const WcWorld *w, i32 radius) {
  u32 total = 0, done = 0;
  for (u32 i = 0; i < w->wanted_count; i++) {
    const WcWanted *wa = &w->wanted[i];
    if (wa->d > radius) break;
    total++;
    WcChunk *c = wc_world_chunk(w, wa->cx, wa->cz);
    if (!c || c->state != WC_CHUNK_LIT) continue;
    b32 ok = true;
    for (i32 s = 0; s < WC_SECTIONS; s++)
      if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) ok = false;
    if (ok) done++;
  }
  return total ? (f32)done / (f32)total : 0.0f;
}

hz_internal void wc_light_window(const WcWorld *w, WcLightEngine *e, i32 ccx, i32 ccz) {
  for (i32 dz = -1; dz <= 1; dz++) {
    for (i32 dx = -1; dx <= 1; dx++) {
      WcChunk *c = wc_world_chunk(w, ccx + dx, ccz + dz);
      e->win[(dx + 1) + (dz + 1) * 3] =
          (c && c->state != WC_CHUNK_GENERATING) ? c : NULL;
    }
  }
}

hz_internal void wc_mark_urgent(WcWorld *w, WcChunk *c, i32 sy) {
  if (c->urgent_mask == 0) w->urgent[w->urgent_count++] = wc_slot_of(w, c);
  c->urgent_mask |= (u16)(1u << sy);
}

// bumps mesh versions of every section whose padded volume holds the cell
hz_internal void wc_mark_block_dirty(WcWorld *w, i32 x, i32 y, i32 z) {
  i32 cx0 = (x - 1) >> 4, cx1 = (x + 1) >> 4;
  i32 cz0 = (z - 1) >> 4, cz1 = (z + 1) >> 4;
  i32 sy0 = (y - 1) >> 4, sy1 = (y + 1) >> 4;
  if (sy0 < 0) sy0 = 0;
  if (sy1 > WC_SECTIONS - 1) sy1 = WC_SECTIONS - 1;
  for (i32 cz = cz0; cz <= cz1; cz++) {
    for (i32 cx = cx0; cx <= cx1; cx++) {
      WcChunk *c = wc_world_chunk(w, cx, cz);
      if (!c) continue;
      for (i32 sy = sy0; sy <= sy1; sy++) {
        c->sec_version[sy]++;
        if (c->state == WC_CHUNK_LIT) wc_mark_urgent(w, c, sy);
      }
    }
  }
  // light changes may have touched more sections nearby: promote them too
  i32 pcx = x >> 4, pcz = z >> 4;
  for (i32 dz = -1; dz <= 1; dz++) {
    for (i32 dx = -1; dx <= 1; dx++) {
      WcChunk *c = wc_world_chunk(w, pcx + dx, pcz + dz);
      if (!c || c->state != WC_CHUNK_LIT) continue;
      for (i32 sy = 0; sy < WC_SECTIONS; sy++) {
        i32 mid = sy * 16 + 8 - y;
        if (c->sec_meshed[sy] >= 0 && c->sec_meshed[sy] != c->sec_version[sy] &&
            mid < 40 && mid > -40)
          wc_mark_urgent(w, c, sy);
      }
    }
  }
}

b32 wc_world_set_block(WcWorld *w, i32 x, i32 y, i32 z, u8 id, b32 record) {
  if (y < 0 || y >= WC_HEIGHT) return false;
  WcChunk *c = wc_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state != WC_CHUNK_LIT) return false;
  i32 lx = x & 15, lz = z & 15;
  i32 i = (y << 8) | (lz << 4) | lx;
  u8 old = c->blocks[i];
  if (old == id) return false;
  c->blocks[i] = id;
  i32 sy = y >> 4;
  if (old == B_AIR) c->section_count[sy]++;
  else if (id == B_AIR) c->section_count[sy]--;
  c->modified = true;
  if (record) wc_edits_record(w->edits, c->cx, c->cz, (u16)i, id);
  // keep h15 in sync for neighbours that are lit later
  i32 yy = WC_HEIGHT - 1;
  for (; yy >= 0; yy--) {
    if (wc_light_opacity[c->blocks[(yy << 8) | (lz << 4) | lx]] > 0) break;
  }
  c->h15[(lz << 4) | lx] = (u8)(yy + 1);
  u64 t0 = os_time_now();
  WcLightEngine *e = &w->lanes[0].light;
  wc_light_window(w, e, c->cx, c->cz);
  wc_light_block_changed(e, 16 + lx, y, 16 + lz, id);
  w->stats.light_ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), t0));
  wc_mark_block_dirty(w, x, y, z);
  return true;
}

// ---- streaming ----

hz_internal void wc_rebuild_wanted(WcWorld *w, i32 pcx, i32 pcz, i32 r) {
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
          (WcWanted){pcx + dx, pcz + dz, m_sqrtf((f32)d2)};
    }
  tctx_temp_allocator_end(ta);
  w->wanted_count = n;

  f32 unload = (f32)r + 4.5f;
  for (u32 i = 0; i < WC_MAX_CHUNKS; i++) {
    WcChunk *c = &w->chunks[i];
    if (!c->in_use) continue;
    f32 dx = (f32)(c->cx - pcx), dz = (f32)(c->cz - pcz);
    if (m_sqrtf(dx * dx + dz * dz) > unload) wc_chunk_free(w, c);
  }
  w->center_cx = pcx;
  w->center_cz = pcz;
  w->center_r = r;
  w->has_center = true;
}

hz_internal b32 wc_neighbors_at_least(const WcWorld *w, const WcChunk *c, u8 state) {
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dz == 0) continue;
      WcChunk *n = wc_world_chunk(w, c->cx + dx, c->cz + dz);
      if (!n || n->state < state) return false;
    }
  return true;
}

// jobs whose 3x3 windows overlap may not run in the same frame
hz_internal b32 wc_claim_window(WcWorld *w, i32 cx, i32 cz, b32 write) {
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      u32 v = w->claims[wc_cell(cx + dx, cz + dz)];
      if ((v >> 1) == w->claim_stamp && (write || (v & 1))) return false;
    }
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++)
      w->claims[wc_cell(cx + dx, cz + dz)] = (w->claim_stamp << 1) | (write ? 1u : 0u);
  return true;
}

hz_internal WcJob *wc_add_job(WcWorld *w, WcJobKind kind, WcChunk *c) {
  if (w->job_count >= WC_MAX_JOBS) return NULL;
  WcJob *j = &w->jobs[w->job_count++];
  memzero_struct(j);
  j->kind = (u8)kind;
  j->slot = wc_slot_of(w, c);
  j->uid = c->uid;
  return j;
}

// ---- applying meshes ----

hz_internal void wc_apply_sections(WcWorld *w, WcChunk *c,
                                   const WcSectionUpdate *opaque,
                                   const WcSectionUpdate *trans) {
  wc_mesh_store_rebuild(w->meshes, &c->parts[0], opaque);
  wc_mesh_store_rebuild(w->meshes, &c->parts[1], trans);
  wc_note_mesh_change(w, c);
  i32 min_h = 255;
  for (i32 i = 0; i < 256; i++)
    if (c->h15[i] < min_h) min_h = c->h15[i];
  c->surface_min = min_h;
  wc_renderable_update(w, c);
}

hz_internal void wc_apply_empty_section(WcWorld *w, WcChunk *c, i32 sy) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  WcSectionUpdate *up = ALLOC_ARRAY(&ta.allocator, WcSectionUpdate, WC_SECTIONS * 2);
  up[sy].changed = true;
  up[WC_SECTIONS + sy].changed = true;
  wc_apply_sections(w, c, up, up + WC_SECTIONS);
  tctx_temp_allocator_end(ta);
}

hz_internal void wc_apply_results(WcWorld *w) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  WcSectionUpdate *up = ALLOC_ARRAY(&ta.allocator, WcSectionUpdate, WC_SECTIONS * 2);
  u8 *consumed = ALLOC_ARRAY(&ta.allocator, u8, w->result_count);
  for (u32 i = 0; i < w->result_count; i++) {
    if (consumed[i]) continue;
    WcJob *r = &w->results[i];
    WcChunk *c = &w->chunks[r->slot];
    if (!c->in_use || c->uid != r->uid) {
      consumed[i] = 1;
      continue;
    }
    mem_zero(up, sizeof(WcSectionUpdate) * WC_SECTIONS * 2);
    u32 upload = 0;
    b32 any = false;
    for (u32 k = i; k < w->result_count; k++) {
      WcJob *rk = &w->results[k];
      if (consumed[k] || rk->slot != r->slot || rk->uid != r->uid) continue;
      i32 sy = rk->sy;
      if (rk->version < c->sec_meshed[sy] || rk->version > c->sec_version[sy]) {
        consumed[k] = 2;
        continue;
      }
      const u32 *base = w->lanes[rk->lane].results + rk->out_offset;
      // a later result for the same section supersedes the earlier one
      if (up[sy].changed) upload -= (up[sy].quads + up[WC_SECTIONS + sy].quads) * WC_QUAD_BYTES;
      up[sy] = (WcSectionUpdate){base, rk->opaque_quads, rk->buckets[0], true};
      up[WC_SECTIONS + sy] = (WcSectionUpdate){
          base + rk->opaque_quads * WC_QUAD_U32, rk->trans_quads, rk->buckets[1], true};
      upload += (rk->opaque_quads + rk->trans_quads) * WC_QUAD_BYTES;
      any = true;
    }
    if (!any) {
      for (u32 k = i; k < w->result_count; k++)
        if (w->results[k].slot == r->slot && w->results[k].uid == r->uid) consumed[k] = 1;
      continue;
    }
    if (upload > wc_mesh_store_upload_space(w->meshes)) break;
    wc_apply_sections(w, c, up, up + WC_SECTIONS);
    for (u32 k = i; k < w->result_count; k++) {
      WcJob *rk = &w->results[k];
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

// returns how many of the round's jobs ran to completion
hz_internal u32 wc_integrate_jobs(WcWorld *w) {
  u32 done = 0;
  for (u32 i = 0; i < w->job_count; i++) {
    WcJob *j = &w->jobs[i];
    WcChunk *c = &w->chunks[j->slot];
    b32 valid = c->in_use && c->uid == j->uid;
    if (j->done) {
      w->job_est_ms[j->kind] += (j->ms - w->job_est_ms[j->kind]) * WC_EST_ALPHA;
      done++;
    }
    switch (j->kind) {
    case WC_JOB_GEN:
      w->stats.pending_gen--;
      // an unrun generation frees its slot; streaming asks for it again
      if (!j->done && valid) wc_chunk_free(w, c);
      break;
    case WC_JOB_LIGHT:
      break;
    case WC_JOB_MESH:
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
hz_internal f32 wc_mesh_frontier(const WcWorld *w) {
  for (u32 i = 0; i < w->wanted_count; i++) {
    const WcWanted *wa = &w->wanted[i];
    if (wa->d > (f32)w->render_distance) break;
    const WcChunk *c = wc_world_chunk(w, wa->cx, wa->cz);
    if (!c || c->state != WC_CHUNK_LIT) return wa->d;
    for (i32 s = 0; s < WC_SECTIONS; s++)
      if (c->section_count[s] > 0 && c->sec_meshed[s] < 0) return wa->d;
  }
  return 1e9f;
}

// jobs of a kind the lanes can finish in left_ms, at least one per lane
hz_internal u32 wc_round_cap(const WcWorld *w, WcJobKind k, f32 left_ms) {
  f32 n = m_ceilf((f32)w->lane_count * left_ms / m_maxf(w->job_est_ms[k], 0.01f));
  return (u32)m_clampf(n, (f32)w->lane_count, (f32)WC_MAX_JOBS);
}

hz_internal void wc_schedule(WcWorld *w, f32 left_ms) {
  w->claim_stamp++;
  i32 r = w->render_distance;
  u32 max_gen = wc_round_cap(w, WC_JOB_GEN, left_ms);
  u32 max_mesh = wc_round_cap(w, WC_JOB_MESH, left_ms);
  u32 gen_count = 0, mesh_count = 0;
  b32 can_mesh = w->result_count == 0;

  if (can_mesh) {
    u32 kept = 0;
    for (u32 u = 0; u < w->urgent_count; u++) {
      WcChunk *c = &w->chunks[w->urgent[u]];
      u16 mask = c->urgent_mask;
      if (!c->in_use || mask == 0) continue;
      c->urgent_mask = 0;
      if (c->state != WC_CHUNK_LIT) continue;
      b32 claimed = false;
      for (i32 sy = 0; sy < WC_SECTIONS; sy++) {
        if (!(mask & (1u << sy))) continue;
        if (c->sec_meshed[sy] == c->sec_version[sy]) continue;
        if (c->section_count[sy] == 0) {
          if (c->sec_meshed[sy] >= 0) wc_apply_empty_section(w, c, sy);
          c->sec_meshed[sy] = c->sec_version[sy];
          continue;
        }
        if (!claimed && !(claimed = wc_claim_window(w, c->cx, c->cz, false))) {
          c->urgent_mask |= (u16)(1u << sy);
          continue;
        }
        WcJob *j = wc_add_job(w, WC_JOB_MESH, c);
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
  f32 horizon = w->job_near_first ? wc_mesh_frontier(w) + WC_LOOKAHEAD : 1e9f;
  for (u32 wi = 0; wi < w->wanted_count; wi++) {
    const WcWanted *wa = &w->wanted[wi];
    if (wa->d > horizon) break;
    WcChunk *c = wc_world_chunk(w, wa->cx, wa->cz);
    if (!c) {
      if (gen_count >= max_gen) continue;
      c = wc_chunk_alloc(w, wa->cx, wa->cz);
      if (!c) continue;
      if (!wc_add_job(w, WC_JOB_GEN, c)) {
        wc_chunk_free(w, c);
        continue;
      }
      gen_count++;
      w->stats.pending_gen++;
      continue;
    }
    if (c->state == WC_CHUNK_GENERATING) continue;
    if (c->state == WC_CHUNK_GENERATED) {
      if (wa->d <= r + 1.5f && wc_neighbors_at_least(w, c, WC_CHUNK_GENERATED) &&
          wc_claim_window(w, c->cx, c->cz, true)) {
        wc_add_job(w, WC_JOB_LIGHT, c);
      }
      continue;
    }
    if (wa->d > r || !wc_neighbors_at_least(w, c, WC_CHUNK_LIT)) continue;
    b32 claimed = false;
    for (i32 sy = 0; sy < WC_SECTIONS; sy++) {
      if (c->sec_meshed[sy] == c->sec_version[sy] ||
          c->sec_pending[sy] == c->sec_version[sy])
        continue;
      if (c->section_count[sy] == 0) {
        if (c->sec_meshed[sy] >= 0) wc_apply_empty_section(w, c, sy);
        c->sec_meshed[sy] = c->sec_version[sy];
        continue;
      }
      if (!can_mesh || mesh_count >= max_mesh) break;
      if (!claimed && !(claimed = wc_claim_window(w, c->cx, c->cz, false))) break;
      WcJob *j = wc_add_job(w, WC_JOB_MESH, c);
      if (!j) break;
      j->sy = (u8)sy;
      j->version = c->sec_version[sy];
      c->sec_pending[sy] = c->sec_version[sy];
      c->mesh_started = true;
      mesh_count++;
      w->stats.pending_mesh++;
    }
  }
}

void wc_world_integrate(WcWorld *w) {
  wc_integrate_jobs(w);
  wc_apply_results(w);
}

void wc_world_update(WcWorld *w, f64 px, f64 pz, f32 budget_ms, b32 near_first) {
  i32 pcx = wc_floor_i(px) >> 4, pcz = wc_floor_i(pz) >> 4;
  i32 r = w->render_distance;
  if (!w->has_center || pcx != w->center_cx || pcz != w->center_cz ||
      r != w->center_r)
    wc_rebuild_wanted(w, pcx, pcz, r);
  w->job_budget_ms = budget_ms;
  w->job_near_first = near_first;
  w->job_start = os_time_now();
  w->job_rounds = 0;
  // short rounds buy latency near the player; otherwise a round spans the budget, as barriers idle lanes
  w->job_round_end_ms = near_first ? m_minf(budget_ms, WC_ROUND_MS) : budget_ms;
  wc_schedule(w, w->job_round_end_ms);
  w->job_cursor = 0;
}

// main, between rounds: integrate the finished round, upload its meshes, schedule the next while budget remains
hz_internal b32 wc_next_round(WcWorld *w) {
  u32 done = wc_integrate_jobs(w);
  wc_apply_results(w);
  if (done == 0) return false;
  f32 used = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), w->job_start));
  f32 left = w->job_budget_ms - used;
  if (left < WC_MIN_ROUND_MS) return false;
  f32 round = w->job_near_first ? m_minf(left, WC_ROUND_MS) : left;
  w->job_round_end_ms = used + round;
  wc_schedule(w, round);
  w->job_cursor = 0;
  return w->job_count > 0;
}

// ---- jobs (any lane) ----

hz_internal void wc_build_mesh_input(const WcWorld *w, WcMeshScratch *m,
                                     const WcChunk *c, i32 sy) {
  const WcChunk *nbr[9];
  for (i32 dz = -1; dz <= 1; dz++)
    for (i32 dx = -1; dx <= 1; dx++) {
      WcChunk *n = wc_world_chunk(w, c->cx + dx, c->cz + dz);
      nbr[(dx + 1) + (dz + 1) * 3] =
          (n && n->state != WC_CHUNK_GENERATING) ? n : NULL;
    }
  i32 oy = sy * 16;
  for (i32 dz = -1; dz <= 16; dz++) {
    i32 ncz = dz < 0 ? 0 : dz > 15 ? 2 : 1;
    i32 lz = (dz + 16) & 15;
    for (i32 dx = -1; dx <= 16; dx++) {
      i32 ncx = dx < 0 ? 0 : dx > 15 ? 2 : 1;
      i32 lx = (dx + 16) & 15;
      const WcChunk *n = nbr[ncx + ncz * 3];
      i32 col = (lz << 4) | lx;
      i32 p_base = (dz + 1) * WC_PAD + (dx + 1);
      const WcChunk *src = n ? n : c;
      i32 cxl = dx < 0 ? 0 : dx > 15 ? 15 : dx;
      i32 czl = dz < 0 ? 0 : dz > 15 ? 15 : dz;
      i32 src_col = n ? col : ((czl << 4) | cxl);
      m->climate[p_base * 2] = src->climate[src_col * 2];
      m->climate[p_base * 2 + 1] = src->climate[src_col * 2 + 1];
      for (i32 dy = -1; dy <= 16; dy++) {
        i32 y = oy + dy;
        i32 pi = (dy + 1) * WC_PAD2 + p_base;
        if (y < 0) {
          m->blocks[pi] = B_BEDROCK;
          m->light[pi] = 0;
        } else if (y >= WC_HEIGHT) {
          m->blocks[pi] = B_AIR;
          m->light[pi] = 0xf0;
        } else if (!n) {
          m->blocks[pi] = B_STONE;
          m->light[pi] = 0;
        } else {
          i32 i = (y << 8) | col;
          m->blocks[pi] = n->blocks[i];
          m->light[pi] = n->light[i];
        }
      }
    }
  }
}

hz_internal void wc_run_gen(WcWorld *w, WcLane *l, WcChunk *c) {
  wc_generate_chunk(&w->terrain, l->gen, c->cx, c->cz, c->blocks, c->climate);
  const WcChunkEdits *e = wc_edits_for_chunk(w->edits, c->cx, c->cz);
  if (e) {
    for (u32 i = 0; i < e->count; i++) c->blocks[e->items[i].index] = e->items[i].id;
  }
  mem_zero(c->light, WC_CHUNK_VOLUME);
  wc_chunk_recount_sections(c);
  wc_chunk_column_fill(c);
  c->state = WC_CHUNK_GENERATED;
}

hz_internal b32 wc_run_mesh(WcWorld *w, WcLane *l, WcJob *j) {
  WcChunk *c = &w->chunks[j->slot];
  WcMeshScratch *m = l->mesh;
  wc_build_mesh_input(w, m, c, j->sy);
  wc_mesh_section(m, c->cx * 16, j->sy * 16, c->cz * 16);
  u32 need = (m->opaque_quads + m->trans_quads) * WC_QUAD_U32;
  if (l->results_used + need > WC_LANE_RESULT_U32) return false;
  u32 *dst = l->results + l->results_used;
  mem_cpy(dst, m->opaque, m->opaque_quads * WC_QUAD_BYTES);
  mem_cpy(dst + m->opaque_quads * WC_QUAD_U32, m->trans, m->trans_quads * WC_QUAD_BYTES);
  j->out_offset = l->results_used;
  j->opaque_quads = m->opaque_quads;
  j->trans_quads = m->trans_quads;
  mem_cpy(j->buckets, m->buckets, sizeof(j->buckets));
  j->lane = (u16)(l - w->lanes);
  l->results_used += need;
  return true;
}

hz_internal void wc_run_round(WcWorld *w, WcLane *l) {
  for (;;) {
    u32 i = (u32)atomic_u32_inc_eval(&w->job_cursor) - 1;
    if (i >= w->job_count) break;
    WcJob *j = &w->jobs[i];
    u64 t0 = os_time_now();
    f32 elapsed = (f32)os_ticks_to_ms(os_time_diff(t0, w->job_start));
    // a job starts only if it is expected to finish in its round; a lane's first job only needs time left
    b32 fits = elapsed + w->job_est_ms[j->kind] <= w->job_round_end_ms ||
               (l->phase_jobs == 0 && elapsed < w->job_round_end_ms);
    if (!j->urgent && !fits) continue;
    l->phase_jobs++;
    WcChunk *c = &w->chunks[j->slot];
    switch (j->kind) {
    case WC_JOB_GEN:
      wc_run_gen(w, l, c);
      j->done = 1;
      break;
    case WC_JOB_LIGHT:
      wc_light_window(w, &l->light, c->cx, c->cz);
      wc_light_chunk(&l->light, c);
      c->state = WC_CHUNK_LIT;
      j->done = 1;
      break;
    case WC_JOB_MESH:
      j->done = (u8)wc_run_mesh(w, l, j);
      break;
    }
    u64 ran = os_time_diff(os_time_now(), t0);
    j->ms = (f32)os_ticks_to_ms(ran);
  }
}

void wc_world_run_jobs(WcWorld *w) {
  // main scheduled before the frame's lane sync, so every lane agrees there is nothing to run
  if (w->job_count == 0) return;
  WcLane *l = &w->lanes[tctx_current()->thread_idx];
  l->phase_jobs = 0;
  for (;;) {
    wc_run_round(w, l);
    lane_sync();
    if (is_main_thread()) {
      w->job_rounds++;
      w->job_more = wc_next_round(w);
    }
    lane_sync();
    if (!w->job_more) break;
  }
}

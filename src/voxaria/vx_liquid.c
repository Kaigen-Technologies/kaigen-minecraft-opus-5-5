#include "voxaria/vx_game.h"

// liquid cellular automaton: levels 1..8 fall, spread toward lower neighbours and settle; water on lava is obsidian

#define VX_LIQ_EMPTY 0ull

force_inline b32 vx_is_liquid(u8 id) { return vx_shape[id] == VX_SHAPE_LIQUID; }

// a liquid's amount in eighths: state 0 is full
force_inline u32 vx_liq_amount(u8 id, u8 state) { return vx_is_liquid(id) ? 8u - (state & 7u) : 0u; }

force_inline u64 vx_liq_key(i32 x, i32 y, i32 z) {
  return ((u64)(u32)(x + (1 << 20)) << 29 | (u64)(u32)(z + (1 << 20)) << 8 | (u64)(u32)y) + 1;
}

void vx_liquids_init(VxLiquids *l, Allocator *alloc) {
  *l = (VxLiquids){0};
  l->set_cap = VX_LIQ_SET_CAP;
  l->set = ALLOC_ARRAY(alloc, u64, l->set_cap);
  l->ring = ALLOC_ARRAY_NO_ZERO(alloc, VxLiqCell, VX_LIQ_RING_CAP);
}

void vx_liquids_clear(VxLiquids *l) {
  mem_zero(l->set, sizeof(u64) * l->set_cap);
  l->head = 0;
  l->count = 0;
}

// open addressing with linear probing; removal shifts the probe run back so no tombstones are needed
hz_internal b32 vx_liq_set_add(VxLiquids *l, u64 key) {
  u32 mask = l->set_cap - 1;
  for (u32 h = (u32)(key * 0x9e3779b97f4a7c15ull >> 40) & mask;; h = (h + 1) & mask) {
    if (l->set[h] == key) return false;
    if (l->set[h] == VX_LIQ_EMPTY) {
      l->set[h] = key;
      return true;
    }
  }
}

hz_internal void vx_liq_set_remove(VxLiquids *l, u64 key) {
  u32 mask = l->set_cap - 1;
  u32 h = (u32)(key * 0x9e3779b97f4a7c15ull >> 40) & mask;
  while (l->set[h] != key) {
    if (l->set[h] == VX_LIQ_EMPTY) return;
    h = (h + 1) & mask;
  }
  l->set[h] = VX_LIQ_EMPTY;
  for (u32 j = (h + 1) & mask; l->set[j] != VX_LIQ_EMPTY; j = (j + 1) & mask) {
    u64 k = l->set[j];
    u32 home = (u32)(k * 0x9e3779b97f4a7c15ull >> 40) & mask;
    // k moves into the hole when its home is not between the hole and j (cyclically)
    b32 stay = h <= j ? (home > h && home <= j) : (home > h || home <= j);
    if (stay) continue;
    l->set[h] = k;
    l->set[j] = VX_LIQ_EMPTY;
    h = j;
  }
}

void vx_liquids_activate(VxLiquids *l, i32 x, i32 y, i32 z) {
  if (y < 0 || y >= VX_HEIGHT) return;
  // half the set's capacity bounds the active cells, so probes stay short
  if (l->count >= VX_LIQ_RING_CAP || l->count * 2 >= l->set_cap) {
    l->dropped++;
    return;
  }
  if (!vx_liq_set_add(l, vx_liq_key(x, y, z))) return;
  l->ring[(l->head + l->count) & (VX_LIQ_RING_CAP - 1)] = (VxLiqCell){x, y, z};
  l->count++;
}

// a cell and its six neighbours, after an edit or a liquid change there
void vx_liquids_touch(VxLiquids *l, const VxWorld *w, i32 x, i32 y, i32 z) {
  for (i32 k = 0; k < 7; k++) {
    i32 nx = x + (k == 1) - (k == 2), ny = y + (k == 3) - (k == 4), nz = z + (k == 5) - (k == 6);
    if (vx_is_liquid(vx_world_block(w, nx, ny, nz))) vx_liquids_activate(l, nx, ny, nz);
  }
}

// a cell liquid may move into: air or a plant it washes away, never a cell in an unloaded chunk
force_inline b32 vx_liq_open(const VxWorld *w, i32 x, i32 y, i32 z, u8 id) {
  if (y < 0 || y >= VX_HEIGHT || !vx_in_world(x, z)) return false;
  VxChunk *c = vx_world_chunk(w, x >> 4, z >> 4);
  if (!c || c->state != VX_CHUNK_LIT) return false;
  return id == B_AIR || (vx_replaceable[id] && !vx_is_liquid(id) && vx_shape[id] == VX_SHAPE_CROSS);
}

// false when the cell could not be written (its chunk is not ready), so the caller keeps its own amount
hz_internal b32 vx_liq_put(VxLiquids *l, VxWorld *w, i32 x, i32 y, i32 z, u8 id, u32 amount) {
  u8 nid = amount ? id : B_AIR;
  u8 st = amount ? (u8)(8u - amount) : 0;
  if (vx_world_block(w, x, y, z) == nid && vx_world_state(w, x, y, z) == st) return true;
  if (!vx_world_set_block_state(w, x, y, z, nid, st, true)) return false;
  l->changes++;
  vx_liquids_touch(l, w, x, y, z);
  return true;
}

// water and lava meeting: the lava hardens to obsidian, the water is spent on it
hz_internal b32 vx_liq_react(VxLiquids *l, VxWorld *w, u8 id, i32 x, i32 y, i32 z, u8 other, i32 ox, i32 oy, i32 oz) {
  b32 hot = id == B_LAVA, wet = id == B_WATER;
  if (!((hot && other == B_WATER) || (wet && other == B_LAVA))) return false;
  if (hot) {
    vx_world_set_block_state(w, x, y, z, B_OBSIDIAN, 0, true);
  } else {
    vx_world_set_block_state(w, ox, oy, oz, B_OBSIDIAN, 0, true);
    vx_liq_put(l, w, x, y, z, id, 0);
  }
  l->reactions++;
  vx_liquids_touch(l, w, x, y, z);
  vx_liquids_touch(l, w, ox, oy, oz);
  return true;
}

// one cell's step; false when it stayed as it was
hz_internal b32 vx_liq_step_cell(VxLiquids *l, VxWorld *w, i32 x, i32 y, i32 z) {
  u8 id = vx_world_block(w, x, y, z);
  if (!vx_is_liquid(id)) return false;
  u32 a = vx_liq_amount(id, vx_world_state(w, x, y, z));
  // fall: as much as the cell below can take
  u8 below = vx_world_block(w, x, y - 1, z);
  if (vx_liq_react(l, w, id, x, y, z, below, x, y - 1, z)) return true;
  u32 b = below == id ? vx_liq_amount(below, vx_world_state(w, x, y - 1, z)) : 0;
  if (below == id || vx_liq_open(w, x, y - 1, z, below)) {
    if (b < 8) {
      u32 m = MIN(a, 8 - b);
      if (!vx_liq_put(l, w, x, y - 1, z, id, b + m)) return false;
      vx_liq_put(l, w, x, y, z, id, a - m);
      return true;
    }
  }
  // spread: level out with the horizontal neighbours at least two eighths lower (amts: 4 bits per candidate)
  u32 cand_mask = 0, total = a, n = 1, amts = 0;
  for (u32 k = 0; k < 4; k++) {
    i32 nx = x + (k == 0) - (k == 1), nz = z + (k == 2) - (k == 3);
    u8 nb = vx_world_block(w, nx, y, nz);
    if (vx_liq_react(l, w, id, x, y, z, nb, nx, y, nz)) return true;
    u32 na;
    if (nb == id) na = vx_liq_amount(nb, vx_world_state(w, nx, y, nz));
    else if (vx_liq_open(w, nx, y, nz, nb)) na = 0;
    else continue;
    if (na + 2 > a) continue;
    amts |= na << (k * 4);
    cand_mask |= 1u << k;
    total += na;
    n++;
  }
  if (!cand_mask) return false;
  u32 avg = total / n, rem = total - avg * n;
  if (avg == 0) return false;
  // what a neighbour could not take stays here
  u32 keep = avg + rem;
  for (u32 k = 0; k < 4; k++) {
    if (!(cand_mask & (1u << k))) continue;
    u32 na = (amts >> (k * 4)) & 15u;
    if (na == avg) continue;
    if (!vx_liq_put(l, w, x + (k == 0) - (k == 1), y, z + (k == 2) - (k == 3), id, avg)) keep += avg - na;
  }
  if (keep == a) return false;
  vx_liq_put(l, w, x, y, z, id, keep);
  return true;
}

void vx_liquids_step(VxLiquids *l, VxWorld *w, u32 budget) {
  l->tick++;
  u32 n = MIN(l->count, budget);
  for (u32 i = 0; i < n; i++) {
    VxLiqCell c = l->ring[l->head];
    l->head = (l->head + 1) & (VX_LIQ_RING_CAP - 1);
    l->count--;
    vx_liq_set_remove(l, vx_liq_key(c.x, c.y, c.z));
    u8 id = vx_world_block(w, c.x, c.y, c.z);
    // lava creeps: it moves on every third step, honey on every second
    u32 period = id == B_LAVA ? 3u : id == B_HONEY ? 2u : 1u;
    if (l->tick % period != 0) {
      vx_liquids_activate(l, c.x, c.y, c.z);
      continue;
    }
    vx_liq_step_cell(l, w, c.x, c.y, c.z);
  }
}

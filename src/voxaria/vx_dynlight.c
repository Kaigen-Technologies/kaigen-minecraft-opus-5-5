#include "voxaria/vx_game.h"

// held-light flood fill: a VX_DYN_SIZE^3 window around the player, spreading like block light so walls stop it

#define VX_DYN_N (VX_DYN_SIZE * VX_DYN_SIZE * VX_DYN_SIZE)
#define VX_DYN_UNKNOWN 255

void vx_dynlight_init(VxDynLight *d, Allocator *alloc) {
  *d = (VxDynLight){0};
  d->texels = ALLOC_ARRAY(alloc, u32, VX_DYN_N);
  d->dist = ALLOC_ARRAY_NO_ZERO(alloc, u8, VX_DYN_N);
  d->opacity = ALLOC_ARRAY_NO_ZERO(alloc, u8, VX_DYN_N);
  d->queue = ALLOC_ARRAY_NO_ZERO(alloc, u32, VX_DYN_N);
  d->queued = ALLOC_ARRAY_NO_ZERO(alloc, u8, VX_DYN_N);
}

force_inline u32 vx_dyn_index(i32 x, i32 y, i32 z) { return (u32)(x | (z << 5) | (y << 10)); }

// opacity of a window cell, fetched from the world once per fill; unloaded cells block
force_inline u8 vx_dyn_opacity(VxDynLight *d, const VxWorld *w, u32 i, i32 x, i32 y, i32 z) {
  if (d->opacity[i] == VX_DYN_UNKNOWN) {
    i32 wy = d->origin.y + y;
    d->opacity[i] = wy < 0 || wy >= VX_HEIGHT ? 15 : vx_light_opacity[vx_world_block(w, d->origin.x + x, wy, d->origin.z + z)];
  }
  return d->opacity[i];
}

b32 vx_dynlight_update(VxDynLight *d, const VxWorld *w, VxV3d src, u8 item) {
  u16 em = vx_emission[item];
  if (em == 0) {
    d->lit = false;
    d->key_item = 0;
    return false;
  }
  iv3 cell = {vx_floor_i(src.x), vx_floor_i(src.y), vx_floor_i(src.z)};
  if (d->lit && d->key_item == item && d->key_edits == w->edit_serial && d->key_cell.x == cell.x &&
      d->key_cell.y == cell.y && d->key_cell.z == cell.z)
    return true;
  d->lit = true;
  d->key_item = item;
  d->key_edits = w->edit_serial;
  d->key_cell = cell;
  d->origin = (iv3){cell.x - VX_DYN_SIZE / 2, cell.y - VX_DYN_SIZE / 2, cell.z - VX_DYN_SIZE / 2};
  d->version++;

  mem_set(d->dist, 255, VX_DYN_N);
  mem_set(d->opacity, VX_DYN_UNKNOWN, VX_DYN_N);
  mem_zero(d->queued, VX_DYN_N);
  // a FIFO with improvement re-queues: a cell sits in it at most once, so VX_DYN_N slots suffice
  u32 head = 0, count = 0;
  const i32 c = VX_DYN_SIZE / 2;
  u32 s = vx_dyn_index(c, c, c);
  d->dist[s] = 0;
  d->queue[0] = s;
  d->queued[s] = 1;
  count = 1;
  while (count) {
    u32 i = d->queue[head];
    head = (head + 1) & (VX_DYN_N - 1);
    count--;
    d->queued[i] = 0;
    i32 x = (i32)(i & 31), z = (i32)((i >> 5) & 31), y = (i32)(i >> 10);
    u32 di = d->dist[i];
    for (u32 k = 0; k < 6; k++) {
      i32 nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3), nz = z + (k == 4) - (k == 5);
      if ((u32)nx >= VX_DYN_SIZE || (u32)ny >= VX_DYN_SIZE || (u32)nz >= VX_DYN_SIZE) continue;
      u32 n = vx_dyn_index(nx, ny, nz);
      u32 nd = di + 1 + vx_dyn_opacity(d, w, n, nx, ny, nz);
      if (nd >= 15 || nd >= d->dist[n]) continue;
      d->dist[n] = (u8)nd;
      if (!d->queued[n]) {
        d->queued[n] = 1;
        d->queue[(head + count) & (VX_DYN_N - 1)] = n;
        count++;
      }
    }
  }
  // each channel falls by one level per step, as the block light does
  u32 er = em & 15u, eg = (em >> 4) & 15u, eb = (em >> 8) & 15u;
  for (u32 i = 0; i < VX_DYN_N; i++) {
    u32 dd = d->dist[i];
    if (dd >= 15) {
      d->texels[i] = 0;
      continue;
    }
    u32 r = er > dd ? er - dd : 0, g = eg > dd ? eg - dd : 0, b = eb > dd ? eb - dd : 0;
    d->texels[i] = (r * 17) | ((g * 17) << 8) | ((b * 17) << 16) | (255u << 24);
  }
  return true;
}

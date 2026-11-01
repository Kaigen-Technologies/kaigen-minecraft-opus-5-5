#include "voxaria/vx.h"

// window coords: wx, wz in [0, 48), y in [0, 256); the center chunk is (1, 1)
#define VX_WIN 48
#define VX_DIRS 6
#define VX_DOWN 3
hz_internal const i32 VX_DX[VX_DIRS] = {1, -1, 0, 0, 0, 0};
hz_internal const i32 VX_DY[VX_DIRS] = {0, 0, 1, -1, 0, 0};
hz_internal const i32 VX_DZ[VX_DIRS] = {0, 0, 0, 0, 1, -1};

#define VX_LIGHT_QUEUE_START (1u << 16)
#define VX_SKY VX_LIGHT_SKY_SHIFT

void vx_light_init(VxLightEngine *e, Allocator *alloc) {
  memzero_struct(e);
  e->alloc = alloc;
  e->qcap = VX_LIGHT_QUEUE_START;
  e->rcap = VX_LIGHT_QUEUE_START;
  e->q = ALLOC_ARRAY_NO_ZERO(alloc, u32, e->qcap);
  e->rq = ALLOC_ARRAY_NO_ZERO(alloc, u32, e->rcap);
}

// rings keep monotonic head/tail; growth unwraps the live range in order
hz_internal void vx_ring_grow(Allocator *alloc, u32 **buf, u32 *cap, u32 *head,
                              u32 *tail) {
  u32 n = *tail - *head;
  u32 new_cap = *cap * 2;
  u32 *nb = ALLOC_ARRAY_NO_ZERO(alloc, u32, new_cap);
  for (u32 i = 0; i < n; i++) nb[i] = (*buf)[(*head + i) & (*cap - 1)];
  *buf = nb;
  *cap = new_cap;
  *head = 0;
  *tail = n;
}

force_inline void vx_push(VxLightEngine *e, u32 entry) {
  if (e->qt - e->qh == e->qcap) vx_ring_grow(e->alloc, &e->q, &e->qcap, &e->qh, &e->qt);
  e->q[e->qt & (e->qcap - 1)] = entry;
  e->qt++;
}

force_inline void vx_rpush(VxLightEngine *e, u32 entry) {
  if (e->rt - e->rh == e->rcap) vx_ring_grow(e->alloc, &e->rq, &e->rcap, &e->rh, &e->rt);
  e->rq[e->rt & (e->rcap - 1)] = entry;
  e->rt++;
}

force_inline u32 vx_coord(i32 wx, i32 y, i32 wz) {
  return (u32)wx | ((u32)wz << 6) | ((u32)y << 12);
}

force_inline u32 vx_nib(u32 l, u32 shift) { return (l >> shift) & 15u; }

// a light write: the light volume re-uploads the cell's section
force_inline void vx_touch(VxLightEngine *e, VxChunk *c, i32 y) {
  e->writes++;
  c->light_version[y >> 4]++;
}

// componentwise max(a, b - 1 - op) over the three block-light channels, 0 floor
force_inline u32 vx_rgb_spread(u32 src, i32 op) {
  u32 out = 0;
  for (u32 s = 0; s < 12; s += 4) {
    i32 v = (i32)vx_nib(src, s) - 1 - op;
    if (v > 0) out |= (u32)v << s;
  }
  return out;
}

force_inline u32 vx_rgb_max(u32 a, u32 b) {
  u32 out = 0;
  for (u32 s = 0; s < 12; s += 4) {
    u32 x = vx_nib(a, s), y = vx_nib(b, s);
    out |= (x > y ? x : y) << s;
  }
  return out;
}

hz_internal void vx_propagate_sky(VxLightEngine *e) {
  while (e->qh != e->qt) {
    u32 ent = e->q[e->qh & (e->qcap - 1)];
    e->qh++;
    i32 wx = (i32)(ent & 63), wz = (i32)((ent >> 6) & 63), y = (i32)((ent >> 12) & 255);
    VxChunk *c = e->win[(wx >> 4) + (wz >> 4) * 3];
    if (!c) continue;
    i32 level = (i32)vx_nib(c->light[(y << 8) | ((wz & 15) << 4) | (wx & 15)], VX_SKY);
    if (level <= 1) continue;
    for (i32 d = 0; d < VX_DIRS; d++) {
      i32 nx = wx + VX_DX[d], ny = y + VX_DY[d], nz = wz + VX_DZ[d];
      if (nx < 0 || nx >= VX_WIN || nz < 0 || nz >= VX_WIN || ny < 0 || ny >= VX_HEIGHT) continue;
      VxChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
      if (!nc) continue;
      i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
      i32 op = vx_light_opacity[nc->blocks[ni]];
      if (op >= 15) continue;
      i32 nl = (d == VX_DOWN && level == 15) ? 15 - op : level - 1 - op;
      if (nl <= 0) continue;
      if ((i32)vx_nib(nc->light[ni], VX_SKY) >= nl) continue;
      nc->light[ni] = (u16)((nc->light[ni] & VX_LIGHT_RGB_MASK) | ((u32)nl << VX_SKY));
      vx_touch(e, nc, ny);
      vx_push(e, vx_coord(nx, ny, nz));
    }
  }
  e->qh = e->qt = 0;
}

// spreads all three block-light channels at once: a cell re-queues when any channel rises
hz_internal void vx_propagate_rgb(VxLightEngine *e) {
  while (e->qh != e->qt) {
    u32 ent = e->q[e->qh & (e->qcap - 1)];
    e->qh++;
    i32 wx = (i32)(ent & 63), wz = (i32)((ent >> 6) & 63), y = (i32)((ent >> 12) & 255);
    VxChunk *c = e->win[(wx >> 4) + (wz >> 4) * 3];
    if (!c) continue;
    u32 l = c->light[(y << 8) | ((wz & 15) << 4) | (wx & 15)] & VX_LIGHT_RGB_MASK;
    if (vx_rgb_spread(l, 0) == 0) continue;
    for (i32 d = 0; d < VX_DIRS; d++) {
      i32 nx = wx + VX_DX[d], ny = y + VX_DY[d], nz = wz + VX_DZ[d];
      if (nx < 0 || nx >= VX_WIN || nz < 0 || nz >= VX_WIN || ny < 0 || ny >= VX_HEIGHT) continue;
      VxChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
      if (!nc) continue;
      i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
      i32 op = vx_light_opacity[nc->blocks[ni]];
      if (op >= 15) continue;
      u32 cur = nc->light[ni] & VX_LIGHT_RGB_MASK;
      u32 next = vx_rgb_max(cur, vx_rgb_spread(l, op));
      if (next == cur) continue;
      nc->light[ni] = (u16)((nc->light[ni] & ~VX_LIGHT_RGB_MASK) | next);
      vx_touch(e, nc, ny);
      vx_push(e, vx_coord(nx, ny, nz));
    }
  }
  e->qh = e->qt = 0;
}

// removes one channel's light that depended on the queued cells, collecting relight seeds in q
hz_internal void vx_unpropagate(VxLightEngine *e, u32 shift) {
  b32 sky = shift == VX_SKY;
  u16 keep = (u16)~(15u << shift);
  while (e->rh != e->rt) {
    u32 ent = e->rq[e->rh & (e->rcap - 1)];
    e->rh++;
    i32 wx = (i32)(ent & 63), wz = (i32)((ent >> 6) & 63), y = (i32)((ent >> 12) & 255),
        level = (i32)((ent >> 20) & 15);
    for (i32 d = 0; d < VX_DIRS; d++) {
      i32 nx = wx + VX_DX[d], ny = y + VX_DY[d], nz = wz + VX_DZ[d];
      if (nx < 0 || nx >= VX_WIN || nz < 0 || nz >= VX_WIN || ny < 0 || ny >= VX_HEIGHT) continue;
      VxChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
      if (!nc) continue;
      i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
      i32 cur = (i32)vx_nib(nc->light[ni], shift);
      if (cur == 0) continue;
      u8 nb = nc->blocks[ni];
      i32 op = vx_light_opacity[nb];
      i32 expected = (sky && d == VX_DOWN && level == 15) ? 15 - op : level - 1 - op;
      u32 coord = vx_coord(nx, ny, nz);
      if (cur <= expected) {
        nc->light[ni] &= keep;
        vx_touch(e, nc, ny);
        vx_rpush(e, coord | ((u32)cur << 20));
        u32 em = sky ? 0 : vx_nib(vx_emission[nb], shift);
        if (em > 0) {
          nc->light[ni] = (u16)((nc->light[ni] & keep) | (em << shift));
          vx_push(e, coord);
        }
      } else {
        vx_push(e, coord);
      }
    }
  }
  e->rh = e->rt = 0;
}

// a neighbour lit earlier never re-sends light into a chunk regenerated later
hz_internal void vx_seed_from_neighbours(VxLightEngine *e, VxChunk *center, b32 sky) {
  for (i32 side = 0; side < 4; side++) {
    for (i32 k = 0; k < 16; k++) {
      i32 nwx, nwz, cwx, cwz;
      if (side == 0) {
        nwx = 32; cwx = 31; nwz = cwz = 16 + k;
      } else if (side == 1) {
        nwx = 15; cwx = 16; nwz = cwz = 16 + k;
      } else if (side == 2) {
        nwz = 32; cwz = 31; nwx = cwx = 16 + k;
      } else {
        nwz = 15; cwz = 16; nwx = cwx = 16 + k;
      }
      VxChunk *nc = e->win[(nwx >> 4) + (nwz >> 4) * 3];
      if (!nc) continue;
      i32 n_base = ((nwz & 15) << 4) | (nwx & 15);
      i32 c_base = ((cwz & 15) << 4) | (cwx & 15);
      for (i32 y = 0; y < VX_HEIGHT; y++) {
        u32 nl = nc->light[(y << 8) | n_base];
        i32 ci = (y << 8) | c_base;
        i32 op = vx_light_opacity[center->blocks[ci]];
        if (op >= 15) continue;
        b32 improves;
        if (sky) {
          improves = (i32)vx_nib(nl, VX_SKY) - 1 - op > (i32)vx_nib(center->light[ci], VX_SKY);
        } else {
          u32 cur = center->light[ci] & VX_LIGHT_RGB_MASK;
          improves = vx_rgb_max(cur, vx_rgb_spread(nl & VX_LIGHT_RGB_MASK, op)) != cur;
        }
        if (improves) vx_push(e, vx_coord(nwx, y, nwz));
      }
    }
  }
}

void vx_light_chunk(VxLightEngine *e, VxChunk *center) {
  e->writes = 0;
  const u8 *b = center->blocks;
  u16 *l = center->light;
  for (i32 z = 0; z < 16; z++) {
    for (i32 x = 0; x < 16; x++) {
      i32 top = center->h15[(z << 4) | x];
      i32 wx = x + 16, wz = z + 16;
      for (i32 d = 0; d < 4; d++) {
        i32 nx = wx + (d == 0 ? 1 : d == 1 ? -1 : 0);
        i32 nz = wz + (d == 2 ? 1 : d == 3 ? -1 : 0);
        VxChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
        if (!nc) continue;
        i32 h = nc->h15[((nz & 15) << 4) | (nx & 15)];
        if (h > top) top = h;
      }
      for (i32 y = top < VX_HEIGHT - 1 ? top : VX_HEIGHT - 1; y >= 0; y--) {
        i32 i = (y << 8) | (z << 4) | x;
        if (vx_nib(l[i], VX_SKY)) vx_push(e, vx_coord(wx, y, wz));
      }
    }
  }
  vx_seed_from_neighbours(e, center, true);
  vx_propagate_sky(e);
  for (i32 s = 0; s < VX_SECTIONS; s++) {
    if (center->section_count[s] == 0) continue;
    i32 start = s * VX_SECTION_VOLUME;
    for (i32 i = start; i < start + VX_SECTION_VOLUME; i++) {
      u32 em = vx_emission[b[i]];
      if (em == 0) continue;
      l[i] = (u16)((l[i] & ~VX_LIGHT_RGB_MASK) | vx_rgb_max(l[i] & VX_LIGHT_RGB_MASK, em));
      i32 x = i & 15, z = (i >> 4) & 15, y = i >> 8;
      vx_push(e, vx_coord(x + 16, y, z + 16));
    }
  }
  vx_seed_from_neighbours(e, center, false);
  vx_propagate_rgb(e);
  for (i32 s = 0; s < VX_SECTIONS; s++) center->light_version[s]++;
}

hz_internal void vx_seed_open_neighbours(VxLightEngine *e, i32 wx, i32 y, i32 wz, u32 mask) {
  for (i32 d = 0; d < VX_DIRS; d++) {
    i32 nx = wx + VX_DX[d], ny = y + VX_DY[d], nz = wz + VX_DZ[d];
    if (nx < 0 || nx >= VX_WIN || nz < 0 || nz >= VX_WIN || ny < 0 || ny >= VX_HEIGHT) continue;
    VxChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
    if (!nc) continue;
    if (nc->light[(ny << 8) | ((nz & 15) << 4) | (nx & 15)] & mask) vx_push(e, vx_coord(nx, ny, nz));
  }
}

void vx_light_block_changed(VxLightEngine *e, i32 wx, i32 y, i32 wz, u8 new_id) {
  e->writes = 0;
  VxChunk *c = e->win[(wx >> 4) + (wz >> 4) * 3];
  if (!c) return;
  i32 i = (y << 8) | ((wz & 15) << 4) | (wx & 15);
  u32 coord = vx_coord(wx, y, wz);
  b32 open = vx_light_opacity[new_id] < 15;
  // sky: remove, then refill from the surviving neighbours
  u32 lv = vx_nib(c->light[i], VX_SKY);
  if (lv > 0) {
    c->light[i] &= (u16)~VX_LIGHT_SKY_FULL;
    vx_touch(e, c, y);
    vx_rpush(e, coord | (lv << 20));
  }
  vx_unpropagate(e, VX_SKY);
  if (open) vx_seed_open_neighbours(e, wx, y, wz, VX_LIGHT_SKY_FULL);
  vx_propagate_sky(e);
  // block light: each channel removes on its own, then one pass refills all three
  for (u32 s = 0; s < 12; s += 4) {
    lv = vx_nib(c->light[i], s);
    if (lv > 0) {
      c->light[i] &= (u16)~(15u << s);
      vx_touch(e, c, y);
      vx_rpush(e, coord | (lv << 20));
    }
    vx_unpropagate(e, s);
  }
  u32 em = vx_emission[new_id];
  if (em > 0) {
    c->light[i] = (u16)((c->light[i] & ~VX_LIGHT_RGB_MASK) | vx_rgb_max(c->light[i] & VX_LIGHT_RGB_MASK, em));
    vx_touch(e, c, y);
    vx_push(e, coord);
  }
  if (open) vx_seed_open_neighbours(e, wx, y, wz, VX_LIGHT_RGB_MASK);
  vx_propagate_rgb(e);
}

void vx_chunk_recount_sections(VxChunk *c) {
  for (i32 s = 0; s < VX_SECTIONS; s++) {
    i32 n = 0;
    const u8 *b = c->blocks + s * VX_SECTION_VOLUME;
    for (i32 i = 0; i < VX_SECTION_VOLUME; i++) n += b[i] != B_AIR;
    c->section_count[s] = n;
  }
}

// sky light straight down each column; records the lowest fully lit y (h15)
void vx_chunk_column_fill(VxChunk *c) {
  const u8 *b = c->blocks;
  u16 *l = c->light;
  for (i32 z = 0; z < 16; z++) {
    for (i32 x = 0; x < 16; x++) {
      i32 level = 15, h15 = 0;
      i32 y = VX_HEIGHT - 1;
      for (; y >= 0; y--) {
        i32 i = (y << 8) | (z << 4) | x;
        i32 op = vx_light_opacity[b[i]];
        if (op >= 15) break;
        if (level == 15) level = 15 - op;
        else level = level - 1 - op;
        if (level <= 0) break;
        if (level == 15) h15 = y;
        l[i] = (u16)(((u32)level << VX_SKY) | (l[i] & VX_LIGHT_RGB_MASK));
      }
      if (level == 15 && y >= 0) h15 = y + 1;
      c->h15[(z << 4) | x] = (u8)(h15 < 255 ? h15 : 255);
    }
  }
}

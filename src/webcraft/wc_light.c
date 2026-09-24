#include "webcraft/wc.h"

// window coords: wx, wz in [0, 48), y in [0, 256); the center chunk is (1, 1)
#define WC_WIN 48
#define WC_DIRS 6
#define WC_DOWN 3
hz_internal const i32 WC_DX[WC_DIRS] = {1, -1, 0, 0, 0, 0};
hz_internal const i32 WC_DY[WC_DIRS] = {0, 0, 1, -1, 0, 0};
hz_internal const i32 WC_DZ[WC_DIRS] = {0, 0, 0, 0, 1, -1};

#define WC_LIGHT_QUEUE_START (1u << 16)

void wc_light_init(WcLightEngine *e, Allocator *alloc) {
  memzero_struct(e);
  e->alloc = alloc;
  e->qcap = WC_LIGHT_QUEUE_START;
  e->rcap = WC_LIGHT_QUEUE_START;
  e->q = ALLOC_ARRAY_NO_ZERO(alloc, u32, e->qcap);
  e->rq = ALLOC_ARRAY_NO_ZERO(alloc, u32, e->rcap);
}

// rings keep monotonic head/tail; growth unwraps the live range in order
hz_internal void wc_ring_grow(Allocator *alloc, u32 **buf, u32 *cap, u32 *head,
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

force_inline void wc_push(WcLightEngine *e, u32 entry) {
  if (e->qt - e->qh == e->qcap) wc_ring_grow(e->alloc, &e->q, &e->qcap, &e->qh, &e->qt);
  e->q[e->qt & (e->qcap - 1)] = entry;
  e->qt++;
}

force_inline void wc_rpush(WcLightEngine *e, u32 entry) {
  if (e->rt - e->rh == e->rcap) wc_ring_grow(e->alloc, &e->rq, &e->rcap, &e->rh, &e->rt);
  e->rq[e->rt & (e->rcap - 1)] = entry;
  e->rt++;
}

force_inline u32 wc_coord(i32 wx, i32 y, i32 wz) {
  return (u32)wx | ((u32)wz << 6) | ((u32)y << 12);
}

// bumps the mesh version of every section whose padded volume holds the cell
hz_internal void wc_touch(WcLightEngine *e, i32 wx, i32 y, i32 wz) {
  e->writes++;
  i32 lx = wx & 15, lz = wz & 15;
  i32 ccx = wx >> 4, ccz = wz >> 4;
  i32 x0 = lx == 0 ? -1 : 0, x1 = lx == 15 ? 1 : 0;
  i32 z0 = lz == 0 ? -1 : 0, z1 = lz == 15 ? 1 : 0;
  i32 sy = y >> 4, ly = y & 15;
  for (i32 dz = z0; dz <= z1; dz++) {
    i32 cz = ccz + dz;
    if (cz < 0 || cz > 2) continue;
    for (i32 dx = x0; dx <= x1; dx++) {
      i32 cx = ccx + dx;
      if (cx < 0 || cx > 2) continue;
      WcChunk *c = e->win[cx + cz * 3];
      if (!c || !c->mesh_started) continue;
      c->sec_version[sy]++;
      if (ly == 0 && sy > 0) c->sec_version[sy - 1]++;
      if (ly == 15 && sy < 15) c->sec_version[sy + 1]++;
    }
  }
}

hz_internal void wc_propagate(WcLightEngine *e, b32 sky) {
  u32 shift = sky ? 4 : 0;
  u8 keep = sky ? 0x0f : 0xf0;
  while (e->qh != e->qt) {
    u32 ent = e->q[e->qh & (e->qcap - 1)];
    e->qh++;
    i32 wx = (i32)(ent & 63), wz = (i32)((ent >> 6) & 63),
        y = (i32)((ent >> 12) & 255);
    WcChunk *c = e->win[(wx >> 4) + (wz >> 4) * 3];
    if (!c) continue;
    i32 level = (c->light[(y << 8) | ((wz & 15) << 4) | (wx & 15)] >> shift) & 15;
    if (level <= 1) continue;
    for (i32 d = 0; d < WC_DIRS; d++) {
      i32 nx = wx + WC_DX[d], ny = y + WC_DY[d], nz = wz + WC_DZ[d];
      if (nx < 0 || nx >= WC_WIN || nz < 0 || nz >= WC_WIN || ny < 0 ||
          ny >= WC_HEIGHT)
        continue;
      WcChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
      if (!nc) continue;
      i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
      i32 op = wc_light_opacity[nc->blocks[ni]];
      if (op >= 15) continue;
      i32 nl = (sky && d == WC_DOWN && level == 15) ? 15 - op : level - 1 - op;
      if (nl <= 0) continue;
      i32 cur = (nc->light[ni] >> shift) & 15;
      if (cur >= nl) continue;
      nc->light[ni] = (u8)((nc->light[ni] & keep) | (nl << shift));
      wc_touch(e, nx, ny, nz);
      wc_push(e, wc_coord(nx, ny, nz));
    }
  }
  e->qh = e->qt = 0;
}

// removes light that depended on the queued cells, collecting relight seeds
hz_internal void wc_unpropagate(WcLightEngine *e, b32 sky) {
  u32 shift = sky ? 4 : 0;
  u8 keep = sky ? 0x0f : 0xf0;
  while (e->rh != e->rt) {
    u32 ent = e->rq[e->rh & (e->rcap - 1)];
    e->rh++;
    i32 wx = (i32)(ent & 63), wz = (i32)((ent >> 6) & 63),
        y = (i32)((ent >> 12) & 255), level = (i32)((ent >> 20) & 15);
    for (i32 d = 0; d < WC_DIRS; d++) {
      i32 nx = wx + WC_DX[d], ny = y + WC_DY[d], nz = wz + WC_DZ[d];
      if (nx < 0 || nx >= WC_WIN || nz < 0 || nz >= WC_WIN || ny < 0 ||
          ny >= WC_HEIGHT)
        continue;
      WcChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
      if (!nc) continue;
      i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
      i32 cur = (nc->light[ni] >> shift) & 15;
      if (cur == 0) continue;
      u8 nb = nc->blocks[ni];
      i32 op = wc_light_opacity[nb];
      i32 expected = (sky && d == WC_DOWN && level == 15) ? 15 - op : level - 1 - op;
      u32 coord = wc_coord(nx, ny, nz);
      if (cur <= expected) {
        nc->light[ni] &= keep;
        wc_touch(e, nx, ny, nz);
        wc_rpush(e, coord | ((u32)cur << 20));
        if (!sky && wc_emission[nb] > 0) {
          nc->light[ni] = (u8)((nc->light[ni] & keep) | (wc_emission[nb] << shift));
          wc_push(e, coord);
        }
      } else {
        wc_push(e, coord);
      }
    }
  }
  e->rh = e->rt = 0;
}

// a neighbour lit earlier never re-sends light into a chunk regenerated later
hz_internal void wc_seed_from_neighbours(WcLightEngine *e, WcChunk *center,
                                         b32 sky) {
  u32 shift = sky ? 4 : 0;
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
      WcChunk *nc = e->win[(nwx >> 4) + (nwz >> 4) * 3];
      if (!nc) continue;
      i32 n_base = ((nwz & 15) << 4) | (nwx & 15);
      i32 c_base = ((cwz & 15) << 4) | (cwx & 15);
      for (i32 y = 0; y < WC_HEIGHT; y++) {
        i32 nl = (nc->light[(y << 8) | n_base] >> shift) & 15;
        if (nl <= 1) continue;
        i32 ci = (y << 8) | c_base;
        i32 op = wc_light_opacity[center->blocks[ci]];
        if (op >= 15) continue;
        if (nl - 1 - op > ((center->light[ci] >> shift) & 15))
          wc_push(e, wc_coord(nwx, y, nwz));
      }
    }
  }
}

void wc_light_chunk(WcLightEngine *e, WcChunk *center) {
  e->writes = 0;
  const u8 *b = center->blocks;
  u8 *l = center->light;
  for (i32 z = 0; z < 16; z++) {
    for (i32 x = 0; x < 16; x++) {
      i32 top = center->h15[(z << 4) | x];
      i32 wx = x + 16, wz = z + 16;
      for (i32 d = 0; d < 4; d++) {
        i32 nx = wx + (d == 0 ? 1 : d == 1 ? -1 : 0);
        i32 nz = wz + (d == 2 ? 1 : d == 3 ? -1 : 0);
        WcChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
        if (!nc) continue;
        i32 h = nc->h15[((nz & 15) << 4) | (nx & 15)];
        if (h > top) top = h;
      }
      for (i32 y = top < WC_HEIGHT - 1 ? top : WC_HEIGHT - 1; y >= 0; y--) {
        i32 i = (y << 8) | (z << 4) | x;
        if (l[i] >> 4) wc_push(e, wc_coord(wx, y, wz));
      }
    }
  }
  wc_seed_from_neighbours(e, center, true);
  wc_propagate(e, true);
  for (i32 s = 0; s < WC_SECTIONS; s++) {
    if (center->section_count[s] == 0) continue;
    i32 start = s * WC_SECTION_VOLUME;
    for (i32 i = start; i < start + WC_SECTION_VOLUME; i++) {
      i32 em = wc_emission[b[i]];
      if (em > 0) {
        if ((l[i] & 15) < em) l[i] = (u8)((l[i] & 0xf0) | em);
        i32 x = i & 15, z = (i >> 4) & 15, y = i >> 8;
        wc_push(e, wc_coord(x + 16, y, z + 16));
      }
    }
  }
  wc_seed_from_neighbours(e, center, false);
  wc_propagate(e, false);
}

void wc_light_block_changed(WcLightEngine *e, i32 wx, i32 y, i32 wz, u8 new_id) {
  e->writes = 0;
  WcChunk *c = e->win[(wx >> 4) + (wz >> 4) * 3];
  if (!c) return;
  i32 i = (y << 8) | ((wz & 15) << 4) | (wx & 15);
  u32 coord = wc_coord(wx, y, wz);
  for (i32 pass = 0; pass < 2; pass++) {
    b32 sky = pass == 0;
    u32 shift = sky ? 4 : 0;
    u8 keep = sky ? 0x0f : 0xf0;
    i32 lv = (c->light[i] >> shift) & 15;
    if (lv > 0) {
      c->light[i] &= keep;
      wc_touch(e, wx, y, wz);
      wc_rpush(e, coord | ((u32)lv << 20));
    }
    wc_unpropagate(e, sky);
    if (!sky && wc_emission[new_id] > 0) {
      c->light[i] = (u8)((c->light[i] & keep) | (wc_emission[new_id] << shift));
      wc_touch(e, wx, y, wz);
      wc_push(e, coord);
    }
    if (wc_light_opacity[new_id] < 15) {
      for (i32 d = 0; d < WC_DIRS; d++) {
        i32 nx = wx + WC_DX[d], ny = y + WC_DY[d], nz = wz + WC_DZ[d];
        if (nx < 0 || nx >= WC_WIN || nz < 0 || nz >= WC_WIN || ny < 0 ||
            ny >= WC_HEIGHT)
          continue;
        WcChunk *nc = e->win[(nx >> 4) + (nz >> 4) * 3];
        if (!nc) continue;
        i32 ni = (ny << 8) | ((nz & 15) << 4) | (nx & 15);
        if ((nc->light[ni] >> shift) & 15) wc_push(e, wc_coord(nx, ny, nz));
      }
    }
    wc_propagate(e, sky);
  }
}

void wc_chunk_recount_sections(WcChunk *c) {
  for (i32 s = 0; s < WC_SECTIONS; s++) {
    i32 n = 0;
    const u8 *b = c->blocks + s * WC_SECTION_VOLUME;
    for (i32 i = 0; i < WC_SECTION_VOLUME; i++) n += b[i] != B_AIR;
    c->section_count[s] = n;
  }
}

// sky light straight down each column; records the lowest fully lit y (h15)
void wc_chunk_column_fill(WcChunk *c) {
  const u8 *b = c->blocks;
  u8 *l = c->light;
  for (i32 z = 0; z < 16; z++) {
    for (i32 x = 0; x < 16; x++) {
      i32 level = 15, h15 = 0;
      i32 y = WC_HEIGHT - 1;
      for (; y >= 0; y--) {
        i32 i = (y << 8) | (z << 4) | x;
        i32 op = wc_light_opacity[b[i]];
        if (op >= 15) break;
        if (level == 15) level = 15 - op;
        else level = level - 1 - op;
        if (level <= 0) break;
        if (level == 15) h15 = y;
        l[i] = (u8)((level << 4) | (l[i] & 15));
      }
      if (level == 15 && y >= 0) h15 = y + 1;
      c->h15[(z << 4) | x] = (u8)(h15 < 255 ? h15 : 255);
    }
  }
}

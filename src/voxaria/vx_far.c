#include "voxaria/vx.h"

// far quad (vx_far.glsl): w0 x10 z10 (+VX_WORLD_HALF) y9 face3, w1 extent-1 along the face's two axes 9+9, layer10 flags4
#define VX_FAR_F_WATER 1u
#define VX_FAR_F_FOLIAGE 2u
#define VX_FAR_F_LAVA 4u

i32 vx_far_tile_of(i32 cx, i32 cz) {
  i32 x = cx - VX_FAR_C0, z = cz - VX_FAR_C0;
  if (x < 0 || z < 0 || x >= VX_FAR_CHUNKS || z >= VX_FAR_CHUNKS) return -1;
  return z * VX_FAR_CHUNKS + x;
}

// what the far terrain keeps: cubes and liquids; plants, torches and furniture are too small to show
force_inline b32 vx_far_solid(u8 id) { return vx_shape[id] == VX_SHAPE_CUBE || vx_shape[id] == VX_SHAPE_LIQUID; }

// the most common solid id of the cell's four columns at row y, or fallback when none is solid
hz_internal u8 vx_far_pick(const u8 *blocks, i32 col, i32 y, u8 fallback) {
  if (y < 0) return fallback;
  const u8 *row = blocks + (y << 8) + col;
  u8 best = fallback;
  u32 best_n = 0;
  for (u32 i = 0; i < 4; i++) {
    u8 id = row[(i & 1) + (i >> 1) * 16];
    if (!vx_far_solid(id)) continue;
    u32 n = (row[0] == id) + (row[1] == id) + (row[16] == id) + (row[17] == id);
    if (n > best_n) best_n = n, best = id;
  }
  return best;
}

force_inline b32 vx_far_row_solid(const u8 *blocks, i32 col, i32 y) {
  const u8 *row = blocks + (y << 8) + col;
  return vx_far_solid(row[0]) || vx_far_solid(row[1]) || vx_far_solid(row[16]) || vx_far_solid(row[17]);
}

void vx_far_reduce(const u8 *blocks, VxFarTile *t) {
  for (i32 c = 0; c < VX_FAR_CELLS; c++) {
    i32 col = ((c >> 3) * 2 << 4) | ((c & 7) * 2);
    i32 ground = 0;
    while (ground < VX_HEIGHT && vx_far_row_solid(blocks, col, ground)) ground++;
    u8 count = 0;
    i32 y = VX_HEIGHT - 1;
    while (y >= 0 && count < VX_FAR_RUNS) {
      while (y >= 0 && !vx_far_row_solid(blocks, col, y)) y--;
      if (y < 0) break;
      // the last slot always holds the ground run, so stacked overhangs never stretch down into pillars
      if (count == VX_FAR_RUNS - 1 && y >= ground) y = ground - 1;
      if (y < 0) break;
      i32 top = y;
      // a single empty row inside a run does not split it: runs are few
      for (;;) {
        while (y >= 0 && vx_far_row_solid(blocks, col, y)) y--;
        if (y < 1 || !vx_far_row_solid(blocks, col, y - 1)) break;
        y--;
      }
      i32 bot = y + 1;
      u8 top_id = vx_far_pick(blocks, col, top, B_STONE);
      t->run[c][count] = (VxFarRun){
          .top = (u8)top,
          .bot = (u8)bot,
          .top_id = top_id,
          .sub_id = vx_far_pick(blocks, col, top - 1, top_id),
          .bot_id = vx_far_pick(blocks, col, bot, top_id),
      };
      count++;
    }
    t->count[c] = count;
  }
}

// ---- region meshing ----

typedef struct {
  u32 *out;
  u32 cap, n;
  u8 lo, hi;
} VxFarOut;

hz_internal void vx_far_emit(VxFarOut *o, i32 x, i32 y, i32 z, u32 face, u32 ea, u32 eb, u32 layer, u32 flags) {
  if (o->n < o->cap) {
    u32 *q = o->out + o->n * VX_QUAD_U32;
    q[0] = (u32)(x + VX_WORLD_HALF) | (u32)(z + VX_WORLD_HALF) << 10 | (u32)y << 20 | face << 29;
    q[1] = (ea - 1) | (eb - 1) << 9 | layer << 18 | flags << 28;
    q[2] = 0;
  }
  o->n++;
  if ((u8)y < o->lo) o->lo = (u8)y;
  i32 top = face == VX_FACE_PY ? y + 1 : face < 2 ? y + (i32)ea : face >= 4 ? y + (i32)eb : y;
  if (top > o->hi) o->hi = (u8)(top > 255 ? 255 : top);
}

force_inline u32 vx_far_layer(u8 id, u32 face) { return vx_tex_layer[vx_face_tex[id][face]]; }

force_inline u32 vx_far_flags(u8 id) {
  if (id == B_WATER) return VX_FAR_F_WATER;
  if (id == B_LAVA) return VX_FAR_F_LAVA;
  return vx_wave[id] == VX_WAVE_LEAVES ? VX_FAR_F_FOLIAGE : 0;
}

// the runs of the cell at chunk-relative cell (x, z) of tile (tx, tz), stepping into neighbour tiles; NULL when unknown
hz_internal const VxFarRun *vx_far_cell(const VxFar *f, i32 tx, i32 tz, i32 x, i32 z, u32 *count) {
  if (x < 0) tx--, x += 8;
  if (x > 7) tx++, x -= 8;
  if (z < 0) tz--, z += 8;
  if (z > 7) tz++, z -= 8;
  if (tx < 0 || tz < 0 || tx >= VX_FAR_CHUNKS || tz >= VX_FAR_CHUNKS) {
    *count = 0;
    return NULL;
  }
  u32 ti = (u32)(tz * VX_FAR_CHUNKS + tx);
  if (!(f->tile_flags[ti] & VX_FAR_HAS)) {
    *count = 0;
    return NULL;
  }
  *count = f->tiles[ti].count[z * 8 + x];
  return f->tiles[ti].run[z * 8 + x];
}

// past the island grid lies open sea down to the floor
hz_internal const VxFarRun VX_FAR_OCEAN = {.top = VX_SEA_LEVEL, .bot = 0, .top_id = B_WATER, .sub_id = B_SAND, .bot_id = B_SAND};

// a side span: block rows y0..y1 of one face of a cell, with its tile
typedef struct {
  u8 y0, y1, used;
  u16 layer;
  u8 flags;
} VxFarSide;

#define VX_FAR_SIDE_MAX 12

// the parts of rows bot..top that no neighbour run covers, each cut into the top block's band, the soil under it and a floating run's base
hz_internal u32 vx_far_exposed(const VxFarRun *r, const VxFarRun *nb, u32 nn, b32 known, u32 face, VxFarSide *out) {
  if (!known) return 0;
  i32 lo = r->bot, hi = r->top;
  u32 n = 0;
  // the runs come top down: walk the gaps between them from the top
  i32 cur = hi;
  for (u32 k = 0; k <= nn && cur >= lo; k++) {
    i32 gap_lo = k < nn ? nb[k].top + 1 : lo;
    if (gap_lo < lo) gap_lo = lo;
    if (cur >= gap_lo) {
      i32 a = gap_lo, b = cur;
      // the top block's band shows its own side, the rest the block under it
      u32 lt = vx_far_layer(r->top_id, face), ls = vx_far_layer(r->sub_id, face);
      // water sides take the opaque texture: the deferred water shade is for surfaces facing the sky
      u32 ft = vx_far_flags(r->top_id) & ~VX_FAR_F_WATER, fs = vx_far_flags(r->sub_id) & ~VX_FAR_F_WATER;
      if (b == r->top && (lt != ls || ft != fs) && n < VX_FAR_SIDE_MAX) {
        out[n++] = (VxFarSide){.y0 = (u8)b, .y1 = (u8)b, .layer = (u16)lt, .flags = (u8)ft};
        b--;
      }
      // a floating run (a sky isle's cloud bowl) shows its bottom block below the top's soil
      u32 lb = vx_far_layer(r->bot_id, face), fb = vx_far_flags(r->bot_id) & ~VX_FAR_F_WATER;
      i32 soil = r->top - 3;
      if (r->bot > 0 && (lb != ls || fb != fs) && a < soil && n + 1 < VX_FAR_SIDE_MAX) {
        out[n++] = (VxFarSide){.y0 = (u8)a, .y1 = (u8)MIN(b, soil - 1), .layer = (u16)lb, .flags = (u8)fb};
        a = soil;
      }
      if (b >= a && n < VX_FAR_SIDE_MAX) out[n++] = (VxFarSide){.y0 = (u8)a, .y1 = (u8)b, .layer = (u16)ls, .flags = (u8)fs};
    }
    if (k < nn && nb[k].bot - 1 < cur) cur = nb[k].bot - 1;
  }
  return n;
}

u32 vx_far_mesh_region(const VxFar *f, u32 r, u32 *out, u32 cap, u8 *lo, u8 *hi) {
  VxFarOut o = {.out = out, .cap = cap, .lo = 255, .hi = 0};
  i32 rx = (i32)(r % VX_FAR_REGIONS) * VX_FAR_REGION, rz = (i32)(r / VX_FAR_REGIONS) * VX_FAR_REGION;
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u8 *done = ALLOC_ARRAY_NO_ZERO(&ta.allocator, u8, VX_FAR_CELLS * VX_FAR_RUNS);
  VxFarSide *sides = ALLOC_ARRAY_NO_ZERO(&ta.allocator, VxFarSide, 8 * 8 * VX_FAR_RUNS * VX_FAR_SIDE_MAX);
  u32 *side_n = ALLOC_ARRAY_NO_ZERO(&ta.allocator, u32, 8 * 8 * VX_FAR_RUNS);
  for (i32 tz = rz; tz < rz + VX_FAR_REGION; tz++)
    for (i32 tx = rx; tx < rx + VX_FAR_REGION; tx++) {
      u32 ti = (u32)(tz * VX_FAR_CHUNKS + tx);
      if (!(f->tile_flags[ti] & VX_FAR_HAS)) continue;
      const VxFarTile *t = &f->tiles[ti];
      i32 x0 = (tx + VX_FAR_C0) * VX_CHUNK, z0 = (tz + VX_FAR_C0) * VX_CHUNK;

      // tops and bottoms: greedy rectangles of equal height and tile inside the chunk
      for (u32 pass = 0; pass < 2; pass++) {
        b32 up = pass == 0;
        u32 face = up ? VX_FACE_PY : VX_FACE_NY;
        mem_zero(done, VX_FAR_CELLS * VX_FAR_RUNS);
        for (i32 c = 0; c < VX_FAR_CELLS; c++)
          for (u32 k = 0; k < t->count[c]; k++) {
            if (done[c * VX_FAR_RUNS + k]) continue;
            const VxFarRun *run = &t->run[c][k];
            if (!up && run->bot == 0) continue;
            // open sea is the ocean plane's
            if (up && run->top_id == B_WATER && run->top == VX_SEA_LEVEL) continue;
            u8 id = up ? run->top_id : run->bot_id;
            u8 y = up ? run->top : run->bot;
            u32 layer = vx_far_layer(id, face), flags = vx_far_flags(id);
            i32 cx = c & 7, cz = c >> 3;
            // the run at cell (x, z) with the same face, or -1
#define VX_FAR_MATCH(cell, kk)                                                                                      \
  for (kk = 0; kk < (i32)t->count[cell]; kk++) {                                                                    \
    const VxFarRun *q = &t->run[cell][kk];                                                                          \
    u8 qid = up ? q->top_id : q->bot_id;                                                                            \
    if (!done[(cell) * VX_FAR_RUNS + kk] && (up ? q->top : q->bot) == y && (up || q->bot != 0) &&                   \
        vx_far_layer(qid, face) == layer && vx_far_flags(qid) == flags &&                                            \
        !(up && qid == B_WATER && q->top == VX_SEA_LEVEL))                                                            \
      break;                                                                                                        \
  }                                                                                                                 \
  if (kk == (i32)t->count[cell]) kk = -1;
            i32 w = 1;
            for (; cx + w < 8; w++) {
              i32 kk;
              VX_FAR_MATCH(c + w, kk);
              if (kk < 0) break;
            }
            i32 h = 1;
            for (; cz + h < 8; h++) {
              b32 row_ok = true;
              for (i32 i = 0; i < w && row_ok; i++) {
                i32 kk;
                VX_FAR_MATCH(c + h * 8 + i, kk);
                row_ok = kk >= 0;
              }
              if (!row_ok) break;
            }
            for (i32 j = 0; j < h; j++)
              for (i32 i = 0; i < w; i++) {
                i32 cell = c + j * 8 + i, kk;
                if (cell == c) {
                  done[c * VX_FAR_RUNS + k] = 1;
                  continue;
                }
                VX_FAR_MATCH(cell, kk);
                if (kk >= 0) done[cell * VX_FAR_RUNS + kk] = 1;
              }
#undef VX_FAR_MATCH
            vx_far_emit(&o, x0 + cx * 2, y, z0 + cz * 2, face, (u32)w * 2, (u32)h * 2, layer, flags);
          }
      }

      // sides: each run's rows no neighbour covers, merged along the face where cells agree
      for (u32 face = 0; face < 6; face++) {
        if (face == VX_FACE_PY || face == VX_FACE_NY) continue;
        i32 dx = face == VX_FACE_PX ? 1 : face == VX_FACE_NX ? -1 : 0;
        i32 dz = face == VX_FACE_PZ ? 1 : face == VX_FACE_NZ ? -1 : 0;
        for (i32 c = 0; c < VX_FAR_CELLS; c++) {
          i32 cx = c & 7, cz = c >> 3;
          u32 nn;
          const VxFarRun *nb = vx_far_cell(f, tx, tz, cx + dx, cz + dz, &nn);
          b32 known = nb != NULL;
          i32 ntx = tx + (cx + dx < 0 ? -1 : cx + dx > 7 ? 1 : 0), ntz = tz + (cz + dz < 0 ? -1 : cz + dz > 7 ? 1 : 0);
          if (ntx < 0 || ntz < 0 || ntx >= VX_FAR_CHUNKS || ntz >= VX_FAR_CHUNKS) nb = &VX_FAR_OCEAN, nn = 1, known = true;
          for (u32 k = 0; k < VX_FAR_RUNS; k++) {
            u32 si = (u32)c * VX_FAR_RUNS + k;
            side_n[si] = k < t->count[c] ? vx_far_exposed(&t->run[c][k], nb, nn, known, face, sides + si * VX_FAR_SIDE_MAX) : 0;
          }
        }
        // merge along z for x faces, along x for z faces
        b32 along_z = dx != 0;
        for (i32 c = 0; c < VX_FAR_CELLS; c++)
          for (u32 k = 0; k < VX_FAR_RUNS; k++) {
            u32 si = (u32)c * VX_FAR_RUNS + k;
            for (u32 s = 0; s < side_n[si]; s++) {
              VxFarSide *sd = &sides[si * VX_FAR_SIDE_MAX + s];
              if (sd->used) continue;
              sd->used = 1;
              i32 cx = c & 7, cz = c >> 3;
              i32 len = 1;
              for (;;) {
                i32 nx = cx + (along_z ? 0 : len), nz = cz + (along_z ? len : 0);
                if (nx > 7 || nz > 7) break;
                i32 nc = nz * 8 + nx;
                b32 found = false;
                for (u32 k2 = 0; k2 < VX_FAR_RUNS && !found; k2++) {
                  u32 si2 = (u32)nc * VX_FAR_RUNS + k2;
                  for (u32 s2 = 0; s2 < side_n[si2]; s2++) {
                    VxFarSide *o2 = &sides[si2 * VX_FAR_SIDE_MAX + s2];
                    if (!o2->used && o2->y0 == sd->y0 && o2->y1 == sd->y1 && o2->layer == sd->layer && o2->flags == sd->flags) {
                      o2->used = 1;
                      found = true;
                      break;
                    }
                  }
                }
                if (!found) break;
                len++;
              }
              i32 bx = x0 + cx * 2 + (dx > 0 ? 1 : 0), bz = z0 + cz * 2 + (dz > 0 ? 1 : 0);
              u32 rows = (u32)(sd->y1 - sd->y0 + 1);
              // x faces span (y, z), z faces (x, y)
              if (along_z) vx_far_emit(&o, bx, sd->y0, bz, face, rows, (u32)len * 2, sd->layer, sd->flags);
              else vx_far_emit(&o, bx, sd->y0, bz, face, (u32)len * 2, rows, sd->layer, sd->flags);
            }
          }
      }
    }
  tctx_temp_allocator_end(ta);
  *lo = o.lo > o.hi ? 0 : o.lo;
  *hi = o.hi;
  return o.n > cap ? cap + 1 : o.n;
}

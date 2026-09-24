#include "webcraft/wc.h"

#define M WC_GEN_MARGIN
#define SPAN WC_GEN_SPAN

hz_internal const f64 WC_CONT_SPLINE[12][2] = {
    {-1.0, 18}, {-0.6, 24}, {-0.4, 36}, {-0.25, 48}, {-0.14, 56}, {-0.07, 60},
    {-0.02, 63}, {0.04, 65}, {0.15, 68}, {0.35, 73}, {0.6, 80},  {1.0, 90},
};

hz_internal f64 wc_spline(f64 x) {
  if (x <= WC_CONT_SPLINE[0][0]) return WC_CONT_SPLINE[0][1];
  for (u32 i = 1; i < 12; i++) {
    if (x <= WC_CONT_SPLINE[i][0]) {
      f64 x0 = WC_CONT_SPLINE[i - 1][0], y0 = WC_CONT_SPLINE[i - 1][1];
      f64 x1 = WC_CONT_SPLINE[i][0], y1 = WC_CONT_SPLINE[i][1];
      f64 t = (x - x0) / (x1 - x0);
      f64 s = t * t * (3.0 - 2.0 * t);
      return y0 + (y1 - y0) * (t * 0.5 + s * 0.5);
    }
  }
  return WC_CONT_SPLINE[11][1];
}

#define WC_DIR4 4
hz_internal const i32 WC_DIR4_X[WC_DIR4] = {1, -1, 0, 0};
hz_internal const i32 WC_DIR4_Z[WC_DIR4] = {0, 0, 1, -1};

// rounds halves toward +infinity
force_inline i32 wc_round_half_up(f64 x) { return wc_floor_i(x + 0.5); }

force_inline i32 wc_floor_div(i32 a, i32 b) {
  i32 q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

void wc_terrain_init(WcTerrain *t, u32 seed, Allocator *alloc) {
  t->seed = seed;
  u32 s = seed;
#define WC_NEXT() (s = s * 1664525u + 1013904223u)
  wc_simplex_init(&t->warp_x, WC_NEXT(), alloc);
  wc_simplex_init(&t->warp_z, WC_NEXT(), alloc);
  wc_simplex_init(&t->cont, WC_NEXT(), alloc);
  wc_simplex_init(&t->ero, WC_NEXT(), alloc);
  wc_simplex_init(&t->peaks, WC_NEXT(), alloc);
  wc_simplex_init(&t->hills, WC_NEXT(), alloc);
  wc_simplex_init(&t->detail, WC_NEXT(), alloc);
  wc_simplex_init(&t->river, WC_NEXT(), alloc);
  wc_simplex_init(&t->temp, WC_NEXT(), alloc);
  wc_simplex_init(&t->humid, WC_NEXT(), alloc);
  wc_simplex_init(&t->variant, WC_NEXT(), alloc);
  wc_simplex_init(&t->cave_a, WC_NEXT(), alloc);
  wc_simplex_init(&t->cave_b, WC_NEXT(), alloc);
  wc_simplex_init(&t->cave_c, WC_NEXT(), alloc);
  wc_simplex_init(&t->cave_mask, WC_NEXT(), alloc);
  wc_simplex_init(&t->patch, WC_NEXT(), alloc);
#undef WC_NEXT
}

WcColumnSample wc_sample_column(const WcTerrain *T, i32 xi, i32 zi) {
  f64 x = (f64)xi, z = (f64)zi;
  f64 wx = x + wc_fbm2(&T->warp_x, x * 0.0035, z * 0.0035, 3) * 45.0;
  f64 wz = z + wc_fbm2(&T->warp_z, x * 0.0035, z * 0.0035, 3) * 45.0;

  f64 cont = wc_fbm2(&T->cont, wx * 0.0011, wz * 0.0011, 5) * 1.25 + 0.17;
  f64 ero = wc_fbm2(&T->ero, wx * 0.0019, wz * 0.0019, 4) * 1.2;
  f64 pv = wc_ridged2(&T->peaks, wx * 0.0042, wz * 0.0042, 5);
  f64 hills = wc_fbm2(&T->hills, wx * 0.0105, wz * 0.0105, 4);
  f64 detail = wc_fbm2(&T->detail, x * 0.045, z * 0.045, 2);

  f64 h = wc_spline(cont);
  f64 inland = wc_smoothstep(-0.04, 0.3, cont);
  f64 mtn = wc_smoothstep(0.12, -0.38, ero) * inland;
  f64 hill_amp = wc_lerp(5, 13, wc_smoothstep(-0.3, 0.3, hills)) * inland *
                 (1.0 - mtn);
  h += mtn * (pv * pv * 105.0 + pv * 22.0);
  h += hills * hill_amp + hills * 2.5;
  h += detail * 1.4;

  // rivers carve valleys along the zero crossings of a noise field
  f64 rv = m_fabs(wc_fbm2(&T->river, wx * 0.0017, wz * 0.0017, 4));
  f64 rw = 0.02;
  f64 river = 1.0;
  if (cont > -0.1) {
    f64 d = rv / rw;
    river = d / 4.0 < 1.0 ? d / 4.0 : 1.0;
    f64 land_fade = wc_smoothstep(-0.1, 0.02, cont);
    if (d < 1.0) {
      f64 bed = WC_SEA_LEVEL - 1 - 4.5 * (1.0 - d * d);
      h = wc_lerp(h, h < bed ? h : bed, land_fade);
    } else if (d < 4.0) {
      f64 t = wc_smoothstep(1, 4, d);
      f64 bank = wc_lerp(WC_SEA_LEVEL + 0.5, h, t * t);
      h = wc_lerp(h, h < bank ? h : bank, land_fade);
    }
  }

  f64 temp = wc_clamp(wc_fbm2(&T->temp, x * 0.0007, z * 0.0007, 3) * 1.6 + 0.1,
                      -1.0, 1.0);
  f64 humid = wc_clamp(
      wc_fbm2(&T->humid, x * 0.001 + 71.3, z * 0.001 - 12.1, 3) * 1.6, -1.0,
      1.0);
  i32 hi = wc_floor_i(h);

  f64 cold_t = temp - (hi - 95 > 0 ? hi - 95 : 0) * 0.014;
  u8 biome;
  if (hi < WC_SEA_LEVEL - 14 && cont < -0.1) {
    biome = cold_t < -0.45 ? WC_BIOME_FROZEN_OCEAN : WC_BIOME_DEEP_OCEAN;
  } else if (hi < WC_SEA_LEVEL - 1 && cont < -0.06) {
    biome = cold_t < -0.45 ? WC_BIOME_FROZEN_OCEAN : WC_BIOME_OCEAN;
  } else if (river < 0.25 && hi <= WC_SEA_LEVEL) {
    biome = cold_t < -0.45 ? WC_BIOME_FROZEN_RIVER : WC_BIOME_RIVER;
  } else if (hi <= WC_SEA_LEVEL + 2 && cont < 0.06) {
    if (mtn > 0.3) biome = WC_BIOME_STONY_SHORE;
    else biome = cold_t < -0.45 ? WC_BIOME_SNOWY_BEACH : WC_BIOME_BEACH;
  } else if (mtn > 0.45 && hi > 118) {
    biome = hi > 150 + temp * 12 ? WC_BIOME_SNOWY_PEAKS : WC_BIOME_MOUNTAINS;
  } else if (cold_t < -0.45) {
    biome = humid > 0.05 ? WC_BIOME_SNOWY_TAIGA : WC_BIOME_SNOWY_PLAINS;
  } else if (cold_t < -0.12) {
    biome = WC_BIOME_TAIGA;
  } else if (temp > 0.42) {
    if (humid < -0.1)
      biome = humid < -0.42 && temp > 0.6 ? WC_BIOME_BADLANDS : WC_BIOME_DESERT;
    else if (humid > 0.4 && hi < WC_SEA_LEVEL + 6) biome = WC_BIOME_SWAMP;
    else biome = WC_BIOME_PLAINS;
  } else if (humid > 0.18) {
    biome = wc_noise2(&T->variant, x * 0.004, z * 0.004) > 0.25
                ? WC_BIOME_BIRCH_FOREST
                : WC_BIOME_FOREST;
  } else if (hi > 100 && mtn > 0.2) {
    biome = WC_BIOME_MEADOW;
  } else {
    biome = humid > -0.05 &&
                    wc_noise2(&T->variant, x * 0.006 + 40, z * 0.006) > 0.35
                ? WC_BIOME_FOREST
                : WC_BIOME_PLAINS;
  }

  i32 height = hi < WC_HEIGHT - 20 ? hi : WC_HEIGHT - 20;
  if (height < 3) height = 3;
  return (WcColumnSample){.height = height,
                          .temp = temp,
                          .humid = humid,
                          .biome = biome,
                          .river = river,
                          .mountain = mtn};
}

force_inline b32 wc_is_soil_top(u8 b) {
  return b == B_GRASS || b == B_SNOWY_GRASS || b == B_PODZOL ||
         b == B_MOSS_BLOCK || b == B_COARSE_DIRT;
}

hz_internal void wc_carve_caves(const WcTerrain *T, WcGenScratch *S, u8 *blocks,
                                i32 x0, i32 z0, i32 max_h) {
  const i32 GX = WC_CAVE_GX;
  i32 gy_count = (max_h + 2 + 3) / 4;
  if (gy_count > 64) gy_count = 64;
  gy_count += 1;
  f32 *tun = S->tun;
  f32 *cav = S->cav;
  for (i32 gy = 0; gy < gy_count; gy++) {
    f64 y = gy * 4;
    for (i32 gz = 0; gz < GX; gz++) {
      for (i32 gx = 0; gx < GX; gx++) {
        f64 x = x0 + gx * 4;
        f64 z = z0 + gz * 4;
        f64 a = wc_noise3(&T->cave_a, x * 0.016, y * 0.026, z * 0.016);
        f64 b = wc_noise3(&T->cave_b, x * 0.016, y * 0.026, z * 0.016);
        i32 k = (gy * GX + gz) * GX + gx;
        tun[k] = (f32)(a * a + b * b);
        cav[k] = (f32)(wc_noise3(&T->cave_c, x * 0.009, y * 0.017, z * 0.009) +
                       wc_noise3(&T->cave_c, x * 0.03, y * 0.05, z * 0.03) * 0.2);
      }
    }
  }
  for (i32 lz = 0; lz < 16; lz++) {
    for (i32 lx = 0; lx < 16; lx++) {
      i32 h = S->h[(lz + M) * SPAN + (lx + M)];
      b32 near_sea = h <= WC_SEA_LEVEL + 3;
      b32 entrance = wc_noise2(&T->cave_mask, (x0 + lx) * 0.012,
                               (z0 + lz) * 0.012) > 0.45 &&
                     !near_sea;
      i32 max_y = entrance ? h : near_sea ? h - 9 : h - 6;
      f64 gxf = lx / 4.0, gzf = lz / 4.0;
      i32 gx0 = lx / 4, gz0 = lz / 4;
      f64 fx = gxf - gx0, fz = gzf - gz0;
      for (i32 y = 1; y <= max_y && y < WC_HEIGHT; y++) {
        f64 gyf = y / 4.0;
        i32 gy0 = y / 4;
        if (gy0 + 1 >= gy_count) break;
        f64 fy = gyf - gy0;
        i32 k000 = (gy0 * GX + gz0) * GX + gx0;
        i32 k100 = k000 + 1, k010 = k000 + GX, k110 = k010 + 1;
        i32 k001 = k000 + GX * GX, k101 = k001 + 1, k011 = k001 + GX,
            k111 = k011 + 1;
        f64 t = wc_lerp(
            wc_lerp(wc_lerp(tun[k000], tun[k100], fx),
                    wc_lerp(tun[k010], tun[k110], fx), fz),
            wc_lerp(wc_lerp(tun[k001], tun[k101], fx),
                    wc_lerp(tun[k011], tun[k111], fx), fz),
            fy);
        // thinner tunnels near the surface, wider deep down
        f64 thick = 0.011 + (y < 40 ? (40 - y) * 0.00025 : 0.0);
        b32 carve = t < thick;
        if (!carve && y < 56) {
          f64 c = wc_lerp(
              wc_lerp(wc_lerp(cav[k000], cav[k100], fx),
                      wc_lerp(cav[k010], cav[k110], fx), fz),
              wc_lerp(wc_lerp(cav[k001], cav[k101], fx),
                      wc_lerp(cav[k011], cav[k111], fx), fz),
              fy);
          carve = c > 0.62 - (56 - y) * 0.002;
        }
        if (!carve) continue;
        i32 i = WC_IDX(lx, y, lz);
        u8 b = blocks[i];
        if (b == B_BEDROCK || b == B_WATER || b == B_ICE) continue;
        // never open holes under water or ice
        u8 above = y + 1 < WC_HEIGHT ? blocks[WC_IDX(lx, y + 1, lz)] : B_AIR;
        if (above == B_WATER || above == B_ICE) continue;
        blocks[i] = y <= 10 ? B_LAVA : B_AIR;
      }
    }
  }
}

hz_internal void wc_vein(u8 *blocks, WcRng *rnd, u8 id, i32 tries, i32 size,
                         i32 y_min, i32 y_max, u8 r0, u8 r1) {
  for (i32 t = 0; t < tries; t++) {
    i32 x = (i32)(wc_rng_next(rnd) * 16);
    i32 y = y_min + (i32)(wc_rng_next(rnd) * (y_max - y_min));
    i32 z = (i32)(wc_rng_next(rnd) * 16);
    i32 n = 1 + (i32)(wc_rng_next(rnd) * size);
    for (i32 k = 0; k < n; k++) {
      if (x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < WC_HEIGHT) {
        i32 i = WC_IDX(x, y, z);
        if (blocks[i] == r0 || blocks[i] == r1) blocks[i] = id;
      }
      i32 d = (i32)(wc_rng_next(rnd) * 6);
      if (d == 0) x++;
      else if (d == 1) x--;
      else if (d == 2) y++;
      else if (d == 3) y--;
      else if (d == 4) z++;
      else z--;
    }
  }
}

hz_internal void wc_place_ores(const WcTerrain *T, u8 *blocks, i32 cx, i32 cz) {
  WcRng rnd = wc_rng(wc_hash2i(cx, cz, T->seed ^ 0x1234567u));
  wc_vein(blocks, &rnd, B_GRANITE, 2, 40, 5, 80, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_DIORITE, 2, 40, 5, 80, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_ANDESITE, 2, 40, 5, 80, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_DIRT, 2, 24, 5, 90, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_GRAVEL, 2, 24, 5, 90, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_COAL_ORE, 18, 14, 5, 128, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_IRON_ORE, 14, 8, 5, 64, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_GOLD_ORE, 3, 7, 5, 32, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_REDSTONE_ORE, 6, 7, 5, 16, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_LAPIS_ORE, 2, 6, 5, 30, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_DIAMOND_ORE, 2, 6, 5, 16, B_STONE, B_STONE);
  wc_vein(blocks, &rnd, B_EMERALD_ORE, 1, 2, 30, 110, B_STONE, B_STONE);
}

hz_internal void wc_put(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, u8 id,
                        b32 force) {
  i32 lx = x - x0, lz = z - z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 1 || y >= WC_HEIGHT) return;
  i32 i = WC_IDX(lx, y, lz);
  u8 cur = blocks[i];
  if (force) {
    if (cur == B_AIR || cur == B_OAK_LEAVES || cur == B_BIRCH_LEAVES ||
        cur == B_SPRUCE_LEAVES || cur == B_TALL_GRASS || cur == B_SNOWY_GRASS ||
        cur == B_GRASS || cur == B_DIRT)
      blocks[i] = id;
  } else if (cur == B_AIR) {
    blocks[i] = id;
  }
}

hz_internal void wc_tree_oak(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z,
                             WcRng *rnd, u8 log, u8 leaf, i32 height,
                             b32 wide) {
  i32 top = y + height;
  for (i32 yy = top - 3; yy <= top; yy++) {
    i32 layer = yy - (top - 3);
    i32 rad = layer < 2 ? (wide ? 3 : 2) : 1;
    for (i32 dz = -rad; dz <= rad; dz++) {
      for (i32 dx = -rad; dx <= rad; dx++) {
        b32 corner = (dx == rad || dx == -rad) && (dz == rad || dz == -rad);
        if (corner && (layer >= 2 || wc_rng_next(rnd) < 0.55)) continue;
        if (layer == 3 && dx != 0 && dz != 0) continue;
        wc_put(blocks, x0, z0, x + dx, yy, z + dz, leaf, false);
      }
    }
  }
  for (i32 yy = y; yy < top; yy++) wc_put(blocks, x0, z0, x, yy, z, log, true);
  wc_put(blocks, x0, z0, x, y - 1, z, B_DIRT, true);
}

typedef struct {
  i32 x, y, z;
  f64 r;
} WcTreeBlob;

hz_internal void wc_tree_big_oak(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y,
                                 i32 z, WcRng *rnd) {
  i32 height = 7 + (i32)(wc_rng_next(rnd) * 4);
  i32 top = y + height;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  WcTreeBlob *blob = ALLOC_ARRAY(&tmp.allocator, WcTreeBlob, 6);
  blob[0] = (WcTreeBlob){x, top - 1, z, 3.2};
  i32 nb = 3 + (i32)(wc_rng_next(rnd) * 3);
  for (i32 i = 0; i < nb; i++) {
    f64 a = wc_rng_next(rnd) * M_PI * 2.0;
    f64 d = 2.0 + wc_rng_next(rnd) * 1.8;
    WcTreeBlob *nbl = &blob[i + 1];
    nbl->x = x + wc_round_half_up(m_cos(a) * d);
    nbl->y = top - 2 - (i32)(wc_rng_next(rnd) * 3);
    nbl->z = z + wc_round_half_up(m_sin(a) * d);
    nbl->r = 2.2 + wc_rng_next(rnd) * 0.8;
  }
  for (i32 b = 0; b < nb + 1; b++) {
    f64 r = blob[b].r;
    i32 bx = blob[b].x, by = blob[b].y, bz = blob[b].z;
    i32 ri = (i32)m_ceil(r);
    for (i32 dy = -ri; dy <= ri; dy++) {
      for (i32 dz = -ri; dz <= ri; dz++) {
        for (i32 dx = -ri; dx <= ri; dx++) {
          f64 d2 = dx * dx + dy * dy * 1.6 + dz * dz;
          if (d2 > r * r * (0.85 + wc_rng_next(rnd) * 0.3)) continue;
          wc_put(blocks, x0, z0, bx + dx, by + dy, bz + dz, B_OAK_LEAVES,
                 false);
        }
      }
    }
    // branch from the trunk to the blob
    if (bx != x || bz != z) {
      i32 sx = bx - x < 0 ? x - bx : bx - x;
      i32 sz = bz - z < 0 ? z - bz : bz - z;
      i32 steps = sx > sz ? sx : sz;
      for (i32 k = 1; k <= steps; k++) {
        f64 t = (f64)k / steps;
        wc_put(blocks, x0, z0, wc_round_half_up(wc_lerp(x, bx, t)),
               wc_round_half_up(wc_lerp(by - 2, by - 1, t)),
               wc_round_half_up(wc_lerp(z, bz, t)), B_OAK_LOG, true);
      }
    }
  }
  tctx_temp_allocator_end(tmp);
  for (i32 yy = y; yy < top; yy++)
    wc_put(blocks, x0, z0, x, yy, z, B_OAK_LOG, true);
  wc_put(blocks, x0, z0, x, y - 1, z, B_DIRT, true);
}

hz_internal void wc_tree_spruce(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y,
                                i32 z, WcRng *rnd) {
  i32 height = 7 + (i32)(wc_rng_next(rnd) * 5);
  i32 top = y + height;
  i32 leaf_start = y + 2 + (i32)(wc_rng_next(rnd) * 2);
  i32 rad = 0, max_rad = 1;
  for (i32 yy = top; yy >= leaf_start; yy--) {
    for (i32 dz = -rad; dz <= rad; dz++) {
      for (i32 dx = -rad; dx <= rad; dx++) {
        if (rad > 0 && (dx == rad || dx == -rad) && (dz == rad || dz == -rad))
          continue;
        wc_put(blocks, x0, z0, x + dx, yy, z + dz, B_SPRUCE_LEAVES, false);
      }
    }
    if (rad >= max_rad) {
      rad = 1;
      max_rad = max_rad + 1 < 3 ? max_rad + 1 : 3;
    } else {
      rad++;
    }
  }
  wc_put(blocks, x0, z0, x, top + 1, z, B_SPRUCE_LEAVES, false);
  for (i32 yy = y; yy < top; yy++)
    wc_put(blocks, x0, z0, x, yy, z, B_SPRUCE_LOG, true);
  wc_put(blocks, x0, z0, x, y - 1, z, B_DIRT, true);
}

hz_internal void wc_place_trees(const WcTerrain *T, WcGenScratch *S, u8 *blocks,
                                i32 x0, i32 z0) {
  const i32 CELL = 5;
  i32 gx0 = wc_floor_div(x0 - M, CELL);
  i32 gx1 = wc_floor_div(x0 + 15 + M, CELL);
  i32 gz0 = wc_floor_div(z0 - M, CELL);
  i32 gz1 = wc_floor_div(z0 + 15 + M, CELL);
  for (i32 gz = gz0; gz <= gz1; gz++) {
    for (i32 gx = gx0; gx <= gx1; gx++) {
      u32 hsh = wc_hash2i(gx, gz, T->seed ^ 0x77777u);
      i32 tx = gx * CELL + (i32)(hsh & 3) + (i32)((hsh >> 2) & 1);
      i32 tz = gz * CELL + (i32)((hsh >> 3) & 3) + (i32)((hsh >> 5) & 1);
      if (tx < x0 - M || tx > x0 + 15 + M || tz < z0 - M || tz > z0 + 15 + M)
        continue;
      f64 roll = (f64)((hsh >> 8) & 0xffff) / 65536.0;
      i32 lx = tx - x0, lz = tz - z0;
      i32 h;
      u8 biome;
      if (lx >= -M && lx < 16 + M && lz >= -M && lz < 16 + M) {
        i32 k = (lz + M) * SPAN + (lx + M);
        h = S->h[k];
        biome = S->biome[k];
      } else {
        WcColumnSample cs = wc_sample_column(T, tx, tz);
        h = cs.height;
        biome = cs.biome;
      }
      if (h < WC_SEA_LEVEL) continue;
      f64 density = 0;
      i32 kind = 0; // 0 oak, 1 birch, 2 spruce, 3 cactus, 4 big oak, 5 swamp oak
      f64 r2 = (f64)((hsh >> 24) & 0xff) / 256.0;
      switch (biome) {
      case WC_BIOME_FOREST:
        density = 0.78;
        kind = r2 < 0.2 ? 1 : r2 < 0.3 ? 4 : 0;
        break;
      case WC_BIOME_BIRCH_FOREST:
        density = 0.75;
        kind = r2 < 0.85 ? 1 : 0;
        break;
      case WC_BIOME_PLAINS:
        density = 0.035;
        kind = r2 < 0.35 ? 4 : 0;
        break;
      case WC_BIOME_MEADOW:
        density = 0.03;
        kind = 1;
        break;
      case WC_BIOME_TAIGA:
        density = 0.62;
        kind = 2;
        break;
      case WC_BIOME_SNOWY_TAIGA:
        density = 0.55;
        kind = 2;
        break;
      case WC_BIOME_SNOWY_PLAINS:
        density = 0.04;
        kind = 2;
        break;
      case WC_BIOME_MOUNTAINS:
        density = h < 135 ? 0.15 : 0.0;
        kind = 2;
        break;
      case WC_BIOME_SWAMP:
        density = 0.3;
        kind = 5;
        break;
      case WC_BIOME_DESERT:
        density = 0.07;
        kind = 3;
        break;
      case WC_BIOME_RIVER:
      case WC_BIOME_FROZEN_RIVER:
        density = h > WC_SEA_LEVEL ? 0.2 : 0.0;
        kind = 0;
        break;
      default:
        break;
      }
      if (roll >= density) continue;
      // ground type from our own chunk when available, else trust the sample
      if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) {
        u8 g = blocks[WC_IDX(lx, h, lz)];
        if (kind == 3) {
          if (g != B_SAND) continue;
        } else if (g != B_GRASS && g != B_DIRT && g != B_SNOWY_GRASS &&
                   g != B_PODZOL && g != B_MOSS_BLOCK && g != B_COARSE_DIRT) {
          continue;
        }
      }
      WcRng rnd = wc_rng(hsh ^ 0x9e3779b9u);
      switch (kind) {
      case 0: {
        i32 height = 4 + (i32)(wc_rng_next(&rnd) * 3);
        wc_tree_oak(blocks, x0, z0, tx, h + 1, tz, &rnd, B_OAK_LOG,
                    B_OAK_LEAVES, height, false);
      } break;
      case 1: {
        i32 height = 5 + (i32)(wc_rng_next(&rnd) * 3);
        wc_tree_oak(blocks, x0, z0, tx, h + 1, tz, &rnd, B_BIRCH_LOG,
                    B_BIRCH_LEAVES, height, false);
      } break;
      case 2:
        wc_tree_spruce(blocks, x0, z0, tx, h + 1, tz, &rnd);
        break;
      case 3: {
        i32 ch = 1 + (i32)(wc_rng_next(&rnd) * 3);
        for (i32 k = 0; k < ch; k++)
          wc_put(blocks, x0, z0, tx, h + 1 + k, tz, B_CACTUS, false);
      } break;
      case 4:
        wc_tree_big_oak(blocks, x0, z0, tx, h + 1, tz, &rnd);
        break;
      case 5: {
        i32 height = 5 + (i32)(wc_rng_next(&rnd) * 2);
        wc_tree_oak(blocks, x0, z0, tx, h + 1, tz, &rnd, B_OAK_LOG,
                    B_OAK_LEAVES, height, true);
      } break;
      default:
        break;
      }
    }
  }
}

hz_internal void wc_place_plants(const WcTerrain *T, WcGenScratch *S, u8 *blocks,
                                 i32 x0, i32 z0) {
  for (i32 lz = 0; lz < 16; lz++) {
    for (i32 lx = 0; lx < 16; lx++) {
      i32 k = (lz + M) * SPAN + (lx + M);
      i32 h = S->h[k];
      if (h + 1 >= WC_HEIGHT) continue;
      u8 ground = blocks[WC_IDX(lx, h, lz)];
      i32 above_i = WC_IDX(lx, h + 1, lz);
      if (blocks[above_i] != B_AIR) continue;
      i32 wx = x0 + lx, wz = z0 + lz;
      f64 r = wc_hash2(wx, wz, T->seed ^ 0xabcdefu);
      u8 biome = S->biome[k];
      f64 flower_noise = wc_noise2(&T->patch, wx * 0.03 + 100, wz * 0.03);
      if (ground == B_GRASS || ground == B_PODZOL || ground == B_MOSS_BLOCK) {
        f64 grass = 0.18, flowers = 0.012, fern = 0;
        if (biome == WC_BIOME_PLAINS) {
          grass = 0.34;
          flowers = 0.03;
        } else if (biome == WC_BIOME_MEADOW) {
          grass = 0.4;
          flowers = 0.12;
        } else if (biome == WC_BIOME_FOREST || biome == WC_BIOME_BIRCH_FOREST) {
          grass = 0.22;
          flowers = 0.02;
        } else if (biome == WC_BIOME_TAIGA) {
          grass = 0.1;
          fern = 0.16;
          flowers = 0.003;
        } else if (biome == WC_BIOME_SWAMP) {
          grass = 0.25;
          flowers = 0.01;
        }
        if (flower_noise > 0.55) flowers *= 4;
        if (r < flowers) {
          i32 f = (i32)(wc_hash2(wx, wz, T->seed ^ 0x111u) * 100);
          u8 which;
          if (flower_noise > 0.3) which = f < 50 ? B_DANDELION : B_DAISY;
          else which = f < 40 ? B_POPPY : f < 70 ? B_DANDELION
                                   : f < 85 ? B_CORNFLOWER : B_DAISY;
          blocks[above_i] = which;
        } else if (r < flowers + fern) {
          blocks[above_i] = B_FERN;
        } else if (r < flowers + fern + grass) {
          blocks[above_i] = B_TALL_GRASS;
        } else if ((biome == WC_BIOME_FOREST || biome == WC_BIOME_TAIGA ||
                    biome == WC_BIOME_SWAMP) &&
                   r > 0.994) {
          blocks[above_i] = r > 0.997 ? B_RED_MUSHROOM : B_BROWN_MUSHROOM;
        }
      } else if (ground == B_SNOWY_GRASS) {
        if (r < 0.03) blocks[above_i] = B_FERN;
      } else if (ground == B_SAND || ground == B_RED_SAND) {
        if ((biome == WC_BIOME_DESERT || biome == WC_BIOME_BADLANDS) &&
            r < 0.012)
          blocks[above_i] = B_DEAD_BUSH;
      }
      // sugar cane next to water
      if ((ground == B_GRASS || ground == B_SAND || ground == B_DIRT) &&
          h == WC_SEA_LEVEL && r > 0.9 && blocks[above_i] == B_AIR) {
        b32 wet = false;
        for (i32 n = 0; n < WC_DIR4; n++) {
          i32 nx = lx + WC_DIR4_X[n], nz = lz + WC_DIR4_Z[n];
          if (nx < 0 || nx > 15 || nz < 0 || nz > 15) continue;
          if (blocks[WC_IDX(nx, h, nz)] == B_WATER) wet = true;
        }
        if (wet && wc_hash2(wx, wz, T->seed ^ 0x222u) < 0.35) {
          i32 hh = 1 + (i32)(wc_hash2(wx, wz, T->seed ^ 0x333u) * 3);
          for (i32 k2 = 1; k2 <= hh; k2++)
            if (blocks[WC_IDX(lx, h + k2, lz)] == B_AIR)
              blocks[WC_IDX(lx, h + k2, lz)] = B_SUGAR_CANE;
        }
      }
    }
  }
}

void wc_generate_chunk(const WcTerrain *T, WcGenScratch *S, i32 cx, i32 cz,
                       u8 *blocks, u8 *climate) {
  mem_zero(blocks, WC_CHUNK_VOLUME);
  i32 x0 = cx * WC_CHUNK, z0 = cz * WC_CHUNK;

  // column samples with a margin for trees crossing chunk borders
  for (i32 lz = -M; lz < WC_CHUNK + M; lz++) {
    for (i32 lx = -M; lx < WC_CHUNK + M; lx++) {
      WcColumnSample s = wc_sample_column(T, x0 + lx, z0 + lz);
      i32 k = (lz + M) * SPAN + (lx + M);
      S->h[k] = s.height;
      S->biome[k] = s.biome;
      S->temp[k] = (f32)s.temp;
      S->humid[k] = (f32)s.humid;
      S->mtn[k] = (f32)s.mountain;
    }
  }
#define H(lx, lz) S->h[((lz) + M) * SPAN + ((lx) + M)]

  i32 max_h = 0;
  for (i32 lz = 0; lz < WC_CHUNK; lz++) {
    for (i32 lx = 0; lx < WC_CHUNK; lx++) {
      i32 k = (lz + M) * SPAN + (lx + M);
      i32 h = S->h[k];
      u8 biome = S->biome[k];
      i32 wx = x0 + lx, wz = z0 + lz;
      if (h > max_h) max_h = h;
      climate[(lz * 16 + lx) * 2] =
          (u8)wc_round_half_up(((f64)S->temp[k] * 0.5 + 0.5) * 255.0);
      climate[(lz * 16 + lx) * 2 + 1] =
          (u8)wc_round_half_up(((f64)S->humid[k] * 0.5 + 0.5) * 255.0);

      i32 slope = 0;
      i32 d0 = H(lx + 1, lz) - h, d1 = H(lx - 1, lz) - h;
      i32 d2 = H(lx, lz + 1) - h, d3 = H(lx, lz - 1) - h;
      if (d0 < 0) d0 = -d0;
      if (d1 < 0) d1 = -d1;
      if (d2 < 0) d2 = -d2;
      if (d3 < 0) d3 = -d3;
      slope = d0 > slope ? d0 : slope;
      slope = d1 > slope ? d1 : slope;
      slope = d2 > slope ? d2 : slope;
      slope = d3 > slope ? d3 : slope;
      u32 r = wc_hash2i(wx, wz, T->seed ^ 0x5bd1e995u);
      i32 soil = 3 + (i32)(r & 1) + (i32)((r >> 1) & 1);
      f64 patch = wc_noise2(&T->patch, wx * 0.05, wz * 0.05);

      u8 top = B_GRASS, under = B_DIRT, deep = B_STONE;
      i32 under_depth = soil;
      b32 underwater = h < WC_SEA_LEVEL;
      switch (biome) {
      case WC_BIOME_OCEAN:
      case WC_BIOME_DEEP_OCEAN:
      case WC_BIOME_FROZEN_OCEAN:
        if (h < WC_SEA_LEVEL - 18) {
          top = patch > 0.3 ? B_CLAY : B_GRAVEL;
          under = B_GRAVEL;
        } else {
          top = patch > 0.45 ? B_GRAVEL : B_SAND;
          under = B_SAND;
        }
        break;
      case WC_BIOME_BEACH:
      case WC_BIOME_SNOWY_BEACH:
        top = B_SAND;
        under = B_SAND;
        deep = B_SANDSTONE;
        under_depth = 4;
        break;
      case WC_BIOME_STONY_SHORE:
        top = patch > 0.2 ? B_GRAVEL : B_STONE;
        under = B_STONE;
        break;
      case WC_BIOME_RIVER:
      case WC_BIOME_FROZEN_RIVER:
        top = patch > 0.35 ? B_CLAY : patch > -0.1 ? B_SAND : B_GRAVEL;
        under = B_SAND;
        if (!underwater) {
          top = biome == WC_BIOME_FROZEN_RIVER ? B_SNOWY_GRASS : B_GRASS;
          under = B_DIRT;
        }
        break;
      case WC_BIOME_DESERT:
        top = B_SAND;
        under = B_SAND;
        deep = B_SANDSTONE;
        under_depth = 5;
        break;
      case WC_BIOME_BADLANDS:
        top = B_RED_SAND;
        under = B_TERRACOTTA;
        deep = B_TERRACOTTA;
        under_depth = 12;
        break;
      case WC_BIOME_SNOWY_PLAINS:
      case WC_BIOME_SNOWY_TAIGA:
        top = B_SNOWY_GRASS;
        break;
      case WC_BIOME_TAIGA:
        top = patch > 0.35 ? B_PODZOL : patch < -0.55 ? B_COARSE_DIRT : B_GRASS;
        break;
      case WC_BIOME_MOUNTAINS:
        if (slope >= 3) {
          top = B_STONE;
          under = B_STONE;
        } else if (patch > 0.55) {
          top = B_GRAVEL;
          under = B_GRAVEL;
        }
        break;
      case WC_BIOME_SNOWY_PEAKS:
        if (slope >= 4) {
          top = patch > 0.3 ? B_PACKED_ICE : B_STONE;
          under = B_STONE;
        } else {
          top = B_SNOW;
          under = B_SNOW;
          under_depth = 2;
        }
        break;
      case WC_BIOME_SWAMP:
        top = patch > 0.5 ? B_MOSS_BLOCK : B_GRASS;
        break;
      case WC_BIOME_MEADOW:
        if (slope >= 4) {
          top = B_STONE;
          under = B_STONE;
        }
        break;
      default:
        break;
      }
      // steep cliffs anywhere expose stone
      if (slope >= 6 && top != B_SAND && top != B_RED_SAND) {
        top = B_STONE;
        under = B_STONE;
      }
      if (underwater && wc_is_soil_top(top)) {
        top = h < WC_SEA_LEVEL - 3 ? (patch > 0 ? B_GRAVEL : B_SAND) : B_DIRT;
      }
      // snow line on high terrain even outside peak biomes
      if (!underwater && h > 158 + S->temp[k] * 10.0 && slope < 4) top = B_SNOW;

      blocks[WC_IDX(lx, 0, lz)] = B_BEDROCK;
      for (i32 y = 1; y < 4; y++) {
        if ((i32)((r >> (y * 3)) & 7) < 4 - y) blocks[WC_IDX(lx, y, lz)] = B_BEDROCK;
      }
      i32 under_top = h - under_depth;
      for (i32 y = 1; y <= h; y++) {
        i32 i = WC_IDX(lx, y, lz);
        if (blocks[i] == B_BEDROCK) continue;
        if (y == h) blocks[i] = top;
        else if (y > under_top) blocks[i] = under;
        else if (y > under_top - 3 && deep != B_STONE) blocks[i] = deep;
        else blocks[i] = B_STONE;
      }
      if (biome == WC_BIOME_BADLANDS) {
        for (i32 y = h - 30 > 1 ? h - 30 : 1; y < h - 1; y++) {
          i32 i = WC_IDX(lx, y, lz);
          if (blocks[i] == B_TERRACOTTA || blocks[i] == B_STONE) {
            i32 band = (y * 7 + (i32)(wc_hash2i(0, y, T->seed) & 3)) % 11;
            blocks[i] = band < 2 ? B_RED_SAND : band == 5 ? B_SANDSTONE : B_TERRACOTTA;
          }
        }
      }
      if (underwater) {
        b32 frozen = biome == WC_BIOME_FROZEN_OCEAN ||
                     biome == WC_BIOME_FROZEN_RIVER ||
                     (S->temp[k] < -0.5f && h < WC_SEA_LEVEL);
        for (i32 y = h + 1; y <= WC_SEA_LEVEL; y++)
          blocks[WC_IDX(lx, y, lz)] =
              (y == WC_SEA_LEVEL && frozen) ? B_ICE : B_WATER;
      }
    }
  }
#undef H

  wc_carve_caves(T, S, blocks, x0, z0, max_h);
  wc_place_ores(T, blocks, cx, cz);
  wc_place_trees(T, S, blocks, x0, z0);
  wc_place_plants(T, S, blocks, x0, z0);
}

v3 wc_find_spawn(const WcTerrain *T) {
  f64 best_x = 0.5, best_y = 100, best_z = 0.5, best_score = -1e9;
  for (i32 r = 0; r < 2000; r += 24) {
    i32 steps = (i32)m_floor((r * M_PI * 2.0) / 24.0);
    if (steps < 1) steps = 1;
    for (i32 i = 0; i < steps; i++) {
      f64 a = ((f64)i / steps) * M_PI * 2.0;
      i32 x = wc_round_half_up(m_cos(a) * r);
      i32 z = wc_round_half_up(m_sin(a) * r);
      WcColumnSample s = wc_sample_column(T, x, z);
      if (s.height <= WC_SEA_LEVEL + 1) continue;
      b32 good = s.biome == WC_BIOME_PLAINS || s.biome == WC_BIOME_FOREST ||
                 s.biome == WC_BIOME_BIRCH_FOREST ||
                 s.biome == WC_BIOME_MEADOW || s.biome == WC_BIOME_TAIGA;
      f64 score = (good ? 100.0 : 0.0) - r * 0.05 -
                  m_fabs((f64)s.height - 75.0) * 0.5;
      if (s.mountain > 0.2) score += 20; // nice views
      if (score > best_score) {
        best_score = score;
        best_x = x + 0.5;
        best_y = s.height + 1;
        best_z = z + 0.5;
      }
    }
    if (best_score > 60 && r > 150) break;
  }
  return (v3){(f32)best_x, (f32)best_y, (f32)best_z};
}

#undef M
#undef SPAN

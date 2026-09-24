#include "webcraft/wc.h"

static_assert(B_COUNT == 74, "WC_FILL_182 must pad the tables to 256 entries");

#define WC_R2(v) v, v,
#define WC_R4(v) WC_R2(v) WC_R2(v)
#define WC_R8(v) WC_R4(v) WC_R4(v)
#define WC_R16(v) WC_R8(v) WC_R8(v)
#define WC_R32(v) WC_R16(v) WC_R16(v)
#define WC_R64(v) WC_R32(v) WC_R32(v)
#define WC_R128(v) WC_R64(v) WC_R64(v)
#define WC_FILL_182(v) WC_R128(v) WC_R32(v) WC_R16(v) WC_R4(v) WC_R2(v)

#define WC_X_SHAPE(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) sh,
#define WC_X_LAYER(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) ly,
#define WC_X_SOLID(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) so,
#define WC_X_OCCLUDES(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) oc,
#define WC_X_OPACITY(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) op,
#define WC_X_EMISSION(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) em,
#define WC_X_TINT(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) ti,
#define WC_X_WAVE(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) wa,
#define WC_X_REPLACEABLE(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) re,
#define WC_X_INVENTORY(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) in,
#define WC_X_LABEL(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) lb,
#define WC_X_FACES(id, lb, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) {ts, ts, tt, tb, tf, ts},

const u8 wc_shape[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_SHAPE) WC_FILL_182(WC_SHAPE_CUBE)};
const u8 wc_layer[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_LAYER) WC_FILL_182(WC_LAYER_OPAQUE)};
const u8 wc_solid[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_SOLID) WC_FILL_182(1)};
const u8 wc_occludes[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_OCCLUDES) WC_FILL_182(1)};
const u8 wc_light_opacity[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_OPACITY) WC_FILL_182(15)};
const u8 wc_emission[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_EMISSION)};
const u8 wc_tint[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_TINT)};
const u8 wc_wave[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_WAVE)};
const u8 wc_replaceable[WC_BLOCK_IDS] = {WC_BLOCK_LIST(WC_X_REPLACEABLE)};
// zero fill is T_STONE on every face
const u8 wc_face_tex[WC_BLOCK_IDS][6] = {WC_BLOCK_LIST(WC_X_FACES)};
const char *const wc_block_label[B_COUNT] = {WC_BLOCK_LIST(WC_X_LABEL)};
const u8 wc_block_in_inventory[B_COUNT] = {WC_BLOCK_LIST(WC_X_INVENTORY)};

const char *const wc_texture_name[T_COUNT] = {
#define WC_X(id, name) name,
    WC_TEXTURE_LIST(WC_X)
#undef WC_X
};

const u8 wc_inventory_order[WC_INVENTORY_COUNT] = {
    B_GRASS, B_DIRT, B_COARSE_DIRT, B_PODZOL, B_STONE, B_COBBLESTONE,
    B_MOSSY_COBBLESTONE, B_STONE_BRICKS, B_GRANITE, B_DIORITE, B_ANDESITE,
    B_BRICKS, B_SAND, B_RED_SAND, B_SANDSTONE, B_GRAVEL, B_CLAY, B_TERRACOTTA,
    B_OAK_LOG, B_BIRCH_LOG, B_SPRUCE_LOG, B_OAK_PLANKS, B_BIRCH_PLANKS,
    B_SPRUCE_PLANKS, B_BOOKSHELF, B_OAK_LEAVES, B_BIRCH_LEAVES, B_SPRUCE_LEAVES,
    B_MOSS_BLOCK, B_GLASS, B_ICE, B_PACKED_ICE, B_SNOW, B_SNOWY_GRASS,
    B_QUARTZ_BLOCK, B_OBSIDIAN, B_GOLD_BLOCK, B_IRON_BLOCK, B_DIAMOND_BLOCK,
    B_EMERALD_BLOCK, B_COAL_ORE, B_IRON_ORE, B_GOLD_ORE, B_DIAMOND_ORE,
    B_LAPIS_ORE, B_REDSTONE_ORE, B_EMERALD_ORE, B_WHITE_WOOL, B_RED_WOOL,
    B_ORANGE_WOOL, B_YELLOW_WOOL, B_LIME_WOOL, B_BLUE_WOOL, B_BLACK_WOOL,
    B_GLOWSTONE, B_SEA_LANTERN, B_TORCH, B_JACK_O_LANTERN, B_PUMPKIN, B_CACTUS,
    B_TALL_GRASS, B_FERN, B_DANDELION, B_POPPY, B_CORNFLOWER, B_DAISY,
    B_DEAD_BUSH, B_SUGAR_CANE, B_RED_MUSHROOM, B_BROWN_MUSHROOM, B_WATER,
    B_LAVA,
};

const u8 wc_default_hotbar[WC_HOTBAR_SLOTS] = {
    B_GRASS, B_COBBLESTONE, B_OAK_PLANKS, B_OAK_LOG, B_GLASS,
    B_STONE_BRICKS, B_TORCH, B_GLOWSTONE, B_WATER,
};

const char *const wc_biome_name[WC_BIOME_COUNT] = {
    "Ocean",  "Deep Ocean",   "Frozen Ocean", "Beach",       "Snowy Beach",
    "Stony Shore", "Plains",  "Forest",       "Birch Forest", "Taiga",
    "Snowy Plains", "Snowy Taiga", "Desert",  "Badlands",    "Mountains",
    "Snowy Peaks", "River",   "Frozen River", "Swamp",       "Meadow",
};

u32 wc_hash_string(const char *s) {
  u32 h = 2166136261u;
  for (; *s; s++) {
    h ^= (u8)*s;
    h *= 16777619u;
  }
  return h;
}

// ---- simplex noise (Gustavson), seeded permutation ----

hz_internal const f64 WC_F2 = 0.3660254037844386;
hz_internal const f64 WC_G2 = 0.21132486540518713;
hz_internal const f64 WC_F3 = 1.0 / 3.0;
hz_internal const f64 WC_G3 = 1.0 / 6.0;

hz_internal const f64 WC_GRAD3[36] = {
    1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0, 1, 0, 1, -1, 0, 1,
    1, 0, -1, -1, 0, -1, 0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1,
};

// 12 directions spread around the circle (cos/sin of i/12 * 2pi + 0.13)
hz_internal const f64 WC_GRAD2[24] = {
    0.9915618937147881,   0.12963414261969486, 0.7939007180717645,
    0.6080474075638648,   0.38351448615092326, 0.9235348606914594,
    -0.1296341426196947,  0.9915618937147881,  -0.6080474075638646,
    0.7939007180717648,   -0.9235348606914594, 0.3835144861509233,
    -0.9915618937147881,  -0.12963414261969464, -0.7939007180717644,
    -0.6080474075638648,  -0.3835144861509238, -0.9235348606914592,
    0.12963414261969458,  -0.9915618937147881, 0.6080474075638648,
    -0.7939007180717645,  0.9235348606914592,  -0.3835144861509239,
};

void wc_simplex_init(WcSimplex *s, u32 seed, Allocator *alloc) {
  s->perm = ALLOC_ARRAY(alloc, u8, 512);
  s->perm_mod12 = ALLOC_ARRAY(alloc, u8, 512);
  WcRng r = wc_rng(seed);
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u8 *p = ALLOC_ARRAY(&tmp.allocator, u8, 256);
  for (u32 i = 0; i < 256; i++) p[i] = (u8)i;
  for (u32 i = 255; i > 0; i--) {
    u32 j = (u32)(wc_rng_next(&r) * (f64)(i + 1));
    u8 t = p[i];
    p[i] = p[j];
    p[j] = t;
  }
  for (u32 i = 0; i < 512; i++) {
    s->perm[i] = p[i & 255];
    s->perm_mod12[i] = (u8)(s->perm[i] % 12);
  }
  tctx_temp_allocator_end(tmp);
}

f64 wc_noise2(const WcSimplex *s, f64 xin, f64 yin) {
  const u8 *perm = s->perm;
  const u8 *pm = s->perm_mod12;
  f64 n0 = 0, n1 = 0, n2 = 0;
  f64 sk = (xin + yin) * WC_F2;
  i32 i = wc_floor_i(xin + sk);
  i32 j = wc_floor_i(yin + sk);
  f64 t = (f64)(i + j) * WC_G2;
  f64 x0 = xin - ((f64)i - t);
  f64 y0 = yin - ((f64)j - t);
  i32 i1 = x0 > y0 ? 1 : 0;
  i32 j1 = x0 > y0 ? 0 : 1;
  f64 x1 = x0 - i1 + WC_G2;
  f64 y1 = y0 - j1 + WC_G2;
  f64 x2 = x0 - 1.0 + 2.0 * WC_G2;
  f64 y2 = y0 - 1.0 + 2.0 * WC_G2;
  i32 ii = i & 255;
  i32 jj = j & 255;
  f64 t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 >= 0) {
    i32 gi = pm[ii + perm[jj]] * 2;
    t0 *= t0;
    n0 = t0 * t0 * (WC_GRAD2[gi] * x0 + WC_GRAD2[gi + 1] * y0);
  }
  f64 t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 >= 0) {
    i32 gi = pm[ii + i1 + perm[jj + j1]] * 2;
    t1 *= t1;
    n1 = t1 * t1 * (WC_GRAD2[gi] * x1 + WC_GRAD2[gi + 1] * y1);
  }
  f64 t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 >= 0) {
    i32 gi = pm[ii + 1 + perm[jj + 1]] * 2;
    t2 *= t2;
    n2 = t2 * t2 * (WC_GRAD2[gi] * x2 + WC_GRAD2[gi + 1] * y2);
  }
  return 99.2 * (n0 + n1 + n2);
}

f64 wc_noise3(const WcSimplex *s, f64 xin, f64 yin, f64 zin) {
  const u8 *perm = s->perm;
  const u8 *pm = s->perm_mod12;
  f64 n0 = 0, n1 = 0, n2 = 0, n3 = 0;
  f64 sk = (xin + yin + zin) * WC_F3;
  i32 i = wc_floor_i(xin + sk);
  i32 j = wc_floor_i(yin + sk);
  i32 k = wc_floor_i(zin + sk);
  f64 t = (f64)(i + j + k) * WC_G3;
  f64 x0 = xin - ((f64)i - t);
  f64 y0 = yin - ((f64)j - t);
  f64 z0 = zin - ((f64)k - t);
  i32 i1, j1, k1, i2, j2, k2;
  if (x0 >= y0) {
    if (y0 >= z0) {
      i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
    } else if (x0 >= z0) {
      i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;
    } else {
      i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;
    }
  } else {
    if (y0 < z0) {
      i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;
    } else if (x0 < z0) {
      i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;
    } else {
      i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
    }
  }
  f64 x1 = x0 - i1 + WC_G3, y1 = y0 - j1 + WC_G3, z1 = z0 - k1 + WC_G3;
  f64 x2 = x0 - i2 + 2.0 * WC_G3, y2 = y0 - j2 + 2.0 * WC_G3,
      z2 = z0 - k2 + 2.0 * WC_G3;
  f64 x3 = x0 - 1.0 + 3.0 * WC_G3, y3 = y0 - 1.0 + 3.0 * WC_G3,
      z3 = z0 - 1.0 + 3.0 * WC_G3;
  i32 ii = i & 255, jj = j & 255, kk = k & 255;
  f64 t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 >= 0) {
    i32 gi = pm[ii + perm[jj + perm[kk]]] * 3;
    t0 *= t0;
    n0 = t0 * t0 *
         (WC_GRAD3[gi] * x0 + WC_GRAD3[gi + 1] * y0 + WC_GRAD3[gi + 2] * z0);
  }
  f64 t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 >= 0) {
    i32 gi = pm[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3;
    t1 *= t1;
    n1 = t1 * t1 *
         (WC_GRAD3[gi] * x1 + WC_GRAD3[gi + 1] * y1 + WC_GRAD3[gi + 2] * z1);
  }
  f64 t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 >= 0) {
    i32 gi = pm[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3;
    t2 *= t2;
    n2 = t2 * t2 *
         (WC_GRAD3[gi] * x2 + WC_GRAD3[gi + 1] * y2 + WC_GRAD3[gi + 2] * z2);
  }
  f64 t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 >= 0) {
    i32 gi = pm[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3;
    t3 *= t3;
    n3 = t3 * t3 *
         (WC_GRAD3[gi] * x3 + WC_GRAD3[gi + 1] * y3 + WC_GRAD3[gi + 2] * z3);
  }
  return 32.0 * (n0 + n1 + n2 + n3);
}

f64 wc_fbm2(const WcSimplex *s, f64 x, f64 y, u32 octaves) {
  f64 sum = 0, amp = 1, norm = 0;
  for (u32 i = 0; i < octaves; i++) {
    sum += amp * wc_noise2(s, x, y);
    norm += amp;
    amp *= 0.5;
    // rotate + scale so octaves do not share a lattice
    f64 nx = (x * 0.8 - y * 0.6) * 2.0 + 19.1;
    f64 ny = (x * 0.6 + y * 0.8) * 2.0 - 7.7;
    x = nx;
    y = ny;
  }
  return sum / norm;
}

f64 wc_ridged2(const WcSimplex *s, f64 x, f64 y, u32 octaves) {
  f64 sum = 0, amp = 1, norm = 0, weight = 1;
  for (u32 i = 0; i < octaves; i++) {
    f64 v = 1.0 - m_fabs(wc_noise2(s, x, y));
    v *= v;
    v *= weight;
    weight = wc_clamp(v * 1.5, 0.0, 1.0);
    sum += v * amp;
    norm += amp;
    amp *= 0.5;
    f64 nx = (x * 0.8 - y * 0.6) * 2.0 + 11.3;
    f64 ny = (x * 0.6 + y * 0.8) * 2.0 - 3.9;
    x = nx;
    y = ny;
  }
  return sum / norm;
}

hz_internal v3 wc_bilerp3(v3 c00, v3 c10, v3 c01, v3 c11, f64 t, f64 h) {
  v3 a = v3_lerp(c00, c10, (f32)t);
  v3 b = v3_lerp(c01, c11, (f32)t);
  return v3_lerp(a, b, (f32)h);
}

v3 wc_grass_color(f64 t, f64 h) {
  return wc_bilerp3((v3){0.5f, 0.72f, 0.5f}, (v3){0.64f, 0.73f, 0.33f},
                    (v3){0.36f, 0.64f, 0.44f}, (v3){0.3f, 0.7f, 0.18f}, t, h);
}

v3 wc_foliage_color(f64 t, f64 h) {
  return wc_bilerp3((v3){0.42f, 0.64f, 0.44f}, (v3){0.58f, 0.66f, 0.24f},
                    (v3){0.32f, 0.6f, 0.36f}, (v3){0.24f, 0.62f, 0.1f}, t, h);
}

v3 wc_water_color(f64 t, f64 h) {
  if (t > 0.65 && h > 0.68) return (v3){0.38f, 0.47f, 0.32f};
  return wc_bilerp3((v3){0.22f, 0.33f, 0.78f}, (v3){0.24f, 0.48f, 0.86f},
                    (v3){0.2f, 0.36f, 0.8f}, (v3){0.22f, 0.52f, 0.72f}, t, h);
}

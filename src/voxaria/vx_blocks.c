#include "voxaria/vx.h"

// VX_FILL_PAD pads the tables to 256 entries: update its count with the block list
static_assert(B_COUNT == 108, "VX_FILL_PAD must pad the tables to 256 entries");

#define VX_R1(v) v,
#define VX_R4(v) VX_R1(v) VX_R1(v) VX_R1(v) VX_R1(v)
#define VX_R16(v) VX_R4(v) VX_R4(v) VX_R4(v) VX_R4(v)
#define VX_R64(v) VX_R16(v) VX_R16(v) VX_R16(v) VX_R16(v)
#define VX_FILL_PAD(v) VX_R64(v) VX_R64(v) VX_R16(v) VX_R4(v)

#define VX_X_SHAPE(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) sh,
#define VX_X_LAYER(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) ly,
#define VX_X_SOLID(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) so,
#define VX_X_OCCLUDES(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) oc,
#define VX_X_OPACITY(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) op,
#define VX_X_EMISSION(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) (u16)((lr) | ((lg) << 4) | ((lbl) << 8)),
#define VX_X_WAVE(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) wa,
#define VX_X_REPLACEABLE(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) re,
#define VX_X_INVENTORY(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) in,
#define VX_X_LABEL(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) lb,
#define VX_X_FACES(id, lb, sh, ly, so, oc, op, lr, lg, lbl, wa, re, in, ts, tt, tb) {ts, ts, tt, tb, ts, ts},

const u8 vx_shape[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_SHAPE) VX_FILL_PAD(VX_SHAPE_CUBE)};
const u8 vx_layer[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_LAYER) VX_FILL_PAD(VX_LAYER_OPAQUE)};
const u8 vx_solid[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_SOLID) VX_FILL_PAD(1)};
const u8 vx_occludes[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_OCCLUDES) VX_FILL_PAD(1)};
const u8 vx_light_opacity[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_OPACITY) VX_FILL_PAD(15)};
const u16 vx_emission[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_EMISSION)};
const u8 vx_wave[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_WAVE)};
const u8 vx_replaceable[VX_BLOCK_IDS] = {VX_BLOCK_LIST(VX_X_REPLACEABLE)};
// zero fill is T_DIRT on every face
const u8 vx_face_tex[VX_BLOCK_IDS][6] = {VX_BLOCK_LIST(VX_X_FACES)};
const char *const vx_block_label[B_COUNT] = {VX_BLOCK_LIST(VX_X_LABEL)};
const u8 vx_block_in_inventory[B_COUNT] = {VX_BLOCK_LIST(VX_X_INVENTORY)};

const char *const vx_texture_name[T_COUNT] = {
#define VX_X(id, name, v) name,
    VX_TEXTURE_LIST(VX_X)
#undef VX_X
};
const u16 vx_tex_layer[T_COUNT] = {
#define VX_X(id, name, v) L_##id,
    VX_TEXTURE_LIST(VX_X)
#undef VX_X
};
const u8 vx_tex_variants[T_COUNT] = {
#define VX_X(id, name, v) v,
    VX_TEXTURE_LIST(VX_X)
#undef VX_X
};

#define D VX_BOX_TEX_BLOCK
#define VX_BOX(x0, y0, z0, x1, y1, z1) {x0, y0, z0}, {x1, y1, z1}
const VxBox vx_boxes[] = {
    // 0: torch stick and flame
    {VX_BOX(7, 0, 7, 9, 10, 9), {D, D, D, D, D, D}},
    // 1-3: pot body, neck and rim
    {VX_BOX(2, 0, 2, 14, 10, 14), {D, D, D, D, D, D}},
    {VX_BOX(4, 10, 4, 12, 12, 12), {D, D, D, D, D, D}},
    {VX_BOX(3, 12, 3, 13, 14, 13), {D, D, D, D, D, D}},
    // 4-5: chest body (lock face toward +z) and its latch
    {VX_BOX(1, 0, 2, 15, 12, 14), {D, D, D, D, T_CHEST_FRONT, D}},
    {VX_BOX(7, 5, 14, 9, 9, 15), {T_CHEST_FRONT, T_CHEST_FRONT, T_CHEST_FRONT, T_CHEST_FRONT, T_CHEST_FRONT, T_CHEST_FRONT}},
    // 6-7: gold chest body and its latch
    {VX_BOX(1, 0, 2, 15, 12, 14), {D, D, D, D, T_GOLD_CHEST_FRONT, D}},
    {VX_BOX(7, 5, 14, 9, 9, 15), {T_GOLD_CHEST_FRONT, T_GOLD_CHEST_FRONT, T_GOLD_CHEST_FRONT, T_GOLD_CHEST_FRONT, T_GOLD_CHEST_FRONT, T_GOLD_CHEST_FRONT}},
    // 8-10: frame bars (copper or wood), a post and a beam along x or z at the top of the cell
    {VX_BOX(6, 0, 6, 10, 16, 10), {D, D, D, D, D, D}},
    {VX_BOX(0, 12, 6, 16, 16, 10), {D, D, D, D, D, D}},
    {VX_BOX(6, 12, 0, 10, 16, 16), {D, D, D, D, D, D}},
    // 11-17: pylon plinth, body and cap, then the four corner pins its gem floats between
    {VX_BOX(1, 0, 1, 15, 3, 15), {D, D, D, D, D, D}},
    {VX_BOX(3, 3, 3, 13, 9, 13), {D, D, D, D, D, D}},
    {VX_BOX(2, 9, 2, 14, 11, 14), {D, D, D, D, D, D}},
    {VX_BOX(2, 11, 2, 4, 14, 4), {D, D, D, D, D, D}},
    {VX_BOX(12, 11, 2, 14, 14, 4), {D, D, D, D, D, D}},
    {VX_BOX(2, 11, 12, 4, 14, 14), {D, D, D, D, D, D}},
    {VX_BOX(12, 11, 12, 14, 14, 14), {D, D, D, D, D, D}},
    // 18-19: signpost and its board, lettered on the faces toward +z and -z
    {VX_BOX(7, 0, 7, 9, 7, 9), {D, D, D, D, D, D}},
    {VX_BOX(1, 6, 7, 15, 15, 9), {D, D, D, D, T_SIGN, T_SIGN}},
};
#undef VX_BOX
#undef D

#define VX_FB(x0, y0, z0, x1, y1, z1) .lo = {x0, y0, z0}, .hi = {x1, y1, z1}
#define D VX_BOX_TEX_BLOCK
#define C T_FURN_CLOTH
const VxBox vx_furn_boxes[] = {
    // 0: platform, a slab at the top of its cell
    {VX_FB(0, 13, 0, 16, 16, 16), {D, D, D, D, D, D}},
    // 1-2: closed door, a panel across the middle of 1x3 cells, and its handle through both faces
    {VX_FB(0, 0, 6, 16, 48, 10), {D, D, D, D, D, D}},
    {VX_FB(12, 21, 4, 14, 25, 12), {T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL}},
    // 3-4: open door, swung flat against its hinge side
    {VX_FB(0, 0, 0, 4, 48, 16), {D, D, D, D, D, D}},
    {VX_FB(4, 21, 11, 6, 25, 14), {T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL, T_ANVIL}},
    // 5-9: work bench, 2x1: top board on four legs
    {VX_FB(0, 10, 1, 32, 14, 15), {D, D, D, D, D, D}},
    {VX_FB(1, 0, 2, 4, 10, 5), {D, D, D, D, D, D}},
    {VX_FB(28, 0, 2, 31, 10, 5), {D, D, D, D, D, D}},
    {VX_FB(1, 0, 11, 4, 10, 14), {D, D, D, D, D, D}},
    {VX_FB(28, 0, 11, 31, 10, 14), {D, D, D, D, D, D}},
    // 10-14: table, 3x2
    {VX_FB(0, 26, 0, 48, 30, 16), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 2, 6, 26, 6), {D, D, D, D, D, D}},
    {VX_FB(42, 0, 2, 46, 26, 6), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 10, 6, 26, 14), {D, D, D, D, D, D}},
    {VX_FB(42, 0, 10, 46, 26, 14), {D, D, D, D, D, D}},
    // 15-20: chair, 1x2, facing +z with its back at -z
    {VX_FB(2, 12, 2, 14, 15, 14), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 2, 4, 12, 4), {D, D, D, D, D, D}},
    {VX_FB(12, 0, 2, 14, 12, 4), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 12, 4, 12, 14), {D, D, D, D, D, D}},
    {VX_FB(12, 0, 12, 14, 12, 14), {D, D, D, D, D, D}},
    {VX_FB(2, 15, 2, 14, 30, 4), {D, D, D, D, D, D}},
    // 21-25: furnace, 3x2: a stone body whose middle cell holds the fire mouth, a hump and a chimney
    {VX_FB(1, 0, 2, 16, 18, 14), {D, D, D, D, D, D}},
    {VX_FB(16, 0, 2, 32, 18, 14), {D, D, D, D, T_FURNACE_FRONT, D}},
    {VX_FB(32, 0, 2, 47, 18, 14), {D, D, D, D, D, D}},
    {VX_FB(5, 18, 3, 43, 22, 13), {D, D, D, D, D, D}},
    {VX_FB(34, 22, 5, 42, 31, 11), {D, D, D, D, D, D}},
    // 26-29: iron anvil, 2x1: base, waist, face and horn
    {VX_FB(6, 0, 4, 26, 3, 12), {D, D, D, D, D, D}},
    {VX_FB(10, 3, 6, 22, 8, 10), {D, D, D, D, D, D}},
    {VX_FB(3, 8, 3, 28, 12, 13), {D, D, D, D, D, D}},
    {VX_FB(28, 9, 6, 31, 11, 10), {D, D, D, D, D, D}},
    // 30-39: bed, 2x1, its long side to the front: frame, legs, head and foot boards, mattress, blanket, pillow
    {VX_FB(0, 2, 1, 32, 6, 15), {D, D, D, D, D, D}},
    {VX_FB(0, 0, 1, 3, 2, 4), {D, D, D, D, D, D}},
    {VX_FB(29, 0, 1, 32, 2, 4), {D, D, D, D, D, D}},
    {VX_FB(0, 0, 12, 3, 2, 15), {D, D, D, D, D, D}},
    {VX_FB(29, 0, 12, 32, 2, 15), {D, D, D, D, D, D}},
    {VX_FB(0, 0, 0, 3, 15, 16), {D, D, D, D, D, D}},
    {VX_FB(29, 0, 0, 32, 10, 16), {D, D, D, D, D, D}},
    {VX_FB(3, 6, 1, 29, 9, 15), {T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW}},
    {VX_FB(11, 7, 0, 29, 10, 16), {C, C, C, C, C, C}},
    {VX_FB(4, 9, 3, 10, 11, 13), {T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW}},
    // 40-49: bookcase, 2x3: back, sides, top, three shelves, three rows of books
    {VX_FB(0, 0, 0, 32, 48, 3), {D, D, D, D, D, D}},
    {VX_FB(0, 0, 0, 2, 48, 16), {D, D, D, D, D, D}},
    {VX_FB(30, 0, 0, 32, 48, 16), {D, D, D, D, D, D}},
    {VX_FB(2, 45, 0, 30, 48, 16), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 3, 30, 2, 15), {D, D, D, D, D, D}},
    {VX_FB(2, 15, 3, 30, 17, 15), {D, D, D, D, D, D}},
    {VX_FB(2, 30, 3, 30, 32, 15), {D, D, D, D, D, D}},
    {VX_FB(2, 2, 4, 30, 13, 14), {T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS}},
    {VX_FB(2, 17, 4, 30, 28, 14), {T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS}},
    {VX_FB(2, 32, 4, 30, 43, 14), {T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS, T_BOOKS}},
    // 50-58: sofa, 3x1, its back at -z: base, cushions, back, arms, feet
    {VX_FB(1, 2, 2, 47, 7, 15), {C, C, C, C, C, C}},
    {VX_FB(4, 7, 5, 44, 9, 15), {C, C, C, C, C, C}},
    {VX_FB(1, 7, 1, 47, 16, 5), {C, C, C, C, C, C}},
    {VX_FB(0, 2, 1, 4, 12, 16), {D, D, D, D, D, D}},
    {VX_FB(44, 2, 1, 48, 12, 16), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 3, 5, 2, 6), {D, D, D, D, D, D}},
    {VX_FB(43, 0, 3, 46, 2, 6), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 12, 5, 2, 15), {D, D, D, D, D, D}},
    {VX_FB(43, 0, 12, 46, 2, 15), {D, D, D, D, D, D}},
    // 59-61: lamp, 1x3: base, pole, shade
    {VX_FB(4, 0, 4, 12, 2, 12), {D, D, D, D, D, D}},
    {VX_FB(7, 2, 7, 9, 33, 9), {D, D, D, D, D, D}},
    {VX_FB(3, 32, 3, 13, 42, 13), {C, C, C, C, C, C}},
    // 62-67: grandfather clock, 1x3: plinth, case, pendulum window, head, face, crown
    {VX_FB(1, 0, 2, 15, 4, 14), {D, D, D, D, D, D}},
    {VX_FB(2, 4, 3, 14, 34, 13), {D, D, D, D, D, D}},
    {VX_FB(5, 10, 13, 11, 26, 14), {T_GLASS, T_GLASS, T_GLASS, T_GLASS, T_GLASS, T_GLASS}},
    {VX_FB(1, 34, 2, 15, 46, 14), {D, D, D, D, D, D}},
    {VX_FB(4, 36, 14, 12, 44, 15), {T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW}},
    {VX_FB(3, 46, 4, 13, 48, 12), {D, D, D, D, D, D}},
    // 68-74: piano, 3x2: body, key shelf, keys, lid, legs, pedal box
    {VX_FB(0, 8, 0, 48, 30, 10), {D, D, D, D, D, D}},
    {VX_FB(2, 13, 10, 46, 16, 15), {D, D, D, D, D, D}},
    {VX_FB(3, 16, 10, 45, 17, 15), {T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW, T_SNOW}},
    {VX_FB(0, 30, 0, 48, 32, 11), {D, D, D, D, D, D}},
    {VX_FB(2, 0, 10, 5, 13, 14), {D, D, D, D, D, D}},
    {VX_FB(43, 0, 10, 46, 13, 14), {D, D, D, D, D, D}},
    {VX_FB(20, 0, 2, 28, 8, 8), {D, D, D, D, D, D}},
};
#undef D
#undef C
#undef VX_FB

const VxFurniture vx_furniture[VX_BLOCK_IDS] = {
    [B_PLATFORM] = {1, 1, 0, 1},  [B_DOOR] = {1, 3, 1, 2},   [B_DOOR_OPEN] = {1, 3, 3, 2}, [B_WORKBENCH] = {2, 1, 5, 5},
    [B_TABLE] = {3, 2, 10, 5},    [B_CHAIR] = {1, 2, 15, 6}, [B_FURNACE] = {3, 2, 21, 5},  [B_ANVIL] = {2, 1, 26, 4},
    [B_BED] = {2, 1, 30, 10},     [B_BOOKCASE] = {2, 3, 40, 10}, [B_SOFA] = {3, 1, 50, 9}, [B_LAMP] = {1, 3, 59, 3},
    [B_CLOCK] = {1, 3, 62, 6},    [B_PIANO] = {3, 2, 68, 7},
};

const VxModel vx_model[VX_BLOCK_IDS] = {
    [B_TORCH] = {0, 1}, [B_ICE_TORCH] = {0, 1}, [B_POT] = {1, 3}, [B_CHEST] = {4, 2}, [B_GOLD_CHEST] = {6, 2},
    [B_COPPER_POST] = {8, 1}, [B_WOOD_POST] = {8, 1}, [B_COPPER_BEAM_X] = {9, 1}, [B_COPPER_BEAM_Z] = {10, 1}, [B_WOOD_BEAM_X] = {9, 1}, [B_WOOD_BEAM_Z] = {10, 1}, [B_PYLON] = {11, 7}, [B_SIGN] = {18, 2},
};

const char *const vx_biome_name[VX_BIOME_COUNT] = {
    "Forest", "Desert", "Snow", "Jungle", "Corruption", "Ocean", "Glowing Mushroom", "Hallow",
};

u32 vx_hash_string(const char *s) {
  u32 h = 2166136261u;
  for (; *s; s++) {
    h ^= (u8)*s;
    h *= 16777619u;
  }
  return h;
}

// ---- simplex noise (Gustavson), seeded permutation ----

hz_internal const f64 VX_F2 = 0.3660254037844386;
hz_internal const f64 VX_G2 = 0.21132486540518713;
hz_internal const f64 VX_F3 = 1.0 / 3.0;
hz_internal const f64 VX_G3 = 1.0 / 6.0;

hz_internal const f64 VX_GRAD3[36] = {
    1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0, 1, 0, 1, -1, 0, 1,
    1, 0, -1, -1, 0, -1, 0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1,
};

// 12 directions spread around the circle (cos/sin of i/12 * 2pi + 0.13)
hz_internal const f64 VX_GRAD2[24] = {
    0.9915618937147881,   0.12963414261969486, 0.7939007180717645,
    0.6080474075638648,   0.38351448615092326, 0.9235348606914594,
    -0.1296341426196947,  0.9915618937147881,  -0.6080474075638646,
    0.7939007180717648,   -0.9235348606914594, 0.3835144861509233,
    -0.9915618937147881,  -0.12963414261969464, -0.7939007180717644,
    -0.6080474075638648,  -0.3835144861509238, -0.9235348606914592,
    0.12963414261969458,  -0.9915618937147881, 0.6080474075638648,
    -0.7939007180717645,  0.9235348606914592,  -0.3835144861509239,
};

void vx_simplex_init(VxSimplex *s, u32 seed, Allocator *alloc) {
  s->perm = ALLOC_ARRAY(alloc, u8, 512);
  s->perm_mod12 = ALLOC_ARRAY(alloc, u8, 512);
  VxRng r = vx_rng(seed);
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u8 *p = ALLOC_ARRAY(&tmp.allocator, u8, 256);
  for (u32 i = 0; i < 256; i++) p[i] = (u8)i;
  for (u32 i = 255; i > 0; i--) {
    u32 j = (u32)(vx_rng_next(&r) * (f64)(i + 1));
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

f64 vx_noise2(const VxSimplex *s, f64 xin, f64 yin) {
  const u8 *perm = s->perm;
  const u8 *pm = s->perm_mod12;
  f64 n0 = 0, n1 = 0, n2 = 0;
  f64 sk = (xin + yin) * VX_F2;
  i32 i = vx_floor_i(xin + sk);
  i32 j = vx_floor_i(yin + sk);
  f64 t = (f64)(i + j) * VX_G2;
  f64 x0 = xin - ((f64)i - t);
  f64 y0 = yin - ((f64)j - t);
  i32 i1 = x0 > y0 ? 1 : 0;
  i32 j1 = x0 > y0 ? 0 : 1;
  f64 x1 = x0 - i1 + VX_G2;
  f64 y1 = y0 - j1 + VX_G2;
  f64 x2 = x0 - 1.0 + 2.0 * VX_G2;
  f64 y2 = y0 - 1.0 + 2.0 * VX_G2;
  i32 ii = i & 255;
  i32 jj = j & 255;
  f64 t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 >= 0) {
    i32 gi = pm[ii + perm[jj]] * 2;
    t0 *= t0;
    n0 = t0 * t0 * (VX_GRAD2[gi] * x0 + VX_GRAD2[gi + 1] * y0);
  }
  f64 t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 >= 0) {
    i32 gi = pm[ii + i1 + perm[jj + j1]] * 2;
    t1 *= t1;
    n1 = t1 * t1 * (VX_GRAD2[gi] * x1 + VX_GRAD2[gi + 1] * y1);
  }
  f64 t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 >= 0) {
    i32 gi = pm[ii + 1 + perm[jj + 1]] * 2;
    t2 *= t2;
    n2 = t2 * t2 * (VX_GRAD2[gi] * x2 + VX_GRAD2[gi + 1] * y2);
  }
  return 99.2 * (n0 + n1 + n2);
}

f64 vx_noise3(const VxSimplex *s, f64 xin, f64 yin, f64 zin) {
  const u8 *perm = s->perm;
  const u8 *pm = s->perm_mod12;
  f64 n0 = 0, n1 = 0, n2 = 0, n3 = 0;
  f64 sk = (xin + yin + zin) * VX_F3;
  i32 i = vx_floor_i(xin + sk);
  i32 j = vx_floor_i(yin + sk);
  i32 k = vx_floor_i(zin + sk);
  f64 t = (f64)(i + j + k) * VX_G3;
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
  f64 x1 = x0 - i1 + VX_G3, y1 = y0 - j1 + VX_G3, z1 = z0 - k1 + VX_G3;
  f64 x2 = x0 - i2 + 2.0 * VX_G3, y2 = y0 - j2 + 2.0 * VX_G3,
      z2 = z0 - k2 + 2.0 * VX_G3;
  f64 x3 = x0 - 1.0 + 3.0 * VX_G3, y3 = y0 - 1.0 + 3.0 * VX_G3,
      z3 = z0 - 1.0 + 3.0 * VX_G3;
  i32 ii = i & 255, jj = j & 255, kk = k & 255;
  f64 t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 >= 0) {
    i32 gi = pm[ii + perm[jj + perm[kk]]] * 3;
    t0 *= t0;
    n0 = t0 * t0 *
         (VX_GRAD3[gi] * x0 + VX_GRAD3[gi + 1] * y0 + VX_GRAD3[gi + 2] * z0);
  }
  f64 t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 >= 0) {
    i32 gi = pm[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3;
    t1 *= t1;
    n1 = t1 * t1 *
         (VX_GRAD3[gi] * x1 + VX_GRAD3[gi + 1] * y1 + VX_GRAD3[gi + 2] * z1);
  }
  f64 t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 >= 0) {
    i32 gi = pm[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3;
    t2 *= t2;
    n2 = t2 * t2 *
         (VX_GRAD3[gi] * x2 + VX_GRAD3[gi + 1] * y2 + VX_GRAD3[gi + 2] * z2);
  }
  f64 t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 >= 0) {
    i32 gi = pm[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3;
    t3 *= t3;
    n3 = t3 * t3 *
         (VX_GRAD3[gi] * x3 + VX_GRAD3[gi + 1] * y3 + VX_GRAD3[gi + 2] * z3);
  }
  return 32.0 * (n0 + n1 + n2 + n3);
}

f64 vx_fbm2(const VxSimplex *s, f64 x, f64 y, u32 octaves) {
  f64 sum = 0, amp = 1, norm = 0;
  for (u32 i = 0; i < octaves; i++) {
    sum += amp * vx_noise2(s, x, y);
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

f64 vx_ridged2(const VxSimplex *s, f64 x, f64 y, u32 octaves) {
  f64 sum = 0, amp = 1, norm = 0, weight = 1;
  for (u32 i = 0; i < octaves; i++) {
    f64 v = 1.0 - m_fabs(vx_noise2(s, x, y));
    v *= v;
    v *= weight;
    weight = vx_clamp(v * 1.5, 0.0, 1.0);
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

#include "voxaria/vx.h"

#define M VX_GEN_MARGIN
#define SPAN VX_GEN_SPAN

// the ring of surface biomes around the central forest, one per sector
#define VX_SECTORS 6
hz_internal const u8 VX_SECTOR_BIOME[VX_SECTORS] = {
    VX_BIOME_SNOW, VX_BIOME_FOREST, VX_BIOME_JUNGLE, VX_BIOME_DESERT, VX_BIOME_HALLOW, VX_BIOME_CORRUPTION,
};

force_inline i32 vx_floor_div(i32 a, i32 b) {
  i32 q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

hz_internal void vx_place_landmarks(VxTerrain *t);

// the dungeon: a grid of halls PITCH apart within HALF of its centre, each 2*HW+1 wide and H tall, under two blocks of brick
#define VX_DUNGEON_FLOOR 92
#define VX_DUNGEON_HALF 44
#define VX_DUNGEON_PITCH 22
#define VX_DUNGEON_HW 3
#define VX_DUNGEON_H 7

void vx_terrain_init(VxTerrain *t, u32 seed, Allocator *alloc) {
  for (u32 i = 0; i < VX_NOISE_COUNT; i++) vx_simplex_init(&t->n[i], 0, alloc);
  vx_terrain_reseed(t, seed);
}

void vx_terrain_reseed(VxTerrain *t, u32 seed) {
  t->seed = seed;
  u32 s = seed;
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  for (u32 i = 0; i < VX_NOISE_COUNT; i++) {
    s = s * 1664525u + 1013904223u;
    VxSimplex tmp;
    vx_simplex_init(&tmp, s, &ta.allocator);
    mem_cpy(t->n[i].perm, tmp.perm, 512);
    mem_cpy(t->n[i].perm_mod12, tmp.perm_mod12, 512);
  }
  tctx_temp_allocator_end(ta);
  t->sector_angle = (f32)(vx_hash2(7, 11, seed) * M_PI * 2.0);
  // a few mushroom pockets spread around the island in the cavern layer, as in Terraria
  for (u32 i = 0; i < VX_MUSHROOM_POCKETS; i++) {
    f64 a = (i + 0.2 + vx_hash2((i32)i, 3, seed) * 0.6) / VX_MUSHROOM_POCKETS * M_PI * 2.0;
    f64 r = 110.0 + vx_hash2((i32)i, 5, seed) * 150.0;
    f64 y = VX_CAVERN_Y + 16 + vx_hash2((i32)i, 9, seed) * 20.0;
    f64 rad = 34.0 + vx_hash2((i32)i, 13, seed) * 18.0;
    t->mushroom[i] = (v4){(f32)(m_cos(a) * r), (f32)y, (f32)(m_sin(a) * r), (f32)rad};
  }
  // the shimmer cave under the central forest, on the bearing farthest from the mushroom pockets
  f64 best_a = 0, best_d = -1;
  for (u32 k = 0; k < 16; k++) {
    f64 a = k / 16.0 * M_PI * 2.0, cx = m_cos(a) * 55.0, cz = m_sin(a) * 55.0, near_d = 1e18;
    for (u32 i = 0; i < VX_MUSHROOM_POCKETS; i++) {
      f64 dx = t->mushroom[i].x - cx, dz = t->mushroom[i].z - cz;
      near_d = m_min(near_d, dx * dx + dz * dz);
    }
    if (near_d > best_d) best_d = near_d, best_a = a;
  }
  t->aether = (v4){(f32)(m_cos(best_a) * 55.0), (f32)(VX_UNDERGROUND_Y - 6), (f32)(m_sin(best_a) * 55.0), 46.0f};
  // the dungeon under the forest, across the spawn from the shimmer cave
  f64 da = best_a + M_PI;
  t->dungeon = (v4){(f32)vx_floor_i(m_cos(da) * 120.0), (f32)VX_DUNGEON_FLOOR, (f32)vx_floor_i(m_sin(da) * 120.0), (f32)VX_DUNGEON_HALF};
  t->town_y = 0;
  vx_place_landmarks(t);
  vx_town_plan(t);
}

// surface height of one biome's landform at a warped position
hz_internal f64 vx_biome_height(const VxTerrain *T, u8 biome, f64 wx, f64 wz, f64 hills, f64 detail, f64 r) {
  switch (biome) {
  case VX_BIOME_SNOW: {
    f64 ridge = vx_ridged2(&T->n[VX_NOISE_DETAIL], wx * 0.012, wz * 0.012, 3);
    return VX_SURFACE_Y + 7 + hills * 13 + ridge * 11;
  }
  case VX_BIOME_JUNGLE: return VX_SURFACE_Y - 1 + hills * 12 + detail * 3.5;
  case VX_BIOME_DESERT: {
    // broad smooth dunes: their one-block steps ring the slopes like contour lines
    f64 dune = vx_fbm2(&T->n[VX_NOISE_DETAIL], wx * 0.011 + 300.0, wz * 0.011, 2);
    return VX_SURFACE_Y + hills * 6 + dune * 11 - 1;
  }
  case VX_BIOME_CORRUPTION: return VX_SURFACE_Y + 1 + hills * 8 + detail * 2;
  default: {
    // spawn sits on a flat meadow that rolls into hills
    f64 amp = vx_lerp(0.15, 1.0, vx_smoothstep(14.0, 90.0, r));
    return VX_SURFACE_Y + 1 + (hills * 9 + detail * 1.2) * amp;
  }
  }
}

// the landmark over column (x, z): a butte, pillar, pyramid, ridge or spire raises it, an oasis digs it
hz_internal void vx_apply_landmarks(const VxTerrain *T, i32 x, i32 z, VxColumnSample *s) {
  for (u32 i = 0; i < T->landmark_count; i++) {
    const VxLandmark *l = &T->landmarks[i];
    f64 dx = x + 0.5 - l->x, dz = z + 0.5 - l->z;
    f64 reach = l->r * (l->kind == VX_LM_TERRACE || l->kind == VX_LM_BUTTE ? 1.9 : l->kind == VX_LM_PILLAR ? 2.3 : 1.45) + 2.0;
    if (dx * dx + dz * dz > reach * reach) continue;
    f64 len = m_sqrt(dx * dx + dz * dz);
    if (l->kind == VX_LM_PYRAMID) {
      f64 m = m_max(m_fabs(dx), m_fabs(dz));
      if (m >= l->r) continue;
      i32 top = l->base - 3 + (i32)(l->r - m);
      if (top > s->height) s->height = top, s->landmark = VX_LM_PYRAMID;
      continue;
    }
    if (l->kind == VX_LM_PEAK) {
      // four faces on a bearing of the seed's, rounded off at the corners and gullied by noise; steepest near the tip
      f64 fa = vx_hash2((i32)i, 17, T->seed) * M_PI, ca = m_cos(fa), sa = m_sin(fa);
      f64 u = dx * ca + dz * sa, v = dz * ca - dx * sa;
      f64 e = (len * 0.2 + m_max(m_fabs(u), m_fabs(v)) * 0.88) / l->r;
      e += vx_noise2(&T->n[VX_NOISE_DETAIL], (x + 0.5) * 0.11 + i * 5.0, (z + 0.5) * 0.11) * 0.05;
      if (e >= 1.0) continue;
      f64 k = 1.0 - e;
      i32 top = MIN(l->base - 2 + (i32)(l->h1 * k * (0.85 + 0.15 * k)), VX_HEIGHT - 8);
      if (top > s->height) s->height = top, s->landmark = VX_LM_PEAK;
      continue;
    }
    if (l->kind == VX_LM_CREVASSE) {
      // a crack r long each way, bent, pinched shut at its ends; its rim is marked so the walls are ice
      f64 fa = vx_hash2((i32)i, 19, T->seed) * M_PI, ca = m_cos(fa), sa = m_sin(fa);
      f64 a = (dx * ca + dz * sa) / l->r, b = dz * ca - dx * sa;
      if (m_fabs(a) >= 1.0) continue;
      b -= m_sin(a * 2.6) * l->r * 0.18;
      f64 pinch = m_sqrt(1.0 - a * a);
      f64 w = 0.6 + l->r2 * pinch + vx_noise2(&T->n[VX_NOISE_DETAIL], (x + 0.5) * 0.2 + i * 3.0, (z + 0.5) * 0.2) * 0.5;
      if (m_fabs(b) < w) {
        i32 floor = s->ground - 3 - (i32)(l->h1 * pinch);
        if (floor < s->height) s->height = floor;
        s->landmark = VX_LM_CREVASSE;
      } else if (m_fabs(b) < w + 1.6 && s->landmark == VX_LM_NONE) {
        s->landmark = VX_LM_CREVASSE;
      }
      continue;
    }
    // the rim wobbles with direction, so the walls read as worn rock rather than a lathe
    f64 ux = len > 1e-6 ? dx / len : 1.0, uz = len > 1e-6 ? dz / len : 0.0;
    f64 wob = 1.0 + vx_noise2(&T->n[VX_NOISE_PATCH], ux * 1.7 + i * 13.0, uz * 1.7) * (l->kind == VX_LM_OASIS ? 0.1 : 0.06);
    f64 d = len / (l->r * wob);
    if (l->kind == VX_LM_TERRACE) {
      // a steep round mountain stepped into four-block courses, each capped by a narrow grass ledge
      f64 e = d + vx_noise2(&T->n[VX_NOISE_DETAIL], (x + 0.5) * 0.07 + i * 7.0, (z + 0.5) * 0.07) * 0.06;
      if (e >= 1.0) continue;
      f64 rise = (1.0 - e) * (1.25 - 0.25 * (1.0 - e)) * l->h1;
      i32 top = l->base + (i32)(rise / 4.0) * 4 + (rise > 2.0 ? 1 : 0);
      if (top > s->height) s->height = top, s->landmark = l->kind;
      continue;
    }
    if (d >= 1.0 && (l->kind == VX_LM_BUTTE || l->kind == VX_LM_PILLAR)) {
      // dunes banked against the foot: a sand cone falling away from the wall, left as sand
      b32 butte = l->kind == VX_LM_BUTTE;
      f64 k = 1.0 - (d - 1.0) / (butte ? 0.8 : 1.2);
      if (k <= 0.0) continue;
      i32 top = l->base + (i32)(k * k * l->h1 * (butte ? 0.4 : 0.25));
      if (top > s->height) s->height = top;
      continue;
    }
    if (d >= 1.0) continue;
    if (l->kind == VX_LM_OASIS) {
      i32 water = l->base - 2;
      i32 floor = water - 1 - (i32)m_round((1.0 - d * d) * 4.0);
      if (floor < s->height) s->height = floor;
      s->water = (u8)water;
      s->landmark = VX_LM_OASIS;
      continue;
    }
    i32 top = l->base + l->h1;
    if (l->kind == VX_LM_BUTTE && len < l->r2 * wob) top = l->base + l->h2;
    // a ragged lip: the odd rim column stands a block lower
    if (d > 0.88 && (vx_hash2i(x, z, T->seed ^ 0x3e5au) & 3u) == 0) top--;
    if (top > s->height) s->height = top, s->landmark = l->kind;
  }
}

// landmarks: fixed spots round the middle of their biome's sector, kept where the ground there is that biome
typedef struct {
  f32 da, R, r, r2;
  i32 h1, h2;
  u8 kind;
} VxLandmarkSpot;

force_inline u8 vx_landmark_biome(u8 kind) {
  if (kind == VX_LM_PEAK || kind == VX_LM_CREVASSE) return VX_BIOME_SNOW;
  return kind == VX_LM_TERRACE || kind == VX_LM_SPIRE ? VX_BIOME_JUNGLE : VX_BIOME_DESERT;
}

hz_internal const VxLandmarkSpot VX_LANDMARK_SPOTS[] = {
    {0.0f, 205.0f, 24.0f, 19.5f, 21, 44, VX_LM_BUTTE},  {0.13f, 230.0f, 5.0f, 0.0f, 24, 0, VX_LM_PILLAR},
    {-0.22f, 165.0f, 3.5f, 0.0f, 18, 0, VX_LM_PILLAR},  {0.25f, 262.0f, 3.0f, 0.0f, 14, 0, VX_LM_PILLAR},
    {-0.12f, 278.0f, 4.0f, 0.0f, 20, 0, VX_LM_PILLAR},  {0.28f, 170.0f, 10.0f, 6.0f, 9, 17, VX_LM_BUTTE},
    {-0.17f, 236.0f, 14.0f, 0.0f, 0, 0, VX_LM_PYRAMID}, {0.2f, 186.0f, 9.0f, 0.0f, 0, 0, VX_LM_OASIS},
    {-0.04f, 292.0f, 7.0f, 0.0f, 0, 0, VX_LM_OASIS},
    {0.06f, 215.0f, 36.0f, 0.0f, 60, 0, VX_LM_TERRACE}, {-0.2f, 255.0f, 18.0f, 0.0f, 36, 0, VX_LM_TERRACE},
    {-0.07f, 190.0f, 4.5f, 0.0f, 40, 0, VX_LM_SPIRE},  {0.2f, 245.0f, 3.5f, 0.0f, 32, 0, VX_LM_SPIRE},
    {0.0f, 212.0f, 48.0f, 0.0f, 60, 0, VX_LM_PEAK},    {-0.36f, 175.0f, 18.0f, 2.0f, 16, 0, VX_LM_CREVASSE},
};

hz_internal void vx_place_landmarks(VxTerrain *t) {
  t->landmark_count = 0;
  for (u32 i = 0; i < ARRAY_SIZE(VX_LANDMARK_SPOTS) && t->landmark_count < VX_LANDMARKS; i++) {
    const VxLandmarkSpot *p = &VX_LANDMARK_SPOTS[i];
    u8 want = vx_landmark_biome(p->kind);
    f64 a = (want == VX_BIOME_SNOW ? 0.5 : want == VX_BIOME_JUNGLE ? 2.5 : 3.5) / VX_SECTORS * M_PI * 2.0 - t->sector_angle + p->da;
    f64 x = m_cos(a) * p->R, z = m_sin(a) * p->R;
    // every column it covers must be inland and of its biome
    b32 ok = true;
    i32 lowest = VX_HEIGHT;
    for (u32 k = 0; k < 9 && ok; k++) {
      f64 ka = k * (M_PI / 4.0), kr = k == 8 ? 0.0 : p->r * 1.3;
      VxColumnSample s = vx_sample_column(t, (i32)m_floor(x + m_cos(ka) * kr), (i32)m_floor(z + m_sin(ka) * kr));
      ok = s.biome == want && s.coast < 0.01f && s.landmark == VX_LM_NONE;
      lowest = MIN(lowest, s.ground);
    }
    if (!ok) continue;
    VxColumnSample c = vx_sample_column(t, (i32)m_floor(x), (i32)m_floor(z));
    // an oasis sits under the lowest point of its rim so it never spills
    i32 base = p->kind == VX_LM_OASIS ? lowest : c.ground;
    if (p->kind == VX_LM_OASIS && base - 2 <= VX_SEA_LEVEL + 1) continue;
    t->landmarks[t->landmark_count++] = (VxLandmark){
        .x = (f32)x, .z = (f32)z, .r = p->r, .r2 = p->r2, .base = base, .h1 = p->h1, .h2 = p->h2, .kind = p->kind};
  }
}

VxColumnSample vx_sample_column(const VxTerrain *T, i32 xi, i32 zi) {
  f64 x = (f64)xi, z = (f64)zi;
  f64 wx = x + vx_fbm2(&T->n[VX_NOISE_WARP_X], x * 0.006, z * 0.006, 3) * 24.0;
  f64 wz = z + vx_fbm2(&T->n[VX_NOISE_WARP_Z], x * 0.006, z * 0.006, 3) * 24.0;
  f64 r = m_sqrt(wx * wx + wz * wz);
  f64 ang = m_atan2(wz, wx);
  f64 hills = vx_fbm2(&T->n[VX_NOISE_HILLS], wx * 0.008, wz * 0.008, 4);
  f64 detail = vx_fbm2(&T->n[VX_NOISE_DETAIL], x * 0.05, z * 0.05, 2);

  // sector weights: each biome owns a wedge, blending into its neighbours over a few degrees
  f64 sec = (ang + T->sector_angle) / (M_PI * 2.0) * VX_SECTORS +
            vx_fbm2(&T->n[VX_NOISE_SECTOR], wx * 0.004, wz * 0.004, 2) * 0.35;
  sec -= m_floor(sec / VX_SECTORS) * VX_SECTORS;
  i32 si = vx_floor_i(sec);
  f64 sf = sec - si;
  i32 sn = sf < 0.5 ? si - 1 : si + 1;
  f64 edge = sf < 0.5 ? sf : 1.0 - sf;
  f64 wn = 0.5 * (1.0 - vx_smoothstep(0.0, 0.12, edge));
  u8 b0 = VX_SECTOR_BIOME[((si % VX_SECTORS) + VX_SECTORS) % VX_SECTORS];
  u8 b1 = VX_SECTOR_BIOME[((sn % VX_SECTORS) + VX_SECTORS) % VX_SECTORS];
  f64 h = vx_lerp(vx_biome_height(T, b0, wx, wz, hills, detail, r), vx_biome_height(T, b1, wx, wz, hills, detail, r), wn);
  u8 biome = b0;
  f64 blend = wn * 2.0;

  // the central forest; the Hallow reaches in to the town's edge
  f64 rf = vx_lerp(b0 == VX_BIOME_HALLOW ? 74.0 : 110.0, b1 == VX_BIOME_HALLOW ? 74.0 : 110.0, wn) + vx_fbm2(&T->n[VX_NOISE_SECTOR], wx * 0.01 + 50.0, wz * 0.01, 2) * 18.0;
  f64 wf = 1.0 - vx_smoothstep(rf - 24.0, rf + 6.0, r);
  if (wf > 0.0) {
    h = vx_lerp(h, vx_biome_height(T, VX_BIOME_FOREST, wx, wz, hills, detail, r), wf);
    if (wf > 0.5) biome = VX_BIOME_FOREST;
    blend = m_max(blend, 1.0 - m_fabs(wf - 0.5) * 2.0);
  }

  f64 town = vx_town_weight(T, xi, zi);
  if (town > 0.0) h = vx_lerp(h, (f64)T->town_y + 0.5, town * 0.5);

  // coastline: the island radius wobbles with direction, the land slopes into a sandy sea floor
  f64 rc = VX_ISLAND_RADIUS * 0.86 + vx_fbm2(&T->n[VX_NOISE_COAST], m_cos(ang) * 1.7, m_sin(ang) * 1.7, 3) * 34.0;
  f64 coast = vx_smoothstep(rc - 46.0, rc + 22.0, r);
  f64 floor_h = VX_SEA_LEVEL - 22 + detail * 3.0 + hills * 3.0;
  h = vx_lerp(h, floor_h, coast);
  i32 hi = vx_floor_i(h);
  if (coast > 0.22 || (coast > 0.02 && hi <= VX_SEA_LEVEL + 2)) biome = VX_BIOME_OCEAN;
  if (hi > VX_HEIGHT - 24) hi = VX_HEIGHT - 24;
  VxColumnSample s = {.height = hi, .ground = hi, .biome = biome, .water = VX_SEA_LEVEL, .coast = (f32)coast,
                      .blend = (f32)vx_clamp(blend, 0, 1)};
  if (biome == VX_BIOME_DESERT || biome == VX_BIOME_JUNGLE || biome == VX_BIOME_SNOW) vx_apply_landmarks(T, xi, zi, &s);
  return s;
}

// the ice peak's mosaic: slate and blue ice block by block, snow down the gullies and on the ledges, a flank of pink ice
hz_internal u8 vx_peak_block(const VxTerrain *T, i32 x, i32 y, i32 z, b32 ledge) {
  f64 g = vx_noise3(&T->n[VX_NOISE_TUNNEL_A], x * 0.09 + 300.0, y * 0.035, z * 0.09);
  f64 p = vx_noise3(&T->n[VX_NOISE_TUNNEL_B], x * 0.03 + 300.0, y * 0.03, z * 0.03);
  u32 hs = vx_hash2i(x * 73 + y, z, T->seed ^ 0x1ce5u);
  if (g > (ledge ? 0.38 : 0.46)) return B_SNOW;
  b32 ice = (g > 0.12) != ((hs & 7u) == 0);
  if (ice) return p > 0.42 ? B_PINK_ICE : B_ICE;
  return (hs >> 4) & 1u ? B_SLATE : B_STONE;
}

// cave mouths open to the sky only where this mask is up; trees avoid it so none float over a mouth
force_inline b32 vx_cave_mouth(const VxTerrain *T, i32 x, i32 z) {
  f64 r2 = (f64)x * x + (f64)z * z;
  return r2 > 70.0 * 70.0 && vx_noise2(&T->n[VX_NOISE_PATCH], x * 0.011 + 31.0, z * 0.011) > 0.55;
}

// how deep inside the nearest mushroom pocket a position is: 1 at the centre, 0 at and beyond its shell
force_inline f64 vx_mushroom_core(const VxTerrain *T, i32 x, i32 y, i32 z) {
  f64 best = 0;
  for (u32 i = 0; i < VX_MUSHROOM_POCKETS; i++) {
    v4 m = T->mushroom[i];
    f64 dx = (x - m.x) / m.w, dy = (y - m.y) / (m.w * 0.55), dz = (z - m.z) / m.w;
    best = MAX(best, 1.0 - vx_smoothstep(0.35, 0.85, m_sqrt(dx * dx + dy * dy + dz * dz)));
  }
  return best;
}

// inside one of the flattened, noise-edged mushroom pockets
force_inline b32 vx_mushroom_zone(const VxTerrain *T, i32 x, i32 y, i32 z) {
  if (y < VX_CAVERN_Y + 8 || y > VX_UNDERGROUND_Y - 6) return false;
  for (u32 i = 0; i < VX_MUSHROOM_POCKETS; i++) {
    v4 m = T->mushroom[i];
    f64 dx = (x - m.x) / m.w, dy = (y - m.y) / (m.w * 0.55), dz = (z - m.z) / m.w;
    f64 d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > 1.69) continue;
    if (d2 < 0.49) return true;
    f64 edge = vx_fbm2(&T->n[VX_NOISE_MUSHROOM], x * 0.04, z * 0.04 + y * 0.03, 2) * 0.35;
    if (m_sqrt(d2) < 1.0 + edge) return true;
  }
  return false;
}

u8 vx_biome_at(const VxTerrain *T, i32 x, i32 y, i32 z) {
  if (vx_mushroom_zone(T, x, y, z)) return VX_BIOME_MUSHROOM;
  return vx_sample_column(T, x, z).biome;
}

// ---- structures: one candidate per cell of a coarse grid, decided from the seed alone so every chunk agrees ----

#define VX_CABIN_CELL 64
#define VX_CABIN_W 11
#define VX_CABIN_D 9
#define VX_CABIN_H 7
#define VX_LAKE_CELL 96

// a cabin's outer corner (x, z) and floor y
hz_internal b32 vx_cabin_site(const VxTerrain *T, i32 gx, i32 gz, i32 *ox, i32 *oy, i32 *oz) {
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0xcab1u);
  if ((h & 255u) > 150u) return false;
  i32 x = gx * VX_CABIN_CELL + 6 + (i32)((h >> 8) % (VX_CABIN_CELL - 12 - VX_CABIN_W));
  i32 z = gz * VX_CABIN_CELL + 6 + (i32)((h >> 16) % (VX_CABIN_CELL - 12 - VX_CABIN_D));
  i32 y = VX_CAVERN_Y + 14 + (i32)((h >> 24) % (VX_UNDERGROUND_Y - VX_CAVERN_Y - 26));
  f64 cx = x + VX_CABIN_W * 0.5, cz = z + VX_CABIN_D * 0.5;
  f64 r2 = cx * cx + cz * cz;
  if (r2 > 280.0 * 280.0 || r2 < 40.0 * 40.0) return false;
  if (vx_mushroom_zone(T, (i32)cx, y + 3, (i32)cz)) return false;
  *ox = x, *oy = y, *oz = z;
  return true;
}

hz_internal VxLake vx_lake_site(const VxTerrain *T, i32 gx, i32 gz) {
  VxLake l = {0};
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0x1a4e5u);
  if ((h & 255u) > 120u) return l;
  l.r = 14.0 + (f64)((h >> 8) & 15u);
  l.x = gx * VX_LAKE_CELL + 34 + (f64)((h >> 12) % (VX_LAKE_CELL - 68));
  l.z = gz * VX_LAKE_CELL + 34 + (f64)((h >> 20) % (VX_LAKE_CELL - 68));
  l.level = VX_CAVERN_Y + 16 + (i32)(((h >> 27) % 12u) * 3u);
  f64 r2 = l.x * l.x + l.z * l.z;
  if (r2 > 300.0 * 300.0 || vx_mushroom_zone(T, (i32)l.x, l.level, (i32)l.z)) return l;
  l.ok = true;
  return l;
}

// distance from the lake's centre over its wobbling radius: < 1 inside
force_inline f64 vx_lake_dist(const VxTerrain *T, const VxLake *l, i32 x, i32 z) {
  f64 dx = x - l->x, dz = z - l->z;
  f64 wob = 1.0 + vx_noise2(&T->n[VX_NOISE_PATCH], x * 0.05 + 300.0, z * 0.05) * 0.18;
  return m_sqrt(dx * dx + dz * dz) / (l->r * wob);
}

hz_internal void vx_tree_forest(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd, u8 log, u8 leaves);
hz_internal void vx_put(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, u8 id, b32 force);
#define VX_RUIN_CELL 72
#define VX_RUIN_W 9
// the underworld's floor and ceiling over a column (position only, so structures can ask anywhere)
force_inline i32 vx_hell_floor(const VxTerrain *T, i32 x, i32 z) {
  // lava rivers wind across the ash flush with its top
  if (m_fabs(vx_noise2(&T->n[VX_NOISE_HELL], x * 0.012 + 150.0, z * 0.012)) < 0.05) return VX_LAVA_Y - 2;
  return vx_floor_i(16.5 + vx_fbm2(&T->n[VX_NOISE_HELL], x * 0.02, z * 0.02, 3) * 4.0);
}
force_inline i32 vx_hell_ceil(const VxTerrain *T, i32 x, i32 z) {
  return vx_floor_i(37.0 + vx_fbm2(&T->n[VX_NOISE_HELL], x * 0.03 + 77.0, z * 0.03, 2) * 5.0);
}
hz_internal b32 vx_ruin_site(const VxTerrain *T, i32 gx, i32 gz, i32 *ox, i32 *oz, i32 *oh);

#define VX_ISLE_CELL 112
#define VX_ISLE_MAX_R 21

typedef struct {
  f64 x, z, r;
  i32 y;
  u32 h;
  b32 ok;
} VxIsle;

#define VX_POND_CELL 80

// a surface pond: its water sits one below the lowest point of its rim, so it never spills
hz_internal VxLake vx_pond_site(const VxTerrain *T, i32 gx, i32 gz) {
  VxLake l = {0};
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0x90edu);
  if ((h & 255u) > 100u) return l;
  l.r = 6.0 + (f64)((h >> 8) % 6u);
  l.x = gx * VX_POND_CELL + 20 + (f64)((h >> 12) % (VX_POND_CELL - 40));
  l.z = gz * VX_POND_CELL + 20 + (f64)((h >> 20) % (VX_POND_CELL - 40));
  f64 c2 = l.x * l.x + l.z * l.z;
  if (c2 < 30.0 * 30.0 || c2 > 280.0 * 280.0) return l;
  VxColumnSample cs = vx_sample_column(T, (i32)l.x, (i32)l.z);
  if (cs.biome == VX_BIOME_DESERT || cs.biome == VX_BIOME_OCEAN || cs.blend > 0.3f) return l;
  i32 lowest = VX_HEIGHT;
  for (i32 k = 0; k < 16; k++) {
    f64 a = k * (M_PI / 8.0);
    i32 rx = (i32)m_floor(l.x + m_cos(a) * l.r * 1.25), rz = (i32)m_floor(l.z + m_sin(a) * l.r * 1.25);
    lowest = MIN(lowest, vx_sample_column(T, rx, rz).height);
  }
  l.level = lowest - 1;
  if (l.level <= VX_SEA_LEVEL + 1) return l;
  l.ok = true;
  return l;
}

// inside a pond's basin (d < 1) or its banks (d < 1.25)
hz_internal f64 vx_pond_dist_at(const VxTerrain *T, i32 x, i32 z, VxLake *out) {
  f64 best = 9.0;
  for (i32 gz = vx_floor_div(z - 20, VX_POND_CELL); gz <= vx_floor_div(z + 20, VX_POND_CELL); gz++)
    for (i32 gx = vx_floor_div(x - 20, VX_POND_CELL); gx <= vx_floor_div(x + 20, VX_POND_CELL); gx++) {
      VxLake l = vx_pond_site(T, gx, gz);
      if (!l.ok) continue;
      f64 dx = x + 0.5 - l.x, dz = z + 0.5 - l.z;
      f64 wob = 1.0 + vx_noise2(&T->n[VX_NOISE_PATCH], x * 0.09 + 50.0, z * 0.09) * 0.15;
      f64 d = m_sqrt(dx * dx + dz * dz) / (l.r * wob);
      if (d < best) {
        best = d;
        if (out) *out = l;
      }
    }
  return best;
}

// a floating island: centre, radius and the height of its grass top; they ring the island over its coast
hz_internal VxIsle vx_isle_site(const VxTerrain *T, i32 gx, i32 gz) {
  VxIsle s = {0};
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0x5c7u);
  if ((h & 255u) > 210u) return s;
  s.r = 14.0 + (f64)((h >> 8) % 6u);
  s.x = gx * VX_ISLE_CELL + 30 + (f64)((h >> 12) % (VX_ISLE_CELL - 60));
  s.z = gz * VX_ISLE_CELL + 30 + (f64)((h >> 20) % (VX_ISLE_CELL - 60));
  s.y = 200 + (i32)((h >> 27) % 14u);
  s.h = h;
  f64 d2 = s.x * s.x + s.z * s.z;
  if (d2 < 300.0 * 300.0 || d2 > 355.0 * 355.0) return s;
  s.ok = true;
  return s;
}


// ---- jungle bee hives: hollow wax shells under the jungle, half full of honey ----

#define VX_HIVE_CELL 64

typedef struct {
  b32 ok;
  i32 x, y, z, rx, ry;
} VxHive;

hz_internal VxHive vx_hive_site(const VxTerrain *T, i32 gx, i32 gz) {
  VxHive v = {0};
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0xb33eu);
  v.x = gx * VX_HIVE_CELL + 14 + (i32)((h >> 4) % (VX_HIVE_CELL - 28));
  v.z = gz * VX_HIVE_CELL + 14 + (i32)((h >> 12) % (VX_HIVE_CELL - 28));
  VxColumnSample cs = vx_sample_column(T, v.x, v.z);
  if (cs.biome != VX_BIOME_JUNGLE || cs.blend > 0.25f) return v;
  v.rx = 7 + (i32)((h >> 20) % 4u);
  v.ry = 5 + (i32)((h >> 24) % 3u);
  v.y = VX_CAVERN_Y + 24 + (i32)((h >> 27) % 24u);
  if (v.y + v.ry + 6 > cs.height - 8) return v;
  v.ok = true;
  return v;
}

hz_internal void vx_place_hives(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  const i32 reach = 12;
  for (i32 gz = vx_floor_div(z0 - reach, VX_HIVE_CELL); gz <= vx_floor_div(z0 + 15 + reach, VX_HIVE_CELL); gz++)
    for (i32 gx = vx_floor_div(x0 - reach, VX_HIVE_CELL); gx <= vx_floor_div(x0 + 15 + reach, VX_HIVE_CELL); gx++) {
      VxHive v = vx_hive_site(T, gx, gz);
      if (!v.ok || v.x + v.rx + 1 < x0 || v.x - v.rx - 1 > x0 + 15 || v.z + v.rx + 1 < z0 || v.z - v.rx - 1 > z0 + 15)
        continue;
      i32 level = v.y - v.ry / 3;
      for (i32 lz = 0; lz < 16; lz++)
        for (i32 lx = 0; lx < 16; lx++) {
          f64 dx = (x0 + lx + 0.5 - (v.x + 0.5)) / v.rx, dz = (z0 + lz + 0.5 - (v.z + 0.5)) / v.rx;
          f64 h2 = dx * dx + dz * dz;
          if (h2 >= 1.0) continue;
          f64 wob = vx_hash2(x0 + lx, z0 + lz, T->seed ^ 0xbee5u) * 0.12;
          for (i32 y = v.y - v.ry - 1; y <= v.y + v.ry + 1; y++) {
            f64 dy = (y + 0.5 - (v.y + 0.5)) / v.ry;
            f64 d = m_sqrt(h2 + dy * dy);
            if (d >= 1.0 + wob * 0.5) continue;
            u8 *b = &blocks[VX_IDX(lx, y, lz)];
            if (d > 0.74 - wob) *b = B_HIVE;
            else *b = y <= level ? B_HONEY : B_AIR;
          }
        }
    }
}

// the shimmer cave: a bowl of shimmer under a dome hung with rounded aetherium lumps, rising to a beach at its rim
hz_internal void vx_place_aether(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  v4 a = T->aether;
  f64 R = a.w, reach = R * 1.3;
  if (x0 + 16 < a.x - reach || x0 > a.x + reach || z0 + 16 < a.z - reach || z0 > a.z + reach) return;
  i32 L = (i32)a.y;
  const i32 lump_cell = 15;
  for (i32 lz = 0; lz < 16; lz++)
    for (i32 lx = 0; lx < 16; lx++) {
      i32 wx = x0 + lx, wz = z0 + lz;
      f64 dx = wx + 0.5 - a.x, dz = wz + 0.5 - a.z;
      f64 d = m_sqrt(dx * dx + dz * dz) / (R * (1.0 + vx_noise2(&T->n[VX_NOISE_PATCH], wx * 0.06 + 70.0, wz * 0.06) * 0.12));
      if (d >= 1.25) continue;
      if (d >= 1.0) {
        for (i32 y = L - 9; y <= L + 22; y++) blocks[VX_IDX(lx, y, lz)] = B_AETHERIUM;
        continue;
      }
      f64 fl = L - 1 - m_max(0.0, 0.78 - d) / 0.78 * 6.0 + m_max(0.0, d - 0.8) / 0.2 * 6.0;
      i32 floor_y = (i32)m_floor(fl);
      f64 ceil_f = L + 13 + (1.0 - d * d) * 5.0;
      // lumps hang from the dome, each rounded at its foot
      i32 gx0 = vx_floor_div(wx, lump_cell), gz0 = vx_floor_div(wz, lump_cell);
      for (i32 gz = gz0 - 1; gz <= gz0 + 1; gz++)
        for (i32 gx = gx0 - 1; gx <= gx0 + 1; gx++) {
          u32 h = vx_hash2i(gx, gz, T->seed ^ 0xae7eu);
          f64 cx = gx * lump_cell + 1.0 + (h & 255u) / 255.0 * (lump_cell - 2.0);
          f64 cz = gz * lump_cell + 1.0 + ((h >> 8) & 255u) / 255.0 * (lump_cell - 2.0);
          f64 rb = 4.5 + ((h >> 16) & 15u) / 15.0 * 3.5, depth = 7.0 + ((h >> 20) & 15u) / 15.0 * 6.0;
          f64 q = m_sqrt((wx + 0.5 - cx) * (wx + 0.5 - cx) + (wz + 0.5 - cz) * (wz + 0.5 - cz)) / rb;
          if (q < 1.0) ceil_f = m_min(ceil_f, L + 13 + (1.0 - d * d) * 5.0 - depth * m_sqrt(1.0 - q * q));
        }
      i32 ceil_y = MAX((i32)m_ceil(ceil_f), MAX(floor_y, L) + 3);
      for (i32 y = floor_y - 3; y <= ceil_y + 3; y++) {
        u8 *b = &blocks[VX_IDX(lx, y, lz)];
        if (y <= floor_y || y >= ceil_y) *b = B_AETHERIUM;
        else *b = y <= L ? B_SHIMMER : B_AIR;
      }
    }
}

// signed offset of c from the nearest hall line, the lines being the multiples of PITCH within half
force_inline i32 vx_dungeon_off(i32 c, i32 half) {
  i32 line = vx_floor_div(c + VX_DUNGEON_PITCH / 2, VX_DUNGEON_PITCH) * VX_DUNGEON_PITCH;
  return c - MAX(-half, MIN(half, line));
}
force_inline i32 vx_iabs(i32 v) { return v < 0 ? -v : v; }

// the dungeon: a buried grid of green brick halls lit by torches on posts and hung with cobwebs, its brick shaft open to the surface
hz_internal void vx_place_dungeon(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  i32 cx = (i32)T->dungeon.x, cz = (i32)T->dungeon.z, F = (i32)T->dungeon.y, half = (i32)T->dungeon.w;
  i32 hw = VX_DUNGEON_HW, reach = half + hw + 2, top_y = F + VX_DUNGEON_H;
  if (x0 + 15 < cx - reach || x0 > cx + reach || z0 + 15 < cz - reach || z0 > cz + reach) return;
  for (i32 lz = 0; lz < 16; lz++)
    for (i32 lx = 0; lx < 16; lx++) {
      i32 u = x0 + lx - cx, v = z0 + lz - cz;
      i32 sv = vx_dungeon_off(v, half), su = vx_dungeon_off(u, half);
      b32 hall_x = vx_iabs(u) <= half + hw && vx_iabs(sv) <= hw, hall_z = vx_iabs(v) <= half + hw && vx_iabs(su) <= hw;
      b32 shell = (vx_iabs(u) <= reach && vx_iabs(sv) <= hw + 2) || (vx_iabs(v) <= reach && vx_iabs(su) <= hw + 2);
      i32 ring = MAX(vx_iabs(u), vx_iabs(v));
      b32 shaft = ring <= hw + 1;
      if (!shell && !shaft) continue;
      // the entrance: a brick shaft from the central crossing up through the ground, its rim a few blocks proud
      if (shaft) {
        i32 ground = VX_HEIGHT - 1;
        while (ground > top_y && !vx_solid[blocks[VX_IDX(lx, ground, lz)]]) ground--;
        for (i32 y = top_y + 1; y <= MIN(ground + 4, VX_HEIGHT - 1); y++) blocks[VX_IDX(lx, y, lz)] = ring <= hw - 1 ? B_AIR : B_DUNGEON_BRICK;
      }
      if (!shell) continue;
      b32 air = hall_x || hall_z;
      for (i32 y = F - 1; y <= top_y + 2; y++) blocks[VX_IDX(lx, y, lz)] = air && y > F && y <= top_y ? B_AIR : B_DUNGEON_BRICK;
      if (!air || (hall_x && hall_z) || shaft) continue;
      // props by place along one hall: s across it, t along it
      i32 s = hall_x ? sv : su, t = hall_x ? u : v;
      u32 h = vx_hash3i(u, F, v, T->seed ^ 0xd06eu);
      if (vx_iabs(s) == hw - 1 && ((t % 6) + 6) % 6 == 0) {
        blocks[VX_IDX(lx, F + 1, lz)] = B_WOOD_POST;
        blocks[VX_IDX(lx, F + 2, lz)] = B_TORCH;
      } else if (vx_iabs(s) == hw) {
        if ((h & 63u) == 0) blocks[VX_IDX(lx, F + 1, lz)] = B_GOLD_CHEST;
        else if ((h & 15u) == 1) blocks[VX_IDX(lx, F + 1, lz)] = B_POT;
        if (((h >> 8) & 7u) < 3u) blocks[VX_IDX(lx, top_y, lz)] = B_COBWEB;
        if (((h >> 12) & 15u) == 0) blocks[VX_IDX(lx, top_y - 1, lz)] = B_COBWEB;
      } else if (((h >> 16) & 31u) == 0) {
        blocks[VX_IDX(lx, top_y, lz)] = B_COBWEB;
      }
    }
}

b32 vx_find_site(const VxTerrain *T, u32 kind, i32 x, i32 z, i32 *ox, i32 *oy, i32 *oz) {
  if (kind == VX_SITE_DUNGEON) {
    // in an outer hall, far from the daylight down the entrance shaft
    *ox = (i32)T->dungeon.x + (i32)T->dungeon.w - VX_DUNGEON_PITCH / 2, *oy = (i32)T->dungeon.y + 1, *oz = (i32)T->dungeon.z + (i32)T->dungeon.w;
    return true;
  }
  if (kind == VX_SITE_AETHER) {
    // over the shimmer's west edge, just above its surface
    *ox = (i32)(T->aether.x - T->aether.w * 0.8f), *oy = (i32)T->aether.y + 1, *oz = (i32)T->aether.z;
    return true;
  }
  i32 cell = kind == VX_SITE_HIVE ? VX_HIVE_CELL : kind == VX_SITE_POND ? VX_POND_CELL : kind == VX_SITE_LAKE ? VX_LAKE_CELL : kind == VX_SITE_RUIN ? VX_RUIN_CELL : kind == VX_SITE_SKY_ISLAND ? 112 : VX_CABIN_CELL;
  i32 gx0 = vx_floor_div(x, cell), gz0 = vx_floor_div(z, cell);
  for (i32 r = 0; r <= 8; r++)
    for (i32 gz = gz0 - r; gz <= gz0 + r; gz++)
      for (i32 gx = gx0 - r; gx <= gx0 + r; gx++) {
        if (MAX(gx > gx0 ? gx - gx0 : gx0 - gx, gz > gz0 ? gz - gz0 : gz0 - gz) != r) continue;
        if (kind == VX_SITE_CABIN) {
          i32 cx, cy, cz;
          if (!vx_cabin_site(T, gx, gz, &cx, &cy, &cz)) continue;
          *ox = cx + 2, *oy = cy + 1, *oz = cz + VX_CABIN_D / 2;
          return true;
        }
        if (kind == VX_SITE_HIVE) {
          VxHive v = vx_hive_site(T, gx, gz);
          if (!v.ok) continue;
          // inside, at the honey's surface
          *ox = v.x - v.rx / 2, *oy = v.y - v.ry / 3 + 1, *oz = v.z;
          return true;
        }
        if (kind == VX_SITE_SKY_ISLAND) {
          VxIsle is = vx_isle_site(T, gx, gz);
          if (!is.ok) continue;
          // on the grass before the house's doorway
          *ox = (i32)m_round(is.x) + 6, *oy = is.y + 1, *oz = (i32)m_round(is.z);
          return true;
        }
        if (kind == VX_SITE_RUIN) {
          i32 bx, bz, height;
          if (!vx_ruin_site(T, gx, gz, &bx, &bz, &height)) continue;
          // a spot out on dry ash, looking back at the tower
          i32 sx = bx + VX_RUIN_W / 2 + 14, sz = bz + VX_RUIN_W / 2;
          for (i32 d = 0; d < 16 && vx_hell_floor(T, sx, sz) <= VX_LAVA_Y; d++) sz += (d & 1) ? d + 1 : -(d + 1);
          *ox = sx, *oy = vx_hell_floor(T, sx, sz), *oz = sz;
          return true;
        }
        if (kind == VX_SITE_MUSHROOM) {
          if (gx != gx0 || gz != gz0) continue;
          // the pocket nearest to (x, z), at its centre
          f64 bd = 1e18;
          for (u32 i = 0; i < VX_MUSHROOM_POCKETS; i++) {
            v4 m = T->mushroom[i];
            f64 d = (m.x - x) * (m.x - x) + (m.z - z) * (m.z - z);
            if (d < bd) bd = d, *ox = (i32)m.x, *oy = (i32)m.y, *oz = (i32)m.z;
          }
          return true;
        }
        if (kind == VX_SITE_POND) {
          VxLake l = vx_pond_site(T, gx, gz);
          if (!l.ok) continue;
          // on the bank, at the pond's level
          *ox = (i32)m_round(l.x - l.r * 1.6), *oy = l.level + 1, *oz = (i32)m_round(l.z);
          return true;
        }
        if (kind == VX_SITE_LAKE) {
          VxLake l = vx_lake_site(T, gx, gz);
          if (!l.ok) continue;
          *ox = (i32)(l.x - l.r * 0.9), *oy = l.level + 2, *oz = (i32)l.z;
          return true;
        }
      }
  return false;
}

force_inline void vx_set_local(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, u8 id) {
  i32 lx = x - x0, lz = z - z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 1 || y >= VX_HEIGHT) return;
  blocks[VX_IDX(lx, y, lz)] = id;
}

// an underground cabin in the biome's wood: two doorways, glass windows, a chest against the back wall, torches and pots
hz_internal void vx_stamp_cabin(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0, i32 bx, i32 by, i32 bz, u32 h) {
  u8 biome = vx_sample_column(T, bx + VX_CABIN_W / 2, bz + VX_CABIN_D / 2).biome;
  u8 wall = biome == VX_BIOME_SNOW ? B_BOREAL_PLANKS : biome == VX_BIOME_JUNGLE ? B_MAHOGANY_PLANKS : B_PLANKS;
  u8 frame = biome == VX_BIOME_SNOW ? B_BOREAL_LOG : biome == VX_BIOME_JUNGLE ? B_MAHOGANY_LOG : B_LOG;
  for (i32 dy = 0; dy < VX_CABIN_H; dy++)
    for (i32 dz = 0; dz < VX_CABIN_D; dz++)
      for (i32 dx = 0; dx < VX_CABIN_W; dx++) {
        b32 ex = dx == 0 || dx == VX_CABIN_W - 1, ez = dz == 0 || dz == VX_CABIN_D - 1;
        b32 shell = ex || ez || dy == 0 || dy == VX_CABIN_H - 1;
        u8 id = B_AIR;
        if (shell) {
          id = wall;
          if (dy == 0) id = B_GRAY_BRICK;
          else if (ex && ez) id = frame;
          else if (dy == VX_CABIN_H - 1 && (ex || ez)) id = frame;
          // doorways on both short walls, three tall
          if (ex && dz >= 3 && dz <= 5 && dy >= 1 && dy <= 3) id = B_AIR;
          // windows on the long walls
          if (ez && !ex && dy >= 2 && dy <= 3 && (dx == 3 || dx == 4 || dx == 6 || dx == 7)) id = B_GLASS;
        }
        vx_set_local(blocks, x0, z0, bx + dx, by + dy, bz + dz, id);
      }
  // a short clearing outside each doorway
  for (i32 side = 0; side < 2; side++)
    for (i32 k = 1; k <= 2; k++)
      for (i32 dz = 3; dz <= 5; dz++)
        for (i32 dy = 1; dy <= 3; dy++) {
          i32 x = side ? bx + VX_CABIN_W - 1 + k : bx - k;
          vx_set_local(blocks, x0, z0, x, by + dy, bz + dz, B_AIR);
          vx_set_local(blocks, x0, z0, x, by, bz + dz, B_GRAY_BRICK);
        }
  // the chest faces the room from the back wall; torches and pots in the corners
  vx_set_local(blocks, x0, z0, bx + VX_CABIN_W / 2, by + 1, bz + 1, (h >> 3) % 4 == 0 ? B_GOLD_CHEST : B_CHEST);
  vx_set_local(blocks, x0, z0, bx + 1, by + 1, bz + 1, B_TORCH);
  vx_set_local(blocks, x0, z0, bx + VX_CABIN_W - 2, by + 1, bz + VX_CABIN_D - 2, B_TORCH);
  vx_set_local(blocks, x0, z0, bx + VX_CABIN_W - 2, by + 1, bz + 1, B_POT);
  if ((h >> 5) & 1) vx_set_local(blocks, x0, z0, bx + 1, by + 1, bz + VX_CABIN_D - 2, B_POT);
  if ((h >> 6) & 1) vx_set_local(blocks, x0, z0, bx + 2, by + 1, bz + 1, B_POT);
}

hz_internal void vx_place_cabins(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  i32 gx0 = vx_floor_div(x0 - VX_CABIN_W - 3, VX_CABIN_CELL), gx1 = vx_floor_div(x0 + 18, VX_CABIN_CELL);
  i32 gz0 = vx_floor_div(z0 - VX_CABIN_D - 3, VX_CABIN_CELL), gz1 = vx_floor_div(z0 + 18, VX_CABIN_CELL);
  for (i32 gz = gz0; gz <= gz1; gz++)
    for (i32 gx = gx0; gx <= gx1; gx++) {
      i32 bx, by, bz;
      if (!vx_cabin_site(T, gx, gz, &bx, &by, &bz)) continue;
      if (bx > x0 + 18 || bx + VX_CABIN_W + 2 < x0 - 2 || bz > z0 + 15 || bz + VX_CABIN_D < z0) continue;
      vx_stamp_cabin(T, blocks, x0, z0, bx, by, bz, vx_hash2i(gx, gz, T->seed ^ 0xcab2u));
    }
}

#define VX_FALL_CELL 40


// a ruined obsidian tower's corner (x, z) and its height above the lava
hz_internal b32 vx_ruin_site(const VxTerrain *T, i32 gx, i32 gz, i32 *ox, i32 *oz, i32 *oh) {
  u32 h = vx_hash2i(gx, gz, T->seed ^ 0x4e11u);
  if ((h & 255u) > 140u) return false;
  i32 x = gx * VX_RUIN_CELL + 8 + (i32)((h >> 8) % (VX_RUIN_CELL - 16 - VX_RUIN_W));
  i32 z = gz * VX_RUIN_CELL + 8 + (i32)((h >> 16) % (VX_RUIN_CELL - 16 - VX_RUIN_W));
  f64 cx = x + VX_RUIN_W * 0.5, cz = z + VX_RUIN_W * 0.5;
  if (cx * cx + cz * cz > 320.0 * 320.0) return false;
  *ox = x, *oz = z;
  *oh = 12 + (i32)((h >> 24) % 10);
  return true;
}

// a crumbling tower of obsidian brick framed by a grid of glowing hellstone brick: bands every five blocks, posts at the corners and middles
hz_internal void vx_stamp_ruin(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0, i32 bx, i32 bz, i32 height, u32 h) {
  i32 base = 6;
  i32 ceil = vx_hell_ceil(T, bx + VX_RUIN_W / 2, bz + VX_RUIN_W / 2) - 2;
  i32 top = MIN(base + height + 10, ceil);
  for (i32 y = base; y <= top; y++)
    for (i32 dz = 0; dz < VX_RUIN_W; dz++)
      for (i32 dx = 0; dx < VX_RUIN_W; dx++) {
        b32 ex = dx == 0 || dx == VX_RUIN_W - 1, ez = dz == 0 || dz == VX_RUIN_W - 1;
        i32 k = y - base;
        b32 storey = k > 8 && (k - 8) % 5 == 0;
        u8 id = B_AIR;
        if (ex || ez) id = (k % 5 == 0 || (ex && ez) || dx == 4 || dz == 4) ? B_HELLSTONE_BRICK : B_OBSIDIAN_BRICK;
        else if (storey && !(dx >= 3 && dx <= 4 && dz >= 3 && dz <= 4)) id = B_OBSIDIAN_BRICK;
        else if (k <= 8) id = B_OBSIDIAN_BRICK;
        // a doorway on the first storey
        if (ex && !ez && k >= 9 && k <= 11 && dz >= 3 && dz <= 5) id = B_AIR;
        // the top crumbles, the higher the more, and the obsidian before its frame
        f64 crumble = vx_smoothstep(height + 2.0, height + 12.0, (f64)k) * (id == B_HELLSTONE_BRICK ? 0.4 : 0.8);
        if (id != B_AIR && vx_hash3i(bx + dx, y, bz + dz, T->seed ^ h) / 4294967296.0 < crumble * 0.9) id = B_AIR;
        if (id == B_AIR && k <= 8) continue;
        vx_set_local(blocks, x0, z0, bx + dx, y, bz + dz, id);
      }
}

// lava pouring from the underworld's ceiling into the lava sea
hz_internal void vx_place_lava_falls(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  for (i32 gz = vx_floor_div(z0, VX_FALL_CELL); gz <= vx_floor_div(z0 + 15, VX_FALL_CELL); gz++)
    for (i32 gx = vx_floor_div(x0, VX_FALL_CELL); gx <= vx_floor_div(x0 + 15, VX_FALL_CELL); gx++) {
      u32 h = vx_hash2i(gx, gz, T->seed ^ 0xfa11u);
      if ((h & 255u) > 45u) continue;
      i32 x = gx * VX_FALL_CELL + (i32)((h >> 8) % VX_FALL_CELL), z = gz * VX_FALL_CELL + (i32)((h >> 16) % VX_FALL_CELL);
      i32 lx = x - x0, lz = z - z0;
      if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
      i32 w = 1 + (i32)((h >> 24) & 1);
      for (i32 ox = 0; ox < w && lx + ox < 16; ox++) {
        i32 y = vx_hell_ceil(T, x, z) - 1;
        if (blocks[VX_IDX(lx + ox, y + 1, lz)] == B_AIR) continue;
        for (; y > 0 && blocks[VX_IDX(lx + ox, y, lz)] == B_AIR; y--) blocks[VX_IDX(lx + ox, y, lz)] = B_LAVA;
      }
    }
}

hz_internal void vx_place_ruins(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  for (i32 gz = vx_floor_div(z0 - VX_RUIN_W, VX_RUIN_CELL); gz <= vx_floor_div(z0 + 15, VX_RUIN_CELL); gz++)
    for (i32 gx = vx_floor_div(x0 - VX_RUIN_W, VX_RUIN_CELL); gx <= vx_floor_div(x0 + 15, VX_RUIN_CELL); gx++) {
      i32 bx, bz, height;
      if (!vx_ruin_site(T, gx, gz, &bx, &bz, &height)) continue;
      if (bx > x0 + 15 || bx + VX_RUIN_W <= x0 || bz > z0 + 15 || bz + VX_RUIN_W <= z0) continue;
      vx_stamp_ruin(T, blocks, x0, z0, bx, bz, height, vx_hash2i(gx, gz, T->seed ^ 0x4e12u));
    }
}

// the island's house: mahogany walls in a dark log frame, paned windows, a flat roof, a gold chest on a sunplate floor
hz_internal void vx_stamp_sky_hut(u8 *blocks, i32 x0, i32 z0, i32 cx, i32 y, i32 cz) {
  i32 bx = cx - 3, bz = cz - 3;
  for (i32 dy = 0; dy < 6; dy++)
    for (i32 dz = 0; dz < 7; dz++)
      for (i32 dx = 0; dx < 7; dx++) {
        b32 ex = dx == 0 || dx == 6, ez = dz == 0 || dz == 6;
        u8 id = B_AIR;
        if (dy == 0) id = B_SUNPLATE;
        else if (dy == 5) id = ex || ez ? B_MAHOGANY_LOG : B_MAHOGANY_PLANKS;
        else if (ex && ez) id = B_MAHOGANY_LOG;
        else if (ex || ez) id = dy == 4 ? B_MAHOGANY_LOG : B_MAHOGANY_PLANKS;
        // two paned windows on each face along x, a doorway on the +x face
        if (ez && !ex && dy >= 2 && dy <= 3 && (dx == 2 || dx == 4)) id = B_GLASS;
        if (dx == 6 && dz == 3 && dy >= 1 && dy <= 2) id = B_AIR;
        vx_set_local(blocks, x0, z0, bx + dx, y + dy, bz + dz, id);
      }
  vx_set_local(blocks, x0, z0, bx + 3, y + 1, bz + 1, B_GOLD_CHEST);
  vx_set_local(blocks, x0, z0, bx + 1, y + 1, bz + 1, B_TORCH);
  vx_set_local(blocks, x0, z0, bx + 1, y + 1, bz + 5, B_POT);
}

// a round clump of leaves
hz_internal void vx_leaf_ball(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, f64 r, VxRng *rnd) {
  i32 ri = (i32)m_ceil(r);
  for (i32 dy = -ri; dy <= ri; dy++)
    for (i32 dz = -ri; dz <= ri; dz++)
      for (i32 dx = -ri; dx <= ri; dx++) {
        f64 d2 = dx * dx + dy * dy * 1.3 + dz * dz;
        if (d2 > r * r + (vx_rng_next(rnd) - 0.5) * 2.0) continue;
        vx_put(blocks, x0, z0, x + dx, y + dy, z + dz, B_LEAVES, false);
      }
}

// the island's tall tree: a straight trunk, a round crown and clumps on short branches below it
hz_internal void vx_tree_sky(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, i32 height, f64 crown, VxRng *rnd) {
  i32 top = y + height;
  vx_leaf_ball(blocks, x0, z0, x, top, z, crown, rnd);
  i32 clumps = height > 9 ? 3 : 0;
  for (i32 c = 0; c < clumps; c++) {
    f64 a = (c + vx_rng_next(rnd) * 0.6) * (M_PI * 2.0 / 3.0);
    i32 cy = top - 3 - (i32)(vx_rng_next(rnd) * height * 0.35);
    i32 ox = (i32)m_round(m_cos(a) * 2.0), oz = (i32)m_round(m_sin(a) * 2.0);
    vx_put(blocks, x0, z0, x + ox / 2, cy - 1, z + oz / 2, B_LOG, false);
    vx_leaf_ball(blocks, x0, z0, x + ox, cy, z + oz, crown * 0.7, rnd);
  }
  for (i32 yy = y; yy < top; yy++) vx_put(blocks, x0, z0, x, yy, z, B_LOG, true);
}

// distance from the island's centre over its wobbling radius: < 1 is the grass top, to 1.1 the cloud lip
force_inline f64 vx_isle_dist(const VxTerrain *T, const VxIsle *s, i32 x, i32 z) {
  f64 dx = x - s->x, dz = z - s->z;
  f64 wob = 1.0 + vx_noise2(&T->n[VX_NOISE_PATCH], x * 0.07 + 900.0, z * 0.07) * 0.12;
  return m_sqrt(dx * dx + dz * dz) / (s->r * wob);
}

// a cloud-based island: a deep round cloud bowl, a meadow on top, a house, a tall tree and a small one
hz_internal void vx_stamp_isle(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0, const VxIsle *s) {
  for (i32 lz = 0; lz < 16; lz++)
    for (i32 lx = 0; lx < 16; lx++) {
      i32 wx = x0 + lx, wz = z0 + lz;
      f64 d = vx_isle_dist(T, s, wx, wz);
      if (d >= 1.1) continue;
      b32 lip = d >= 0.97;
      i32 top = lip ? s->y - 1 : s->y;
      f64 under = vx_noise2(&T->n[VX_NOISE_DETAIL], wx * 0.12, wz * 0.12 + 400.0);
      f64 dd = m_min(d / 1.1, 1.0);
      i32 bottom = s->y - 1 - (i32)m_round(m_sqrt(1.0 - dd * dd) * s->r * (0.62 + under * 0.08));
      for (i32 y = bottom; y <= top; y++) {
        u8 id = B_CLOUD;
        if (!lip && y >= top - 2) id = y == top ? B_GRASS : B_DIRT;
        blocks[VX_IDX(lx, y, lz)] = id;
      }
      // a dense meadow on top
      if (!lip && top + 1 < VX_HEIGHT) {
        f64 r = vx_hash2(wx, wz, T->seed ^ 0x51c7u);
        if (r < 0.04) blocks[VX_IDX(lx, top + 1, lz)] = B_FLOWERS;
        else if (r < 0.7) blocks[VX_IDX(lx, top + 1, lz)] = B_GRASS_PLANT;
      }
    }
  f64 ta = (f64)((s->h >> 4) & 255u) / 256.0 * M_PI * 2.0;
  VxRng rnd = vx_rng(s->h ^ 0x7ee5u);
  for (i32 t = 0; t < 2; t++) {
    f64 a = ta + t * 2.3;
    i32 tx = (i32)m_floor(s->x + m_cos(a) * s->r * 0.6), tz = (i32)m_floor(s->z + m_sin(a) * s->r * 0.6);
    f64 d = vx_isle_dist(T, s, tx, tz);
    if (d >= 0.9) continue;
    i32 ty = s->y + 1;
    if (t == 0) vx_tree_sky(blocks, x0, z0, tx, ty, tz, 14 + (i32)(vx_rng_next(&rnd) * 4), 3.4, &rnd);
    else vx_tree_sky(blocks, x0, z0, tx, ty, tz, 5 + (i32)(vx_rng_next(&rnd) * 2), 1.7, &rnd);
  }
  vx_stamp_sky_hut(blocks, x0, z0, (i32)m_round(s->x), s->y, (i32)m_round(s->z));
}

hz_internal void vx_place_isles(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  const i32 reach = (i32)(VX_ISLE_MAX_R * 1.25) + 6;
  for (i32 gz = vx_floor_div(z0 - reach, VX_ISLE_CELL); gz <= vx_floor_div(z0 + 15 + reach, VX_ISLE_CELL); gz++)
    for (i32 gx = vx_floor_div(x0 - reach, VX_ISLE_CELL); gx <= vx_floor_div(x0 + 15 + reach, VX_ISLE_CELL); gx++) {
      VxIsle s = vx_isle_site(T, gx, gz);
      if (!s.ok || m_abs(s.x - (x0 + 8)) > s.r * 1.25 + 14 || m_abs(s.z - (z0 + 8)) > s.r * 1.25 + 14) continue;
      vx_stamp_isle(T, blocks, x0, z0, &s);
    }
}

// the house beside spawn: plank walls on a brick footing, log posts, windows, two doorways, a stepped brick roof
hz_internal void vx_place_spawn_hut(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  if (x0 > VX_HUT_X + VX_HUT_W + 2 || x0 + 15 < VX_HUT_X - 3 || z0 > VX_HUT_Z + VX_HUT_D + 2 || z0 + 15 < VX_HUT_Z - 3)
    return;
  i32 fy = vx_sample_column(T, VX_HUT_X + VX_HUT_W / 2, VX_HUT_Z + VX_HUT_D / 2).height + 1;
  for (i32 dz = -2; dz < VX_HUT_D + 2; dz++)
    for (i32 dx = -2; dx < VX_HUT_W + 2; dx++) {
      i32 x = VX_HUT_X + dx, z = VX_HUT_Z + dz;
      b32 inside = dx >= 0 && dx < VX_HUT_W && dz >= 0 && dz < VX_HUT_D;
      b32 ex = dx == 0 || dx == VX_HUT_W - 1, ez = dz == 0 || dz == VX_HUT_D - 1;
      // level ground around it, the footing below it
      for (i32 y = fy - 6; y < fy; y++) vx_set_local(blocks, x0, z0, x, y, z, y == fy - 1 && !inside ? B_GRASS : B_DIRT);
      for (i32 y = fy; y < fy + 14; y++) vx_set_local(blocks, x0, z0, x, y, z, B_AIR);
      if (!inside) continue;
      vx_set_local(blocks, x0, z0, x, fy - 1, z, B_GRAY_BRICK);
      for (i32 dy = 0; dy < 5; dy++) {
        u8 id = B_AIR;
        if (ex || ez) id = (ex && ez) ? B_LOG : dy == 0 ? B_GRAY_BRICK : B_PLANKS;
        if (ez && !ex && dy >= 2 && dy <= 3 && (dx == 2 || dx == 3 || dx == 7 || dx == 8)) id = B_GLASS;
        if (ex && !ez && dz >= 3 && dz <= 5 && dy <= 2) id = B_AIR;
        vx_set_local(blocks, x0, z0, x, fy + dy, z, id);
      }
      // the roof steps in toward the ridge running along x
      i32 edge = MIN(dz, VX_HUT_D - 1 - dz);
      for (i32 k = 0; k <= edge; k++) vx_set_local(blocks, x0, z0, x, fy + 5 + k, z, k == edge ? B_RED_BRICK : B_PLANKS);
    }
  // the eaves overhang by one
  for (i32 dx = -1; dx <= VX_HUT_W; dx++) {
    vx_set_local(blocks, x0, z0, VX_HUT_X + dx, fy + 4, VX_HUT_Z - 1, B_RED_BRICK);
    vx_set_local(blocks, x0, z0, VX_HUT_X + dx, fy + 4, VX_HUT_Z + VX_HUT_D, B_RED_BRICK);
  }
  vx_set_local(blocks, x0, z0, VX_HUT_X + 2, fy, VX_HUT_Z + 1, B_CHEST);
  vx_set_local(blocks, x0, z0, VX_HUT_X + VX_HUT_W - 2, fy, VX_HUT_Z + 1, B_TORCH);
  vx_set_local(blocks, x0, z0, VX_HUT_X - 1, fy, VX_HUT_Z + 2, B_TORCH);
  vx_set_local(blocks, x0, z0, VX_HUT_X - 1, fy, VX_HUT_Z + 6, B_TORCH);
  vx_set_local(blocks, x0, z0, VX_HUT_X + VX_HUT_W, fy, VX_HUT_Z + 2, B_TORCH);
  vx_set_local(blocks, x0, z0, VX_HUT_X + VX_HUT_W, fy, VX_HUT_Z + 6, B_TORCH);
}

// pots on cave floors, clustered, with a rare chest; not inside the glowing mushroom pockets
hz_internal void vx_place_pots(const VxTerrain *T, VxGenScratch *S, u8 *blocks, i32 x0, i32 z0) {
  for (i32 lz = 0; lz < 16; lz++)
    for (i32 lx = 0; lx < 16; lx++) {
      i32 wx = x0 + lx, wz = z0 + lz;
      i32 top = S->h[(lz + M) * SPAN + (lx + M)] - 6;
      f64 cluster = vx_noise2(&T->n[VX_NOISE_PATCH], wx * 0.06 + 700.0, wz * 0.06);
      for (i32 y = VX_CAVERN_Y + 3; y < top && y < VX_HEIGHT - 2; y++) {
        i32 i = VX_IDX(lx, y, lz);
        if (blocks[i] != B_AIR || blocks[i + 256] != B_AIR) continue;
        u8 below = blocks[i - 256];
        if (!vx_occludes[below] || below == B_MUSHROOM_GRASS || (below == B_MUD && vx_mushroom_zone(T, wx, y, wz))) continue;
        f64 r = vx_hash3i(wx, y, wz, T->seed ^ 0x9077u) / 4294967296.0;
        f64 p = cluster > 0.35 ? 0.03 : 0.004;
        if (r < p) blocks[i] = B_POT;
        else if (r > 0.99975 && y < VX_UNDERGROUND_Y - 4) blocks[i] = y < VX_CAVERN_Y + 36 ? B_GOLD_CHEST : B_CHEST;
      }
    }
}

// trilinear lookup into a 4-block cave grid laid out (gy, gz, gx)
force_inline f64 vx_grid(const f32 *g, i32 lx, i32 y, i32 lz) {
  const i32 GX = VX_CAVE_GX;
  i32 gx0 = lx >> 2, gz0 = lz >> 2, gy0 = y >> 2;
  f64 fx = (lx & 3) * 0.25, fz = (lz & 3) * 0.25, fy = (y & 3) * 0.25;
  i32 k000 = (gy0 * GX + gz0) * GX + gx0;
  i32 k100 = k000 + 1, k010 = k000 + GX, k110 = k010 + 1;
  i32 k001 = k000 + GX * GX, k101 = k001 + 1, k011 = k001 + GX, k111 = k011 + 1;
  return vx_lerp(vx_lerp(vx_lerp(g[k000], g[k100], fx), vx_lerp(g[k010], g[k110], fx), fz),
                 vx_lerp(vx_lerp(g[k001], g[k101], fx), vx_lerp(g[k011], g[k111], fx), fz), fy);
}

hz_internal void vx_fill_grids(const VxTerrain *T, VxGenScratch *S, i32 x0, i32 z0) {
  const i32 GX = VX_CAVE_GX;
  for (i32 gy = 0; gy < VX_CAVE_GY; gy++) {
    f64 y = gy * 4;
    for (i32 gz = 0; gz < GX; gz++) {
      for (i32 gx = 0; gx < GX; gx++) {
        f64 x = x0 + gx * 4, z = z0 + gz * 4;
        i32 k = (gy * GX + gz) * GX + gx;
        f64 a = vx_noise3(&T->n[VX_NOISE_TUNNEL_A], x * 0.014, y * 0.022, z * 0.014);
        f64 b = vx_noise3(&T->n[VX_NOISE_TUNNEL_B], x * 0.014, y * 0.022, z * 0.014);
        S->tun[k] = (f32)(a * a + b * b);
        S->cav[k] = (f32)(vx_noise3(&T->n[VX_NOISE_CAVERN], x * 0.012, y * 0.02, z * 0.012) +
                          vx_noise3(&T->n[VX_NOISE_CAVERN_FINE], x * 0.035, y * 0.05, z * 0.035) * 0.25);
        S->soil[k] = (f32)vx_noise3(&T->n[VX_NOISE_DIRT], x * 0.03, y * 0.04, z * 0.03);
      }
    }
  }
}

hz_internal void vx_vein(u8 *blocks, VxRng *rnd, u8 id, i32 tries, i32 size, i32 y_min, i32 y_max, u8 r0, u8 r1) {
  for (i32 t = 0; t < tries; t++) {
    i32 x = (i32)(vx_rng_next(rnd) * 16);
    i32 y = y_min + (i32)(vx_rng_next(rnd) * (y_max - y_min));
    i32 z = (i32)(vx_rng_next(rnd) * 16);
    i32 n = 1 + (i32)(vx_rng_next(rnd) * size);
    for (i32 k = 0; k < n; k++) {
      if (x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < VX_HEIGHT) {
        i32 i = VX_IDX(x, y, z);
        if (blocks[i] == r0 || blocks[i] == r1) blocks[i] = id;
      }
      i32 d = (i32)(vx_rng_next(rnd) * 6);
      if (d == 0) x++;
      else if (d == 1) x--;
      else if (d == 2) y++;
      else if (d == 3) y--;
      else if (d == 4) z++;
      else z--;
    }
  }
}

hz_internal void vx_place_ores(const VxTerrain *T, u8 *blocks, i32 cx, i32 cz) {
  VxRng rnd = vx_rng(vx_hash2i(cx, cz, T->seed ^ 0x1234567u));
  vx_vein(blocks, &rnd, B_CLAY, 3, 24, VX_UNDERGROUND_Y, VX_SURFACE_Y + 10, B_DIRT, B_DIRT);
  vx_vein(blocks, &rnd, B_COPPER_ORE, 8, 10, VX_UNDERGROUND_Y - 20, VX_SURFACE_Y + 4, B_STONE, B_DIRT);
  vx_vein(blocks, &rnd, B_TIN_ORE, 6, 10, VX_UNDERGROUND_Y - 20, VX_SURFACE_Y + 4, B_STONE, B_DIRT);
  vx_vein(blocks, &rnd, B_IRON_ORE, 8, 10, VX_CAVERN_Y + 30, VX_UNDERGROUND_Y + 10, B_STONE, B_STONE);
  vx_vein(blocks, &rnd, B_SILVER_ORE, 6, 9, VX_CAVERN_Y + 10, VX_UNDERGROUND_Y - 10, B_STONE, B_STONE);
  vx_vein(blocks, &rnd, B_GOLD_ORE, 5, 9, VX_CAVERN_Y + 2, VX_CAVERN_Y + 45, B_STONE, B_STONE);
  vx_vein(blocks, &rnd, B_DEMONITE_ORE, 4, 8, VX_CAVERN_Y + 2, VX_SURFACE_Y, B_EBONSTONE, B_EBONSTONE);
  vx_vein(blocks, &rnd, B_HELLSTONE, 14, 14, 3, VX_CAVERN_Y - 2, B_ASH, B_ASH);
  static_assert(B_SAPPHIRE == B_AMETHYST + 1 && B_EMERALD == B_AMETHYST + 3, "gems are consecutive");
  for (i32 g = 0; g < 3; g++) {
    u8 gem = (u8)(B_AMETHYST + (i32)(vx_rng_next(&rnd) * 4));
    vx_vein(blocks, &rnd, gem, 1, 4, VX_CAVERN_Y + 4, VX_UNDERGROUND_Y - 8, B_STONE, B_STONE);
  }
}

hz_internal void vx_put(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, u8 id, b32 force) {
  i32 lx = x - x0, lz = z - z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 1 || y >= VX_HEIGHT) return;
  i32 i = VX_IDX(lx, y, lz);
  u8 cur = blocks[i];
  if (cur == B_AIR || (force && (vx_replaceable[cur] || vx_layer[cur] == VX_LAYER_CUTOUT || cur == B_DIRT || cur == B_GRASS)))
    blocks[i] = id;
}

// tall trunk with a round crown and a couple of side tufts (Terraria's forest tree in 3D)
hz_internal void vx_tree_forest(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd, u8 log, u8 leaves) {
  i32 height = 8 + (i32)(vx_rng_next(rnd) * 7);
  i32 top = y + height;
  f64 cr = 2.6 + vx_rng_next(rnd) * 1.2;
  i32 ri = (i32)m_ceil(cr);
  for (i32 dy = -ri; dy <= ri; dy++)
    for (i32 dz = -ri; dz <= ri; dz++)
      for (i32 dx = -ri; dx <= ri; dx++) {
        f64 d2 = dx * dx + dy * dy * 1.25 + dz * dz;
        if (d2 > cr * cr + (vx_rng_next(rnd) - 0.5) * 2.0) continue;
        vx_put(blocks, x0, z0, x + dx, top + dy, z + dz, leaves, false);
      }
  i32 tufts = (i32)(vx_rng_next(rnd) * 3);
  for (i32 t = 0; t < tufts; t++) {
    i32 ty = y + 3 + (i32)(vx_rng_next(rnd) * (height - 6));
    i32 side = (i32)(vx_rng_next(rnd) * 4);
    i32 dx = side == 0 ? 1 : side == 1 ? -1 : 0, dz = side == 2 ? 1 : side == 3 ? -1 : 0;
    vx_put(blocks, x0, z0, x + dx, ty, z + dz, log, false);
    static_assert(VX_GEN_MARGIN >= 4, "tree crowns reach 4 blocks from the trunk");
    for (i32 oy = 0; oy < 2; oy++)
      for (i32 oz = -1; oz <= 1; oz++)
        for (i32 ox = -1; ox <= 1; ox++)
          if (oy == 0 || (ox == 0 && oz == 0)) vx_put(blocks, x0, z0, x + dx * 2 + ox, ty + oy, z + dz * 2 + oz, leaves, false);
  }
  for (i32 yy = y; yy < top; yy++) vx_put(blocks, x0, z0, x, yy, z, log, true);
  // root flare
  for (i32 d = 0; d < 4; d++) {
    if (vx_rng_next(rnd) < 0.5) continue;
    vx_put(blocks, x0, z0, x + (d == 0) - (d == 1), y, z + (d == 2) - (d == 3), log, false);
  }
}

// pearlwood: a slim dark trunk under two or three round clumps, each gold or violet, as the Hallow's trees
hz_internal void vx_tree_hallow(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd) {
  i32 height = 7 + (i32)(vx_rng_next(rnd) * 6);
  i32 top = y + height;
  i32 clumps = 2 + (vx_rng_next(rnd) < 0.5);
  for (i32 c = 0; c < clumps; c++) {
    f64 cr = c == 0 ? 2.2 + vx_rng_next(rnd) * 0.8 : 1.4 + vx_rng_next(rnd) * 0.7;
    i32 cx = x + (c == 0 ? 0 : (i32)m_round((vx_rng_next(rnd) - 0.5) * 4.0));
    i32 cz = z + (c == 0 ? 0 : (i32)m_round((vx_rng_next(rnd) - 0.5) * 4.0));
    i32 cy = c == 0 ? top : top - 2 - (i32)(vx_rng_next(rnd) * 3);
    u8 leaves = vx_rng_next(rnd) < 0.55 ? B_GOLD_LEAVES : B_HALLOW_LEAVES;
    i32 ri = (i32)m_ceil(cr);
    for (i32 dy = -ri; dy <= ri; dy++)
      for (i32 dz = -ri; dz <= ri; dz++)
        for (i32 dx = -ri; dx <= ri; dx++)
          if (dx * dx + dy * dy * 1.2 + dz * dz <= cr * cr + (vx_rng_next(rnd) - 0.5) * 1.5)
            vx_put(blocks, x0, z0, cx + dx, cy + dy, cz + dz, leaves, false);
    // a branch from the trunk to each side clump
    if (c > 0)
      for (i32 k = 0; k <= 2; k++)
        vx_put(blocks, x0, z0, x + (cx - x) * k / 2, cy - 1 - (2 - k) / 2, z + (cz - z) * k / 2, B_PEARL_LOG, false);
  }
  for (i32 yy = y; yy < top; yy++) vx_put(blocks, x0, z0, x, yy, z, B_PEARL_LOG, true);
}

// boreal pine: a slim spire of stacked cones that shrink upward over a short bare trunk
hz_internal void vx_tree_boreal(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd) {
  i32 height = 12 + (i32)(vx_rng_next(rnd) * 7);
  i32 top = y + height;
  // cones stack down from the tip so no bare trunk shows above them
  const i32 tier_h = 3;
  i32 tiers = (height - 3) / (tier_h - 1);
  i32 ty = top - 1 - (tiers - 1) * (tier_h - 1);
  for (i32 t = 0; t < tiers; t++) {
    f64 base = 2.5 - t * (1.9 / tiers);
    for (i32 dy = 0; dy < tier_h; dy++) {
      f64 r = base * (1.0 - dy / (f64)(tier_h + 0.5));
      i32 ri = (i32)m_ceil(r);
      for (i32 dz = -ri; dz <= ri; dz++)
        for (i32 dx = -ri; dx <= ri; dx++)
          if (dx * dx + dz * dz <= r * r + 0.3) vx_put(blocks, x0, z0, x + dx, ty + dy, z + dz, B_BOREAL_LEAVES, false);
    }
    ty += tier_h - 1;
  }
  vx_put(blocks, x0, z0, x, top, z, B_BOREAL_LEAVES, false);
  vx_put(blocks, x0, z0, x, top + 1, z, B_BOREAL_LEAVES, false);
  for (i32 yy = y; yy < top; yy++) vx_put(blocks, x0, z0, x, yy, z, B_BOREAL_LOG, true);
}

// rich mahogany: a tall bare trunk under broad flat canopy tiers, with vines hanging from their rims
hz_internal void vx_mahogany_disc(u8 *blocks, i32 x0, i32 z0, f64 cx, i32 y, f64 cz, f64 r, VxRng *rnd) {
  i32 ri = (i32)m_ceil(r);
  for (i32 dy = 0; dy < 2; dy++) {
    f64 rr = dy ? r - 1.4 : r;
    for (i32 dz = -ri; dz <= ri; dz++)
      for (i32 dx = -ri; dx <= ri; dx++) {
        f64 ex = (f64)dx, ez = (f64)dz;
        if (ex * ex + ez * ez > rr * rr + (vx_rng_next(rnd) - 0.5) * 3.0) continue;
        vx_put(blocks, x0, z0, (i32)m_floor(cx) + dx, y + dy, (i32)m_floor(cz) + dz, B_JUNGLE_LEAVES, false);
      }
  }
  for (i32 v = 0; v < 5; v++) {
    f64 a = vx_rng_next(rnd) * M_PI * 2.0;
    f64 vr = r * (0.6 + vx_rng_next(rnd) * 0.35);
    i32 vx = (i32)m_floor(cx + m_cos(a) * vr), vz = (i32)m_floor(cz + m_sin(a) * vr);
    i32 len = 2 + (i32)(vx_rng_next(rnd) * 5);
    for (i32 k = 1; k <= len; k++) vx_put(blocks, x0, z0, vx, y - k, vz, B_VINE, false);
  }
}

hz_internal void vx_tree_mahogany(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd) {
  b32 big = vx_rng_next(rnd) < 0.4;
  i32 height = (big ? 20 : 14) + (i32)(vx_rng_next(rnd) * 9);
  i32 top = y + height;
  i32 w = big ? 2 : 1;
  f64 c = big ? 1.0 : 0.5;
  for (i32 yy = y - 1; yy < top; yy++)
    for (i32 o = 0; o < w * w; o++) vx_put(blocks, x0, z0, x + o % w, yy, z + o / w, B_MAHOGANY_LOG, true);
  vx_mahogany_disc(blocks, x0, z0, x + c, top, z + c, (big ? 5.5 : 4.0) + vx_rng_next(rnd) * 1.5, rnd);
  // lower tiers on short branches, offset to a side
  i32 tiers = (i32)(vx_rng_next(rnd) * (big ? 3 : 2));
  for (i32 t = 0; t < tiers; t++) {
    f64 a = vx_rng_next(rnd) * M_PI * 2.0;
    i32 ty = top - 4 - (i32)(vx_rng_next(rnd) * (height * 0.4));
    i32 reach = 2 + (i32)(vx_rng_next(rnd) * 2);
    i32 bx = 0, bz = 0;
    for (i32 k = 1; k <= reach; k++) {
      bx = (i32)m_round(m_cos(a) * k), bz = (i32)m_round(m_sin(a) * k);
      vx_put(blocks, x0, z0, x + bx, ty - 1 + k / 2, z + bz, B_MAHOGANY_LOG, false);
    }
    vx_mahogany_disc(blocks, x0, z0, x + bx + 0.5, ty + reach / 2, z + bz + 0.5, 2.5 + vx_rng_next(rnd) * 1.2, rnd);
  }
  if (big)
    for (i32 d = 0; d < 4; d++) {
      i32 dx = d == 0 ? -1 : d == 1 ? 2 : (i32)(vx_rng_next(rnd) * 2);
      i32 dz = d == 2 ? -1 : d == 3 ? 2 : (i32)(vx_rng_next(rnd) * 2);
      vx_put(blocks, x0, z0, x + dx, y, z + dz, B_MAHOGANY_LOG, false);
    }
}

// palm: a trunk that leans and curves toward the sea, a star of drooping fronds and a coconut cluster
hz_internal void vx_tree_palm(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd) {
  i32 height = 7 + (i32)(vx_rng_next(rnd) * 5);
  f64 a = vx_rng_next(rnd) * M_PI * 2.0;
  f64 lx = m_cos(a), lz = m_sin(a);
  i32 tx = x, tz = z;
  for (i32 k = 0; k < height; k++) {
    f64 lean = (k / (f64)height) * (k / (f64)height) * 1.4;
    tx = x + (i32)m_round(lx * lean);
    tz = z + (i32)m_round(lz * lean);
    vx_put(blocks, x0, z0, tx, y + k, tz, B_PALM_LOG, true);
  }
  i32 top = y + height;
  vx_put(blocks, x0, z0, tx, top, tz, B_PALM_LEAVES, false);
  for (i32 f = 0; f < 8; f++) {
    i32 dx = (f == 0 || f == 4 || f == 5) ? 1 : (f == 1 || f == 6 || f == 7) ? -1 : 0;
    i32 dz = (f == 2 || f == 4 || f == 6) ? 1 : (f == 3 || f == 5 || f == 7) ? -1 : 0;
    i32 len = f < 4 ? 3 : 2;
    for (i32 k = 1; k <= len; k++) {
      i32 dy = k <= 1 ? 0 : -(k - 1) / 2 - (k == len ? 1 : 0);
      vx_put(blocks, x0, z0, tx + dx * k, top + dy, tz + dz * k, B_PALM_LEAVES, false);
    }
  }
}

// saguaro-style cactus: a column with one or two arms that turn upward
hz_internal void vx_cactus(u8 *blocks, i32 x0, i32 z0, i32 x, i32 y, i32 z, VxRng *rnd) {
  i32 height = 2 + (i32)(vx_rng_next(rnd) * 3);
  for (i32 k = 0; k < height; k++) vx_put(blocks, x0, z0, x, y + k, z, B_CACTUS, false);
  if (height < 3) return;
  i32 arms = 1 + (i32)(vx_rng_next(rnd) * 2);
  for (i32 a = 0; a < arms; a++) {
    i32 side = (i32)(vx_rng_next(rnd) * 4);
    i32 dx = side == 0 ? 1 : side == 1 ? -1 : 0, dz = side == 2 ? 1 : side == 3 ? -1 : 0;
    i32 ay = y + 1 + (i32)(vx_rng_next(rnd) * (height - 2));
    vx_put(blocks, x0, z0, x + dx, ay, z + dz, B_CACTUS, false);
    i32 up = 1 + (i32)(vx_rng_next(rnd) * 2);
    for (i32 k = 1; k <= up; k++) vx_put(blocks, x0, z0, x + dx, ay + k, z + dz, B_CACTUS, false);
  }
}

// just outside an oasis pit, where its palms grow
hz_internal b32 vx_oasis_ring(const VxTerrain *T, i32 x, i32 z) {
  for (u32 i = 0; i < T->landmark_count; i++) {
    const VxLandmark *l = &T->landmarks[i];
    if (l->kind != VX_LM_OASIS) continue;
    f64 dx = x + 0.5 - l->x, dz = z + 0.5 - l->z, d2 = (dx * dx + dz * dz) / (l->r * l->r);
    if (d2 > 1.25 * 1.25 && d2 < 2.0 * 2.0) return true;
  }
  return false;
}

hz_internal void vx_place_trees(const VxTerrain *T, VxGenScratch *S, u8 *blocks, i32 x0, i32 z0) {
  // a canopy reaches up to R blocks from its trunk; trunks past the scratch margin sample their own column
  const i32 CELL = 7, R = 8;
  i32 gx0 = vx_floor_div(x0 - R, CELL), gx1 = vx_floor_div(x0 + 15 + R, CELL);
  i32 gz0 = vx_floor_div(z0 - R, CELL), gz1 = vx_floor_div(z0 + 15 + R, CELL);
  for (i32 gz = gz0; gz <= gz1; gz++) {
    for (i32 gx = gx0; gx <= gx1; gx++) {
      u32 hsh = vx_hash2i(gx, gz, T->seed ^ 0x77777u);
      i32 tx = gx * CELL + (i32)(hsh % 5), tz = gz * CELL + (i32)((hsh >> 4) % 5);
      if (tx < x0 - R || tx > x0 + 15 + R || tz < z0 - R || tz > z0 + 15 + R) continue;
      if (tx * tx + tz * tz < 8 * 8) continue;
      if (tx > VX_HUT_X - 6 && tx < VX_HUT_X + VX_HUT_W + 5 && tz > VX_HUT_Z - 6 && tz < VX_HUT_Z + VX_HUT_D + 5) continue;
      if (vx_town_clear(tx, tz)) continue;
      // the town stands in an open meadow: trees thin out toward it
      f64 open = vx_town_weight(T, tx, tz);
      i32 lx = tx - x0, lz = tz - z0;
      VxColumnSample c;
      if (lx >= -M && lx < 16 + M && lz >= -M && lz < 16 + M) {
        i32 k = (lz + M) * SPAN + (lx + M);
        c = (VxColumnSample){.height = S->h[k], .biome = S->biome[k], .landmark = S->landmark[k], .coast = S->coast[k]};
      } else {
        c = vx_sample_column(T, tx, tz);
      }
      i32 h = c.height;
      u8 biome = c.biome;
      if (h <= VX_SEA_LEVEL || vx_cave_mouth(T, tx, tz) || vx_pond_dist_at(T, tx, tz, NULL) < 1.5) continue;
      if (c.landmark != VX_LM_NONE && c.landmark != VX_LM_BUTTE && c.landmark != VX_LM_PILLAR) continue;
      f64 roll = (f64)((hsh >> 12) & 0xffff) / 65536.0;
      if (open > 0.0 && roll < open * 0.35) continue;
      f64 patch = vx_smoothstep(-0.2, 0.4, vx_noise2(&T->n[VX_NOISE_PATCH], tx * 0.02, tz * 0.02));
      VxRng rnd = vx_rng(hsh ^ 0x9e3779b9u);
      if (biome == VX_BIOME_OCEAN) {
        // palms line the beach just above the waterline
        if (h > VX_SEA_LEVEL + 4 || roll >= 0.35) continue;
        vx_tree_palm(blocks, x0, z0, tx, h + 1, tz, &rnd);
        continue;
      }
      if (c.coast > 0.05f) continue;
      switch (biome) {
      case VX_BIOME_FOREST:
      case VX_BIOME_CORRUPTION:
        if (roll < vx_lerp(0.35, 0.8, patch) * (biome == VX_BIOME_CORRUPTION ? 0.4 : 1.0))
          vx_tree_forest(blocks, x0, z0, tx, h + 1, tz, &rnd, biome == VX_BIOME_CORRUPTION ? B_EBON_LOG : B_LOG,
                         biome == VX_BIOME_CORRUPTION ? B_CORRUPT_LEAVES : B_LEAVES);
        break;
      case VX_BIOME_HALLOW:
        if (roll < vx_lerp(0.35, 0.75, patch)) vx_tree_hallow(blocks, x0, z0, tx, h + 1, tz, &rnd);
        break;
      case VX_BIOME_SNOW:
        if (roll < vx_lerp(0.1, 0.4, patch)) vx_tree_boreal(blocks, x0, z0, tx, h + 1, tz, &rnd);
        break;
      case VX_BIOME_JUNGLE:
        if (roll < 0.55) vx_tree_mahogany(blocks, x0, z0, tx, h + 1, tz, &rnd);
        break;
      case VX_BIOME_DESERT:
        // palms ring an oasis
        if (vx_oasis_ring(T, tx, tz)) {
          if (roll < 0.7) vx_tree_palm(blocks, x0, z0, tx, h + 1, tz, &rnd);
        } else if (roll < 0.6) {
          vx_cactus(blocks, x0, z0, tx, h + 1, tz, &rnd);
        }
        break;
      default: break;
      }
    }
  }
}

// the desert's scatter of small cacti between the saguaros, one or two blocks tall
hz_internal void vx_place_desert_cacti(const VxTerrain *T, VxGenScratch *S, u8 *blocks, i32 x0, i32 z0) {
  for (i32 lz = 0; lz < 16; lz++) {
    for (i32 lx = 0; lx < 16; lx++) {
      i32 k = (lz + M) * SPAN + (lx + M);
      i32 h = S->h[k];
      if (S->biome[k] != VX_BIOME_DESERT || S->landmark[k] != VX_LM_NONE || S->coast[k] > 0.05f) continue;
      if (h <= VX_SEA_LEVEL || h + 3 >= VX_HEIGHT || blocks[VX_IDX(lx, h, lz)] != B_SAND) continue;
      i32 wx = x0 + lx, wz = z0 + lz;
      f64 r = vx_hash2(wx, wz, T->seed ^ 0xca7c5u);
      if (r >= 0.03 || vx_town_clear(wx, wz) || vx_oasis_ring(T, wx, wz)) continue;
      i32 tall = r < 0.012 ? 2 : 1;
      if (blocks[VX_IDX(lx, h + 1, lz)] != B_AIR || blocks[VX_IDX(lx, h + tall, lz)] != B_AIR) continue;
      for (i32 y = h + 1; y <= h + tall; y++) blocks[VX_IDX(lx, y, lz)] = B_CACTUS;
    }
  }
}

hz_internal void vx_place_plants(const VxTerrain *T, VxGenScratch *S, u8 *blocks, i32 x0, i32 z0) {
  for (i32 lz = 0; lz < 16; lz++) {
    for (i32 lx = 0; lx < 16; lx++) {
      i32 h = S->h[(lz + M) * SPAN + (lx + M)];
      if (h + 2 >= VX_HEIGHT) continue;
      u8 ground = blocks[VX_IDX(lx, h, lz)];
      i32 above = VX_IDX(lx, h + 1, lz);
      if (blocks[above] != B_AIR) continue;
      i32 wx = x0 + lx, wz = z0 + lz;
      f64 r = vx_hash2(wx, wz, T->seed ^ 0xabcdefu);
      if (ground == B_GRASS) {
        f64 patch = vx_noise2(&T->n[VX_NOISE_PATCH], wx * 0.04 + 100, wz * 0.04);
        // sparse tufts as in the reference (about one block in five), flowers mostly in patches
        if (r < 0.012 + (patch > 0.4 ? 0.06 : 0.0)) blocks[above] = B_FLOWERS;
        else if (r < 0.2) blocks[above] = B_GRASS_PLANT;
        else if (r > 0.996) blocks[above] = B_MUSHROOM;
      } else if (ground == B_JUNGLE_GRASS) {
        if (r < 0.55) blocks[above] = B_JUNGLE_PLANT;
      } else if (ground == B_CORRUPT_GRASS) {
        if (r < 0.3) blocks[above] = B_CORRUPT_PLANT;
      } else if (ground == B_HALLOW_GRASS) {
        if (r < 0.22) blocks[above] = B_HALLOW_PLANT;
      }
    }
  }
}

#define VX_SHROOM_CELL 14

// giant glowing mushrooms in the pockets' hollows: the site sets the cap's height, each column runs its stem down to the floor
hz_internal void vx_place_giant_mushrooms(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  const i32 reach = 11;
  for (i32 gz = vx_floor_div(z0 - reach, VX_SHROOM_CELL); gz <= vx_floor_div(z0 + 15 + reach, VX_SHROOM_CELL); gz++)
    for (i32 gx = vx_floor_div(x0 - reach, VX_SHROOM_CELL); gx <= vx_floor_div(x0 + 15 + reach, VX_SHROOM_CELL); gx++) {
      u32 h = vx_hash2i(gx, gz, T->seed ^ 0x5407u);
      f64 sx = gx * VX_SHROOM_CELL + 3.0 + (h & 255u) / 255.0 * (VX_SHROOM_CELL - 6.0);
      f64 sz = gz * VX_SHROOM_CELL + 3.0 + ((h >> 8) & 255u) / 255.0 * (VX_SHROOM_CELL - 6.0);
      i32 cap_y = -1;
      for (u32 i = 0; i < VX_MUSHROOM_POCKETS && cap_y < 0; i++) {
        v4 m = T->mushroom[i];
        f64 q = m_sqrt((sx - m.x) * (sx - m.x) + (sz - m.z) * (sz - m.z)) / m.w;
        if (q < 0.7) cap_y = (i32)m.y + (i32)((h >> 16) % 15u) - (i32)(q * 6.0);
      }
      if (cap_y < 0 || sx + reach < x0 || sx - reach > x0 + 16 || sz + reach < z0 || sz - reach > z0 + 16) continue;
      f64 rc = 4.0 + ((h >> 20) & 15u) / 15.0 * 3.5, rs = 1.4 + ((h >> 24) & 15u) / 15.0 * 1.0;
      for (i32 lz = 0; lz < 16; lz++)
        for (i32 lx = 0; lx < 16; lx++) {
          f64 dx = x0 + lx + 0.5 - sx, dz = z0 + lz + 0.5 - sz, d = m_sqrt(dx * dx + dz * dz);
          if (d > rc) continue;
          f64 dd = d / rc;
          // the cap domes up two blocks at its middle and droops a block at its rim
          i32 top = cap_y + (dd < 0.35 ? 2 : dd < 0.7 ? 1 : 0), bot = dd > 0.82 ? cap_y - 1 : cap_y;
          for (i32 y = bot; y <= top; y++)
            if (blocks[VX_IDX(lx, y, lz)] == B_AIR) blocks[VX_IDX(lx, y, lz)] = B_GLOW_CAP;
          // the stem flares toward its foot
          if (d <= rs * 1.4) {
            for (i32 y = bot - 1; y > VX_CAVERN_Y && bot - y < 48; y--) {
              u8 *b = &blocks[VX_IDX(lx, y, lz)];
              if (*b != B_AIR && *b != B_GLOW_MUSHROOM && *b != B_POT) break;
              if (d <= rs * m_min(1.4, 1.0 + (bot - y) * 0.02)) *b = B_GLOW_STEM;
            }
          }
          if (d > rs && dd < 0.75 && (vx_hash2i(x0 + lx, z0 + lz, h) & 3u) == 0 && blocks[VX_IDX(lx, bot - 1, lz)] == B_AIR) {
            // gills under the cap glow back down on the stem
            blocks[VX_IDX(lx, bot - 1, lz)] = B_GLOW_MUSHROOM;
          }
        }
    }
}

// cavern floors: mushroom grass and its plants in the glowing zone, life crystals elsewhere
hz_internal void vx_decorate_caverns(const VxTerrain *T, u8 *blocks, i32 x0, i32 z0) {
  for (i32 lz = 0; lz < 16; lz++) {
    for (i32 lx = 0; lx < 16; lx++) {
      i32 wx = x0 + lx, wz = z0 + lz;
      for (i32 y = VX_CAVERN_Y + 4; y < VX_UNDERGROUND_Y; y++) {
        i32 i = VX_IDX(lx, y, lz);
        if (blocks[i] != B_AIR) continue;
        u8 below = blocks[i - 256];
        b32 mush = vx_mushroom_zone(T, wx, y, wz);
        if (below == B_MUD && mush) {
          blocks[i - 256] = B_MUSHROOM_GRASS;
          f64 r = vx_hash3i(wx, y, wz, T->seed ^ 0x6d757368u) / 4294967296.0;
          if (r < 0.2) blocks[i] = B_GLOW_MUSHROOM;
        } else if (below == B_STONE && !mush) {
          f64 r = vx_hash3i(wx, y, wz, T->seed ^ 0x6c696665u) / 4294967296.0;
          if (r < 0.00035) blocks[i] = B_LIFE_CRYSTAL;
        }
        u8 up = blocks[i + 256];
        if (mush && up == B_MUD) blocks[i + 256] = B_MUSHROOM_GRASS;
      }
    }
  }
}

void vx_gen_state(VxGenStates *s, i32 lx, i32 y, i32 lz, u8 id, u8 state) {
  if (!s || !state) return;
  if (s->count >= VX_GEN_STATES) {
    log_warn("vx_gen_state: more than % state bytes in one chunk", fmt_u32(VX_GEN_STATES));
    return;
  }
  s->items[s->count++] = (VxGenState){.index = (u16)VX_IDX(lx, y, lz), .id = id, .state = state};
}

void vx_generate_chunk(const VxTerrain *T, VxGenScratch *S, i32 cx, i32 cz, u8 *blocks, u8 *biome_out, VxGenStates *states,
                       b32 surface) {
  mem_zero(blocks, VX_CHUNK_VOLUME);
  i32 x0 = cx * VX_CHUNK, z0 = cz * VX_CHUNK;
  for (i32 lz = -M; lz < VX_CHUNK + M; lz++) {
    for (i32 lx = -M; lx < VX_CHUNK + M; lx++) {
      VxColumnSample s = vx_sample_column(T, x0 + lx, z0 + lz);
      i32 k = (lz + M) * SPAN + (lx + M);
      S->h[k] = s.height;
      S->ground[k] = s.ground;
      S->landmark[k] = s.landmark;
      S->water[k] = s.water;
      S->biome[k] = s.biome;
      S->coast[k] = s.coast;
    }
  }
  if (!surface) vx_fill_grids(T, S, x0, z0);
  // the lakes whose basin or dam can reach this chunk
  VxLake *lakes = S->lakes;
  u32 lake_count = 0;
  for (i32 gz = vx_floor_div(z0 - 50, VX_LAKE_CELL); !surface && gz <= vx_floor_div(z0 + 66, VX_LAKE_CELL); gz++)
    for (i32 gx = vx_floor_div(x0 - 50, VX_LAKE_CELL); gx <= vx_floor_div(x0 + 66, VX_LAKE_CELL); gx++) {
      VxLake l = vx_lake_site(T, gx, gz);
      if (l.ok && lake_count < VX_GEN_LAKES && m_abs(l.x - (x0 + 8)) < l.r * 1.3 + 12 && m_abs(l.z - (z0 + 8)) < l.r * 1.3 + 12)
        lakes[lake_count++] = l;
    }

  for (i32 lz = 0; lz < VX_CHUNK; lz++) {
    for (i32 lx = 0; lx < VX_CHUNK; lx++) {
      i32 k = (lz + M) * SPAN + (lx + M);
      i32 h = S->h[k];
      u8 biome = S->biome[k];
      biome_out[lz * 16 + lx] = biome;
      i32 wx = x0 + lx, wz = z0 + lz;
      u32 r = vx_hash2i(wx, wz, T->seed ^ 0x5bd1e995u);
      i32 hell_floor = vx_hell_floor(T, wx, wz);
      i32 hell_ceil = vx_hell_ceil(T, wx, wz);
      i32 ash_top = VX_CAVERN_Y + (i32)(r & 3);
      // the ash floor's top is a mosaic with blots of hellstone
      u8 hell_top = vx_noise2(&T->n[VX_NOISE_PATCH], wx * 0.18 + 500.0, wz * 0.18) > 0.0 ? B_HELLSTONE : B_ASH;
      b32 mouth = vx_cave_mouth(T, wx, wz);

      // the lake over this column, if any: d < 1 is the basin, 1..1.18 its dam
      const VxLake *lake = NULL;
      f64 lake_d = 9.0;
      for (u32 li = 0; li < lake_count; li++) {
        f64 d = vx_lake_dist(T, &lakes[li], wx, wz);
        if (d < lake_d) lake_d = d, lake = &lakes[li];
      }
      if (lake_d > 1.18) lake = NULL;

      u8 top = B_GRASS, sub = B_DIRT, deep = B_STONE, soil = B_DIRT;
      i32 sub_depth = 5 + (i32)((r >> 2) & 3);
      switch (biome) {
      case VX_BIOME_DESERT: top = B_SAND, sub = B_SAND, soil = B_SANDSTONE, sub_depth = 6; break;
      case VX_BIOME_SNOW: top = B_SNOW, sub = B_SNOW, soil = B_ICE, sub_depth = 4; break;
      case VX_BIOME_JUNGLE: top = B_JUNGLE_GRASS, sub = B_MUD, soil = B_MUD; break;
      case VX_BIOME_CORRUPTION: top = B_CORRUPT_GRASS, deep = B_EBONSTONE; break;
      case VX_BIOME_HALLOW:
        // pale pearlsand blots in the cyan grass
        top = vx_noise2(&T->n[VX_NOISE_PATCH], wx * 0.05 + 31.0, wz * 0.05) > 0.42 ? B_PEARLSAND : B_HALLOW_GRASS;
        break;
      case VX_BIOME_OCEAN: top = B_SAND, sub = B_SAND, soil = B_SAND, sub_depth = 6; break;
      default: break;
      }
      if (h < VX_SEA_LEVEL && (top == B_GRASS || top == B_HALLOW_GRASS)) top = B_DIRT;
      // a landmark's rock reaches from under the dunes to its cap
      u8 lm = S->landmark[k];
      // a red rim course under the cap, pitted sandstone walls below
      if (lm == VX_LM_BUTTE || lm == VX_LM_PILLAR) top = B_MESA, sub = B_BUTTE;
      if (lm == VX_LM_PYRAMID) top = B_SANDSTONE, sub = B_SANDSTONE;
      if (lm == VX_LM_TERRACE || lm == VX_LM_SPIRE) sub = B_SLATE;
      if (lm == VX_LM_CREVASSE) top = h < S->ground[k] ? B_PINK_ICE : B_SNOW, sub = B_PINK_ICE;
      if (lm != VX_LM_NONE && lm != VX_LM_OASIS) sub_depth = lm == VX_LM_CREVASSE ? 18 : h - S->ground[k] + 4;
      i32 water_top = S->water[k];

      if (surface) {
        // only what shows from above: the top layers over plain fill
        u8 fill = soil == B_ICE || soil == B_SANDSTONE ? soil : deep;
        for (i32 y = 0; y <= h; y++) {
          u8 b = y == h ? top : y > h - sub_depth ? sub : fill;
          if (lm == VX_LM_PEAK && y > h - sub_depth) b = vx_peak_block(T, wx, y, wz, y == h);
          blocks[VX_IDX(lx, y, lz)] = b;
        }
        for (i32 y = h + 1; y <= water_top; y++) blocks[VX_IDX(lx, y, lz)] = B_WATER;
        continue;
      }
      for (i32 y = 0; y <= h; y++) {
        u8 b;
        if (y <= 2) {
          b = B_ASH;
        } else if (y < hell_ceil) {
          b = y < hell_floor ? (y >= hell_floor - 2 ? hell_top : B_ASH) : (y <= VX_LAVA_Y ? B_LAVA : B_AIR);
        } else if (y < ash_top) {
          b = B_ASH;
        } else if (y < VX_UNDERGROUND_Y) {
          f64 sn = vx_grid(S->soil, lx, y, lz);
          b = sn > 0.55 ? (soil == B_ICE || soil == B_SANDSTONE ? soil : B_DIRT) : deep;
          // the ice biome under the snow: ice with veins of snow and a little stone
          if (biome == VX_BIOME_SNOW && y > VX_CAVERN_Y + 6) b = sn > 0.3 ? B_SNOW : sn < -0.45 ? B_STONE : B_ICE;
          if (biome == VX_BIOME_JUNGLE && sn > 0.1) b = B_MUD;
          if (vx_mushroom_zone(T, wx, y, wz)) b = B_MUD;
        } else if (y <= h - sub_depth) {
          f64 sn = vx_grid(S->soil, lx, y, lz);
          b = sn < -0.3 ? deep : soil;
          if (deep == B_EBONSTONE && sn < 0.1) b = B_EBONSTONE;
        } else {
          b = lm == VX_LM_PEAK ? vx_peak_block(T, wx, y, wz, y == h) : y == h ? top : sub;
        }
        blocks[VX_IDX(lx, y, lz)] = b;
      }
      for (i32 y = h + 1; y <= water_top; y++) blocks[VX_IDX(lx, y, lz)] = B_WATER;

      // caves: tunnels from below the soil to the cavern layer, wide caverns in the cavern layer
      i32 cave_top = mouth ? h : h - 7;
      if (lm != VX_LM_NONE && lm != VX_LM_OASIS) cave_top = S->ground[k] - 7;
      for (i32 y = ash_top + 2; y <= cave_top && y < VX_HEIGHT - 4; y++) {
        b32 carve = false;
        f64 t = vx_grid(S->tun, lx, y, lz);
        f64 thick = y > VX_UNDERGROUND_Y ? 0.010 : 0.016;
        carve = t < thick;
        if (!carve && y < VX_UNDERGROUND_Y - 2) {
          f64 c = vx_grid(S->cav, lx, y, lz);
          // mushroom pockets open into one great dark cavern held up by a few pillars
          f64 th = vx_mushroom_zone(T, wx, y, wz) ? 0.38 - vx_mushroom_core(T, wx, y, wz) * 0.9 : 0.5;
          // caverns pinch closed toward the underworld roof and the underground layer
          th += vx_smoothstep(VX_UNDERGROUND_Y - 16, VX_UNDERGROUND_Y - 2, y) * 0.4;
          th += (1.0 - vx_smoothstep(ash_top + 2, ash_top + 10, y)) * 0.4;
          carve = c > th;
        }
        u8 fill = B_AIR;
        if (lake && y <= lake->level + 10) {
          if (lake_d >= 1.0) {
            // the dam keeps the water from facing open cave
            if (y <= lake->level) carve = false;
          } else {
            f64 k = 1.0 - lake_d * lake_d;
            if (y > lake->level - 1 - k * 7.0 && y <= lake->level + k * 10.0) carve = true;
            if (carve && y <= lake->level) fill = B_WATER;
          }
        }
        if (!carve) continue;
        i32 i = VX_IDX(lx, y, lz);
        u8 above = blocks[i + 256];
        if (blocks[i] == B_WATER || above == B_WATER) continue;
        blocks[i] = fill;
      }
    }
  }
  // surface ponds: the basin fills to its level, the ground above it inside the pond is gone
  for (i32 lz = 0; lz < 16; lz++)
    for (i32 lx = 0; lx < 16; lx++) {
      VxLake pond;
      f64 d = vx_pond_dist_at(T, x0 + lx, z0 + lz, &pond);
      if (d >= 1.0) continue;
      i32 depth = 1 + (i32)m_round((1.0 - d * d) * 3.5);
      b32 frozen = S->biome[(lz + M) * SPAN + (lx + M)] == VX_BIOME_SNOW;
      for (i32 y = pond.level - depth + 1; y < VX_HEIGHT - 1; y++) {
        u8 *b = &blocks[VX_IDX(lx, y, lz)];
        if (y <= pond.level) *b = frozen && y == pond.level ? B_ICE : B_WATER;
        else if (*b == B_AIR) break;
        else *b = B_AIR;
      }
      // a sandy or muddy bed
      u8 bed = S->biome[(lz + M) * SPAN + (lx + M)] == VX_BIOME_JUNGLE ? B_MUD : B_SAND;
      blocks[VX_IDX(lx, pond.level - depth, lz)] = bed;
      S->h[(lz + M) * SPAN + (lx + M)] = pond.level;
    }

  if (surface) {
    vx_place_isles(T, blocks, x0, z0);
    vx_place_trees(T, S, blocks, x0, z0);
    vx_place_desert_cacti(T, S, blocks, x0, z0);
    vx_town_stamp(T, blocks, NULL, x0, z0);
    vx_place_spawn_hut(T, blocks, x0, z0);
    return;
  }
  // a spring pours from each lake's roof into the water
  for (u32 li = 0; li < lake_count; li++) {
    const VxLake *lk = &lakes[li];
    u32 h = vx_hash2i((i32)lk->x, (i32)lk->z, T->seed ^ 0x5b41u);
    f64 a = (f64)(h & 1023u) / 1024.0 * M_PI * 2.0;
    i32 sx = (i32)(lk->x + m_cos(a) * lk->r * 0.45), sz = (i32)(lk->z + m_sin(a) * lk->r * 0.45);
    i32 lx = sx - x0, lz = sz - z0;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
    i32 y = lk->level + 1;
    while (y < VX_HEIGHT - 1 && blocks[VX_IDX(lx, y, lz)] == B_AIR) y++;
    if (y - lk->level < 4 || blocks[VX_IDX(lx, y, lz)] == B_WATER) continue;
    for (i32 k = lk->level + 1; k < y; k++) blocks[VX_IDX(lx, k, lz)] = B_WATER;
  }
  vx_place_ores(T, blocks, cx, cz);
  vx_decorate_caverns(T, blocks, x0, z0);
  vx_place_pots(T, S, blocks, x0, z0);
  vx_place_giant_mushrooms(T, blocks, x0, z0);
  vx_place_cabins(T, blocks, x0, z0);
  vx_place_ruins(T, blocks, x0, z0);
  vx_place_hives(T, blocks, x0, z0);
  vx_place_aether(T, blocks, x0, z0);
  vx_place_dungeon(T, blocks, x0, z0);
  vx_place_lava_falls(T, blocks, x0, z0);
  vx_place_isles(T, blocks, x0, z0);
  vx_place_trees(T, S, blocks, x0, z0);
  vx_place_desert_cacti(T, S, blocks, x0, z0);
  vx_place_plants(T, S, blocks, x0, z0);
  vx_town_stamp(T, blocks, states, x0, z0);
  vx_place_spawn_hut(T, blocks, x0, z0);
}

v3 vx_find_spawn(const VxTerrain *T) {
  VxColumnSample s = vx_sample_column(T, 0, 0);
  return (v3){0.5f, (f32)(s.height + 1), 0.5f};
}

#undef M
#undef SPAN

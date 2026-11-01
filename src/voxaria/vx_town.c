#include "voxaria/vx.h"

// the town round spawn: a fixed street plan on a levelled plateau, stamped chunk by chunk like every structure

typedef enum {
  VX_TP_ROAD,
  VX_TP_WOOD_HOUSE,  // plank walls on a stone footing, log corners, a stepped plank roof
  VX_TP_BRICK_HOUSE, // two storeys: a red brick frame round plank panels, a red brick roof
  VX_TP_KEEP,        // a stone tower box with dark bands and crenellations
  VX_TP_COTTAGE,     // boreal planks under a slate roof
  VX_TP_GREENHOUSE,  // an open copper frame
  VX_TP_ARMOURY,     // a brick house whose ground floor is a grey brick armoury with a smith inside
  VX_TP_BANNER_WALL, // the long hall of monster banners: a grey brick wall banded in red, a cobble walk, torch pillars
  VX_TP_MUSEUM,      // the furniture museum: double rows of furniture sets between cobble roads
  VX_TP_PYLON_AVENUE, // the pylon avenue: a marble strip of pylons beside a marble walk and a cobble road, signposts, torch pillars
  VX_TP_WHITE_KEEP,   // the keep's pale twin: marble walls framed in dark bands and battlements
} VxTownKind;

typedef struct {
  i16 x0, z0, x1, z1; // footprint, max exclusive
  u8 kind, face;      // face: the side the door opens to, a VxFace
  u8 keeper;          // a market table before the house kept by this VxTownRole + 1, or 0
} VxTownPart;

// main street along z west of the Guide's hut, a cross street along x to its north
hz_internal const VxTownPart VX_TOWN[] = {
    {0, -68, 5, 60, VX_TP_ROAD, 0, 0},
    {-62, -14, 84, -9, VX_TP_ROAD, 0, 0},
    {-14, -62, -2, -51, VX_TP_BRICK_HOUSE, VX_FACE_PX, 0},
    {-13, -44, -2, -35, VX_TP_WOOD_HOUSE, VX_FACE_PX, 0},
    {-12, -6, -3, 3, VX_TP_KEEP, VX_FACE_PX, 0},
    {-13, 12, -2, 21, VX_TP_WOOD_HOUSE, VX_FACE_PX, VX_ROLE_MERCHANT + 1},
    {-14, 30, -2, 41, VX_TP_BRICK_HOUSE, VX_FACE_PX, VX_ROLE_CLOTHIER + 1},
    {-12, 48, -3, 55, VX_TP_COTTAGE, VX_FACE_PX, 0},
    {8, -62, 19, -53, VX_TP_WOOD_HOUSE, VX_FACE_NX, 0},
    {8, -44, 20, -33, VX_TP_ARMOURY, VX_FACE_NX, VX_ROLE_DEMOLITIONIST + 1},
    {8, 12, 20, 23, VX_TP_BRICK_HOUSE, VX_FACE_NX, VX_ROLE_DRYAD + 1},
    {8, 30, 19, 39, VX_TP_WOOD_HOUSE, VX_FACE_NX, VX_ROLE_MECHANIC + 1},
    {8, 48, 16, 55, VX_TP_COTTAGE, VX_FACE_NX, 0},
    {-32, -25, -21, -16, VX_TP_WOOD_HOUSE, VX_FACE_PZ, 0},
    {-52, -24, -43, -16, VX_TP_COTTAGE, VX_FACE_PZ, 0},
    {24, -27, 36, -16, VX_TP_BRICK_HOUSE, VX_FACE_PZ, 0},
    {46, -25, 57, -16, VX_TP_WOOD_HOUSE, VX_FACE_PZ, 0},
    {-32, -7, -21, 2, VX_TP_WOOD_HOUSE, VX_FACE_NZ, 0},
    {24, -7, 36, 4, VX_TP_BRICK_HOUSE, VX_FACE_NZ, 0},
    {44, 2, 56, 13, VX_TP_BRICK_HOUSE, VX_FACE_NZ, 0},
    {47, -8, 56, 1, VX_TP_WHITE_KEEP, VX_FACE_PX, 0},
    {37, -27, 45, -18, VX_TP_KEEP, VX_FACE_PZ, 0},
    {-52, -7, -43, 1, VX_TP_COTTAGE, VX_FACE_NZ, 0},
    {62, -36, 80, -18, VX_TP_GREENHOUSE, 0, 0},
    {35, 6, 42, 62, VX_TP_BANNER_WALL, VX_FACE_NX, 0},
    {-64, 8, -24, 56, VX_TP_MUSEUM, 0, 0},
    {58, -17, 85, 3, VX_TP_PYLON_AVENUE, 0, 0},
    {86, -16, 97, -5, VX_TP_BRICK_HOUSE, VX_FACE_NX, 0},
};

// the plateau: a rectangle round the plan that damps the hills, blending back into them over VX_TOWN_FADE blocks
#define VX_TOWN_CX 10
#define VX_TOWN_CZ (-4)
#define VX_TOWN_HX 76.0
#define VX_TOWN_HZ 66.0
#define VX_TOWN_FADE 18.0

hz_internal f64 vx_town_dist(i32 x, i32 z) {
  f64 dx = m_fabs((f64)(x - VX_TOWN_CX)) - VX_TOWN_HX, dz = m_fabs((f64)(z - VX_TOWN_CZ)) - VX_TOWN_HZ;
  dx = dx > 0 ? dx : 0, dz = dz > 0 ? dz : 0;
  return m_sqrt(dx * dx + dz * dz);
}

f64 vx_town_weight(const VxTerrain *T, i32 x, i32 z) {
  if (T->town_y <= 0) return 0;
  f64 d = vx_town_dist(x, z);
  return d >= VX_TOWN_FADE ? 0 : 1.0 - vx_smoothstep(0.0, VX_TOWN_FADE, d);
}

b32 vx_town_clear(i32 x, i32 z) {
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++) {
    const VxTownPart *p = &VX_TOWN[i];
    if (x >= p->x0 - 4 && x < p->x1 + 4 && z >= p->z0 - 4 && z < p->z1 + 4) return true;
  }
  return false;
}

hz_internal i32 vx_town_floor(const VxTerrain *T, const VxTownPart *p);

void vx_town_plan(VxTerrain *t) {
  t->town_y = 0;
  t->town_y = vx_sample_column(t, VX_TOWN_CX, VX_TOWN_CZ).height;
  t->armoury_y = 0;
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++)
    if (VX_TOWN[i].kind == VX_TP_ARMOURY) t->armoury_y = vx_town_floor(t, &VX_TOWN[i]);
}

typedef struct {
  u8 *blocks;
  VxGenStates *st;
  i32 x0, z0;
} VxTownOut;

force_inline void vx_tset(const VxTownOut *o, i32 x, i32 y, i32 z, u8 id) {
  i32 lx = x - o->x0, lz = z - o->z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 1 || y >= VX_HEIGHT) return;
  o->blocks[VX_IDX(lx, y, lz)] = id;
}

// a block with a state byte: a wall torch's side, a furniture cell's part and turn
force_inline void vx_tset_st(const VxTownOut *o, i32 x, i32 y, i32 z, u8 id, u8 state) {
  i32 lx = x - o->x0, lz = z - o->z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 1 || y >= VX_HEIGHT) return;
  o->blocks[VX_IDX(lx, y, lz)] = id;
  vx_gen_state(o->st, lx, y, lz, id, state);
}

// a piece of furniture from its part-0 cell, its width stepping along +x, +z, -x or -z for quarter turns 0-3
hz_internal void vx_town_furn(const VxTownOut *o, i32 x, i32 y, i32 z, u8 id, u32 rot) {
  VxFurniture fu = vx_furniture[id];
  i32 dx = rot == 0 ? 1 : rot == 2 ? -1 : 0, dz = rot == 1 ? 1 : rot == 3 ? -1 : 0;
  for (u32 py = 0; py < fu.h; py++)
    for (u32 px = 0; px < fu.w; px++)
      vx_tset_st(o, x + dx * (i32)px, y + (i32)py, z + dz * (i32)px, id, (u8)(rot | (px + py * fu.w) << 2));
}

force_inline u8 vx_tget(const VxTownOut *o, i32 x, i32 y, i32 z) {
  i32 lx = x - o->x0, lz = z - o->z0;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 0 || y >= VX_HEIGHT) return B_AIR;
  return o->blocks[VX_IDX(lx, y, lz)];
}

// a house box in local frame: u along the door wall, v away from the road, rotated into world space by face
typedef struct {
  const VxTownPart *p;
  i32 w, d; // extent along u and v
  i32 fy;   // first floor row
} VxLot;

force_inline void vx_lot_world(const VxLot *l, i32 u, i32 v, i32 *x, i32 *z) {
  const VxTownPart *p = l->p;
  switch (p->face) {
  case VX_FACE_PX: *x = p->x1 - 1 - v, *z = p->z0 + u; break;
  case VX_FACE_NX: *x = p->x0 + v, *z = p->z1 - 1 - u; break;
  case VX_FACE_PZ: *x = p->x1 - 1 - u, *z = p->z1 - 1 - v; break;
  default: *x = p->x0 + u, *z = p->z0 + v; break;
  }
}

// the furniture turn whose width runs along +u, so its front faces the road
force_inline u32 vx_lot_rot(const VxLot *l) {
  switch (l->p->face) {
  case VX_FACE_PX: return 1;
  case VX_FACE_NX: return 3;
  case VX_FACE_PZ: return 2;
  default: return 0;
  }
}

// a wall torch in front of the door wall hangs on it: the wall lies at +v
force_inline u8 vx_lot_wall_side(const VxLot *l) {
  switch (l->p->face) {
  case VX_FACE_PX: return VX_SIDE_NX;
  case VX_FACE_NX: return VX_SIDE_PX;
  case VX_FACE_PZ: return VX_SIDE_NZ;
  default: return VX_SIDE_PZ;
  }
}

force_inline void vx_lset(const VxTownOut *o, const VxLot *l, i32 u, i32 y, i32 v, u8 id) {
  i32 x, z;
  vx_lot_world(l, u, v, &x, &z);
  vx_tset(o, x, l->fy + y, z, id);
}

// a gable roof whose ridge runs along u: each course steps in from the front and back walls, the ends filled
hz_internal void vx_town_roof(const VxTownOut *o, const VxLot *l, i32 base, u8 slope, u8 gable, b32 stepped_ends) {
  i32 rise = (l->d + 1) / 2;
  for (i32 k = 0; k <= rise; k++) {
    i32 v0 = k - 1, v1 = l->d - k;
    if (v0 > v1) break;
    for (i32 u = -1; u <= l->w; u++) {
      b32 end = u == 0 || u == l->w - 1;
      b32 eave = u == -1 || u == l->w;
      for (i32 v = v0; v <= v1; v++) {
        b32 skin = v == v0 || v == v1 || k == rise || v1 - v0 <= 1;
        if (eave) {
          if (skin) vx_lset(o, l, u, base + k, v, slope);
        } else if (end) {
          vx_lset(o, l, u, base + k, v, skin ? slope : gable);
          // crow steps: the gable's edge stands one course proud of the roof
          if (stepped_ends && skin) vx_lset(o, l, u, base + k + 1, v, slope);
        } else if (skin) {
          vx_lset(o, l, u, base + k, v, slope);
        } else {
          vx_lset(o, l, u, base + k, v, B_AIR);
        }
      }
    }
  }
}

// windows every few blocks along each wall, a two-high doorway in the middle of the door wall
hz_internal b32 vx_town_window(const VxLot *l, i32 u, i32 v, i32 dy, i32 storey_h) {
  i32 row = dy % storey_h;
  if (row < 2 || row > 3) return false;
  b32 front = v == 0 || v == l->d - 1;
  i32 t = front ? u : v, len = front ? l->w : l->d;
  if (t <= 1 || t >= len - 2) return false;
  return ((t - 1) % 4) == 1 || ((t - 1) % 4) == 2;
}

hz_internal void vx_town_house(const VxTerrain *T, const VxTownOut *o, const VxLot *l, u32 kind) {
  b32 brick = kind == VX_TP_BRICK_HOUSE || kind == VX_TP_ARMOURY, keep = kind == VX_TP_KEEP || kind == VX_TP_WHITE_KEEP, cottage = kind == VX_TP_COTTAGE;
  b32 white = kind == VX_TP_WHITE_KEEP;
  i32 storey = 5, storeys = brick ? 2 : keep ? 2 : 1;
  i32 h = storey * storeys;
  u8 panel = cottage ? B_BOREAL_PLANKS : B_PLANKS;
  i32 door = l->w / 2;
  for (i32 v = 0; v < l->d; v++)
    for (i32 u = 0; u < l->w; u++) {
      b32 eu = u == 0 || u == l->w - 1, ev = v == 0 || v == l->d - 1;
      b32 corner = eu && ev, wall = eu || ev;
      vx_lset(o, l, u, -1, v, keep ? B_GRAY_BRICK : B_PLANKS);
      // a stone footing down to the ground where the lot falls away
      i32 x, z;
      vx_lot_world(l, u, v, &x, &z);
      if (x >= o->x0 && x < o->x0 + 16 && z >= o->z0 && z < o->z0 + 16)
        for (i32 y = vx_sample_column(T, x, z).height + 1; y < l->fy - 1; y++) vx_tset(o, x, y, z, wall ? B_GRAY_BRICK : B_DIRT);
      for (i32 dy = 0; dy < h + 1; dy++) {
        u8 id = B_AIR;
        if (wall) {
          if (keep) {
            if (white) id = corner || dy == 0 || dy == storey || dy >= h - 1 ? B_OBSIDIAN_BRICK : B_MARBLE;
            else id = corner || dy == 0 || dy == h - 1 ? B_GRAY_BRICK : B_OBSIDIAN_BRICK;
          } else if (brick) {
            b32 band = dy == 0 || dy == storey || dy == h - 1;
            id = corner || band || (u % 4 == 0 && v == 0) || (u % 4 == 0 && v == l->d - 1) ? B_RED_BRICK : panel;
            if (dy == 0) id = B_GRAY_BRICK;
          } else {
            id = dy == 0 ? B_GRAY_BRICK : corner ? (cottage ? B_BOREAL_LOG : B_LOG) : panel;
          }
          if (dy < h && !corner && vx_town_window(l, u, v, dy, storey)) id = B_GLASS;
          if (v == 0 && u == door && dy >= 1 && dy <= 3) id = B_AIR;
          if (dy == h) id = keep ? ((u + v) % 2 == 0 ? (white ? B_OBSIDIAN_BRICK : B_GRAY_BRICK) : B_AIR) : B_AIR;
        } else if (dy == storey && storeys > 1) {
          id = B_PLANKS;
        }
        vx_lset(o, l, u, dy, v, id);
      }
    }
  // the keep's flat roof inside its battlements
  if (keep)
    for (i32 v = 1; v < l->d - 1; v++)
      for (i32 u = 1; u < l->w - 1; u++) vx_lset(o, l, u, h - 1, v, B_GRAY_BRICK);
  if (!keep) {
    u8 slope = brick ? B_RED_BRICK : cottage ? B_GRAY_BRICK : B_PLANKS;
    vx_town_roof(o, l, h, slope, panel, brick);
  }
  // the door, and a torch on the wall either side of it
  i32 dx, dz;
  vx_lot_world(l, door, 0, &dx, &dz);
  vx_town_furn(o, dx, l->fy + 1, dz, B_DOOR, vx_lot_rot(l) & 1);
  for (i32 side = -2; side <= 2; side += 4) {
    i32 x, z;
    vx_lot_world(l, door + side, -1, &x, &z);
    vx_tset_st(o, x, l->fy + 2, z, B_TORCH, vx_lot_wall_side(l));
  }
  // a market table out by the road, beside the door
  if (l->p->keeper && kind != VX_TP_ARMOURY) {
    i32 tx, tz;
    vx_lot_world(l, door + 2, -3, &tx, &tz);
    vx_town_furn(o, tx, vx_sample_column(T, tx, tz).height + 1, tz, B_TABLE, vx_lot_rot(l));
  }
}

// the armoury's ground floor: a grey brick lining inside the walls, torches on the back wall, a table with chairs and an anvil
hz_internal void vx_town_armoury_room(const VxTownOut *o, const VxLot *l) {
  i32 w = l->w, d = l->d, door = w / 2;
  for (i32 v = 1; v < d - 1; v++)
    for (i32 u = 1; u < w - 1; u++) {
      if (u != 1 && u != w - 2 && v != 1 && v != d - 2) continue;
      for (i32 dy = 0; dy < 5; dy++)
        if (!(v == 1 && u == door && dy <= 3)) vx_lset(o, l, u, dy, v, B_GRAY_BRICK);
    }
  u32 rot = vx_lot_rot(l);
  for (i32 side = 0; side < 2; side++) {
    i32 x, z;
    vx_lot_world(l, side ? w - 4 : 3, d - 3, &x, &z);
    vx_tset_st(o, x, l->fy + 3, z, B_TORCH, vx_lot_wall_side(l));
  }
  i32 x, z;
  // seen from the door, the table and its smith to the right, the anvil to the left
  vx_lot_world(l, 2, 4, &x, &z);
  vx_town_furn(o, x, l->fy, z, B_TABLE, rot);
  vx_lot_world(l, 3, 3, &x, &z);
  vx_town_furn(o, x, l->fy, z, B_CHAIR, rot);
  vx_lot_world(l, 5, 4, &x, &z);
  vx_town_furn(o, x, l->fy, z, B_CHAIR, (rot + 3) & 3);
  vx_lot_world(l, w - 4, 2, &x, &z);
  vx_town_furn(o, x, l->fy, z, B_ANVIL, rot);
}

hz_internal VxLot vx_town_lot(const VxTownPart *p, i32 fy) {
  b32 along_x = p->face == VX_FACE_PZ || p->face == VX_FACE_NZ;
  return (VxLot){.p = p, .w = along_x ? p->x1 - p->x0 : p->z1 - p->z0, .d = along_x ? p->z1 - p->z0 : p->x1 - p->x0, .fy = fy};
}

b32 vx_town_armoury(const VxTerrain *T, VxTownRoom *r) {
  if (T->town_y <= 0) return false;
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++) {
    const VxTownPart *p = &VX_TOWN[i];
    if (p->kind != VX_TP_ARMOURY) continue;
    VxLot l = vx_town_lot(p, T->armoury_y);
    i32 x0, z0, x1, z1, x2, z2;
    vx_lot_world(&l, 0, 0, &x0, &z0);
    vx_lot_world(&l, 1, 0, &x1, &z1);
    vx_lot_world(&l, 0, 1, &x2, &z2);
    *r = (VxTownRoom){.ox = x0 + 0.5, .oz = z0 + 0.5, .ux = (f32)(x1 - x0), .uz = (f32)(z1 - z0), .vx = (f32)(x2 - x0),
                      .vz = (f32)(z2 - z0), .y = l.fy, .w = l.w, .d = l.d};
    return true;
  }
  return false;
}

b32 vx_town_avenue(const VxTerrain *T, f64 *x, f64 *y, f64 *z, f64 *half_len) {
  if (T->town_y <= 0) return false;
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++) {
    const VxTownPart *p = &VX_TOWN[i];
    if (p->kind != VX_TP_PYLON_AVENUE) continue;
    *x = (p->x0 + p->x1) * 0.5, *z = (p->z0 + p->z1) * 0.5, *y = T->town_y, *half_len = (p->x1 - p->x0) * 0.5;
    return true;
  }
  return false;
}

// where a keeper stands: behind the middle of its market table facing the road, or behind the armoury's table inside
b32 vx_town_keeper(const VxTerrain *T, u32 role, f64 *x, i32 *y, f64 *z, f32 *yaw) {
  if (T->town_y <= 0) return false;
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++) {
    const VxTownPart *p = &VX_TOWN[i];
    if (p->keeper != role + 1) continue;
    b32 inside = p->kind == VX_TP_ARMOURY;
    VxLot l = vx_town_lot(p, 0);
    i32 cx, cz;
    vx_lot_world(&l, inside ? 3 : l.w / 2 + 3, inside ? 5 : -2, &cx, &cz);
    *x = cx + 0.5, *z = cz + 0.5;
    *y = inside ? T->armoury_y : vx_sample_column(T, cx, cz).height + 1;
    // yaw looks along (-sin, -cos): toward -v, the road
    *yaw = p->face == VX_FACE_PX ? -PI * 0.5f : p->face == VX_FACE_NX ? PI * 0.5f : p->face == VX_FACE_PZ ? PI : 0.0f;
    return true;
  }
  return false;
}

// posts every three blocks, rails at mid height and along the top, the roof beams across
hz_internal void vx_town_greenhouse(const VxTerrain *T, const VxTownOut *o, const VxTownPart *p, i32 fy) {
  const i32 H = 7;
  for (i32 z = p->z0; z < p->z1; z++)
    for (i32 x = p->x0; x < p->x1; x++) {
      b32 ex = x == p->x0 || x == p->x1 - 1, ez = z == p->z0 || z == p->z1 - 1;
      b32 gx = (x - p->x0) % 3 == 0, gz = (z - p->z0) % 3 == 0;
      for (i32 y = fy; y < fy + H + 1; y++) vx_tset(o, x, y, z, B_AIR);
      if ((ex || gx) && (ez || gz) && (ex || ez)) {
        for (i32 y = vx_sample_column(T, x, z).height + 1; y < fy + H; y++) vx_tset(o, x, y, z, B_COPPER_POST);
      } else if (ez) {
        vx_tset(o, x, fy + 2, z, B_COPPER_BEAM_X);
        vx_tset(o, x, fy + H - 1, z, B_COPPER_BEAM_X);
      } else if (ex) {
        vx_tset(o, x, fy + 2, z, B_COPPER_BEAM_Z);
        vx_tset(o, x, fy + H - 1, z, B_COPPER_BEAM_Z);
      }
      if (gz && !ez) vx_tset(o, x, fy + H, z, B_COPPER_BEAM_X);
      // garden rows of tilled soil inside the frame
      if (!ex && !ez && (z - p->z0) % 3 != 0) vx_tset(o, x, vx_sample_column(T, x, z).height, z, B_DIRT);
    }
}

// the banner wall at the lot's +x end facing -x: three banner rows between red bands, a cobble walk, a torch pillar every eight blocks
hz_internal void vx_town_banner_wall(const VxTerrain *T, const VxTownOut *o, const VxTownPart *p, i32 fy) {
  // banner rows start on an even row, where a tile layer's top half meets the upper block
  i32 y0 = fy + 1 + ((fy + 1) & 1);
  i32 wx = p->x1 - 2, top = y0 + 12;
  for (i32 z = MAX(p->z0, o->z0); z < MIN(p->z1, o->z0 + 16); z++) {
    for (i32 x = MAX(p->x0, o->x0); x < MIN(p->x1, o->x0 + 16); x++) {
      i32 gy = vx_sample_column(T, x, z).height;
      if (x >= wx) {
        for (i32 y = gy + 1; y < y0; y++) vx_tset(o, x, y, z, B_GRAY_BRICK);
        for (i32 dy = 0; dy <= 12; dy++) {
          i32 y = y0 + dy, k = dy % 4;
          u8 id = k == 3 || dy == 12 ? B_RED_BRICK : k == 2 ? B_GRAY_BRICK : B_BANNER;
          if (x != wx && id == B_BANNER) id = B_GRAY_BRICK;
          vx_tset(o, x, y, z, id);
          if (id == B_BANNER) {
            u32 row = (u32)dy / 4, layer = vx_hash2i(z, (i32)row, 0x27d4eb2du) % 16u;
            vx_tset_st(o, x, y, z, B_BANNER, (u8)(VX_FACE_NX | layer << 3));
          }
        }
        vx_tset(o, x, top + 1, z, B_RED_BRICK);
      } else {
        // the cap overhangs the walk by one block
        if (x == wx - 1) vx_tset(o, x, top, z, B_RED_BRICK);
        // the walk, raised or cut to the wall's base row
        b32 walk = x >= wx - 3;
        for (i32 y = gy + 1; y < y0 - 1; y++) vx_tset(o, x, y, z, B_DIRT);
        for (i32 y = y0 - 1; y <= gy; y++) vx_tset(o, x, y, z, B_AIR);
        vx_tset(o, x, y0 - 1, z, walk ? B_COBBLE : B_GRASS);
        if (walk || vx_shape[vx_tget(o, x, y0, z)] == VX_SHAPE_CROSS) vx_tset(o, x, y0, z, B_AIR);
      }
    }
  }
  // pillars on the grass before the walk, a torch either side of each top
  i32 px = wx - 4;
  for (i32 z = p->z0 + 2; z < p->z1 - 1; z += 8) {
    if (px < o->x0 - 1 || px > o->x0 + 16) continue;
    for (i32 dy = 0; dy < 4; dy++) vx_tset(o, px, y0 + dy, z, dy == 0 ? B_GRAY_BRICK : B_PLANKS);
    vx_tset_st(o, px, y0 + 3, z + 1, B_TORCH, VX_SIDE_NZ);
    vx_tset_st(o, px, y0 + 3, z - 1, B_TORCH, VX_SIDE_PZ);
  }
}

// a showpiece of a museum set at set-local (u along z, v back from the road); side 1 faces +x, its pieces running down z
hz_internal void vx_museum_put(const VxTownOut *o, i32 fx, i32 y, i32 z0, u32 side, i32 u, i32 v, u8 id) {
  i32 w = vx_furniture[id].w;
  i32 x = side ? fx - v : fx + v, z = side ? z0 + u + w - 1 : z0 + u;
  u32 rot = side ? 3 : 1;
  for (i32 py = 0; py < vx_furniture[id].h; py++)
    for (i32 px = 0; px < w; px++)
      vx_tset_st(o, x, y + py, side ? z - px : z + px, id, (u8)(rot | (u32)(px + py * w) << 2 | VX_FURN_SHOW));
}

// one furniture set eight blocks long: a table between chairs and a lamp by the road, a bed with a seat and a tall piece behind
hz_internal void vx_museum_set(const VxTownOut *o, i32 fx, i32 y, i32 z0, u32 side, u32 h) {
  vx_museum_put(o, fx, y, z0, side, 0, 0, B_CHAIR);
  vx_museum_put(o, fx, y, z0, side, 1, 0, B_TABLE);
  vx_museum_put(o, fx, y, z0, side, 4, 0, B_CHAIR);
  vx_museum_put(o, fx, y, z0, side, 5, 0, B_LAMP);
  hz_internal const u8 seat[] = {B_SOFA, B_PIANO, B_SOFA};
  vx_museum_put(o, fx, y, z0, side, 0, 1, seat[h % ARRAY_SIZE(seat)]);
  vx_museum_put(o, fx, y, z0, side, 3, 1, B_BED);
  if ((h >> 3) & 1) {
    vx_museum_put(o, fx, y, z0, side, 5, 1, B_BOOKCASE);
  } else {
    vx_museum_put(o, fx, y, z0, side, 5, 1, B_CLOCK);
    vx_museum_put(o, fx, y, z0, side, 6, 1, B_LAMP);
  }
  // a stone candle post closes the set by the road, its back filled to the next set
  vx_tset(o, fx, y, z0 + 7, B_GRAY_BRICK);
  vx_tset(o, fx, y + 1, z0 + 7, B_TORCH);
  vx_tset(o, side ? fx - 1 : fx + 1, y, z0 + 7, B_GRAY_BRICK);
}

// a lot levelled to floor fy: the ground filled up under it, cleared above, its top row the given block
hz_internal void vx_town_pad(const VxTerrain *T, const VxTownOut *o, const VxTownPart *p, i32 fy, u8 top) {
  for (i32 z = MAX(p->z0, o->z0); z < MIN(p->z1, o->z0 + 16); z++)
    for (i32 x = MAX(p->x0, o->x0); x < MIN(p->x1, o->x0 + 16); x++) {
      i32 gy = vx_sample_column(T, x, z).height;
      for (i32 y = gy + 1; y < fy - 1; y++) vx_tset(o, x, y, z, B_DIRT);
      for (i32 y = fy; y <= MAX(gy, fy + 1); y++) vx_tset(o, x, y, z, B_AIR);
      vx_tset(o, x, fy - 1, z, top);
    }
}

// the museum: a levelled cobble pad, five double rows of sets back to back with a three-wide road either side of each pair
hz_internal void vx_town_museum(const VxTerrain *T, const VxTownOut *o, const VxTownPart *p, i32 fy) {
  vx_town_pad(T, o, p, fy, B_COBBLE);
  for (i32 b = 0; b < 5; b++) {
    i32 rx = p->x0 + 3 + b * 7;
    for (u32 side = 0; side < 2; side++)
      for (i32 z = p->z0; z + 8 <= p->z1; z += 8)
        vx_museum_set(o, side ? rx + 3 : rx, fy, z, side, vx_hash2i(rx + (i32)side, z, 0x5e75u));
  }
}

// a wall torch on each side of a pillar's block at (x, y, z)
hz_internal void vx_town_torch_ring(const VxTownOut *o, i32 x, i32 y, i32 z) {
  vx_tset_st(o, x - 1, y, z, B_TORCH, VX_SIDE_PX);
  vx_tset_st(o, x + 1, y, z, B_TORCH, VX_SIDE_NX);
  vx_tset_st(o, x, y, z - 1, B_TORCH, VX_SIDE_PZ);
  vx_tset_st(o, x, y, z + 1, B_TORCH, VX_SIDE_NZ);
}

// a pillar h blocks tall from row y, torches round its top block
hz_internal void vx_town_pillar(const VxTownOut *o, i32 x, i32 y, i32 z, i32 h, u8 id) {
  for (i32 k = 0; k < h; k++) vx_tset(o, x, y + k, z, id);
  vx_town_torch_ring(o, x, y + h - 1, z);
}

// a pearlwood tree: a tall dark trunk under a broad gold crown, two violet clumps lower down
hz_internal void vx_town_hallow_tree(const VxTownOut *o, i32 x, i32 y, i32 z, u32 h) {
  i32 top = y + 10 + (i32)(h % 3);
  for (u32 c = 0; c < 3; c++) {
    i32 cx = x + (c == 1 ? 2 : c == 2 ? -2 : 0), cz = z + (c == 1 ? -1 : c == 2 ? 1 : 0), cy = c ? top - 3 - (i32)c : top;
    f32 r = c ? 1.7f : 2.8f;
    i32 ri = (i32)r + 1;
    for (i32 dy = -ri; dy <= ri; dy++)
      for (i32 dz = -ri; dz <= ri; dz++)
        for (i32 dx = -ri; dx <= ri; dx++)
          if ((f32)(dx * dx + dz * dz) + (f32)(dy * dy) * 1.4f <= r * r) vx_tset(o, cx + dx, cy + dy, cz + dz, c ? B_HALLOW_LEAVES : B_GOLD_LEAVES);
    if (c) vx_tset(o, x + (cx - x) / 2, cy - 1, z + (cz - z) / 2, B_PEARL_LOG);
  }
  for (i32 yy = y; yy < top; yy++) vx_tset(o, x, yy, z, B_PEARL_LOG);
}

// the pylon avenue along +x, rows from -z: signpost lawn, marble walk, cobble road, pylon strip, pillared verge, pearlwood
hz_internal void vx_town_pylon_avenue(const VxTerrain *T, const VxTownOut *o, const VxTownPart *p, i32 fy) {
  vx_town_pad(T, o, p, fy, B_COBBLE);
  hz_internal const u8 rows[20] = {B_GRASS,  B_GRASS,      B_MARBLE, B_MARBLE, B_MARBLE, B_GRAY_BRICK, B_COBBLE,
                                   B_COBBLE, B_COBBLE,     B_GRAY_BRICK, B_MARBLE, B_MARBLE, B_MARBLE, B_GRAY_BRICK,
                                   B_COBBLE, B_COBBLE,     B_COBBLE, B_HALLOW_GRASS, B_HALLOW_GRASS, B_HALLOW_GRASS};
  for (i32 z = p->z0; z < p->z1; z++)
    for (i32 x = p->x0; x < p->x1; x++) vx_tset(o, x, fy - 1, z, rows[MIN(z - p->z0, 19)]);
  // the pylons, two blocks apart down the strip's middle, each its own colour
  for (i32 x = p->x0 + 2, k = 0; x < p->x1 - 2; x += 2, k++) vx_tset_st(o, x, fy, p->z0 + 11, B_PYLON, (u8)(k % 8));
  // sandstone pillars at either side of the walk's ends and middle, brick pillars down the verge
  for (i32 k = 0; k < 3; k++) {
    i32 x = k == 0 ? p->x0 : k == 1 ? p->x0 + 13 : p->x1 - 1;
    vx_town_pillar(o, x, fy, p->z0 + 1, 4, B_LIMESTONE_BRICK);
    vx_town_pillar(o, x, fy, p->z0 + 5, 4, B_LIMESTONE_BRICK);
  }
  for (i32 x = p->x0 + 3; x < p->x1 - 1; x += 7) vx_town_pillar(o, x, fy, p->z0 + 15, 4, B_RED_BRICK);
  // light boxes down the walk's road edge, between the sandstone pillars
  for (i32 x = p->x0 + 2; x < p->x1 - 1; x += 4)
    if (x != p->x0 + 13 && x != p->x0 + 14) vx_tset(o, x, fy, p->z0 + 5, B_LIGHT_BOX);
  hz_internal const u8 lawn[4] = {VX_SIGN_SKY_ISLAND, VX_SIGN_CORRUPTION, VX_SIGN_HALLOW, VX_SIGN_DESERT};
  for (i32 k = 0; k < 4; k++) vx_tset_st(o, p->x0 + 3 + k * 6, fy, p->z0, B_SIGN, VX_SIGN_STATE(0, lawn[k]));
  hz_internal const u8 verge[3] = {VX_SIGN_JUNGLE, VX_SIGN_SNOW, VX_SIGN_OCEAN};
  for (i32 k = 0; k < 3; k++) vx_tset_st(o, p->x0 + 6 + k * 7, fy, p->z0 + 14, B_SIGN, VX_SIGN_STATE(0, verge[k]));
  vx_town_hallow_tree(o, p->x0 + 6, fy, p->z0 + 18, 0);
  vx_town_hallow_tree(o, p->x0 + 20, fy, p->z0 + 18, 1);
}

// a lot's floor: one row above the highest ground under it, sampled the same way by every chunk
hz_internal i32 vx_town_floor(const VxTerrain *T, const VxTownPart *p) {
  i32 top = 0;
  for (i32 j = 0; j <= 2; j++)
    for (i32 i = 0; i <= 2; i++) {
      i32 x = p->x0 + (p->x1 - 1 - p->x0) * i / 2, z = p->z0 + (p->z1 - 1 - p->z0) * j / 2;
      top = MAX(top, vx_sample_column(T, x, z).height);
    }
  return top + 1;
}

void vx_town_stamp(const VxTerrain *T, u8 *blocks, VxGenStates *states, i32 x0, i32 z0) {
  if (T->town_y <= 0) return;
  VxTownOut o = {.blocks = blocks, .st = states, .x0 = x0, .z0 = z0};
  for (u32 i = 0; i < ARRAY_SIZE(VX_TOWN); i++) {
    const VxTownPart *p = &VX_TOWN[i];
    // roofs overhang the footprint by one block, torches hang one out, a market table stands three out
    if (p->x1 + 4 <= x0 || p->x0 - 4 >= x0 + 16 || p->z1 + 4 <= z0 || p->z0 - 4 >= z0 + 16) continue;
    if (p->kind == VX_TP_ROAD) {
      // the road follows the ground, paved where it crosses this chunk
      for (i32 z = MAX(p->z0, z0); z < MIN(p->z1, z0 + 16); z++)
        for (i32 x = MAX(p->x0, x0); x < MIN(p->x1, x0 + 16); x++) {
          i32 gy = vx_sample_column(T, x, z).height;
          vx_tset(&o, x, gy, z, B_COBBLE);
          if (vx_shape[vx_tget(&o, x, gy + 1, z)] == VX_SHAPE_CROSS) vx_tset(&o, x, gy + 1, z, B_AIR);
        }
      continue;
    }
    i32 fy = vx_town_floor(T, p);
    if (p->kind == VX_TP_GREENHOUSE) {
      vx_town_greenhouse(T, &o, p, fy);
      continue;
    }
    if (p->kind == VX_TP_MUSEUM) {
      vx_town_museum(T, &o, p, fy);
      continue;
    }
    if (p->kind == VX_TP_PYLON_AVENUE) {
      vx_town_pylon_avenue(T, &o, p, fy);
      continue;
    }
    if (p->kind == VX_TP_BANNER_WALL) {
      vx_town_banner_wall(T, &o, p, fy);
      continue;
    }
    VxLot l = vx_town_lot(p, fy);
    // clear the lot's air and plants, then build
    for (i32 z = p->z0 - 1; z <= p->z1; z++)
      for (i32 x = p->x0 - 1; x <= p->x1; x++)
        for (i32 y = fy; y < fy + 20; y++) vx_tset(&o, x, y, z, B_AIR);
    vx_town_house(T, &o, &l, p->kind);
    if (p->kind == VX_TP_ARMOURY) vx_town_armoury_room(&o, &l);
  }
}

#ifndef VX_ENTITY_GEO_H
#define VX_ENTITY_GEO_H

// entity vertex emission shared by the effects and the puppet
#include "voxaria/vx_game.h"

#define VX_EFACES 6
#define VX_ECORNERS 4
#define VX_XYZ 3
#define VX_UV 2

hz_internal const u8 VX_ENT_CORNERS[VX_EFACES][VX_ECORNERS][VX_XYZ] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
};
hz_internal const u8 VX_ENT_UV[VX_ECORNERS][VX_UV] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
hz_internal const v3 VX_FACE_N[VX_EFACES] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

force_inline void vx_evtx(VxEntities *e, v3 p, f32 u, f32 v, u32 layer, u32 face, v3 n, f32 sky, f32 blk, u32 flags,
                          v3 t) {
  if (e->vert_count >= VX_ENTITY_MAX_VERTS) return;
  f32 *d = e->verts + e->vert_count * VX_ENTITY_STRIDE;
  d[0] = p.x;
  d[1] = p.y;
  d[2] = p.z;
  d[3] = u;
  d[4] = v;
  d[5] = (f32)layer;
  d[6] = (f32)face;
  d[7] = sky;
  d[8] = blk;
  d[9] = (f32)flags;
  d[10] = t.x;
  d[11] = t.y;
  d[12] = t.z;
  d[13] = n.x;
  d[14] = n.y;
  d[15] = n.z;
  e->vert_count++;
}

force_inline void vx_light_at(const VxWorld *w, VxV3d p, f32 *sky, f32 *blk) {
  u16 l = vx_world_light(w, vx_floor_i(p.x), vx_floor_i(p.y), vx_floor_i(p.z));
  *sky = (f32)(l >> VX_LIGHT_SKY_SHIFT) / 15.0f;
  u32 r = l & 15, g = (l >> 4) & 15, b = (l >> 8) & 15;
  *blk = (f32)(r > g ? (r > b ? r : b) : (g > b ? g : b)) / 15.0f;
}

// an instanced box over [lo, hi] through xf (camera-relative), tinted (linear rgb), the tile at one block per uv unit
void vx_ebox(VxEntities *e, m4 xf, v3 lo, v3 hi, u32 layer, v3 tint, f32 sky, f32 blk);
// as vx_ebox, with box flags (VX_BOX_SELF_LIT) and a 0..1 white hit flash
void vx_ebox_ex(VxEntities *e, m4 xf, v3 lo, v3 hi, u32 layer, v3 tint, u32 flags, f32 flash);
// a translucent jelly box, forward shaded: tint is its body colour, VX_BOX_SELF_LIT makes it glow
void vx_ejelly(VxEntities *e, m4 xf, v3 lo, v3 hi, v3 tint, u32 flags, f32 flash);

#endif

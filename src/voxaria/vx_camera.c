#include "voxaria/vx_game.h"

// third-person orbit camera with voxel collision, first-person toggle, and targeting reached from the player

#define VX_CAM_PIVOT 2.15f    // above the feet: the chest-high point the orbit turns about
#define VX_CAM_SHOULDER 0.75f // right of the pivot, so the crosshair clears the character
#define VX_CAM_PAD 0.3f       // kept between the camera and any wall
#define VX_CAM_MIN 0.9f

void vx_camera_init(VxCamera *c) { *c = (VxCamera){.mode = VX_CAM_THIRD, .dist = 6.0f, .cur_dist = 6.0f}; }

// distance along d to the first block that stops the camera: solid or view-blocking, never plants or liquids
hz_internal f32 vx_camera_trace(const VxWorld *w, VxV3d o, v3 d, f32 max_dist) {
  i32 x = vx_floor_i(o.x), y = vx_floor_i(o.y), z = vx_floor_i(o.z);
  i32 sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
  const f64 inf = 1e30;
  f64 tdx = d.x != 0 ? m_abs(1.0 / d.x) : inf, tdy = d.y != 0 ? m_abs(1.0 / d.y) : inf;
  f64 tdz = d.z != 0 ? m_abs(1.0 / d.z) : inf;
  f64 tmx = d.x != 0 ? (d.x > 0 ? x + 1 - o.x : o.x - x) * tdx : inf;
  f64 tmy = d.y != 0 ? (d.y > 0 ? y + 1 - o.y : o.y - y) * tdy : inf;
  f64 tmz = d.z != 0 ? (d.z > 0 ? z + 1 - o.z : o.z - z) * tdz : inf;
  f64 t = 0;
  while (t < max_dist) {
    u8 id = vx_world_block(w, x, y, z);
    if (vx_solid[id] == 1 || vx_occludes[id]) return (f32)t;
    if (tmx < tmy && tmx < tmz) x += sx, t = tmx, tmx += tdx;
    else if (tmy < tmz) y += sy, t = tmy, tmy += tdy;
    else z += sz, t = tmz, tmz += tdz;
  }
  return max_dist;
}

void vx_camera_update(VxCamera *c, const VxPlayer *p, const VxWorld *w, f32 dt, b32 bobbing) {
  if (c->mode == VX_CAM_FIRST) {
    c->fwd = vx_player_forward(p);
    c->pos = vx_player_camera(p, bobbing);
    c->cur_dist = 0;
    return;
  }
  f32 yaw = p->yaw + c->orbit, cp = m_cosf(p->pitch);
  v3 f = {-m_sinf(yaw) * cp, m_sinf(p->pitch), -m_cosf(yaw) * cp};
  c->fwd = f;
  v3 right = {m_cosf(yaw), 0, -m_sinf(yaw)};
  v3 up = v3_cross(right, f);
  VxV3d pivot = {p->pos.x + right.x * VX_CAM_SHOULDER, p->pos.y + VX_CAM_PIVOT, p->pos.z + right.z * VX_CAM_SHOULDER};
  // the shoulder offset itself must not push the pivot into a wall
  VxV3d chest = {p->pos.x, p->pos.y + VX_CAM_PIVOT, p->pos.z};
  f32 side = vx_camera_trace(w, chest, right, VX_CAM_SHOULDER + VX_CAM_PAD) - VX_CAM_PAD;
  if (side < VX_CAM_SHOULDER) {
    f32 k = m_maxf(0, side);
    pivot = (VxV3d){chest.x + right.x * k, chest.y, chest.z + right.z * k};
  }
  // five rays, centre and a cross the size of the near plane's footprint, so no edge of the view clips
  v3 back = v3_scale(f, -1);
  f32 want = c->dist, hit = want;
  for (u32 k = 0; k < 5; k++) {
    f32 ox = k == 1 ? 1.0f : k == 2 ? -1.0f : 0.0f, oy = k == 3 ? 1.0f : k == 4 ? -1.0f : 0.0f;
    v3 o = v3_add(v3_scale(right, ox * 0.25f), v3_scale(up, oy * 0.2f));
    VxV3d s = {pivot.x + o.x, pivot.y + o.y, pivot.z + o.z};
    hit = m_minf(hit, vx_camera_trace(w, s, back, want + VX_CAM_PAD) - VX_CAM_PAD);
  }
  hit = m_maxf(VX_CAM_MIN, hit);
  // walls pull the camera in at once; it eases back out when they clear
  if (hit < c->cur_dist) c->cur_dist = hit;
  else c->cur_dist += (hit - c->cur_dist) * (1 - m_expf(-dt * 4));
  c->pos = (VxV3d){pivot.x + back.x * c->cur_dist, pivot.y + back.y * c->cur_dist, pivot.z + back.z * c->cur_dist};
}

VxV3d vx_player_reach_origin(const VxPlayer *p) { return (VxV3d){p->pos.x, p->pos.y + 1.4, p->pos.z}; }

// where a view ray starts counting: level with the player, so nothing between the camera and the player is aimed at
VxV3d vx_camera_ray_start(const VxCamera *cam, VxV3d o, v3 d, const VxPlayer *p) {
  if (cam->mode == VX_CAM_FIRST) return o;
  VxV3d c = vx_player_reach_origin(p);
  f64 t = (c.x - o.x) * d.x + (c.y - o.y) * d.y + (c.z - o.z) * d.z;
  if (t <= 0) return o;
  return (VxV3d){o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
}

force_inline f64 vx_dist2_to_block(VxV3d o, i32 x, i32 y, i32 z) {
  f64 dx = x + 0.5 - o.x, dy = y + 0.5 - o.y, dz = z + 0.5 - o.z;
  return dx * dx + dy * dy + dz * dz;
}

// the block along a view ray, if the player can reach it; smart cursor picks the reachable block nearest the ray instead
VxRayHit vx_camera_target(const VxCamera *c, VxV3d o, v3 d, const VxPlayer *p, const VxWorld *w, f32 reach, b32 smart) {
  VxV3d ro = vx_player_reach_origin(p);
  f32 r2 = reach * reach;
  VxRayHit h = vx_raycast(w, vx_camera_ray_start(c, o, d, p), d, reach + 2.0f);
  if (h.hit && vx_dist2_to_block(ro, h.x, h.y, h.z) <= r2) return h;
  if (!smart) return (VxRayHit){0};
  // nearest exposed, breakable block to the view ray within reach
  i32 R = (i32)m_ceilf(reach);
  i32 px = vx_floor_i(ro.x), py = vx_floor_i(ro.y), pz = vx_floor_i(ro.z);
  f64 best = 1e30;
  VxRayHit out = {0};
  for (i32 y = py - R; y <= py + R; y++)
    for (i32 z = pz - R; z <= pz + R; z++)
      for (i32 x = px - R; x <= px + R; x++) {
        if (vx_dist2_to_block(ro, x, y, z) > r2) continue;
        u8 id = vx_world_block(w, x, y, z);
        if (id == B_AIR || vx_shape[id] == VX_SHAPE_LIQUID) continue;
        // which face looks at the camera and is open
        f64 vx = x + 0.5 - o.x, vy = y + 0.5 - o.y, vz = z + 0.5 - o.z;
        f64 along = vx * d.x + vy * d.y + vz * d.z;
        if (along <= 0) continue;
        f64 qx = vx - d.x * along, qy = vy - d.y * along, qz = vz - d.z * along;
        f64 perp = (qx * qx + qy * qy + qz * qz) / (along * along);
        // within about 20 degrees of the crosshair
        if (perp >= best || perp > 0.13) continue;
        i32 nx = 0, ny = 0, nz = 0;
        f64 ax = m_abs(vx), ay = m_abs(vy), az = m_abs(vz);
        if (ax >= ay && ax >= az) nx = vx > 0 ? -1 : 1;
        else if (ay >= az) ny = vy > 0 ? -1 : 1;
        else nz = vz > 0 ? -1 : 1;
        u8 open = vx_world_block(w, x + nx, y + ny, z + nz);
        if (vx_occludes[open] || vx_solid[open] == 1) continue;
        best = perp;
        out = (VxRayHit){.hit = true, .x = x, .y = y, .z = z, .nx = nx, .ny = ny, .nz = nz,
                         .dist = (f32)m_sqrt(vx * vx + vy * vy + vz * vz), .id = id};
      }
  return out;
}

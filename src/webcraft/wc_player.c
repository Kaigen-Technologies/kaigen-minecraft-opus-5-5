#include "webcraft/wc_game.h"

// first-person physics: aabb vs voxels, sprint, sneak edge guard, swim, creative flight

#define WC_HALF 0.3
#define WC_PLAYER_HEIGHT 1.8
#define WC_EYE 1.62f
#define WC_EYE_SNEAK 1.32f
#define WC_GRAVITY 30.0f
#define WC_JUMP_V 8.9f
#define WC_EPS 1e-4

void wc_player_init(WcPlayer *p) {
  *p = (WcPlayer){.pos = {0, 100, 0}, .sensitivity = 0.0022f, .last_space = -1, .last_w = -1, .eye_offset = WC_EYE};
}

WcV3d wc_player_eye(const WcPlayer *p) { return (WcV3d){p->pos.x, p->pos.y + p->eye_offset, p->pos.z}; }

WcV3d wc_player_camera(const WcPlayer *p, b32 bobbing) {
  WcV3d e = wc_player_eye(p);
  if (!bobbing || p->flying) return e;
  f32 b = p->bob_amount;
  f32 s = m_sinf(p->bob * 2) * 0.045f * b;
  f32 c = m_cosf(p->bob) * 0.03f * b;
  f32 rx = m_cosf(p->yaw), rz = -m_sinf(p->yaw);
  return (WcV3d){e.x + rx * c, e.y + m_absf(s) * 1.3f, e.z + rz * c};
}

v3 wc_player_forward(const WcPlayer *p) {
  f32 cp = m_cosf(p->pitch);
  return (v3){-m_sinf(p->yaw) * cp, m_sinf(p->pitch), -m_cosf(p->yaw) * cp};
}

void wc_player_look(WcPlayer *p, f32 dx, f32 dy) {
  p->yaw -= dx * p->sensitivity;
  p->pitch -= dy * p->sensitivity;
  f32 lim = PI / 2 - 0.001f;
  p->pitch = m_clampf(p->pitch, -lim, lim);
}

hz_internal b32 wc_solid_at(const WcWorld *w, i32 x, i32 y, i32 z) {
  if (y < 0) return true;
  if (y >= WC_HEIGHT) return false;
  return wc_solid[wc_world_block(w, x, y, z)] == 1;
}

hz_internal b32 wc_collides(const WcWorld *w, f64 px, f64 py, f64 pz) {
  i32 x0 = wc_floor_i(px - WC_HALF + WC_EPS), x1 = wc_floor_i(px + WC_HALF - WC_EPS);
  i32 y0 = wc_floor_i(py + WC_EPS), y1 = wc_floor_i(py + WC_PLAYER_HEIGHT - WC_EPS);
  i32 z0 = wc_floor_i(pz - WC_HALF + WC_EPS), z1 = wc_floor_i(pz + WC_HALF - WC_EPS);
  for (i32 y = y0; y <= y1; y++)
    for (i32 z = z0; z <= z1; z++)
      for (i32 x = x0; x <= x1; x++)
        if (wc_solid_at(w, x, y, z)) return true;
  return false;
}

force_inline f64 *wc_axis(WcV3d *v, u32 axis) { return axis == 0 ? &v->x : axis == 1 ? &v->y : &v->z; }

// moves along one axis, stopping at the first obstacle; true if blocked
hz_internal b32 wc_move_axis(WcPlayer *p, const WcWorld *w, u32 axis, f64 amount) {
  if (amount == 0) return false;
  f64 *pa = wc_axis(&p->pos, axis);
  f64 target = *pa + amount;
  WcV3d test = p->pos;
  *wc_axis(&test, axis) = target;
  if (!wc_collides(w, test.x, test.y, test.z)) {
    *pa = target;
    return false;
  }
  // resolve against the block grid
  if (axis == 1) {
    if (amount > 0) *pa = wc_floor_i(target + WC_PLAYER_HEIGHT - WC_EPS) - WC_PLAYER_HEIGHT - WC_EPS;
    else *pa = wc_floor_i(target + WC_EPS) + 1 + WC_EPS;
  } else {
    if (amount > 0) *pa = wc_floor_i(target + WC_HALF - WC_EPS) - WC_HALF - WC_EPS;
    else *pa = wc_floor_i(target - WC_HALF + WC_EPS) + 1 + WC_HALF + WC_EPS;
  }
  // fallback: undo the move entirely
  if (wc_collides(w, p->pos.x, p->pos.y, p->pos.z)) *pa = target - amount;
  return true;
}

hz_internal b32 wc_ground_below(const WcPlayer *p, const WcWorld *w, f64 px, f64 pz) {
  i32 y = wc_floor_i(p->pos.y - 0.05);
  i32 x0 = wc_floor_i(px - WC_HALF + WC_EPS), x1 = wc_floor_i(px + WC_HALF - WC_EPS);
  i32 z0 = wc_floor_i(pz - WC_HALF + WC_EPS), z1 = wc_floor_i(pz + WC_HALF - WC_EPS);
  for (i32 z = z0; z <= z1; z++)
    for (i32 x = x0; x <= x1; x++)
      if (wc_solid_at(w, x, y, z)) return true;
  return false;
}

force_inline f32 *wc_vaxis(v3 *v, u32 axis) { return axis == 0 ? &v->x : axis == 1 ? &v->y : &v->z; }

void wc_player_update(WcPlayer *p, f32 dt, const WcPlayerInput *in, const WcWorld *w, f32 now) {
  // toggles: double-tap space to fly, F to fly, double-tap W to sprint
  if (in->jump_pressed) {
    if (now - p->last_space < 0.3f) {
      p->flying = !p->flying;
      p->vel.y = 0;
      p->last_space = -1;
    } else {
      p->last_space = now;
    }
  }
  if (in->fly_pressed) {
    p->flying = !p->flying;
    p->vel.y = 0;
  }
  if (in->forward_pressed) {
    if (now - p->last_w < 0.3f) p->sprinting = true;
    p->last_w = now;
  }
  if (in->sprint_key) p->sprinting = true;
  if (!in->forward) p->sprinting = false;
  p->sneaking = !p->flying && in->sneak;
  if (p->sneaking) p->sprinting = false;

  f32 ix = 0, iz = 0;
  if (in->forward) iz -= 1;
  if (in->back) iz += 1;
  if (in->left) ix -= 1;
  if (in->right) ix += 1;
  f32 il = m_sqrtf(ix * ix + iz * iz);
  if (il > 0) {
    ix /= il;
    iz /= il;
  }
  f32 sy = m_sinf(p->yaw), cy = m_cosf(p->yaw);
  f32 wx = ix * cy + iz * sy;
  f32 wz = -ix * sy + iz * cy;

  i32 bx = wc_floor_i(p->pos.x), bz = wc_floor_i(p->pos.z);
  u8 feet = wc_world_block(w, bx, wc_floor_i(p->pos.y + 0.2), bz);
  u8 body = wc_world_block(w, bx, wc_floor_i(p->pos.y + 0.9), bz);
  p->in_water = feet == B_WATER || body == B_WATER || feet == B_LAVA || body == B_LAVA;
  f64 ey = p->pos.y + p->eye_offset;
  u8 eye_block = wc_world_block(w, bx, wc_floor_i(ey), bz);
  u8 above_eye = wc_world_block(w, bx, wc_floor_i(ey) + 1, bz);
  p->head_in_water = eye_block == B_WATER && (above_eye == B_WATER || ey - m_floor(ey) < 0.86);

  f32 speed;
  if (p->flying) speed = p->sprinting ? 21.0f : 10.9f;
  else if (p->in_water) speed = p->sprinting ? 3.2f : 2.3f;
  else if (p->sneaking) speed = 1.3f;
  else speed = p->sprinting ? 5.6f : 4.32f;

  v3 *v = &p->vel;
  f32 accel = p->flying ? 12.0f : p->on_ground ? 16.0f : p->in_water ? 6.0f : 3.2f;
  f32 blend = 1 - m_expf(-accel * dt);
  v->x += (wx * speed - v->x) * blend;
  v->z += (wz * speed - v->z) * blend;

  if (p->flying) {
    f32 vy = 0;
    if (in->jump) vy += speed * 0.75f;
    if (in->sneak) vy -= speed * 0.75f;
    v->y += (vy - v->y) * (1 - m_expf(-10 * dt));
  } else if (p->in_water) {
    v->y -= WC_GRAVITY * 0.25f * dt;
    v->y *= m_expf(-2.5f * dt);
    if (in->jump) v->y += (3.2f - v->y) * (1 - m_expf(-6 * dt));
    if (v->y < -4) v->y = -4;
  } else {
    v->y -= WC_GRAVITY * dt;
    if (v->y < -60) v->y = -60;
    if (in->jump && p->on_ground) {
      v->y = WC_JUMP_V;
      p->on_ground = false;
      if (p->sprinting) {
        v->x += -sy * 1.2f;
        v->z += -cy * 1.2f;
      }
    }
  }

  // integrate with substeps
  f32 vmax = m_maxf(m_absf(v->x), m_maxf(m_absf(v->y), m_absf(v->z)));
  i32 steps = (i32)m_maxf(1.0f, m_ceilf(vmax * dt / 0.35f));
  f32 h = dt / steps;
  b32 grounded = false;
  for (i32 i = 0; i < steps; i++) {
    if (wc_move_axis(p, w, 1, v->y * h)) {
      if (v->y < 0) grounded = true;
      v->y = 0;
    }
    for (u32 axis = 0; axis <= 2; axis += 2) {
      f32 *va = wc_vaxis(v, axis);
      f64 amt = *va * h;
      if (p->sneaking && p->on_ground && amt != 0) {
        f64 nx = axis == 0 ? p->pos.x + amt : p->pos.x;
        f64 nz = axis == 2 ? p->pos.z + amt : p->pos.z;
        if (!wc_ground_below(p, w, nx, nz)) {
          *va = 0;
          continue;
        }
      }
      if (wc_move_axis(p, w, axis, amt)) {
        *va = 0;
        p->sprinting = false;
      }
    }
  }
  p->on_ground = grounded || (v->y == 0 && wc_collides(w, p->pos.x, p->pos.y - 0.02, p->pos.z));
  // land when descending onto the ground, not merely when standing there
  if (p->flying && grounded && !in->jump) p->flying = false;

  f32 target_eye = p->sneaking ? WC_EYE_SNEAK : WC_EYE;
  p->eye_offset += (target_eye - p->eye_offset) * (1 - m_expf(-14 * dt));

  // view bobbing and sprint fov
  f32 hs = m_sqrtf(v->x * v->x + v->z * v->z);
  f32 moving = p->on_ground && hs > 0.5f ? m_minf(1.0f, hs / 5.0f) : 0.0f;
  p->bob_amount += (moving - p->bob_amount) * (1 - m_expf(-8 * dt));
  p->bob += hs * dt * 1.9f;
  f32 fov_target = p->sprinting ? (p->flying ? 0.15f : 0.1f) : 0.0f;
  p->fov_boost += (fov_target - p->fov_boost) * (1 - m_expf(-8 * dt));

  if (p->pos.y < -40) {
    p->pos.y = 200;
    p->vel = (v3){0, 0, 0};
  }
}

b32 wc_player_intersects_block(const WcPlayer *p, i32 x, i32 y, i32 z) {
  WcV3d q = p->pos;
  return q.x + WC_HALF > x && q.x - WC_HALF < x + 1 && q.y + WC_PLAYER_HEIGHT > y && q.y < y + 1 &&
         q.z + WC_HALF > z && q.z - WC_HALF < z + 1;
}

WcRayHit wc_raycast(const WcWorld *w, WcV3d o, v3 d, f32 max_dist) {
  i32 x = wc_floor_i(o.x), y = wc_floor_i(o.y), z = wc_floor_i(o.z);
  i32 step_x = d.x > 0 ? 1 : -1, step_y = d.y > 0 ? 1 : -1, step_z = d.z > 0 ? 1 : -1;
  const f64 inf = 1e30;
  f64 tdx = d.x != 0 ? m_abs(1.0 / d.x) : inf;
  f64 tdy = d.y != 0 ? m_abs(1.0 / d.y) : inf;
  f64 tdz = d.z != 0 ? m_abs(1.0 / d.z) : inf;
  f64 tmx = d.x != 0 ? (d.x > 0 ? x + 1 - o.x : o.x - x) * tdx : inf;
  f64 tmy = d.y != 0 ? (d.y > 0 ? y + 1 - o.y : o.y - y) * tdy : inf;
  f64 tmz = d.z != 0 ? (d.z > 0 ? z + 1 - o.z : o.z - z) * tdz : inf;
  i32 nx = 0, ny = 0, nz = 0;
  f64 t = 0;
  for (i32 i = 0; i < 256; i++) {
    u8 id = wc_world_block(w, x, y, z);
    if (id != B_AIR && wc_shape[id] != WC_SHAPE_LIQUID)
      return (WcRayHit){.hit = true, .x = x, .y = y, .z = z, .nx = nx, .ny = ny, .nz = nz, .dist = (f32)t, .id = id};
    if (tmx < tmy && tmx < tmz) {
      x += step_x;
      t = tmx;
      tmx += tdx;
      nx = -step_x;
      ny = 0;
      nz = 0;
    } else if (tmy < tmz) {
      y += step_y;
      t = tmy;
      tmy += tdy;
      nx = 0;
      ny = -step_y;
      nz = 0;
    } else {
      z += step_z;
      t = tmz;
      tmz += tdz;
      nx = 0;
      ny = 0;
      nz = -step_z;
    }
    if (t > max_dist) return (WcRayHit){0};
  }
  return (WcRayHit){0};
}

#undef WC_HALF
#undef WC_EPS

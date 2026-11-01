#include "voxaria/vx_game.h"

// terraria-scale platforming in 3d: aabb vs voxels, step-up, variable jump, coyote time, swim, double jump, wings, hook

#define VX_HALF 0.55
#define VX_EPS 1e-4
#define VX_EYE 2.35f
#define VX_GRAVITY 72.0f
#define VX_MAX_FALL 40.0f
#define VX_RUN 8.5f
#define VX_JUMP_V 16.0f
#define VX_JUMP_HOLD 0.28f // seconds a held jump keeps rising at reduced gravity
#define VX_DOUBLE_V 15.0f
#define VX_COYOTE 0.1f
#define VX_JUMP_BUFFER 0.12f
#define VX_WING_LIFT 9.0f
#define VX_GLIDE_FALL 3.5f
#define VX_HOOK_SPEED 42.0f
#define VX_HOOK_RANGE 18.0f
#define VX_HOOK_PULL 17.0f
#define VX_HOOK_HANG 1.6f
#define VX_HAND_Y 1.6

void vx_player_init(VxPlayer *p) {
  *p = (VxPlayer){.pos = {0, 100, 0}, .sensitivity = 0.0022f, .eye_offset = VX_EYE, .run_mult = 1};
}

VxV3d vx_player_eye(const VxPlayer *p) {
  return (VxV3d){p->pos.x, p->pos.y + p->step_smooth + p->eye_offset, p->pos.z};
}

VxV3d vx_player_camera(const VxPlayer *p, b32 bobbing) {
  VxV3d e = vx_player_eye(p);
  if (!bobbing || p->flying) return e;
  f32 b = p->bob_amount;
  f32 s = m_sinf(p->bob * 2) * 0.05f * b;
  f32 c = m_cosf(p->bob) * 0.035f * b;
  f32 rx = m_cosf(p->yaw), rz = -m_sinf(p->yaw);
  return (VxV3d){e.x + rx * c, e.y + m_absf(s) * 1.3f, e.z + rz * c};
}

v3 vx_player_forward(const VxPlayer *p) {
  f32 cp = m_cosf(p->pitch);
  return (v3){-m_sinf(p->yaw) * cp, m_sinf(p->pitch), -m_cosf(p->yaw) * cp};
}

void vx_player_look(VxPlayer *p, f32 dx, f32 dy) {
  p->yaw -= dx * p->sensitivity;
  p->pitch -= dy * p->sensitivity;
  f32 lim = PI / 2 - 0.001f;
  p->pitch = m_clampf(p->pitch, -lim, lim);
}

hz_internal b32 vx_solid_at(const VxWorld *w, i32 x, i32 y, i32 z) {
  if (y < 0) return true;
  if (y >= VX_HEIGHT) return false;
  // the world's edge is an invisible wall, as in Terraria
  if (!vx_in_world(x, z)) return true;
  return vx_solid[vx_world_block(w, x, y, z)] == 1;
}

b32 vx_aabb_collides(const VxWorld *w, f64 px, f64 py, f64 pz, f64 half, f64 height) {
  i32 x0 = vx_floor_i(px - half + VX_EPS), x1 = vx_floor_i(px + half - VX_EPS);
  i32 y0 = vx_floor_i(py + VX_EPS), y1 = vx_floor_i(py + height - VX_EPS);
  i32 z0 = vx_floor_i(pz - half + VX_EPS), z1 = vx_floor_i(pz + half - VX_EPS);
  for (i32 y = y0; y <= y1; y++)
    for (i32 z = z0; z <= z1; z++)
      for (i32 x = x0; x <= x1; x++)
        if (vx_solid_at(w, x, y, z)) return true;
  return false;
}

force_inline b32 vx_collides(const VxWorld *w, f64 px, f64 py, f64 pz) {
  return vx_aabb_collides(w, px, py, pz, VX_HALF, VX_PLAYER_HEIGHT);
}

// the top of the highest platform the feet cross going from y0 down to y1, or -1: platforms only stop a fall from above
hz_internal f64 vx_platform_top(const VxWorld *w, f64 px, f64 pz, f64 y0, f64 y1) {
  i32 x0 = vx_floor_i(px - VX_HALF + VX_EPS), x1 = vx_floor_i(px + VX_HALF - VX_EPS);
  i32 z0 = vx_floor_i(pz - VX_HALF + VX_EPS), z1 = vx_floor_i(pz + VX_HALF - VX_EPS);
  for (i32 y = vx_floor_i(y0 - 1 + 1e-3); y >= vx_floor_i(y1 - 1); y--) {
    f64 top = y + 1;
    if (top > y0 + 1e-3 || top <= y1) continue;
    for (i32 z = z0; z <= z1; z++)
      for (i32 x = x0; x <= x1; x++)
        if (vx_world_block(w, x, y, z) == B_PLATFORM) return top;
  }
  return -1;
}

force_inline f64 *vx_axis(VxV3d *v, u32 axis) { return axis == 0 ? &v->x : axis == 1 ? &v->y : &v->z; }
force_inline f32 *vx_vaxis(v3 *v, u32 axis) { return axis == 0 ? &v->x : axis == 1 ? &v->y : &v->z; }

// moves along one axis, stopping at the first obstacle; true if blocked
hz_internal b32 vx_move_axis(VxPlayer *p, const VxWorld *w, u32 axis, f64 amount) {
  if (amount == 0) return false;
  f64 *pa = vx_axis(&p->pos, axis);
  f64 target = *pa + amount;
  VxV3d test = p->pos;
  *vx_axis(&test, axis) = target;
  if (!vx_collides(w, test.x, test.y, test.z)) {
    *pa = target;
    return false;
  }
  // resolve against the block grid
  if (axis == 1) {
    if (amount > 0) *pa = vx_floor_i(target + VX_PLAYER_HEIGHT - VX_EPS) - VX_PLAYER_HEIGHT - VX_EPS;
    else *pa = vx_floor_i(target + VX_EPS) + 1 + VX_EPS;
  } else {
    if (amount > 0) *pa = vx_floor_i(target + VX_HALF - VX_EPS) - VX_HALF - VX_EPS;
    else *pa = vx_floor_i(target - VX_HALF + VX_EPS) + 1 + VX_HALF + VX_EPS;
  }
  // fallback: undo the move entirely
  if (vx_collides(w, p->pos.x, p->pos.y, p->pos.z)) *pa = target - amount;
  return true;
}

// a horizontal move blocked by a one-block ledge climbs it, as Terraria walks up single tiles
hz_internal b32 vx_step_up(VxPlayer *p, const VxWorld *w, u32 axis, f64 amount) {
  f64 up = vx_floor_i(p->pos.y + VX_EPS) + 1 + VX_EPS - p->pos.y;
  if (up > 1.0 + VX_EPS || up <= 0) return false;
  VxV3d test = p->pos;
  test.y += up;
  if (vx_collides(w, test.x, test.y, test.z)) return false;
  *vx_axis(&test, axis) += amount;
  if (vx_collides(w, test.x, test.y, test.z)) return false;
  p->pos = test;
  p->step_smooth -= (f32)up;
  p->events |= VX_PEV_STEP_UP;
  return true;
}

hz_internal void vx_hook_update(VxPlayer *p, const VxPlayerInput *in, const VxWorld *w, f32 dt) {
  VxV3d hand = {p->pos.x, p->pos.y + VX_HAND_Y, p->pos.z};
  if (in->hook_pressed && p->has_hook) {
    if (p->hook_state != VX_HOOK_NONE) {
      p->hook_state = VX_HOOK_NONE;
    } else {
      p->hook_state = VX_HOOK_FLYING;
      p->hook_pos = hand;
      p->hook_vel = v3_scale(in->aim, VX_HOOK_SPEED);
      p->events |= VX_PEV_HOOK_FIRE;
    }
  }
  if (p->hook_state == VX_HOOK_FLYING) {
    i32 steps = (i32)m_ceilf(VX_HOOK_SPEED * dt / 0.25f);
    for (i32 i = 0; i < steps && p->hook_state == VX_HOOK_FLYING; i++) {
      f32 h = dt / steps;
      VxV3d n = {p->hook_pos.x + p->hook_vel.x * h, p->hook_pos.y + p->hook_vel.y * h, p->hook_pos.z + p->hook_vel.z * h};
      if (vx_solid_at(w, vx_floor_i(n.x), vx_floor_i(n.y), vx_floor_i(n.z))) {
        // the claw sits just inside the block it bit
        p->hook_state = VX_HOOK_ATTACHED;
        p->hook_pos = n;
        p->events |= VX_PEV_HOOK_HIT;
        f64 dx = p->hook_pos.x - hand.x, dy = p->hook_pos.y - hand.y, dz = p->hook_pos.z - hand.z;
        p->hook_len = (f32)m_sqrt(dx * dx + dy * dy + dz * dz);
      } else {
        p->hook_pos = n;
      }
    }
    f64 dx = p->hook_pos.x - hand.x, dy = p->hook_pos.y - hand.y, dz = p->hook_pos.z - hand.z;
    if (dx * dx + dy * dy + dz * dz > VX_HOOK_RANGE * VX_HOOK_RANGE) p->hook_state = VX_HOOK_NONE;
  }
  // the hook lets go when its block is mined away
  if (p->hook_state == VX_HOOK_ATTACHED &&
      !vx_solid_at(w, vx_floor_i(p->hook_pos.x), vx_floor_i(p->hook_pos.y), vx_floor_i(p->hook_pos.z)))
    p->hook_state = VX_HOOK_NONE;
}

void vx_player_update(VxPlayer *p, f32 dt, const VxPlayerInput *in, const VxWorld *w, f32 now) {
  UNUSED(now);
  p->events = 0;
  p->landed_fall = 0;
  if (in->fly_pressed) {
    p->flying = !p->flying;
    p->vel.y = 0;
    p->hook_state = VX_HOOK_NONE;
  }

  // move input relative to the camera's yaw
  f32 ix = 0, iz = 0;
  if (in->forward) iz -= 1;
  if (in->back) iz += 1;
  if (in->left) ix -= 1;
  if (in->right) ix += 1;
  f32 il = m_sqrtf(ix * ix + iz * iz);
  if (il > 0) ix /= il, iz /= il;
  // the touch stick is analog: a half push runs at half speed
  ix += in->stick_x;
  iz += in->stick_z;
  il = m_sqrtf(ix * ix + iz * iz);
  if (il > 1) ix /= il, iz /= il;
  f32 sy = m_sinf(p->yaw), cy = m_cosf(p->yaw);
  f32 wx = ix * cy + iz * sy;
  f32 wz = -ix * sy + iz * cy;

  i32 bx = vx_floor_i(p->pos.x), bz = vx_floor_i(p->pos.z);
  u8 feet = vx_world_block(w, bx, vx_floor_i(p->pos.y + 0.3), bz);
  u8 body = vx_world_block(w, bx, vx_floor_i(p->pos.y + 1.3), bz);
  b32 was_in_water = p->in_water;
  p->in_water = vx_shape[feet] == VX_SHAPE_LIQUID || vx_shape[body] == VX_SHAPE_LIQUID;
  p->in_lava = feet == B_LAVA || body == B_LAVA;
  if (p->in_water && !was_in_water && p->vel.y < -3) p->events |= VX_PEV_SPLASH;
  f64 ey = p->pos.y + p->eye_offset;
  u8 eye_block = vx_world_block(w, bx, vx_floor_i(ey), bz);
  u8 above_eye = vx_world_block(w, bx, vx_floor_i(ey) + 1, bz);
  p->head_in_water = eye_block == B_WATER && (above_eye == B_WATER || ey - m_floor(ey) < 0.86);

  if (in->jump_pressed) p->jump_buffer = VX_JUMP_BUFFER;
  else p->jump_buffer = m_maxf(0, p->jump_buffer - dt);
  vx_hook_update(p, in, w, dt);

  v3 *v = &p->vel;
  // on the wing (flapping or gliding last step) the player flies faster and steers harder than a plain jump, as Terraria's wings
  b32 winged = !p->on_ground && (p->flapping || p->gliding);
  f32 speed = p->flying ? 18.0f : (p->in_water ? VX_RUN * 0.55f : VX_RUN) * p->run_mult * (winged ? 1.3f : 1.0f);
  f32 accel = p->flying ? 12.0f : p->on_ground ? (il > 0 ? 10.0f : 14.0f) : p->in_water ? 5.0f : winged ? 7.0f : 4.5f;
  if (p->knock_t > 0) {
    p->knock_t -= dt;
    accel = 1.5f;
  }
  f32 blend = 1 - m_expf(-accel * dt);
  v->x += (wx * speed - v->x) * blend;
  v->z += (wz * speed - v->z) * blend;

  if (p->flying) {
    f32 vy = 0;
    if (in->jump) vy += speed * 0.75f;
    if (in->down) vy -= speed * 0.75f;
    v->y += (vy - v->y) * (1 - m_expf(-10 * dt));
  } else if (p->hook_state == VX_HOOK_ATTACHED) {
    // a rope that reels in: gravity and steering swing the player under the hook, the rope only pulls
    p->hook_len = m_maxf(VX_HOOK_HANG, p->hook_len - VX_HOOK_PULL * dt);
    v->y -= VX_GRAVITY * dt;
    VxV3d hand = {p->pos.x, p->pos.y + VX_HAND_Y, p->pos.z};
    v3 d = {(f32)(hand.x + v->x * dt - p->hook_pos.x), (f32)(hand.y + v->y * dt - p->hook_pos.y),
            (f32)(hand.z + v->z * dt - p->hook_pos.z)};
    f32 len = v3_length(d);
    if (len > p->hook_len && len > 1e-4f) {
      v3 on = v3_scale(d, p->hook_len / len);
      v3 to = {(f32)(p->hook_pos.x + on.x - hand.x), (f32)(p->hook_pos.y + on.y - hand.y),
               (f32)(p->hook_pos.z + on.z - hand.z)};
      *v = v3_scale(to, 1.0f / dt);
    }
    // swinging bleeds a little energy, as a rope does
    *v = v3_scale(*v, m_expf(-0.4f * dt));
    p->double_used = false;
    p->wing_time = p->wing_time_max;
    if (in->jump_pressed) {
      p->hook_state = VX_HOOK_NONE;
      v->y = VX_JUMP_V * 0.8f;
      p->jump_hold = VX_JUMP_HOLD * 0.5f;
      p->jumping = true;
      p->jump_buffer = 0;
      p->events |= VX_PEV_JUMP;
    }
  } else if (p->in_water) {
    v->y -= VX_GRAVITY * 0.18f * dt;
    v->y *= m_expf(-3.0f * dt);
    if (in->jump) v->y += (5.5f - v->y) * (1 - m_expf(-6 * dt));
    if (v->y < -5) v->y = -5;
    p->jumping = false;
  } else {
    b32 can_ground_jump = p->on_ground || p->coyote > 0;
    if (p->jump_buffer > 0 && can_ground_jump) {
      if (!p->on_ground) p->events |= VX_PEV_COYOTE;
      v->y = VX_JUMP_V;
      p->jump_hold = VX_JUMP_HOLD;
      p->jumping = true;
      p->on_ground = false;
      p->coyote = 0;
      p->jump_buffer = 0;
      p->events |= VX_PEV_JUMP;
    } else if (in->jump_pressed && !p->on_ground && p->has_double_jump && !p->double_used) {
      v->y = VX_DOUBLE_V;
      p->jump_hold = VX_JUMP_HOLD * 0.6f;
      p->jumping = true;
      p->double_used = true;
      p->jump_buffer = 0;
      p->events |= VX_PEV_DOUBLE_JUMP;
    }
    if (!in->jump) p->jumping = false;
    f32 g = VX_GRAVITY;
    if (p->jumping && p->jump_hold > 0) {
      g *= 0.2f;
      p->jump_hold -= dt;
    }
    // wings lift while jump is held after the jump's rise, then glide on what is left of the fall
    b32 rising = p->jumping && p->jump_hold > 0;
    p->flapping = false;
    p->gliding = false;
    if (!p->on_ground && in->jump && !rising && p->wing_time_max > 0 && (!p->has_double_jump || p->double_used)) {
      if (p->wing_time > 0) {
        v->y += (VX_WING_LIFT - v->y) * (1 - m_expf(-7 * dt));
        p->wing_time -= dt;
        p->flapping = true;
        g = 0;
      } else if (v->y < -VX_GLIDE_FALL) {
        v->y = -VX_GLIDE_FALL;
        p->gliding = true;
      } else {
        p->gliding = v->y < 0;
      }
    }
    v->y -= g * dt;
    if (v->y < -VX_MAX_FALL) v->y = -VX_MAX_FALL;
  }

  // integrate with substeps
  f32 vmax = m_maxf(m_absf(v->x), m_maxf(m_absf(v->y), m_absf(v->z)));
  i32 steps = (i32)m_maxf(1.0f, m_ceilf(vmax * dt / 0.35f));
  f32 h = dt / steps;
  b32 grounded = false;
  // holding down drops through platforms, as in Terraria
  b32 platforms = !in->down && !p->flying;
  for (i32 i = 0; i < steps; i++) {
    f64 feet = p->pos.y;
    if (vx_move_axis(p, w, 1, v->y * h)) {
      if (v->y < 0) grounded = true;
      else p->jump_hold = 0;
      v->y = 0;
    } else if (platforms && v->y < 0) {
      f64 top = vx_platform_top(w, p->pos.x, p->pos.z, feet, p->pos.y);
      if (top >= 0) {
        p->pos.y = top + VX_EPS;
        grounded = true;
        v->y = 0;
      }
    }
    for (u32 axis = 0; axis <= 2; axis += 2) {
      f32 *va = vx_vaxis(v, axis);
      f64 amt = *va * h;
      VxV3d before = p->pos;
      if (!vx_move_axis(p, w, axis, amt)) continue;
      // blocked: a grounded walker climbs a single-block ledge instead
      p->pos = before;
      if ((p->on_ground || grounded) && !p->flying && vx_step_up(p, w, axis, amt)) continue;
      vx_move_axis(p, w, axis, amt);
      *va = 0;
    }
  }
  b32 was_ground = p->on_ground;
  p->on_ground = grounded || (v->y <= 0 && (vx_collides(w, p->pos.x, p->pos.y - 0.02, p->pos.z) ||
                                             (platforms && vx_platform_top(w, p->pos.x, p->pos.z, p->pos.y,
                                                                           p->pos.y - 0.02) >= 0)));
  if (p->flying && grounded && !in->jump) p->flying = false;

  // falls are measured from their peak; water, wings, hook and flight cancel them
  if (p->on_ground) {
    if (!was_ground) {
      p->landed_fall = (f32)(p->fall_peak - p->pos.y);
      p->events |= VX_PEV_LAND;
    }
    p->fall_peak = p->pos.y;
    p->coyote = VX_COYOTE;
    p->double_used = false;
    p->wing_time = p->wing_time_max;
  } else {
    p->coyote = m_maxf(0, p->coyote - dt);
    if (p->in_water || p->flying || p->flapping || p->gliding || p->hook_state == VX_HOOK_ATTACHED) p->fall_peak = p->pos.y;
    else p->fall_peak = m_max(p->fall_peak, p->pos.y);
  }

  p->eye_offset = VX_EYE;
  p->step_smooth *= m_expf(-16 * dt);

  // the body turns toward where it runs; bobbing follows the stride
  f32 hs = m_sqrtf(v->x * v->x + v->z * v->z);
  if (hs > 0.5f) {
    f32 target = m_atan2f(-v->x, -v->z);
    f32 d = m_fmodf(target - p->body_yaw + PI * 3, PI * 2) - PI;
    p->body_yaw += d * (1 - m_expf(-14 * dt));
  }
  f32 moving = p->on_ground && hs > 0.5f ? m_minf(1.0f, hs / VX_RUN) : 0.0f;
  p->bob_amount += (moving - p->bob_amount) * (1 - m_expf(-8 * dt));
  p->bob += hs * dt * 1.2f;
  p->stride += hs * dt;
  f32 fov_target = p->flying ? 0.12f : m_minf(0.06f, hs * 0.005f);
  p->fov_boost += (fov_target - p->fov_boost) * (1 - m_expf(-8 * dt));

  if (p->pos.y < -40) {
    p->pos.y = 200;
    p->vel = (v3){0, 0, 0};
    p->fall_peak = p->pos.y;
  }
}

b32 vx_player_intersects_block(const VxPlayer *p, i32 x, i32 y, i32 z) {
  VxV3d q = p->pos;
  return q.x + VX_HALF > x && q.x - VX_HALF < x + 1 && q.y + VX_PLAYER_HEIGHT > y && q.y < y + 1 &&
         q.z + VX_HALF > z && q.z - VX_HALF < z + 1;
}

VxRayHit vx_raycast(const VxWorld *w, VxV3d o, v3 d, f32 max_dist) {
  i32 x = vx_floor_i(o.x), y = vx_floor_i(o.y), z = vx_floor_i(o.z);
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
    u8 id = vx_world_block(w, x, y, z);
    if (id != B_AIR && vx_shape[id] != VX_SHAPE_LIQUID)
      return (VxRayHit){.hit = true, .x = x, .y = y, .z = z, .nx = nx, .ny = ny, .nz = nz, .dist = (f32)t, .id = id};
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
    if (t > max_dist) return (VxRayHit){0};
  }
  return (VxRayHit){0};
}

#undef VX_HALF
#undef VX_EPS

#include "voxaria/vx_game.h"

// impact feedback in the manner of trauma shake: events add trauma, the view shakes by its square, and it bleeds away

#define VX_TRAUMA_DECAY 1.6f // per second: a full jolt settles in 0.6 s
#define VX_HITSTOP_RATE 0.06f // the simulation's speed during a hit-stop

void vx_shake(VxGame *g, f32 amount) {
  VxJuice *j = &g->juice;
  j->trauma = m_minf(1, j->trauma + amount);
  if (g->scripted && amount >= 0.2f) log_info("juice: shake +% (trauma %)", fmt_f32_p(amount, 2), fmt_f32_p(j->trauma, 2));
}

void vx_hitstop(VxGame *g, f32 seconds) {
  VxJuice *j = &g->juice;
  if (seconds <= j->hitstop) return;
  j->hitstop = seconds;
  if (g->scripted && seconds >= 0.05f) log_info("juice: hit-stop % ms", fmt_f32_p(seconds * 1000, 0));
}

f32 vx_juice_update(VxGame *g, f32 dt) {
  VxJuice *j = &g->juice;
  j->t += dt;
  j->trauma = m_maxf(0, j->trauma - VX_TRAUMA_DECAY * dt);
  if (j->hitstop <= 0) return dt;
  j->hitstop -= dt;
  return dt * VX_HITSTOP_RATE;
}

// smooth noise in -1..1: three incommensurate sines, a different phase per axis
hz_internal f32 vx_juice_noise(f32 t, f32 axis) {
  return m_sinf(t * 23.1f + axis * 1.7f) * 0.55f + m_sinf(t * 37.3f + axis * 4.1f) * 0.3f +
         m_sinf(t * 11.7f + axis * 2.9f) * 0.15f;
}

void vx_juice_view(const VxGame *g, VxV3d *pos, f32 *yaw, f32 *pitch, b32 first) {
  const VxJuice *j = &g->juice;
  if (!g->settings.screen_shake || j->trauma <= 0) return;
  f32 k = j->trauma * j->trauma;
  // in first person the eye barely moves, so a jolt cannot push it into a wall
  f32 move = (first ? 0.06f : 0.28f) * k;
  f32 cy = m_cosf(*yaw), sy = m_sinf(*yaw);
  f32 dx = vx_juice_noise(j->t, 0) * move, dy = vx_juice_noise(j->t, 1) * move;
  pos->x += cy * dx;
  pos->z -= sy * dx;
  pos->y += dy;
  *yaw += vx_juice_noise(j->t, 2) * 0.03f * k;
  *pitch += vx_juice_noise(j->t, 3) * 0.03f * k;
}

#undef VX_TRAUMA_DECAY
#undef VX_HITSTOP_RATE

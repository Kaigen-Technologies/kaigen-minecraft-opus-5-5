#include "voxaria/vx_game.h"

// life, mana and breath, the damage that takes them, death and the respawn at the world spawn

#define VX_IMMUNE_TIME 0.67f   // Terraria's 40 ticks of invincibility after a hit
#define VX_FALL_SAFE 25.0f     // blocks a fall may cover before it hurts
#define VX_FALL_DAMAGE 10.0f   // per block past that
#define VX_LAVA_DAMAGE 50.0f
#define VX_BREATH_TIME 20.0f
#define VX_DROWN_RATE 15.0f    // life per second once out of breath

void vx_vitals_reset(VxVitals *v) {
  *v = (VxVitals){.hp = 100, .hp_max = 100, .mana = 20, .mana_max = 20, .breath = VX_BREATH_TIME,
                  .breath_max = VX_BREATH_TIME};
}

hz_internal void vx_vitals_die(VxGame *g, const char *cause) {
  VxVitals *v = &g->vitals;
  v->hp = 0;
  v->dead = true;
  v->death_t = 0;
  v->death_msg = cause;
  VxPlayer *p = &g->player;
  p->hook_state = VX_HOOK_NONE;
  p->vel = (v3){0, 0, 0};
  log_info("vitals: % (respawn in % s)", fmt_cstr(cause), fmt_f32_p(VX_RESPAWN_TIME, 0));
  vx_audio_play(&g->audio, VX_SND_DEATH);
  Allocator ta = tctx_temp_allocator(NULL);
  vx_event(g, str_format(&ta, "%", fmt_cstr(cause)));
}

b32 vx_vitals_damage(VxGame *g, f32 amount, const char *cause) {
  VxVitals *v = &g->vitals;
  if (v->dead || v->immune > 0 || amount <= 0) return false;
  v->hp -= amount;
  v->immune = VX_IMMUNE_TIME;
  v->since_hurt = 0;
  v->hurt_flash = 1;
  v->last_damage = amount;
  v->hits++;
  if (g->scripted)
    log_info("vitals: % damage (%), life % / %", fmt_f32_p(amount, 0), fmt_cstr(cause), fmt_f32_p(m_maxf(v->hp, 0), 0),
             fmt_f32_p(v->hp_max, 0));
  vx_shake(g, m_minf(0.25f + amount / 60.0f, 0.6f));
  vx_hitstop(g, 0.04f);
  if (v->hp <= 0) vx_vitals_die(g, cause);
  else vx_audio_play(&g->audio, VX_SND_HURT);
  return true;
}

hz_internal void vx_vitals_respawn(VxGame *g) {
  VxVitals *v = &g->vitals;
  VxPlayer *p = &g->player;
  p->pos = g->spawn_pos;
  p->vel = (v3){0, 0, 0};
  p->fall_peak = p->pos.y;
  p->flying = false;
  f32 hp_max = v->hp_max, mana_max = v->mana_max;
  vx_vitals_reset(v);
  v->hp = v->hp_max = hp_max;
  v->mana = v->mana_max = mana_max;
  v->immune = VX_IMMUNE_TIME * 3;
  vx_render_invalidate_history(&g->renderer);
  log_info("vitals: respawned at % % %", fmt_f64_p(p->pos.x, 1), fmt_f64_p(p->pos.y, 1), fmt_f64_p(p->pos.z, 1));
}

void vx_vitals_update(VxGame *g, f32 dt) {
  VxVitals *v = &g->vitals;
  VxPlayer *p = &g->player;
  v->hurt_flash = m_maxf(0, v->hurt_flash - dt * 2.5f);
  if (v->dead) {
    v->death_t += dt;
    if (v->death_t >= VX_RESPAWN_TIME) vx_vitals_respawn(g);
    return;
  }
  v->immune = m_maxf(0, v->immune - dt);
  v->since_hurt += dt;

  // falls are measured from their peak; landing in water is safe
  if ((p->events & VX_PEV_LAND) && !p->in_water && p->landed_fall > VX_FALL_SAFE)
    vx_vitals_damage(g, m_floorf((p->landed_fall - VX_FALL_SAFE) * VX_FALL_DAMAGE), "You fell to your death.");
  if (p->in_lava) vx_vitals_damage(g, VX_LAVA_DAMAGE, "You tried to swim in lava.");
  if (v->dead) return;

  // breath runs out under water, then life drains past the hit immunity
  if (p->head_in_water) {
    v->breath = m_maxf(0, v->breath - dt);
    if (v->breath <= 0) {
      v->hp -= VX_DROWN_RATE * dt;
      v->since_hurt = 0;
      if (v->hp <= 0) {
        vx_vitals_die(g, "You drowned.");
        return;
      }
    }
  } else {
    v->breath = m_minf(v->breath_max, v->breath + dt * v->breath_max / 2.0f);
  }

  // natural regeneration grows the longer the player goes unhurt, faster when standing still
  if (v->since_hurt > 3.0f && v->hp < v->hp_max) {
    f32 still = m_absf(p->vel.x) + m_absf(p->vel.z) < 0.3f ? 1.5f : 1.0f;
    f32 rate = m_minf(4.0f, 0.5f + (v->since_hurt - 3.0f) * 0.25f) * still;
    v->hp = m_minf(v->hp_max, v->hp + rate * dt);
  }
  v->mana_idle += dt;
  if (v->mana_idle > 1.0f) v->mana = m_minf(v->mana_max, v->mana + (v->mana_max * 0.1f + 2.0f) * dt);
}

b32 vx_vitals_use_mana(VxVitals *v, f32 amount) {
  if (v->mana < amount) return false;
  v->mana -= amount;
  v->mana_idle = 0;
  return true;
}

#undef VX_IMMUNE_TIME
#undef VX_FALL_SAFE
#undef VX_FALL_DAMAGE
#undef VX_LAVA_DAMAGE
#undef VX_BREATH_TIME
#undef VX_DROWN_RATE

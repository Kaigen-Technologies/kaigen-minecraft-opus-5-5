#include "voxaria/vx_entity_geo.h"

// melee arcs, arrows and sparks: hits on creatures, trails and impacts

#define VX_CHEST_Y 1.45
#define VX_CRIT_CHANCE 0.04f
#define VX_ARROW_SPEED 30.0f
#define VX_ARROW_GRAVITY 16.0f
#define VX_SPARK_SPEED 28.0f
#define VX_BEAM_SPEED 24.0f
#define VX_FROST_SPEED 21.0f

hz_internal u32 vx_cb_rgba(u32 rgb, f32 a) { return ((rgb >> 16) & 255) | (rgb & 0xFF00) | ((rgb & 255) << 16) | (u32)(a * 255.0f) << 24; }
// an srgb 0xRRGGBB as linear rgb, for self-lit boxes
hz_internal v3 vx_cb_lin(u32 rgb) {
  v3 c = {(f32)((rgb >> 16) & 255) / 255.0f, (f32)((rgb >> 8) & 255) / 255.0f, (f32)(rgb & 255) / 255.0f};
  return (v3){c.x * c.x, c.y * c.y, c.z * c.z};
}

void vx_combat_init(VxCombat *c, Allocator *alloc, u64 seed) {
  *c = (VxCombat){.proj = ALLOC_ARRAY(alloc, VxProjectile, VX_MAX_PROJECTILES), .rng = random_create(seed ^ 0xB1ADEu)};
}

// a blade's glow by its metal
hz_internal u32 vx_cb_trail_tint(const VxItemInfo *it) {
  switch (it->mat) {
  case VX_IM_COPPER: return vx_cb_rgba(0xFFA060, 0.9f);
  case VX_IM_TIN: return vx_cb_rgba(0xD8D0B0, 0.9f);
  case VX_IM_IRON: return vx_cb_rgba(0xD0DCF0, 0.9f);
  case VX_IM_SILVER: return vx_cb_rgba(0xE8F2FF, 0.9f);
  case VX_IM_GOLD: return vx_cb_rgba(0xFFE070, 0.9f);
  case VX_IM_BLUE: return vx_cb_rgba(0x8FE4FF, 0.9f);
  case VX_IM_WHITE: return vx_cb_rgba(0x9CF0FF, 0.9f);
  case VX_IM_PURPLE: return vx_cb_rgba(0xA050FF, 0.9f);
  default: return vx_cb_rgba(0xFFFFFF, 0.9f);
  }
}

hz_internal VxV3d vx_cb_chest(const VxPlayer *p) { return (VxV3d){p->pos.x, p->pos.y + VX_CHEST_Y, p->pos.z}; }
hz_internal VxV3d vx_cb_aim_point(const VxGame *g);
hz_internal VxV3d vx_cb_hand(const VxGame *g, v3 a, f32 reach);

void vx_combat_swing(VxGame *g, const VxItemInfo *weapon, f32 dur) {
  VxCombat *c = &g->combat;
  c->swing_id++;
  c->t = c->dur = dur;
  c->damage = weapon->damage;
  c->weapon = weapon->name;
  // broadswords reach farther and push harder than shortswords
  b32 broad = weapon->art == VX_ART_BROADSWORD;
  c->reach = broad ? 3.3f : 2.5f;
  c->knock = broad ? 8.0f : 5.5f;
  c->trail_tint = vx_cb_trail_tint(weapon);
  c->tip_valid = false;
  c->stab = weapon->art == VX_ART_SHORTSWORD;
  if (c->stab) c->reach = 2.7f;
  c->glint_tint = weapon == vx_item(I_NIGHTS_EDGE)       ? vx_cb_rgba(0xC060FF, 1)
                  : weapon == vx_item(I_ENCHANTED_SWORD) ? vx_cb_rgba(0xFF9CF0, 1)
                  : weapon == vx_item(I_ICE_BLADE)       ? vx_cb_rgba(0xE0F8FF, 1)
                                                         : 0;
  // as in Terraria, the Enchanted Sword throws a blade of light and the Ice Blade a shard with every swing
  if ((weapon == vx_item(I_ENCHANTED_SWORD) || weapon == vx_item(I_ICE_BLADE)) && c->proj_count < VX_MAX_PROJECTILES) {
    b32 beam = weapon == vx_item(I_ENCHANTED_SWORD);
    VxV3d o = vx_cb_chest(&g->player);
    VxV3d aim = vx_cb_aim_point(g);
    v3 a = v3_normalize((v3){(f32)(aim.x - o.x), (f32)(aim.y - o.y), (f32)(aim.z - o.z)});
    o.x += a.x * 1.1, o.y += a.y * 1.1, o.z += a.z * 1.1;
    c->proj[c->proj_count++] = (VxProjectile){.p = o, .v = v3_scale(a, beam ? VX_BEAM_SPEED : VX_FROST_SPEED),
                                              .life = beam ? 0.7f : 0.8f, .damage = weapon->damage * (beam ? 0.8f : 0.6f),
                                              .knock = 3.0f, .hue = random_f32(&c->rng),
                                              .kind = (u8)(beam ? VX_PROJ_BEAM : VX_PROJ_FROST)};
  }
}

// damage with Terraria's ±15 % spread and a crit chance; true if it crit
hz_internal f32 vx_cb_roll(VxCombat *c, f32 base, b32 *crit) {
  *crit = random_f32(&c->rng) < VX_CRIT_CHANCE;
  return base * random_f32_range(&c->rng, 0.85f, 1.15f);
}

// a camera swung round the player films from the side: the player still aims where they face
hz_internal b32 vx_cb_side_view(const VxGame *g) { return g->cam.mode == VX_CAM_THIRD && m_absf(g->cam.orbit) > 0.05f; }

hz_internal v3 vx_cb_aim_dir(const VxGame *g) {
  return v3_normalize(vx_cb_side_view(g) ? vx_player_forward(&g->player) : g->cam.fwd);
}

// the weapon hand when held out along a: the right shoulder plus a straight arm
hz_internal VxV3d vx_cb_hand(const VxGame *g, v3 a, f32 reach) {
  const VxPlayer *p = &g->player;
  v3 r = {m_cosf(p->yaw), 0, -m_sinf(p->yaw)};
  return (VxV3d){p->pos.x + r.x * 0.58 + a.x * reach, p->pos.y + 1.84 + a.y * reach, p->pos.z + r.z * 0.58 + a.z * reach};
}

// what the crosshair is on: the first block or creature along the aim, within 64 blocks
hz_internal VxV3d vx_cb_aim_point(const VxGame *g) {
  v3 f = vx_cb_aim_dir(g);
  VxV3d o = vx_cb_side_view(g) ? vx_player_eye(&g->player) : g->cam.pos;
  const VxCreatures *cs = &g->mobs;
  for (f32 t = 0.5f; t < 64.0f; t += 0.2f) {
    VxV3d q = {o.x + f.x * t, o.y + f.y * t, o.z + f.z * t};
    if (vx_solid[vx_world_block(&g->world, vx_floor_i(q.x), vx_floor_i(q.y), vx_floor_i(q.z))] == 1) return q;
    for (u32 i = 0; i < cs->count; i++) {
      const VxCreature *m = &cs->items[i];
      if (vx_creature_friendly(m->kind)) continue;
      f32 half, h;
      vx_creature_size(m, &half, &h);
      if (m_absf((f32)(q.x - m->pos.x)) < half && m_absf((f32)(q.z - m->pos.z)) < half && q.y > m->pos.y &&
          q.y < m->pos.y + h)
        return (VxV3d){m->pos.x, m->pos.y + h * 0.5, m->pos.z};
    }
  }
  return (VxV3d){o.x + f.x * 64, o.y + f.y * 64, o.z + f.z * 64};
}

b32 vx_combat_shoot(VxGame *g, const VxItemInfo *weapon) {
  VxCombat *c = &g->combat;
  if (c->proj_count >= VX_MAX_PROJECTILES) return false;
  const VxPlayer *p = &g->player;
  // fired from the weapon's tip at what the crosshair is on, so third person's shoulder camera aims true
  UNUSED(p);
  VxV3d aim = vx_cb_aim_point(g), sh = vx_cb_hand(g, vx_cb_aim_dir(g), 0);
  v3 a = v3_normalize((v3){(f32)(aim.x - sh.x), (f32)(aim.y - sh.y), (f32)(aim.z - sh.z)});
  VxV3d o = vx_cb_hand(g, a, weapon->kind == VX_IK_WAND ? 1.75f : 1.2f);
  VxProjectile pr = {.p = o};
  if (weapon->kind == VX_IK_BOW) {
    if (vx_inventory_remove(&g->inv, I_WOODEN_ARROW, 1) == 0) return false;
    pr.kind = VX_PROJ_ARROW;
    pr.v = v3_scale(a, VX_ARROW_SPEED);
    pr.damage = (f32)(weapon->damage + vx_item(I_WOODEN_ARROW)->damage);
    pr.knock = 5.0f;
    pr.life = 4.0f;
  } else if (vx_vitals_use_mana(&g->vitals, weapon->mana)) {
    pr.kind = VX_PROJ_SPARK;
    pr.v = v3_scale(a, VX_SPARK_SPEED);
    pr.damage = weapon->damage;
    pr.knock = 2.5f;
    pr.life = 1.3f;
    pr.hue = random_f32(&c->rng);
    // the wand tip blooms white with every bolt and throws a few pastel glints
    VxV3d tip = o;
    vx_entities_burst(&g->ents, VX_MOTE_FLASH, tip, v3_scale(a, 1.0f), 0.3f, 1, 0.26f, 1.7f, vx_cb_rgba(0xFFF6FF, 1));
    vx_entities_burst(&g->ents, VX_MOTE_FLASH, tip, v3_scale(a, 2.5f), 1.2f, 1, 0.3f, 1.1f, vx_cb_rgba(0xFFC8F0, 1));
    vx_entities_burst(&g->ents, VX_MOTE_FLASH, tip, v3_scale(a, 2.5f), 1.2f, 1, 0.3f, 1.1f, vx_cb_rgba(0xC8F0FF, 1));
    vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, tip, v3_scale(a, 5.0f), 2.6f, 7, 0.6f, 0.32f, vx_cb_rgba(0xFFC8F4, 1));
    vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, tip, v3_scale(a, 5.0f), 2.6f, 6, 0.6f, 0.32f, vx_cb_rgba(0xC8F4FF, 1));
  } else {
    return false;
  }
  c->proj[c->proj_count++] = pr;
  c->shots++;
  return true;
}

// the nearest point of a creature's box to p
hz_internal VxV3d vx_cb_closest(const VxCreature *m, f32 half, f32 h, VxV3d p) {
  return (VxV3d){vx_clamp(p.x, m->pos.x - half, m->pos.x + half), vx_clamp(p.y, m->pos.y, m->pos.y + h),
                 vx_clamp(p.z, m->pos.z - half, m->pos.z + half)};
}

hz_internal b32 vx_cb_hit(VxGame *g, u32 i, f32 base, v3 dir, f32 knock, const char *by) {
  VxCombat *c = &g->combat;
  b32 crit;
  f32 dmg = vx_cb_roll(c, base, &crit);
  if (!vx_creature_hurt(g, i, dmg, dir, knock, crit, by)) return false;
  c->hits++;
  const VxCreature *m = &g->mobs.items[i];
  f32 half, h;
  vx_creature_size(m, &half, &h);
  // an impact flash where it landed
  VxV3d at = {m->pos.x - dir.x * half, m->pos.y + h * 0.55, m->pos.z - dir.z * half};
  vx_entities_burst(&g->ents, VX_MOTE_SPARK, at, v3_scale(dir, 2.0f), 4.0f, crit ? 14 : 8, 0.35f, 0.14f,
                    crit ? vx_cb_rgba(0xFFC040, 1) : vx_cb_rgba(0xFFF4D8, 1));
  return true;
}

hz_internal void vx_cb_melee(VxGame *g, f32 dt) {
  VxCombat *c = &g->combat;
  if (c->t <= 0) return;
  c->t -= dt;
  const VxPlayer *p = &g->player;
  if (g->vitals.dead) {
    c->t = 0;
    return;
  }
  v3 a = vx_cb_aim_dir(g);
  v3 r = v3_cross(a, (v3){0, 1, 0});
  r = v3_length(r) < 1e-3f ? (v3){1, 0, 0} : v3_normalize(r);
  v3 up = v3_cross(r, a);
  VxV3d chest = vx_cb_chest(p);
  // the arc faces the view, a block ahead of the chest, overhead from the weapon side to low across: Terraria's swing plane
  VxV3d centre = {chest.x + a.x, chest.y + a.y, chest.z + a.z};
  f32 k = m_saturatef(1 - c->t / c->dur);
  f32 e = 1 - (1 - k) * (1 - k);
  f32 th = m_lerpf(0.9f, 4.0f, e), rad = c->reach * 0.72f;
  v3 tip = v3_add(v3_scale(r, m_cosf(th)), v3_scale(up, m_sinf(th)));
  if (c->stab) {
    // a thrust: a narrow streak out along the view and back, a glint at the point
    f32 o = m_sinf(k * PI);
    VxV3d s0 = {chest.x + r.x * 0.3 + a.x * 0.5, chest.y + a.y * 0.5 - 0.1, chest.z + r.z * 0.3 + a.z * 0.5};
    f32 len = c->reach * o;
    for (u32 s = 0; s < 6; s++) {
      f32 f = ((f32)s + random_f32(&c->rng)) / 6.0f * len;
      VxV3d at = {s0.x + a.x * f, s0.y + a.y * f, s0.z + a.z * f};
      vx_entities_burst(&g->ents, VX_MOTE_TRAIL, at, (v3){0, 0, 0}, 0.05f, 1, 0.12f, 0.14f + 0.1f * f / m_maxf(len, 0.1f),
                        c->trail_tint);
    }
    if (o > 0.9f && random_f32(&c->rng) < 0.5f)
      vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, (VxV3d){s0.x + a.x * len, s0.y + a.y * len, s0.z + a.z * len}, v3_scale(a, 1.0f),
                        0.6f, 1, 0.25f, 0.22f, vx_cb_rgba(0xFFFFFF, 1));
    c->tip_valid = false;
  } else if (c->tip_valid) {
    // a crescent between last frame's blade and this one's, brightest at the edge
    for (u32 s = 0; s < 8; s++) {
      f32 f = ((f32)s + random_f32(&c->rng)) / 8.0f;
      v3 t = v3_normalize(v3_lerp(c->tip_prev, tip, f));
      for (u32 band = 0; band < 4; band++) {
        f32 rr = rad * (1.0f - 0.1f * (f32)band);
        VxV3d at = {centre.x + t.x * rr, centre.y + t.y * rr, centre.z + t.z * rr};
        // the leading edge burns near white, the inner bands in the blade's colour
        u32 tint = band == 0 ? (c->trail_tint & 0xFF000000u) | ((c->trail_tint & 0xFEFEFEu) >> 1) + 0x7F7F7Fu : c->trail_tint;
        vx_entities_burst(&g->ents, VX_MOTE_TRAIL, at, (v3){0, 0, 0}, 0.1f, 1, 0.22f, 0.4f - 0.07f * (f32)band, tint);
      }
    }
    // the special blades shed dust off the tip as they cut
    if (c->glint_tint) {
      VxV3d at = {centre.x + tip.x * rad, centre.y + tip.y * rad, centre.z + tip.z * rad};
      v3 sweep = v3_scale(v3_sub(tip, c->tip_prev), 30.0f);
      vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, at, sweep, 1.2f, 3, 0.45f, 0.2f, c->glint_tint);
    }
  }
  if (!c->stab) {
    c->tip_prev = tip;
    c->tip_valid = true;
  }
  // every creature in the arc is hit once per swing
  VxCreatures *cs = &g->mobs;
  for (u32 i = 0; i < cs->count; i++) {
    VxCreature *m = &cs->items[i];
    if (m->hit_swing == c->swing_id || m->hp <= 0 || vx_creature_friendly(m->kind)) continue;
    f32 half, h;
    vx_creature_size(m, &half, &h);
    VxV3d q = vx_cb_closest(m, half, h, chest);
    v3 d = {(f32)(q.x - chest.x), (f32)(q.y - chest.y), (f32)(q.z - chest.z)};
    f32 dist = v3_length(d);
    if (dist > c->reach + 0.25f) continue;
    if (dist > 0.9f && v3_dot(v3_scale(d, 1.0f / dist), a) < 0.2f) continue;
    v3 kd = {(f32)(m->pos.x - p->pos.x), 0, (f32)(m->pos.z - p->pos.z)};
    kd = v3_length(kd) > 1e-3f ? v3_normalize(kd) : (v3){a.x, 0, a.z};
    if (vx_cb_hit(g, i, c->damage, kd, c->knock, c->weapon)) {
      m->hit_swing = c->swing_id;
      // the blade hangs a moment on what it struck
      vx_hitstop(g, 0.035f);
    }
  }
}

// a boss's projectile, through blocks; a bolt turns on the player once its fan has spread; true when it is spent
hz_internal b32 vx_cb_hostile(VxGame *g, VxProjectile *pr, f32 dt) {
  VxPlayer *p = &g->player;
  pr->age += dt;
  if (pr->life <= 0) return true;
  b32 alive = g->started && !g->vitals.dead, lance = pr->kind == VX_PROJ_LANCE;
  v3 to = {(f32)(p->pos.x - pr->p.x), (f32)(p->pos.y + VX_PLAYER_HEIGHT * 0.5 - pr->p.y), (f32)(p->pos.z - pr->p.z)};
  if (!lance && pr->age > 0.45f && alive) {
    f32 l = v3_length(to), vl = v3_length(pr->v), sp = m_minf(vl + 22.0f * dt, 17.0f);
    if (l > 1e-3f && vl > 1e-3f) {
      v3 d = v3_lerp(v3_scale(pr->v, 1.0f / vl), v3_scale(to, 1.0f / l), 1 - m_expf(-2.2f * dt));
      pr->v = v3_scale(v3_normalize(d), sp);
    }
  }
  pr->p.x += pr->v.x * dt, pr->p.y += pr->v.y * dt, pr->p.z += pr->v.z * dt;
  u32 hex = vx_hue_hex(pr->hue, lance ? 0.2f : 0.1f);
  vx_entities_burst(&g->ents, VX_MOTE_TRAIL, pr->p, (v3){0, 0, 0}, 0.05f, 1, lance ? 0.25f : 0.16f, lance ? 0.7f : 0.12f,
                    vx_cb_rgba(hex, 0.9f));
  f32 reach = lance ? 1.0f : 0.25f;
  if (!alive || m_absf((f32)(pr->p.x - p->pos.x)) > VX_PLAYER_HALF + reach || m_absf((f32)(pr->p.z - p->pos.z)) > VX_PLAYER_HALF + reach ||
      pr->p.y < p->pos.y - reach || pr->p.y > p->pos.y + VX_PLAYER_HEIGHT + reach)
    return false;
  f32 dmg = m_maxf(1, m_roundf(pr->damage - (f32)vx_inventory_defense(&g->inv) * 0.5f));
  if (!vx_vitals_damage(g, dmg, "You were slain by Empress of Light.")) return false;
  vx_world_text_add(g, VX_WT_DAMAGE, (VxV3d){p->pos.x, p->pos.y + VX_PLAYER_HEIGHT + 0.3, p->pos.z}, 0, (u32)dmg);
  v3 push = {pr->v.x, 0, pr->v.z};
  push = v3_length(push) > 1e-3f ? v3_scale(v3_normalize(push), 7.0f) : (v3){0, 0, 0};
  p->vel.x = push.x, p->vel.z = push.z, p->vel.y = m_maxf(p->vel.y, 5.0f);
  p->knock_t = 0.25f;
  vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, pr->p, (v3){0, 1.5f, 0}, 3.0f, 10, 0.4f, 0.16f, vx_cb_rgba(hex, 1));
  return true;
}

// the wand's bolt in its rainbow at this moment: it cycles as it flies
hz_internal u32 vx_cb_bolt_hex(const VxProjectile *pr, f32 back) { return vx_hue_hex(pr->hue + pr->age * 1.6f - back * 2.0f, 0.18f); }

// a magic bolt's trail along this frame's travel, so it stays unbroken at any frame rate, and the dust it sheds
hz_internal void vx_cb_magic_wake(VxGame *g, VxProjectile *pr, f32 travel, f32 dt) {
  VxCombat *c = &g->combat;
  u32 n = MIN((u32)m_ceilf(travel / 0.18f), 16u);
  for (u32 k = 0; k < n; k++) {
    f32 back = (f32)k / (f32)n * dt;
    VxV3d q = {pr->p.x - pr->v.x * back, pr->p.y - pr->v.y * back, pr->p.z - pr->v.z * back};
    u32 tint = pr->kind == VX_PROJ_SPARK ? vx_cb_rgba(vx_cb_bolt_hex(pr, back), 0.95f)
               : pr->kind == VX_PROJ_BEAM ? vx_cb_rgba((k & 1) ? 0x80F0FF : 0xFF9CF0, 0.85f)
                                          : vx_cb_rgba(0xA8ECFF, 0.85f);
    vx_entities_burst(&g->ents, VX_MOTE_TRAIL, q, (v3){0, 0, 0}, 0.06f, 1, pr->kind == VX_PROJ_SPARK ? 0.32f : 0.2f,
                      pr->kind == VX_PROJ_SPARK ? 0.6f : 0.34f, tint);
  }
  // pastel glints drift off the line and hang a moment: the reference's sparkling stream
  hz_internal const u32 pastel[4] = {0xFFB4EC, 0xD2B8FF, 0xFFFFFF, 0xB4F0FF};
  u32 glints = pr->kind == VX_PROJ_SPARK ? 6 : 2;
  for (u32 k = 0; k < glints; k++) {
    u32 tint = pr->kind == VX_PROJ_FROST ? 0xE8FAFF : pastel[random_u32(&c->rng) & 3u];
    vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, pr->p, v3_scale(pr->v, 0.1f), 1.8f, 1, 0.9f, 0.34f, vx_cb_rgba(tint, 1));
  }
  // soft pastel puffs swell along the wand's stream, so it reads as a glowing cloud and not a line
  if (pr->kind == VX_PROJ_SPARK && random_f32(&c->rng) < 0.6f)
    vx_entities_burst(&g->ents, VX_MOTE_FLASH, pr->p, v3_scale(pr->v, 0.05f), 0.8f, 1, 0.4f, 0.9f,
                      vx_cb_rgba(pastel[random_u32(&c->rng) & 3u], 0.7f));
}

// a magic bolt bursting where it lands: a white flash, a ring and a spray of glints
hz_internal void vx_cb_magic_pop(VxGame *g, const VxProjectile *pr) {
  b32 spark = pr->kind == VX_PROJ_SPARK;
  u32 ring = spark ? vx_cb_bolt_hex(pr, 0) : pr->kind == VX_PROJ_BEAM ? 0xFF9CF0 : 0xA8ECFF;
  f32 s = pr->life <= 0 ? 0.6f : 1.0f;
  vx_entities_burst(&g->ents, VX_MOTE_FLASH, pr->p, (v3){0, 0, 0}, 0, 1, 0.18f, 1.1f * s, vx_cb_rgba(0xFFFFFF, 1));
  vx_entities_burst(&g->ents, VX_MOTE_RING, pr->p, (v3){0, 0, 0}, 0, 1, 0.3f, 1.2f * s, vx_cb_rgba(ring, 1));
  vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, pr->p, (v3){0, 1.0f, 0}, 4.5f * s, 8, 0.5f, 0.24f, vx_cb_rgba(0xFFC0F0, 1));
  vx_entities_burst(&g->ents, VX_MOTE_SPARKLE, pr->p, (v3){0, 1.0f, 0}, 4.5f * s, 6, 0.5f, 0.24f, vx_cb_rgba(spark ? 0xC0F4FF : ring, 1));
}

hz_internal void vx_cb_projectiles(VxGame *g, f32 dt) {
  VxCombat *c = &g->combat;
  const VxWorld *w = &g->world;
  VxCreatures *cs = &g->mobs;
  for (u32 i = 0; i < c->proj_count;) {
    VxProjectile *pr = &c->proj[i];
    pr->life -= dt;
    if (pr->kind >= VX_PROJ_HOSTILE) {
      if (vx_cb_hostile(g, pr, dt)) c->proj[i] = c->proj[--c->proj_count];
      else i++;
      continue;
    }
    b32 dead = pr->life <= 0;
    b32 spark = pr->kind == VX_PROJ_SPARK, star = pr->kind == VX_PROJ_STAR;
    b32 magic = spark || pr->kind == VX_PROJ_BEAM || pr->kind == VX_PROJ_FROST;
    pr->age += dt;
    if (!star && !magic) pr->v.y -= VX_ARROW_GRAVITY * dt;
    f32 travel = v3_length(pr->v) * dt;
    u32 steps = MAX((u32)m_ceilf(travel / 0.35f), 1u);
    f32 sdt = dt / (f32)steps;
    for (u32 s = 0; s < steps && !dead; s++) {
      pr->p.x += pr->v.x * sdt, pr->p.y += pr->v.y * sdt, pr->p.z += pr->v.z * sdt;
      u8 b = vx_world_block(w, vx_floor_i(pr->p.x), vx_floor_i(pr->p.y), vx_floor_i(pr->p.z));
      // stars fall through trees, which Terraria does not count as ground, and settle on liquids
      b32 stop = star ? (vx_solid[b] == 1 && !vx_is_leaves(b) && !vx_is_log(b)) || vx_shape[b] == VX_SHAPE_LIQUID
                      : vx_solid[b] == 1;
      if (stop) {
        dead = true;
        if (star) {
          // it comes to rest in the last open cell on its path
          VxV3d at = {pr->p.x - pr->v.x * sdt, pr->p.y - pr->v.y * sdt, pr->p.z - pr->v.z * sdt};
          vx_entities_spawn_drop(&g->ents, at, I_FALLEN_STAR, 1);
          vx_entities_burst(&g->ents, VX_MOTE_SPARK, at, (v3){0, 4.0f, 0}, 4.0f, 24, 0.9f, 0.14f, vx_cb_rgba(0xFFE070, 1));
          vx_audio_play(&g->audio, VX_SND_SHIMMER);
          if (g->scripted) log_info("loot: fallen star landed at % % %", fmt_f32_p((f32)at.x, 1), fmt_f32_p((f32)at.y, 1), fmt_f32_p((f32)at.z, 1));
        } else if (magic) {
          vx_cb_magic_pop(g, pr);
        } else {
          vx_entities_dust(&g->ents, b, vx_floor_i(pr->p.x), vx_floor_i(pr->p.y), vx_floor_i(pr->p.z), 3);
          f64 dx = pr->p.x - g->player.pos.x, dy = pr->p.y - g->player.pos.y, dz = pr->p.z - g->player.pos.z;
          vx_audio_play_vol(&g->audio, VX_SND_ARROW_HIT, 0.8f * m_saturatef(1 - (f32)m_sqrt(dx * dx + dy * dy + dz * dz) / 32));
        }
        break;
      }
      for (u32 m = 0; m < cs->count && !star; m++) {
        const VxCreature *cr = &cs->items[m];
        if (cr->hp <= 0 || vx_creature_friendly(cr->kind)) continue;
        f32 half, h;
        vx_creature_size(cr, &half, &h);
        if (m_absf((f32)(pr->p.x - cr->pos.x)) > half + 0.2f || m_absf((f32)(pr->p.z - cr->pos.z)) > half + 0.2f ||
            pr->p.y < cr->pos.y - 0.2 || pr->p.y > cr->pos.y + h + 0.2)
          continue;
        v3 dir = {pr->v.x, 0, pr->v.z};
        dir = v3_length(dir) > 1e-3f ? v3_normalize(dir) : (v3){0, 0, 1};
        const char *by = spark ? "Wand of Sparking" : pr->kind == VX_PROJ_BEAM ? "Enchanted Sword" : pr->kind == VX_PROJ_FROST ? "Ice Blade" : "Wooden Arrow";
        if (vx_cb_hit(g, m, pr->damage, dir, pr->knock, by)) {
          dead = true;
          if (magic) vx_cb_magic_pop(g, pr);
        }
        break;
      }
    }
    if (dead) {
      // a bolt that runs out of range fades in a smaller pop
      if (magic && pr->life <= 0) vx_cb_magic_pop(g, pr);
      c->proj[i] = c->proj[--c->proj_count];
      continue;
    }
    // a star streaks a long golden tail
    if (star) {
      // laid along the whole frame's travel so the streak stays unbroken at any frame rate
      u32 n = MIN((u32)m_ceilf(travel / 0.35f), 24u);
      for (u32 k = 0; k < n; k++) {
        f32 back = (f32)k / (f32)n * dt;
        VxV3d q = {pr->p.x - pr->v.x * back, pr->p.y - pr->v.y * back, pr->p.z - pr->v.z * back};
        vx_entities_burst(&g->ents, VX_MOTE_TRAIL, q, (v3){0, 0, 0}, 0.1f, 1, 0.55f, 0.55f, vx_cb_rgba(0xFFD84A, 0.9f));
      }
      vx_entities_streak(&g->ents, pr->p, pr->v, 0.1f, 1.0f, vx_cb_rgba(0xFFE890, 1));
      if (random_f32(&c->rng) < 0.5f)
        vx_entities_burst(&g->ents, VX_MOTE_SPARK, pr->p, v3_scale(pr->v, 0.05f), 1.5f, 1, 0.8f, 0.1f, vx_cb_rgba(0xFFF0A0, 1));
    }
    if (magic) vx_cb_magic_wake(g, pr, travel, dt);
    // an arrow draws a faint white line behind it
    if (pr->kind == VX_PROJ_ARROW) {
      VxV3d q = {pr->p.x - pr->v.x * dt * 0.5, pr->p.y - pr->v.y * dt * 0.5, pr->p.z - pr->v.z * dt * 0.5};
      vx_entities_burst(&g->ents, VX_MOTE_TRAIL, q, (v3){0, 0, 0}, 0.02f, 1, 0.14f, 0.07f, vx_cb_rgba(0xFFFFFF, 0.5f));
    }
    i++;
  }
}

void vx_combat_star(VxGame *g, VxV3d target, v3 heading) {
  VxCombat *c = &g->combat;
  if (c->proj_count >= VX_MAX_PROJECTILES) return;
  // in from one side of the sky at a steep slant, as Terraria's stars come down
  f32 a = v3_length(heading) > 1e-3f ? m_atan2f(heading.z, heading.x) : random_f32(&c->rng) * PI * 2;
  v3 dir = v3_normalize((v3){m_cosf(a) * 0.55f, -1.0f, m_sinf(a) * 0.55f});
  f32 speed = 42.0f, dist = 90.0f;
  c->proj[c->proj_count++] = (VxProjectile){
      .p = {target.x - dir.x * dist, target.y - dir.y * dist, target.z - dir.z * dist},
      .v = v3_scale(dir, speed),
      .life = dist / speed + 2.0f,
      .kind = VX_PROJ_STAR,
  };
}

void vx_combat_hostile(VxGame *g, VxProjectileKind kind, VxV3d p, v3 v, f32 hue, f32 life, f32 damage) {
  VxCombat *c = &g->combat;
  if (c->proj_count >= VX_MAX_PROJECTILES) return;
  c->proj[c->proj_count++] = (VxProjectile){.p = p, .v = v, .life = life, .damage = damage, .hue = hue, .kind = (u8)kind};
}

void vx_combat_update(VxGame *g, f32 dt) {
  vx_cb_melee(g, dt);
  vx_cb_projectiles(g, dt);
}

// a box frame along a direction, camera-relative
hz_internal m4 vx_cb_frame(v3 at, v3 dir) {
  v3 z = v3_normalize(dir);
  v3 up = m_absf(z.y) > 0.95f ? (v3){1, 0, 0} : (v3){0, 1, 0};
  v3 x = v3_normalize(v3_cross(up, z)), y = v3_cross(z, x);
  m4 m = {0};
  m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
  m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
  m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
  m.m[3][0] = at.x, m.m[3][1] = at.y, m.m[3][2] = at.z, m.m[3][3] = 1;
  return m;
}

void vx_combat_build(VxEntities *e, const VxCombat *c, VxV3d cam) {
  u32 L = vx_tex_layer[T_PUPPET];
  v3 wood = {0.42f, 0.24f, 0.11f}, steel = {0.48f, 0.5f, 0.55f}, feather = {0.9f, 0.88f, 0.84f};
  for (u32 i = 0; i < c->proj_count; i++) {
    const VxProjectile *pr = &c->proj[i];
    v3 at = {(f32)(pr->p.x - cam.x), (f32)(pr->p.y - cam.y), (f32)(pr->p.z - cam.z)};
    m4 f = vx_cb_frame(at, pr->v);
    if (pr->kind == VX_PROJ_ARROW) {
      vx_ebox_ex(e, f, (v3){-0.03f, -0.03f, -0.5f}, (v3){0.03f, 0.03f, 0.42f}, L, wood, 0, 0);
      vx_ebox_ex(e, f, (v3){-0.07f, -0.07f, 0.38f}, (v3){0.07f, 0.07f, 0.58f}, L, steel, 0, 0);
      vx_ebox_ex(e, f, (v3){-0.1f, -0.012f, -0.52f}, (v3){0.1f, 0.012f, -0.3f}, L, feather, 0, 0);
      vx_ebox_ex(e, f, (v3){-0.012f, -0.1f, -0.52f}, (v3){0.012f, 0.1f, -0.3f}, L, feather, 0, 0);
    } else if (pr->kind == VX_PROJ_PRISM) {
      // a long shard of light, white at its core
      v3 col = v3_scale(vx_cb_lin(vx_hue_hex(pr->hue, 0.1f)), 2.2f);
      vx_ebox_ex(e, f, (v3){-0.05f, -0.05f, -0.9f}, (v3){0.05f, 0.05f, 0.9f}, L, col, VX_BOX_SELF_LIT, 0);
      vx_ebox_ex(e, f, (v3){-0.022f, -0.022f, -0.7f}, (v3){0.022f, 0.022f, 0.7f}, L, (v3){2.4f, 2.4f, 2.4f}, VX_BOX_SELF_LIT, 0);
    } else if (pr->kind == VX_PROJ_LANCE) {
      // a crescent face-on to its flight: a disc less an offset disc, cyan at the rim and white along its inner edge
      m4 s = m4_rotate_z(f, 0.6f + pr->age * 0.8f);
      f32 R = 1.5f, cell = 0.15f;
      v3 rim = v3_scale(vx_cb_lin(vx_hue_hex(pr->hue, 0.15f)), 1.8f);
      for (i32 j = -10; j <= 10; j++)
        for (i32 k = -10; k <= 10; k++) {
          f32 x = (f32)k * cell, y = (f32)j * cell, r = m_sqrtf(x * x + y * y);
          f32 ix = x - R * 0.5f, iy = y - R * 0.1f, inner = m_sqrtf(ix * ix + iy * iy) - R * 0.82f;
          if (r > R || inner < 0) continue;
          v3 col = v3_lerp((v3){2.6f, 2.6f, 2.6f}, rim, m_saturatef(inner / (R * 0.3f)));
          vx_ebox_ex(e, s, (v3){x - cell * 0.5f, y - cell * 0.5f, -0.05f}, (v3){x + cell * 0.5f, y + cell * 0.5f, 0.05f}, L, col,
                     VX_BOX_SELF_LIT, 0);
        }
    } else if (pr->kind == VX_PROJ_STAR) {
      // a crossed pair of glowing slabs reads as a star from any side
      vx_ebox_ex(e, f, (v3){-0.32f, -0.08f, -0.32f}, (v3){0.32f, 0.08f, 0.32f}, L, (v3){1.4f, 1.15f, 0.4f}, VX_BOX_SELF_LIT, 0);
      vx_ebox_ex(e, f, (v3){-0.08f, -0.32f, -0.32f}, (v3){0.08f, 0.32f, 0.32f}, L, (v3){1.4f, 1.15f, 0.4f}, VX_BOX_SELF_LIT, 0);
    } else if (pr->kind == VX_PROJ_BEAM) {
      // a sword of light: blade, guard and hilt, cyan with a white core
      v3 cyan = {0.25f, 0.95f, 1.0f}, pink = {1.0f, 0.4f, 0.9f};
      vx_ebox_ex(e, f, (v3){-0.06f, -0.12f, -0.2f}, (v3){0.06f, 0.12f, 0.85f}, L, cyan, VX_BOX_HOT_GLOW, 0);
      vx_ebox_ex(e, f, (v3){-0.025f, -0.05f, -0.15f}, (v3){0.025f, 0.05f, 0.8f}, L, (v3){1, 1, 1}, VX_BOX_HOT_GLOW, 0);
      vx_ebox_ex(e, f, (v3){-0.06f, -0.3f, -0.3f}, (v3){0.06f, 0.3f, -0.2f}, L, pink, VX_BOX_HOT_GLOW, 0);
      vx_ebox_ex(e, f, (v3){-0.05f, -0.05f, -0.6f}, (v3){0.05f, 0.05f, -0.3f}, L, pink, VX_BOX_HOT_GLOW, 0);
    } else if (pr->kind == VX_PROJ_FROST) {
      // an ice shard: a long diamond crossed with a short one, turning as it flies
      m4 s = m4_rotate_z(f, pr->age * 9.0f);
      m4 d = m4_rotate_y(m4_rotate_x(s, 0.785f), 0.0f);
      v3 ice = {0.55f, 0.9f, 1.0f};
      vx_ebox_ex(e, s, (v3){-0.07f, -0.07f, -0.45f}, (v3){0.07f, 0.07f, 0.45f}, L, ice, VX_BOX_HOT_GLOW, 0);
      vx_ebox_ex(e, d, (v3){-0.14f, -0.03f, -0.14f}, (v3){0.14f, 0.03f, 0.14f}, L, (v3){0.9f, 1.0f, 1.0f}, VX_BOX_HOT_GLOW, 0);
    } else {
      // the wand's bolt: a white-hot core in a sheath of its current colour
      v3 col = vx_cb_lin(vx_cb_bolt_hex(pr, 0));
      vx_ebox_ex(e, f, (v3){-0.06f, -0.06f, -0.4f}, (v3){0.06f, 0.06f, 0.4f}, L, col, VX_BOX_HOT_GLOW, 0);
      vx_ebox_ex(e, f, (v3){-0.03f, -0.03f, -0.35f}, (v3){0.03f, 0.03f, 0.45f}, L, (v3){1, 1, 1}, VX_BOX_HOT_GLOW, 0);
    }
  }
}

#undef VX_CHEST_Y
#undef VX_CRIT_CHANCE
#undef VX_ARROW_SPEED
#undef VX_ARROW_GRAVITY
#undef VX_SPARK_SPEED
#undef VX_BEAM_SPEED
#undef VX_FROST_SPEED

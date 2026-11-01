#include "voxaria/vx_entity_geo.h"

// creatures: Terraria's four movement behaviours over voxel collision, contact damage, hits, and box-puppet art

#define VX_CR_GRAVITY 60.0f
#define VX_CR_MAX_FALL 36.0f
#define VX_CR_EPS 1e-4
#define VX_CR_DESPAWN 110.0   // blocks from the player
#define VX_CR_AGGRO 36.0f     // blocks within which a creature chases the player
#define VX_CR_HIT_IMMUNE 0.2f // seconds between hits that land on one creature
#define VX_CR_SPAWN_MIN 26.0f  // spawns land beyond this many blocks, out of the player's face
#define VX_CR_SPAWN_MAX 58.0f
#define VX_CR_SPAWN_EVERY 0.35f

// name death_msg behaviour half height hp damage defense knock_resist speed
const VxCreatureInfo vx_creature_info[VX_CR_COUNT] = {
    [VX_CR_SLIME] = {"Slime", "You were slain by a Slime.", VX_BEH_HOPPER, 0.8f, 1.3f, 14, 6, 0, 0.0f, 4.6f},
    [VX_CR_ZOMBIE] = {"Zombie", "You were slain by a Zombie.", VX_BEH_WALKER, 0.5f, 2.6f, 45, 14, 6, 0.5f, 3.4f},
    [VX_CR_SKELETON] = {"Skeleton", "You were slain by a Skeleton.", VX_BEH_WALKER, 0.5f, 2.6f, 60, 20, 8, 0.5f, 4.2f},
    [VX_CR_DEMON_EYE] = {"Demon Eye", "You were slain by a Demon Eye.", VX_BEH_SWOOPER, 0.5f, 1.0f, 60, 18, 2, 0.2f, 7.5f},
    [VX_CR_EATER] = {"Eater of Souls", "You were slain by an Eater of Souls.", VX_BEH_SWOOPER, 0.55f, 1.1f, 40, 22, 8, 0.0f, 6.0f},
    [VX_CR_BAT] = {"Cave Bat", "You were slain by a Cave Bat.", VX_BEH_FLUTTERER, 0.35f, 0.7f, 16, 13, 2, 0.2f, 7.0f},
    [VX_CR_GUIDE] = {"Guide", "", VX_BEH_TOWNSFOLK, (f32)VX_PLAYER_HALF, (f32)VX_PLAYER_HEIGHT, 250, 0, 15, 0.5f, 2.2f},
    [VX_CR_EYE_OF_CTHULHU] = {"Eye of Cthulhu", "You were slain by Eye of Cthulhu.", VX_BEH_BOSS, 2.5f, 5.0f, 2800, 15, 12, 1.0f, 12.0f},
    [VX_CR_SERVANT] = {"Servant of Cthulhu", "You were slain by a Servant of Cthulhu.", VX_BEH_SWOOPER, 0.35f, 0.7f, 8, 12, 0, 0.0f, 9.0f},
    [VX_CR_EMPRESS] = {"Empress of Light", "You were slain by Empress of Light.", VX_BEH_BOSS, 1.5f, 6.9f, 5500, 40, 30, 1.0f, 14.0f},
};

// the Eye's second form bites harder and drops its guard, Terraria's 23 damage and 0 defense
hz_internal f32 vx_cr_damage(const VxCreature *m) {
  return m->kind == VX_CR_EYE_OF_CTHULHU && m->boss_phase == 2 ? 23.0f : vx_creature_info[m->kind].damage;
}
hz_internal f32 vx_cr_defense(const VxCreature *m) {
  return m->kind == VX_CR_EYE_OF_CTHULHU && m->boss_phase == 2 ? 0.0f : vx_creature_info[m->kind].defense;
}

hz_internal const u32 VX_SLIME_COLORS[VX_SLIME_COUNT] = {0x5CC84A, 0x4A8CF0, 0x9CDCF2, 0x86B82A, 0xF0701E, 0xF27AD0};
hz_internal const f32 VX_SLIME_HP[VX_SLIME_COUNT] = {14, 25, 40, 30, 50, 150};
hz_internal const u32 VX_BAT_COLORS[VX_BAT_COUNT] = {0x5A463E, 0x8CC4E0, 0x5E7A24, 0x8A2A1A};
hz_internal const char *const VX_SLIME_NAMES[VX_SLIME_COUNT] = {"Green Slime", "Blue Slime", "Ice Slime",
                                                                "Jungle Slime", "Lava Slime", "Pinky"};
hz_internal const char *const VX_SLIME_DEATHS[VX_SLIME_COUNT] = {
    "You were slain by a Green Slime.",  "You were slain by a Blue Slime.", "You were slain by an Ice Slime.",
    "You were slain by a Jungle Slime.", "You were slain by a Lava Slime.", "You were slain by Pinky."};
hz_internal const char *const VX_BAT_NAMES[VX_BAT_COUNT] = {"Cave Bat", "Ice Bat", "Jungle Bat", "Hellbat"};
hz_internal const char *const VX_BAT_DEATHS[VX_BAT_COUNT] = {
    "You were slain by a Cave Bat.", "You were slain by an Ice Bat.", "You were slain by a Jungle Bat.",
    "You were slain by a Hellbat."};

hz_internal const char *const VX_ROLE_NAMES[VX_ROLE_COUNT] = {"Guide", "Merchant", "Clothier", "Dryad", "Mechanic", "Demolitionist"};

const char *vx_creature_name(const VxCreature *m) {
  if (m->kind == VX_CR_SLIME) return VX_SLIME_NAMES[m->variant % VX_SLIME_COUNT];
  if (m->kind == VX_CR_GUIDE) return VX_ROLE_NAMES[m->variant % VX_ROLE_COUNT];
  if (m->kind == VX_CR_BAT) return VX_BAT_NAMES[m->variant % VX_BAT_COUNT];
  return vx_creature_info[m->kind].name;
}

hz_internal const char *vx_cr_death_msg(const VxCreature *m) {
  if (m->kind == VX_CR_SLIME) return VX_SLIME_DEATHS[m->variant % VX_SLIME_COUNT];
  if (m->kind == VX_CR_BAT) return VX_BAT_DEATHS[m->variant % VX_BAT_COUNT];
  return vx_creature_info[m->kind].death_msg;
}

hz_internal f32 vx_cr_lin(u32 c8) {
  f32 c = (f32)c8 / 255.0f;
  return c <= 0.04045f ? c / 12.92f : m_powf((c + 0.055f) / 1.055f, 2.4f);
}
hz_internal v3 vx_cr_rgb(u32 c) { return (v3){vx_cr_lin((c >> 16) & 255), vx_cr_lin((c >> 8) & 255), vx_cr_lin(c & 255)}; }

void vx_creatures_init(VxCreatures *c, Allocator *alloc, u64 seed) {
  *c = (VxCreatures){.items = ALLOC_ARRAY(alloc, VxCreature, VX_MAX_CREATURES), .rng = random_create(seed ^ 0xC2EA7u)};
}

VxCreature *vx_creature_spawn(VxCreatures *c, const VxWorld *w, VxCreatureKind kind, u32 variant, VxV3d p) {
  if (c->count >= VX_MAX_CREATURES || kind >= VX_CR_COUNT) return NULL;
  const VxCreatureInfo *ci = &vx_creature_info[kind];
  VxCreature *m = &c->items[c->count++];
  f32 hp = kind == VX_CR_SLIME ? VX_SLIME_HP[variant % VX_SLIME_COUNT] : ci->hp;
  *m = (VxCreature){.pos = p, .hp = hp, .hp_max = hp, .kind = (u8)kind, .variant = (u8)variant,
                    .seed = random_u32(&c->rng), .yaw = random_f32_range(&c->rng, -PI, PI),
                    .timer = random_f32_range(&c->rng, 0.3f, 1.5f), .voice_t = random_f32_range(&c->rng, 2.0f, 8.0f)};
  // rises out of anything solid it was placed in, as far as a tree's height
  f32 half, h;
  vx_creature_size(m, &half, &h);
  for (u32 k = 0; k < 24 && vx_aabb_collides(w, m->pos.x, m->pos.y, m->pos.z, half, h); k++)
    m->pos.y = vx_floor_i(m->pos.y) + 1.01;
  // slimes carry something they swallowed, seen through the jelly; molten ones burn it away
  if (kind == VX_CR_SLIME && variant != VX_SLIME_LAVA) {
    f32 r = random_f32(&c->rng);
    m->held = r < 0.45f   ? I_GEL
              : r < 0.65f ? I_COPPER_COIN
              : r < 0.75f ? (variant == VX_SLIME_ICE ? B_ICE_TORCH : B_TORCH)
              : r < 0.83f ? I_WOODEN_ARROW
              : r < 0.9f  ? I_HEALING_POTION
              : r < 0.95f ? I_GLOWSTICK
                          : I_SILVER_COIN;
  }
  return m;
}

void vx_creature_size(const VxCreature *m, f32 *half, f32 *height) {
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  f32 s = m->kind == VX_CR_SLIME && m->variant == VX_SLIME_PINKY ? 0.55f : 1.0f;
  *half = ci->half * s;
  *height = ci->height * s;
}

force_inline f64 *vx_cr_axis(VxV3d *v, u32 a) { return a == 0 ? &v->x : a == 1 ? &v->y : &v->z; }
force_inline f32 *vx_cr_vaxis(v3 *v, u32 a) { return a == 0 ? &v->x : a == 1 ? &v->y : &v->z; }

// moves one axis to the first obstacle on the block grid; true if blocked
hz_internal b32 vx_cr_move_axis(const VxWorld *w, VxCreature *m, f32 half, f32 h, u32 a, f64 amount) {
  if (amount == 0) return false;
  f64 *pa = vx_cr_axis(&m->pos, a);
  f64 target = *pa + amount;
  VxV3d t = m->pos;
  *vx_cr_axis(&t, a) = target;
  if (!vx_aabb_collides(w, t.x, t.y, t.z, half, h)) {
    *pa = target;
    return false;
  }
  if (a == 1) *pa = amount > 0 ? vx_floor_i(target + h - VX_CR_EPS) - h - VX_CR_EPS : vx_floor_i(target + VX_CR_EPS) + 1 + VX_CR_EPS;
  else *pa = amount > 0 ? vx_floor_i(target + half - VX_CR_EPS) - half - VX_CR_EPS
                        : vx_floor_i(target - half + VX_CR_EPS) + 1 + half + VX_CR_EPS;
  if (vx_aabb_collides(w, m->pos.x, m->pos.y, m->pos.z, half, h)) *pa = target - amount;
  return true;
}

// integrates vel over dt in substeps under a block; returns blocked axes as bits (1 x, 2 y down, 4 z, 8 y up)
hz_internal u32 vx_cr_move(const VxWorld *w, VxCreature *m, f32 dt) {
  f32 half, h;
  vx_creature_size(m, &half, &h);
  f32 travel = m_maxf(m_absf(m->vel.x), m_maxf(m_absf(m->vel.y), m_absf(m->vel.z))) * dt;
  u32 steps = (u32)m_ceilf(travel / 0.45f);
  steps = MAX(steps, 1u);
  f32 sdt = dt / (f32)steps;
  u32 blocked = 0;
  for (u32 s = 0; s < steps; s++) {
    for (u32 a = 0; a < 3; a++) {
      f32 *va = vx_cr_vaxis(&m->vel, a);
      if (vx_cr_move_axis(w, m, half, h, a, *va * sdt)) blocked |= a == 1 ? (*va < 0 ? 2u : 8u) : (a == 0 ? 1u : 4u);
    }
  }
  return blocked;
}

// a ground creature blocked by a one-block ledge climbs it, as Terraria's walkers do
hz_internal b32 vx_cr_step_up(const VxWorld *w, VxCreature *m, f32 dx, f32 dz) {
  f32 half, h;
  vx_creature_size(m, &half, &h);
  f64 up = vx_floor_i(m->pos.y + VX_CR_EPS) + 1 + VX_CR_EPS - m->pos.y;
  if (up > 1.0 + VX_CR_EPS || up <= 0) return false;
  if (vx_aabb_collides(w, m->pos.x, m->pos.y + up, m->pos.z, half, h)) return false;
  if (vx_aabb_collides(w, m->pos.x + dx, m->pos.y + up, m->pos.z + dz, half, h)) return false;
  m->pos.y += up;
  return true;
}

hz_internal void vx_cr_face(VxCreature *m, f32 dx, f32 dz, f32 k) {
  if (dx * dx + dz * dz < 1e-4f) return;
  f32 want = m_atan2f(-dx, -dz);
  m->yaw += m_wrap_pi(want - m->yaw) * k;
}

hz_internal void vx_cr_hopper(VxCreatures *cs, VxCreature *m, v3 to, b32 aggro, f32 dt) {
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  f32 squash_to = 0;
  if (m->on_ground) {
    f32 k = m_expf(-12 * dt);
    m->vel.x *= k;
    m->vel.z *= k;
    m->timer -= dt;
    // gathers itself before a hop
    if (m->timer < 0.22f) squash_to = -0.22f;
    if (m->timer <= 0) {
      f32 dx = to.x, dz = to.z, l = m_sqrtf(dx * dx + dz * dz);
      if (!aggro || l < 1e-3f) {
        f32 a = random_f32_range(&cs->rng, -PI, PI);
        dx = m_sinf(a), dz = m_cosf(a), l = 1;
      }
      f32 sp = ci->speed * (aggro ? 1.0f : 0.5f) * random_f32_range(&cs->rng, 0.8f, 1.15f);
      m->vel.x = dx / l * sp;
      m->vel.z = dz / l * sp;
      m->vel.y = random_f32_range(&cs->rng, 12.0f, 15.5f) * (aggro ? 1.0f : 0.8f);
      m->on_ground = false;
      m->timer = aggro ? random_f32_range(&cs->rng, 0.5f, 1.3f) : random_f32_range(&cs->rng, 1.2f, 2.8f);
      vx_cr_face(m, dx, dz, 1);
    }
  } else {
    squash_to = m_clampf(m->vel.y * 0.022f, -0.12f, 0.28f);
  }
  m->squash += (squash_to - m->squash) * m_minf(1, dt * 14);
}

hz_internal void vx_cr_walker(const VxWorld *w, VxCreatures *cs, VxCreature *m, v3 to, b32 aggro, u32 blocked, f32 dt) {
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  m->timer -= dt;
  if (!aggro && m->timer <= 0) {
    m->yaw = random_f32_range(&cs->rng, -PI, PI);
    m->timer = random_f32_range(&cs->rng, 2.0f, 5.0f);
  }
  f32 dx, dz;
  if (aggro) {
    f32 l = m_sqrtf(to.x * to.x + to.z * to.z);
    dx = l > 0.6f ? to.x / l : 0, dz = l > 0.6f ? to.z / l : 0;
    vx_cr_face(m, dx, dz, m_minf(1, dt * 8));
  } else {
    dx = -m_sinf(m->yaw) * 0.45f, dz = -m_cosf(m->yaw) * 0.45f;
  }
  f32 blend = 1 - m_expf(-(m->on_ground ? 8.0f : 2.0f) * dt);
  m->vel.x += (dx * ci->speed - m->vel.x) * blend;
  m->vel.z += (dz * ci->speed - m->vel.z) * blend;
  if (m->on_ground && (blocked & 5u)) {
    // a ledge is climbed; a wall is jumped
    if (!vx_cr_step_up(w, m, dx * 0.3f, dz * 0.3f)) {
      m->vel.y = 14.0f;
      m->on_ground = false;
    }
  }
  f32 sp = m_sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
  m->anim += sp * dt * 2.4f;
}

hz_internal void vx_cr_flyer(VxCreatures *cs, VxCreature *m, v3 to, b32 aggro, u32 blocked, f32 dt) {
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  b32 bat = ci->behaviour == VX_BEH_FLUTTERER;
  m->timer -= dt;
  v3 want;
  if (bat) {
    // erratic: a fresh jitter every fraction of a second around the line to the target
    if (m->timer <= 0) {
      m->timer = random_f32_range(&cs->rng, 0.2f, 0.6f);
      m->jitter_a = random_f32_range(&cs->rng, -PI, PI);
      m->jitter_b = random_f32_range(&cs->rng, -PI, PI);
    }
    v3 j = {m_sinf(m->jitter_a) * 1.6f, m_sinf(m->jitter_b + m->anim * 3.1f) * 1.2f, m_cosf(m->jitter_a) * 1.6f};
    v3 d = aggro ? v3_add(v3_normalize(to), j) : j;
    want = v3_scale(v3_normalize(d), ci->speed);
    f32 k = 1 - m_expf(-3.5f * dt);
    m->vel = v3_add(m->vel, v3_scale(v3_sub(want, m->vel), k));
  } else {
    // swoops: accelerates at the target and overshoots, as Terraria's demon eyes do
    v3 d = aggro ? v3_normalize(to) : (v3){-m_sinf(m->yaw), m_sinf(m->age * 0.7f) * 0.3f, -m_cosf(m->yaw)};
    f32 accel = m->kind == VX_CR_EATER ? 9.0f : 7.0f;
    m->vel = v3_add(m->vel, v3_scale(d, accel * dt));
    f32 sp = v3_length(m->vel);
    if (sp > ci->speed) m->vel = v3_scale(m->vel, ci->speed / sp);
  }
  // walls bounce a flyer back
  if (blocked & 1u) m->vel.x = -m->vel.x * 0.7f;
  if (blocked & 4u) m->vel.z = -m->vel.z * 0.7f;
  if (blocked & 10u) m->vel.y = -m->vel.y * 0.7f;
  vx_cr_face(m, m->vel.x, m->vel.z, m_minf(1, dt * (bat ? 10.0f : 5.0f)));
  m->anim += dt;
}

// ---- spawning: Terraria's rules by layer, time of day and biome ----

typedef enum {
  VX_ENV_SURFACE_DAY,
  VX_ENV_SURFACE_NIGHT,
  VX_ENV_UNDERGROUND,
  VX_ENV_UNDERWORLD,
  VX_ENV_COUNT,
} VxSpawnEnv;

hz_internal const u32 VX_ENV_CAP[VX_ENV_COUNT] = {4, 8, 7, 7};
hz_internal const char *const VX_ENV_NAMES[VX_ENV_COUNT] = {"surface day", "surface night", "underground", "underworld"};

force_inline b32 vx_cr_night(f32 day_time) { return vx_is_night(day_time); }

hz_internal VxSpawnEnv vx_cr_env(const VxWorld *w, i32 x, i32 y, i32 z, b32 night) {
  if (y < VX_CAVERN_Y) return VX_ENV_UNDERWORLD;
  u32 sky = vx_world_light(w, x, y, z) >> VX_LIGHT_SKY_SHIFT;
  if (y >= VX_UNDERGROUND_Y - 8 && sky >= 8) return night ? VX_ENV_SURFACE_NIGHT : VX_ENV_SURFACE_DAY;
  return VX_ENV_UNDERGROUND;
}

hz_internal u32 vx_cr_slime_for(u8 biome, VxSpawnEnv env, f32 r) {
  if (env == VX_ENV_UNDERWORLD) return VX_SLIME_LAVA;
  if (biome == VX_BIOME_SNOW) return VX_SLIME_ICE;
  if (biome == VX_BIOME_JUNGLE) return VX_SLIME_JUNGLE;
  if (env == VX_ENV_SURFACE_DAY && r < 0.012f) return VX_SLIME_PINKY;
  return env == VX_ENV_UNDERGROUND || r > 0.72f ? VX_SLIME_BLUE : VX_SLIME_GREEN;
}

hz_internal u32 vx_cr_bat_for(u8 biome, VxSpawnEnv env) {
  if (env == VX_ENV_UNDERWORLD) return VX_BAT_HELL;
  if (biome == VX_BIOME_SNOW) return VX_BAT_ICE;
  if (biome == VX_BIOME_JUNGLE) return VX_BAT_JUNGLE;
  return VX_BAT_CAVE;
}

// the creature an environment spawns at a spot, with its variant
hz_internal VxCreatureKind vx_cr_pick(Random *rng, VxSpawnEnv env, u8 biome, i32 y, u32 *variant) {
  f32 r = random_f32(rng), r2 = random_f32(rng);
  *variant = 0;
  switch (env) {
  case VX_ENV_SURFACE_DAY:
    if (biome == VX_BIOME_CORRUPTION && r < 0.55f) return VX_CR_EATER;
    *variant = vx_cr_slime_for(biome, env, r2);
    return VX_CR_SLIME;
  case VX_ENV_SURFACE_NIGHT:
    if (biome == VX_BIOME_CORRUPTION && r < 0.45f) return VX_CR_EATER;
    if (r < 0.55f) return VX_CR_ZOMBIE;
    if (r < 0.9f) return VX_CR_DEMON_EYE;
    *variant = vx_cr_slime_for(biome, env, r2);
    return VX_CR_SLIME;
  case VX_ENV_UNDERGROUND:
    // skeletons keep to the caverns
    if (y < VX_UNDERGROUND_Y - 20 && r < 0.3f) return VX_CR_SKELETON;
    if (r < 0.65f) {
      *variant = vx_cr_bat_for(biome, env);
      return VX_CR_BAT;
    }
    *variant = vx_cr_slime_for(biome, env, r2);
    return VX_CR_SLIME;
  default:
    if (r < 0.6f) {
      *variant = VX_BAT_HELL;
      return VX_CR_BAT;
    }
    *variant = VX_SLIME_LAVA;
    return VX_CR_SLIME;
  }
}

// open ground nearest the player's height in a column: a floor with three free blocks over it, out of liquid
hz_internal b32 vx_cr_ground_in(const VxWorld *w, i32 x, i32 z, i32 py, i32 *out_y) {
  for (i32 k = 0; k <= 48; k++) {
    i32 y = py + ((k & 1) ? (k + 1) / 2 : -(k / 2));
    if (y < 2 || y >= VX_HEIGHT - 4) continue;
    u8 floor = vx_world_block(w, x, y - 1, z);
    if (vx_solid[floor] != 1 || vx_is_leaves(floor)) continue;
    b32 open = true;
    for (i32 d = 0; d < 3 && open; d++) {
      u8 b = vx_world_block(w, x, y + d, z);
      open = !vx_solid[b] && vx_shape[b] != VX_SHAPE_LIQUID;
    }
    if (!open) continue;
    *out_y = y;
    return true;
  }
  return false;
}

hz_internal void vx_cr_spawn_update(VxGame *g, f32 dt) {
  VxCreatures *cs = &g->mobs;
  const VxWorld *w = &g->world;
  const VxPlayer *p = &g->player;
  cs->spawn_timer -= dt;
  if (cs->spawn_timer > 0 || !g->started || g->vitals.dead || p->flying || cs->natural_off) return;
  cs->spawn_timer = VX_CR_SPAWN_EVERY * random_f32_range(&cs->rng, 0.6f, 1.4f);
  b32 night = vx_cr_night(g->day_time);
  i32 px = vx_floor_i(p->pos.x), py = vx_floor_i(p->pos.y + 0.5), pz = vx_floor_i(p->pos.z);
  VxSpawnEnv here = vx_cr_env(w, px, py, pz, night);
  // the cap counts creatures near the player: another layer's crowd does not crowd this one
  u32 nearby = 0;
  for (u32 i = 0; i < cs->count; i++) {
    const VxCreature *m = &cs->items[i];
    f64 dx = m->pos.x - p->pos.x, dy = m->pos.y - p->pos.y, dz = m->pos.z - p->pos.z;
    nearby += !vx_creature_friendly(m->kind) && dx * dx + dy * dy + dz * dz < 72.0 * 72.0;
  }
  if (nearby >= VX_ENV_CAP[here]) return;
  for (u32 attempt = 0; attempt < 4; attempt++) {
    f32 a = random_f32_range(&cs->rng, -PI, PI), r = random_f32_range(&cs->rng, VX_CR_SPAWN_MIN, VX_CR_SPAWN_MAX);
    i32 x = vx_floor_i(p->pos.x + m_sinf(a) * r), z = vx_floor_i(p->pos.z + m_cosf(a) * r);
    if (!vx_in_world(x, z) || !vx_world_is_ready(w, x + 0.5, z + 0.5)) continue;
    i32 y;
    if (!vx_cr_ground_in(w, x, z, py, &y)) continue;
    VxSpawnEnv env = vx_cr_env(w, x, y + 1, z, night);
    // a spot in another layer than the player's spawns nothing: caves under a surface player stay empty
    if (env != here) continue;
    u8 biome = vx_biome_at(&w->terrain, x, y, z);
    u32 variant;
    VxCreatureKind kind = vx_cr_pick(&cs->rng, env, biome, y, &variant);
    u8 beh = vx_creature_info[kind].behaviour;
    f64 fy = y + 0.01;
    if (beh == VX_BEH_SWOOPER || beh == VX_BEH_FLUTTERER) fy += random_f32_range(&cs->rng, 1.0f, 2.0f);
    VxCreature *m = vx_creature_spawn(cs, w, kind, variant, (VxV3d){x + 0.5, fy, z + 0.5});
    if (m && g->scripted)
      log_info("creatures: spawned % at % % % in % (% alive)", fmt_cstr(vx_creature_name(m)), fmt_i32(x), fmt_i32(y), fmt_i32(z), fmt_cstr(VX_ENV_NAMES[env]), fmt_u32(cs->count));
    return;
  }
}

// ---- townsfolk: the Guide keeps to his hut, the market keepers to their tables on the main street ----

#define VX_TALK_REACH 7.0f
#define VX_TIP_FIRST 2u

// the greeting, the night warning, then tips in turn
hz_internal const char *const VX_GUIDE_LINES[] = {
    "Greetings, traveller. I am the Guide. Right-click me whenever you need a hint about surviving out here.",
    "Night has fallen. Zombies and demon eyes roam until dawn: wall yourself in with a door, or stay close to my hut.",
    "Everything starts with wood. Chop trees with your axe, then make a Work Bench from ten planks to craft at it.",
    "Slimes drop gel. One gel and one plank make three torches, and a lit cave is a safe cave.",
    "Ore lies deeper the rarer it is: copper and tin near the surface, silver and gold further down.",
    "Smash the pots you find in caves. They hold coins, torches and healing potions.",
    "When you are hurt, press H to drink a healing potion. Do not wait until it is too late.",
    "If you see a star fall at night, go and find it before morning. Fallen stars feed magic.",
    "Glowing mushrooms grow in deep mud caverns. Their light carries a long way in the dark.",
    "Lava waits at the bottom of the world. Never dig straight down.",
    "A grappling hook will catch on almost anything. Use it to climb out of trouble.",
};

// what each keeper says, in turn
hz_internal const char *const VX_KEEPER_LINES[VX_ROLE_COUNT][2] = {
    {"", ""},
    {"Fine wares at fair prices! Torches, rope, a potion or two. Coin first, mind.",
     "If it glitters underground, bring it here. I pay in silver."},
    {"A good coat makes the adventurer. That one of yours could use a hem.",
     "Every banner on the long hall was stitched by these hands."},
    {"The Hallow spreads beyond the hill. Its trees sing if you listen at dawn.",
     "Keep the corruption from the town's gardens, and the town keeps you."},
    {"The greenhouse frame is copper: it never rusts, only greens.",
     "Wires, levers, pressure plates. Give me a week and the doors will open themselves."},
    {"Those four sets on the stands? Sun, storm, nebula and stardust. Not for sale.",
     "Mind the anvil. Last fellow who leaned on it lost a beard."},
};

// a townsperson's home spot, and the way a keeper faces at its table; false while it has none
hz_internal b32 vx_cr_home(const VxGame *g, u32 role, f64 *x, i32 *y, f64 *z, f32 *yaw) {
  if (role == VX_ROLE_GUIDE) {
    *x = VX_HUT_X + VX_HUT_W * 0.5, *z = VX_HUT_Z + VX_HUT_D * 0.5, *yaw = 0;
    *y = vx_sample_column(&g->world.terrain, vx_floor_i(*x), vx_floor_i(*z)).height + 1;
    return true;
  }
  return vx_town_keeper(&g->world.terrain, role, x, y, z, yaw);
}

// true if a walker one step along (dx, dz) would drop more than two blocks or wade in
hz_internal b32 vx_cr_ledge_ahead(const VxWorld *w, const VxCreature *m, f32 dx, f32 dz) {
  i32 x = vx_floor_i(m->pos.x + dx), z = vx_floor_i(m->pos.z + dz), y = vx_floor_i(m->pos.y + 0.05);
  u8 at = vx_world_block(w, x, y, z);
  if (vx_shape[at] == VX_SHAPE_LIQUID) return true;
  for (i32 k = 1; k <= 3; k++) {
    u8 b = vx_world_block(w, x, y - k, z);
    if (vx_shape[b] == VX_SHAPE_LIQUID) return true;
    if (vx_solid[b] == 1) return false;
  }
  return true;
}

// strolls and stands about home by day, keeps indoors at night, waves when the player comes near, stops to talk
hz_internal void vx_cr_townsfolk(VxGame *g, VxCreature *m, u32 blocked, b32 talking, f32 dt) {
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  VxCreatures *cs = &g->mobs;
  const VxPlayer *p = &g->player;
  f64 home_x, home_z;
  i32 home_y;
  f32 home_yaw;
  if (!vx_cr_home(g, m->variant, &home_x, &home_y, &home_z, &home_yaw))
    home_x = m->pos.x, home_y = vx_floor_i(m->pos.y), home_z = m->pos.z, home_yaw = m->yaw;
  b32 keeper = m->variant != VX_ROLE_GUIDE;
  f32 hx = (f32)(home_x - m->pos.x), hz = (f32)(home_z - m->pos.z);
  f32 hd = m_sqrtf(hx * hx + hz * hz);
  // keepers mind their tables; the Guide wanders by day and stays in by night
  f32 roam = keeper ? 0.0f : vx_cr_night(g->day_time) ? 2.5f : 9.0f;
  f32 px = (f32)(p->pos.x - m->pos.x), pz = (f32)(p->pos.z - m->pos.z);
  f32 pd = m_sqrtf(px * px + pz * pz);
  m->timer -= dt;
  // out past the roam at night, he goes home as soon as the player cannot see him, as Terraria's townsfolk do
  if (hd > roam + 1.5f && vx_cr_night(g->day_time) && !talking) {
    VxV3d c = g->cam.pos;
    v3 to = {(f32)(m->pos.x - c.x), (f32)(m->pos.y + 1.4 - c.y), (f32)(m->pos.z - c.z)};
    f32 l = v3_length(to);
    if (l > 48 || v3_dot(to, g->cam.fwd) < 0.25f * l) {
      m->pos = (VxV3d){home_x, home_y + 0.01, home_z};
      m->vel = (v3){0};
      m->strolling = 0;
      m->timer = random_f32_range(&cs->rng, 2.0f, 6.0f);
      return;
    }
  }
  if (talking) {
    m->strolling = 0;
  } else if (keeper) {
    // a keeper only steps straight back to its spot
    m->strolling = hd > 0.6f;
    if (m->strolling) m->jitter_a = m_atan2f(-hx, -hz);
  } else if (m->timer <= 0 || (m->strolling == 0 && hd > roam + 1.5f)) {
    m->strolling = (u8)(!m->strolling || hd > roam);
    if (m->strolling) {
      // a fresh heading, turned back toward home when out past the roam
      m->jitter_a = hd > roam ? m_atan2f(-hx, -hz) + random_f32_range(&cs->rng, -0.4f, 0.4f)
                              : random_f32_range(&cs->rng, -PI, PI);
      m->timer = random_f32_range(&cs->rng, 1.5f, 4.0f);
    } else {
      m->timer = random_f32_range(&cs->rng, 2.0f, 6.0f);
    }
  }
  f32 dx = -m_sinf(m->jitter_a), dz = -m_cosf(m->jitter_a);
  if (m->strolling && m->on_ground) {
    // the step is tried toward what it bumped, and only while still heading that way; a wall too high turns it around
    f32 sx = (blocked & 1u) ? (m->vel.x > 0 ? 0.3f : -0.3f) : 0, sz = (blocked & 4u) ? (m->vel.z > 0 ? 0.3f : -0.3f) : 0;
    b32 wall = (sx != 0 || sz != 0) && sx * dx + sz * dz > 0 && !vx_cr_step_up(&g->world, m, sx, sz);
    if (wall || vx_cr_ledge_ahead(&g->world, m, dx * 0.7f, dz * 0.7f)) {
      m->jitter_a += PI + random_f32_range(&cs->rng, -0.5f, 0.5f);
      dx = -m_sinf(m->jitter_a), dz = -m_cosf(m->jitter_a);
    }
  }
  f32 sp = m->strolling ? ci->speed : 0;
  f32 blend = 1 - m_expf(-(m->on_ground ? 8.0f : 2.0f) * dt);
  m->vel.x += (dx * sp - m->vel.x) * blend;
  m->vel.z += (dz * sp - m->vel.z) * blend;
  if (talking) vx_cr_face(m, px, pz, m_minf(1, dt * 8));
  else if (m->strolling) vx_cr_face(m, dx, dz, m_minf(1, dt * 6));
  else if (keeper && hd < 1.0f) vx_cr_face(m, -m_sinf(home_yaw), -m_cosf(home_yaw), m_minf(1, dt * 4));
  f32 hs = m_sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
  m->anim += hs * dt * 2.4f;
  // the head follows the player while they are near
  f32 look = 0;
  if (pd < VX_TALK_REACH + 3) look = m_clampf(m_wrap_pi(m_atan2f(-px, -pz) - m->yaw), -0.9f, 0.9f);
  m->look += (look - m->look) * m_minf(1, dt * 6);
  m->wave = m_maxf(0, m->wave - dt);
  if (pd < 6 && !m->greeted && g->started && !g->vitals.dead) {
    m->greeted = 1;
    m->wave = 1.4f;
  } else if (pd > 16) {
    m->greeted = 0;
  }
}

// each townsperson lives at its home: placed there whenever it is missing and its floor is loaded
hz_internal void vx_cr_townsfolk_ensure(VxGame *g) {
  VxCreatures *cs = &g->mobs;
  const VxWorld *w = &g->world;
  if (!g->started) return;
  u32 present = 0;
  for (u32 i = 0; i < cs->count; i++)
    if (cs->items[i].kind == VX_CR_GUIDE) present |= 1u << (cs->items[i].variant % VX_ROLE_COUNT);
  for (u32 role = 0; role < VX_ROLE_COUNT; role++) {
    f64 x, z;
    i32 fy;
    f32 yaw;
    if ((present >> role) & 1u || !vx_cr_home(g, role, &x, &fy, &z, &yaw) || !vx_world_is_ready(w, x, z)) continue;
    VxCreature *m = vx_creature_spawn(cs, w, VX_CR_GUIDE, role, (VxV3d){x, fy + 0.01, z});
    if (!m) return;
    if (role != VX_ROLE_GUIDE) m->yaw = yaw;
    // a zero seed means no talk partner
    m->seed |= 1u;
    if (g->scripted)
      log_info("creatures: the % is at home, % % %", fmt_cstr(VX_ROLE_NAMES[role]), fmt_f64_p(x, 1), fmt_i32(fy), fmt_f64_p(z, 1));
  }
}

hz_internal const VxCreature *vx_cr_by_seed(const VxCreatures *cs, u32 seed) {
  for (u32 i = 0; seed && i < cs->count; i++)
    if (cs->items[i].seed == seed && vx_creature_friendly(cs->items[i].kind)) return &cs->items[i];
  return NULL;
}

const VxCreature *vx_talk_partner(const VxGame *g) { return vx_cr_by_seed(&g->mobs, g->talk_seed); }

const char *vx_talk_text(const VxGame *g) {
  const VxCreature *m = vx_talk_partner(g);
  if (m && m->variant != VX_ROLE_GUIDE) return VX_KEEPER_LINES[m->variant % VX_ROLE_COUNT][g->talk_line & 1];
  return VX_GUIDE_LINES[g->talk_line % ARRAY_SIZE(VX_GUIDE_LINES)];
}

b32 vx_creature_talk(VxGame *g, VxV3d o, v3 d) {
  VxCreatures *cs = &g->mobs;
  const VxPlayer *p = &g->player;
  if (g->vitals.dead || v3_length(d) < 1e-4f) return false;
  // the nearest townsperson the ray enters, by slab test against its box
  VxCreature *best = NULL;
  f32 best_t = 32.0f;
  for (u32 i = 0; i < cs->count; i++) {
    VxCreature *m = &cs->items[i];
    if (!vx_creature_friendly(m->kind)) continue;
    f64 ex = m->pos.x - p->pos.x, ez = m->pos.z - p->pos.z;
    if (ex * ex + ez * ez > VX_TALK_REACH * VX_TALK_REACH) continue;
    f32 half, h;
    vx_creature_size(m, &half, &h);
    v3 lo = {(f32)(m->pos.x - half - o.x), (f32)(m->pos.y - o.y), (f32)(m->pos.z - half - o.z)};
    v3 hi = {lo.x + 2 * half, lo.y + h, lo.z + 2 * half}, dv = d;
    f32 t0 = 0, t1 = best_t;
    for (u32 a = 0; a < 3 && t0 <= t1; a++) {
      f32 la = *vx_cr_vaxis(&lo, a), ha = *vx_cr_vaxis(&hi, a), da = *vx_cr_vaxis(&dv, a);
      if (m_absf(da) < 1e-6f) {
        if (la > 0 || ha < 0) t0 = t1 + 1;
        continue;
      }
      f32 ta = la / da, tb = ha / da;
      t0 = m_maxf(t0, m_minf(ta, tb));
      t1 = m_minf(t1, m_maxf(ta, tb));
    }
    if (t0 <= t1) best = m, best_t = t0;
  }
  if (!best) return false;
  // a wall between the eye and the townsperson hides them
  for (f32 t = 0.1f; t < best_t; t += 0.1f) {
    u8 b = vx_world_block(&g->world, vx_floor_i(o.x + d.x * t), vx_floor_i(o.y + d.y * t), vx_floor_i(o.z + d.z * t));
    if (vx_solid[b] == 1) return false;
  }
  // the greeting once, the night warning on opening a talk at night, otherwise the next tip in turn
  u32 tips = ARRAY_SIZE(VX_GUIDE_LINES) - VX_TIP_FIRST;
  if (g->talk_count == 0) g->talk_line = 0, g->talk_count = 1;
  else if (g->talk_seed != best->seed && vx_cr_night(g->day_time)) g->talk_line = 1;
  else g->talk_line = VX_TIP_FIRST + (g->talk_count++ - 1) % tips;
  g->talk_seed = best->seed;
  best->wave = m_maxf(best->wave, 0.6f);
  if (g->scripted) log_info("talk: %: %", fmt_cstr(vx_creature_name(best)), fmt_cstr(vx_talk_text(g)));
  return true;
}

// a talk ends when the player walks away or leaves play; the first night brings the Guide's warning
hz_internal void vx_cr_talk_update(VxGame *g) {
  const VxCreature *m = vx_talk_partner(g);
  if (m) {
    f64 dx = m->pos.x - g->player.pos.x, dz = m->pos.z - g->player.pos.z;
    if (dx * dx + dz * dz > (VX_TALK_REACH + 2) * (VX_TALK_REACH + 2) || g->vitals.dead || g->mode == VX_MODE_INVENTORY)
      m = NULL;
  }
  if (!m) g->talk_seed = 0;
  if (!g->night_hint && g->started && vx_cr_night(g->day_time)) {
    for (u32 i = 0; i < g->mobs.count; i++) {
      if (g->mobs.items[i].kind != VX_CR_GUIDE) continue;
      g->night_hint = true;
      vx_event(g, STR_FROM_CSTR("The Guide: Night has fallen. Find shelter, or stay close to my hut until dawn."));
      break;
    }
  }
}

// ---- the Eye of Cthulhu: hovers sending Servants, dashes in volleys of three, and at half life tears open a mouth ----

typedef enum {
  VX_EOC_HOVER,     // drifts above and ahead of the player; the first form sends Servants
  VX_EOC_AIM,       // stops and turns on the player before a dash
  VX_EOC_DASH,      // charges through where the player was
  VX_EOC_TRANSFORM, // spins up, sheds its iris, spins down with a mouth
  VX_EOC_FLEE,      // dawn or a dead player: rises away
  VX_EMP_BOLTS,     // the Empress: fans prismatic bolts out all round her
  VX_EMP_LANCE,     // the Empress: throws crescents at the player
} VxEocState;

#define VX_EOC_R 3.0f    // the eyeball's drawn radius, its centre half its box's height up
#define VX_EOC_TRANSFORM_S 2.4f
#define VX_EMP_CORE 4.5f // the Empress's chest above her feet, where her bolts start

// an srgb 0xRRGGBB as the motes' packed rgba
hz_internal u32 vx_cr_abgr(u32 rgb, f32 a) {
  return ((rgb >> 16) & 255) | (rgb & 0xFF00) | ((rgb & 255) << 16) | (u32)(m_saturatef(a) * 255.0f + 0.5f) << 24;
}

i32 vx_boss_index(const VxGame *g) {
  for (u32 i = 0; i < g->mobs.count; i++)
    if (vx_creature_info[g->mobs.items[i].kind].behaviour == VX_BEH_BOSS && g->mobs.items[i].hp > 0) return (i32)i;
  return -1;
}

VxCreature *vx_boss_summon(VxGame *g, VxCreatureKind kind, b32 force) {
  if (vx_boss_index(g) >= 0 || !g->started || g->vitals.dead) return NULL;
  if (!force && !vx_cr_night(g->day_time)) return NULL;
  // it arrives high over the land ahead of the player, so its approach is seen
  b32 emp = kind == VX_CR_EMPRESS;
  const VxPlayer *p = &g->player;
  v3 f = {g->cam.fwd.x, 0, g->cam.fwd.z};
  f = v3_length(f) > 1e-3f ? v3_normalize(f) : (v3){0, 0, -1};
  f32 out = emp ? 22.0f : 34.0f;
  VxV3d at = {p->pos.x + f.x * out, p->pos.y + (emp ? 9 : 14), p->pos.z + f.z * out};
  VxCreature *m = vx_creature_spawn(&g->mobs, &g->world, emp ? VX_CR_EMPRESS : VX_CR_EYE_OF_CTHULHU, 0, at);
  if (!m) return NULL;
  m->boss_phase = 1;
  m->boss_state = VX_EOC_HOVER;
  m->timer = emp ? 2.5f : 3.0f;
  m->voice_t = 1.2f;
  m->aggro = 1;
  m->dashes = 0;
  m->yaw = m_atan2f(f.x, f.z);
  g->boss_pending = 0;
  vx_event_rgb(g, emp ? STR_FROM_CSTR("Empress of Light has awoken!") : STR_FROM_CSTR("Eye of Cthulhu has awoken!"), 0xAF4BFF);
  vx_audio_play(&g->audio, VX_SND_ROAR);
  if (!vx_cr_night(g->day_time)) log_warn("creatures: % was summoned by day and will flee at once", fmt_cstr(vx_creature_name(m)));
  return m;
}

// at dusk a warning, then a boss: the Eye for a player with 200 life and iron armour, once it is beaten the Empress over the Hallow
hz_internal void vx_cr_boss_event(VxGame *g, f32 dt) {
  b32 night = g->started && vx_cr_night(g->day_time);
  if (night && !g->boss_was_night && vx_boss_index(g) < 0 && g->boss_pending <= 0) {
    if (!g->boss_defeated && g->vitals.hp_max >= 200 && vx_inventory_defense(&g->inv) >= 7 &&
        random_f32(&g->mobs.rng) < 1.0f / 3.0f) {
      vx_event_rgb(g, STR_FROM_CSTR("You feel an evil presence watching you..."), 0x32FF82);
      g->boss_pending = 8.0f;
      g->boss_pending_kind = VX_CR_EYE_OF_CTHULHU;
    } else if (g->boss_defeated && !g->empress_defeated && g->biome_w[VX_BIOME_HALLOW] > 0.5f) {
      vx_event_rgb(g, STR_FROM_CSTR("The dusk shimmers with prismatic light..."), 0xFF8CE6);
      g->boss_pending = 6.0f;
      g->boss_pending_kind = VX_CR_EMPRESS;
    }
  }
  g->boss_was_night = night;
  if (g->boss_pending > 0) {
    g->boss_pending -= dt;
    if (g->boss_pending <= 0) vx_boss_summon(g, (VxCreatureKind)g->boss_pending_kind, false);
  }
}

// the Empress: hovers before the player, then fans bolts, throws crescents or charges in turn; quicker below half life
hz_internal void vx_cr_empress(VxGame *g, VxCreature *m, v3 to, f32 dt) {
  VxCreatures *cs = &g->mobs;
  f32 dist = v3_length(to);
  v3 dir = dist > 1e-3f ? v3_scale(to, 1.0f / dist) : (v3){0, 0, -1};
  v3 look = dir;
  if (m->boss_state != VX_EOC_FLEE && (g->vitals.dead || !vx_cr_night(g->day_time))) {
    m->boss_state = VX_EOC_FLEE;
    if (g->scripted) log_info("creatures: Empress of Light flees");
  }
  VxV3d c = {m->pos.x, m->pos.y + VX_EMP_CORE, m->pos.z};
  if (m->boss_phase == 1 && m->hp < m->hp_max * 0.5f) {
    m->boss_phase = 2;
    vx_entities_death_burst(&g->ents, c, 2.4f, vx_cr_abgr(0xFFB4F0, 1));
    vx_audio_play(&g->audio, VX_SND_ROAR);
    vx_shake(g, 0.4f);
    if (g->scripted) log_info("creatures: Empress of Light grows furious");
  }
  b32 p2 = m->boss_phase == 2;
  m->timer -= dt;
  f32 raise = 0;
  // a frame facing the player: her bolts fan out in its plane
  v3 side = v3_cross(dir, (v3){0, 1, 0});
  side = v3_length(side) > 1e-3f ? v3_normalize(side) : (v3){1, 0, 0};
  v3 up = v3_cross(side, dir);
  switch (m->boss_state) {
  case VX_EOC_HOVER: {
    // ahead of the player and up, pulled round in front of the camera like the Eye's
    v3 flat = {-to.x, 0, -to.z};
    f32 fl = v3_length(flat);
    flat = fl > 1e-3f ? v3_scale(flat, 1.0f / fl) : (v3){-m_sinf(m->yaw), 0, -m_cosf(m->yaw)};
    v3 view = {g->cam.fwd.x, 0, g->cam.fwd.z};
    f32 vl = v3_length(view);
    if (vl > 1e-3f) {
      flat = v3_add(flat, v3_scale(view, 1.5f / vl));
      fl = v3_length(flat);
      flat = fl > 1e-3f ? v3_scale(flat, 1.0f / fl) : v3_scale(view, 1.0f / vl);
    }
    v3 goal = v3_add(v3_add(to, v3_scale(flat, 11.0f)), (v3){0, 3.0f + 0.8f * m_sinf(m->anim * 0.9f), 0});
    f32 gl = v3_length(goal), top = p2 ? 14.0f : 10.0f;
    v3 want = gl > 0.3f ? v3_scale(goal, m_minf(top, gl * 2.0f) / gl) : (v3){0, 0, 0};
    m->vel = v3_add(m->vel, v3_scale(v3_sub(want, m->vel), 1 - m_expf(-3.0f * dt)));
    if (m->timer <= 0) {
      // bolts, crescents, bolts, a charge, round again
      u32 step = m->dashes++ % 4u;
      m->jitter_b = 0;
      m->jitter_a = 0;
      if (step == 1) {
        m->boss_state = VX_EMP_LANCE;
        m->timer = 1.2f;
      } else if (step == 3) {
        m->boss_state = VX_EOC_AIM;
        m->timer = 0.45f;
      } else {
        m->boss_state = VX_EMP_BOLTS;
        m->timer = 1.3f;
        vx_audio_play(&g->audio, VX_SND_SHIMMER);
      }
    }
  } break;
  case VX_EMP_BOLTS: {
    m->vel = v3_scale(m->vel, m_expf(-4.0f * dt));
    raise = 1;
    m->jitter_b += (p2 ? 46.0f : 30.0f) * dt;
    for (; m->jitter_b >= 1; m->jitter_b -= 1) {
      // the golden angle spreads each bolt into the widest gap of the fan
      m->spin = m_fmodf(m->spin + 2.3999632f, 2 * PI);
      v3 o = v3_add(v3_scale(side, m_cosf(m->spin)), v3_scale(up, m_sinf(m->spin)));
      VxV3d at = {c.x + o.x * 1.3, c.y + o.y * 1.3, c.z + o.z * 1.3};
      vx_combat_hostile(g, VX_PROJ_PRISM, at, v3_add(v3_scale(o, 9.0f), v3_scale(dir, 2.0f)), m->spin / (2 * PI), 4.5f,
                        p2 ? 36.0f : 30.0f);
    }
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_HOVER;
      m->timer = p2 ? 1.2f : 2.0f;
    }
  } break;
  case VX_EMP_LANCE: {
    m->vel = v3_scale(m->vel, m_expf(-4.0f * dt));
    raise = 0.6f;
    f32 n = p2 ? 5.0f : 3.0f, k = 1 - m->timer / 1.2f;
    // crescents from alternate hands, led onto where the player is going
    while (m->jitter_a < n && k >= m->jitter_a / n) {
      f32 s = ((u32)m->jitter_a & 1u) ? 1.0f : -1.0f;
      VxV3d from = {c.x + side.x * s * 2.4f, c.y + 0.9, c.z + side.z * s * 2.4f};
      const VxPlayer *p = &g->player;
      VxV3d tgt = {p->pos.x + p->vel.x * 0.35f, p->pos.y + VX_PLAYER_HEIGHT * 0.5 + p->vel.y * 0.2f, p->pos.z + p->vel.z * 0.35f};
      v3 d = {(f32)(tgt.x - from.x), (f32)(tgt.y - from.y), (f32)(tgt.z - from.z)};
      d = v3_length(d) > 1e-3f ? v3_normalize(d) : dir;
      vx_combat_hostile(g, VX_PROJ_LANCE, from, v3_scale(d, p2 ? 21.0f : 17.0f), 0.46f + 0.04f * s, 3.5f, p2 ? 44.0f : 38.0f);
      vx_audio_play(&g->audio, VX_SND_SPARK);
      m->jitter_a += 1;
    }
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_HOVER;
      m->timer = p2 ? 1.4f : 2.2f;
    }
  } break;
  case VX_EOC_AIM:
    m->vel = v3_scale(m->vel, m_expf(-7.0f * dt));
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_DASH;
      m->timer = 0.8f;
      m->vel = v3_scale(dir, p2 ? 30.0f : 24.0f);
      vx_audio_play(&g->audio, VX_SND_BOSS_DASH);
      vx_shake(g, dist < 16 ? 0.15f : 0.0f);
    }
    break;
  case VX_EOC_DASH: {
    look = v3_length(m->vel) > 1 ? v3_normalize(m->vel) : dir;
    if (m->timer < 0.3f) m->vel = v3_scale(m->vel, m_expf(-4.5f * dt));
    // a wake of every colour along the charge
    m->jitter_b += dt * (v3_length(m->vel) > 8 ? 80.0f : 0.0f);
    for (; m->jitter_b >= 1; m->jitter_b -= 1) {
      v3 o = {random_f32_range(&cs->rng, -1, 1), random_f32_range(&cs->rng, -1.6f, 1.6f), random_f32_range(&cs->rng, -1, 1)};
      VxV3d at = {c.x + o.x, c.y + o.y, c.z + o.z};
      vx_entities_streak(&g->ents, at, v3_scale(m->vel, 0.3f), 0.4f, 0.45f, vx_cr_abgr(vx_hue_hex(random_f32(&cs->rng), 0.3f), 1));
    }
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_HOVER;
      m->timer = p2 ? 1.0f : 1.6f;
    }
  } break;
  default:
    m->vel.y = m_minf(m->vel.y + 20.0f * dt, 30.0f);
    m->vel.x *= m_expf(-1.5f * dt);
    m->vel.z *= m_expf(-1.5f * dt);
    break;
  }
  // through blocks, never below the player's feet
  m->pos.x += m->vel.x * dt;
  m->pos.y += m->vel.y * dt;
  m->pos.z += m->vel.z * dt;
  f64 floor_y = g->player.pos.y - 1.0;
  if (m->boss_state != VX_EOC_FLEE && m->pos.y < floor_y) {
    m->pos.y = floor_y;
    m->vel.y = m_maxf(m->vel.y, 0);
  }
  vx_cr_face(m, look.x, look.z, m_minf(1, dt * 6.0f));
  m->mouth += (raise - m->mouth) * m_minf(1, dt * 8.0f);
  m->anim += dt;
}

hz_internal void vx_cr_boss(VxGame *g, VxCreature *m, v3 to, f32 dt) {
  VxCreatures *cs = &g->mobs;
  b32 p2 = m->boss_phase == 2;
  f32 dist = v3_length(to);
  v3 dir = dist > 1e-3f ? v3_scale(to, 1.0f / dist) : (v3){0, 0, -1};
  v3 look = dir;
  if (m->boss_state != VX_EOC_FLEE && (g->vitals.dead || !vx_cr_night(g->day_time))) {
    m->boss_state = VX_EOC_FLEE;
    if (g->scripted) log_info("creatures: Eye of Cthulhu flees");
  }
  if (!p2 && m->hp < m->hp_max * 0.5f && m->boss_state != VX_EOC_TRANSFORM && m->boss_state != VX_EOC_FLEE) {
    m->boss_state = VX_EOC_TRANSFORM;
    m->timer = VX_EOC_TRANSFORM_S;
    if (g->scripted) log_info("creatures: Eye of Cthulhu transforms");
  }
  m->timer -= dt;
  VxV3d c = {m->pos.x, m->pos.y + 2.5, m->pos.z};
  switch (m->boss_state) {
  case VX_EOC_HOVER: {
    // above the player and out along its bearing, pulled round in front of the camera so it never hovers behind the view
    v3 flat = {-to.x, 0, -to.z};
    f32 fl = v3_length(flat);
    flat = fl > 1e-3f ? v3_scale(flat, 1.0f / fl) : (v3){-m_sinf(m->yaw), 0, -m_cosf(m->yaw)};
    v3 view = {g->cam.fwd.x, 0, g->cam.fwd.z};
    f32 vl = v3_length(view);
    if (vl > 1e-3f) {
      flat = v3_add(flat, v3_scale(view, 1.5f / vl));
      fl = v3_length(flat);
      flat = fl > 1e-3f ? v3_scale(flat, 1.0f / fl) : v3_scale(view, 1.0f / vl);
    }
    v3 goal = v3_add(v3_add(to, v3_scale(flat, 11.0f)), (v3){0, 5.0f, 0});
    f32 gl = v3_length(goal), top = p2 ? 16.0f : 12.0f;
    v3 want = gl > 0.3f ? v3_scale(goal, m_minf(top, gl * 2.5f) / gl) : (v3){0, 0, 0};
    m->vel = v3_add(m->vel, v3_scale(v3_sub(want, m->vel), 1 - m_expf(-3.0f * dt)));
    // the first form sends Servants out of its pupil at the player
    m->voice_t -= dt;
    u32 servants = 0;
    for (u32 i = 0; i < cs->count; i++) servants += cs->items[i].kind == VX_CR_SERVANT;
    if (!p2 && m->voice_t <= 0 && servants < 6 && dist < 48) {
      m->voice_t = 0.8f;
      VxCreature *s = vx_creature_spawn(cs, &g->world, VX_CR_SERVANT, 0,
                                        (VxV3d){c.x + dir.x * VX_EOC_R, c.y + dir.y * VX_EOC_R - 0.35, c.z + dir.z * VX_EOC_R});
      if (s) {
        s->vel = v3_scale(dir, 8.0f);
        s->aggro = 1;
        s->yaw = m->yaw;
        vx_entities_burst(&g->ents, VX_MOTE_DROPLET, (VxV3d){c.x + dir.x * VX_EOC_R, c.y + dir.y * VX_EOC_R, c.z + dir.z * VX_EOC_R},
                          v3_scale(dir, 3.0f), 2.5f, 8, 0.6f, 0.12f, vx_cr_abgr(0xB0141E, 1));
        if (g->scripted) log_info("creatures: Eye of Cthulhu sends a Servant");
      }
    }
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_AIM;
      m->timer = p2 ? 0.2f : 0.35f;
      m->dashes = 3;
    }
  } break;
  case VX_EOC_AIM:
    m->vel = v3_scale(m->vel, m_expf(-7.0f * dt));
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_DASH;
      m->timer = p2 ? 0.75f : 0.95f;
      m->vel = v3_scale(dir, p2 ? 27.0f : 21.0f);
      m->dashes--;
      vx_audio_play(&g->audio, VX_SND_BOSS_DASH);
      vx_shake(g, dist < 16 ? (p2 ? 0.2f : 0.12f) : 0.0f);
    }
    break;
  case VX_EOC_DASH: {
    look = v3_length(m->vel) > 1 ? v3_normalize(m->vel) : dir;
    if (m->timer < (p2 ? 0.3f : 0.4f)) m->vel = v3_scale(m->vel, m_expf(-4.5f * dt));
    // a burning wake along the charge, bloodier once it has a mouth
    f32 sp = v3_length(m->vel);
    m->jitter_b += dt * (sp > 8 ? (p2 ? 90.0f : 60.0f) : 0.0f);
    v3 side = v3_cross(look, (v3){0, 1, 0});
    side = v3_length(side) > 1e-3f ? v3_normalize(side) : (v3){1, 0, 0};
    v3 up = v3_cross(side, look);
    for (; m->jitter_b >= 1; m->jitter_b -= 1) {
      f32 a = random_f32_range(&cs->rng, -PI, PI), r = random_f32_range(&cs->rng, 0.2f, 1.0f) * VX_EOC_R * 0.85f;
      v3 o = v3_add(v3_scale(side, m_cosf(a) * r), v3_scale(up, m_sinf(a) * r));
      o = v3_add(o, v3_scale(look, -VX_EOC_R * 0.4f));
      VxV3d at = {c.x + o.x, c.y + o.y, c.z + o.z};
      b32 hot = random_f32(&cs->rng) < 0.3f;
      vx_entities_streak(&g->ents, at, v3_scale(m->vel, 0.3f), 0.32f, hot ? 0.35f : 0.55f,
                         vx_cr_abgr(hot ? 0xFFE0C8 : (p2 ? 0xFF2A2A : 0xFF5A3C), 1));
      if (p2 && random_f32(&cs->rng) < 0.35f)
        vx_entities_burst(&g->ents, VX_MOTE_DROPLET, at, v3_scale(m->vel, 0.15f), 2.0f, 1, 0.8f, 0.14f, vx_cr_abgr(0x9A1018, 1));
    }
    if (m->timer <= 0) {
      m->boss_state = m->dashes > 0 ? VX_EOC_AIM : VX_EOC_HOVER;
      m->timer = m->dashes > 0 ? (p2 ? 0.15f : 0.3f) : (p2 ? 1.3f : 3.4f);
      m->voice_t = 0.5f;
    }
  } break;
  case VX_EOC_TRANSFORM: {
    m->vel = v3_scale(m->vel, m_expf(-3.0f * dt));
    // the roll rate swells and fades over the change; the iris tears away at the peak
    f32 k = m_saturatef(1 - m->timer / VX_EOC_TRANSFORM_S);
    m->spin = m_fmodf(m->spin + m_sinf(k * PI) * 30.0f * dt, 2 * PI);
    if (k >= 0.5f && m->boss_phase == 1) {
      m->boss_phase = 2;
      VxV3d front = {c.x + dir.x * VX_EOC_R, c.y + dir.y * VX_EOC_R, c.z + dir.z * VX_EOC_R};
      hz_internal const u32 iris[4] = {0x3C78D8, 0x24489A, 0x0A0A0E, 0xF2EEE6};
      vx_entities_debris(&g->ents, front, v3_scale(dir, 6.0f), 5.0f, 34, 0.4f, iris, 4);
      vx_entities_burst(&g->ents, VX_MOTE_DROPLET, front, v3_add(v3_scale(dir, 4.0f), (v3){0, 3, 0}), 5.0f, 40, 1.0f, 0.16f,
                        vx_cr_abgr(0xA0101A, 1));
      vx_entities_death_burst(&g->ents, front, 1.6f, vx_cr_abgr(0xFF3040, 1));
      vx_audio_play(&g->audio, VX_SND_ROAR);
      vx_shake(g, 0.6f);
      vx_hitstop(g, 0.12f);
      if (g->scripted) log_info("creatures: Eye of Cthulhu bares its mouth");
    }
    if (m->timer <= 0) {
      m->boss_state = VX_EOC_HOVER;
      m->timer = 0.8f;
      m->spin = 0;
    }
  } break;
  default:
    m->vel.y = m_minf(m->vel.y + 30.0f * dt, 40.0f);
    m->vel.x *= m_expf(-1.5f * dt);
    m->vel.z *= m_expf(-1.5f * dt);
    look = v3_normalize((v3){look.x, 1.5f, look.z});
    break;
  }
  // it passes through blocks, as Terraria's does, but never sinks below the player's feet: a buried eye is an unseen one
  m->pos.x += m->vel.x * dt;
  m->pos.y += m->vel.y * dt;
  m->pos.z += m->vel.z * dt;
  f64 floor_y = g->player.pos.y - 0.4;
  if (m->boss_state != VX_EOC_FLEE && m->pos.y < floor_y) {
    m->pos.y = floor_y;
    m->vel.y = m_maxf(m->vel.y, 0);
  }
  f32 k = m_minf(1, dt * (m->boss_state == VX_EOC_DASH ? 14.0f : 6.0f));
  vx_cr_face(m, look.x, look.z, k);
  m->pitch += (m_asinf(m_clampf(look.y, -1, 1)) - m->pitch) * k;
  // the mouth gapes and snaps while it hunts
  m->mouth = p2 ? 0.8f + 0.2f * m_sinf(m->anim * (m->boss_state == VX_EOC_DASH ? 16.0f : 7.0f)) : 0;
  m->anim += dt;
}

void vx_creatures_update(VxGame *g, f32 dt) {
  vx_cr_spawn_update(g, dt);
  vx_cr_townsfolk_ensure(g);
  vx_cr_talk_update(g);
  vx_cr_boss_event(g, dt);
  VxCreatures *cs = &g->mobs;
  const VxWorld *w = &g->world;
  VxPlayer *p = &g->player;
  b32 alive = g->started && !g->vitals.dead;
  f32 defense = (f32)vx_inventory_defense(&g->inv);
  for (u32 i = 0; i < cs->count;) {
    VxCreature *m = &cs->items[i];
    const VxCreatureInfo *ci = &vx_creature_info[m->kind];
    b32 friendly = ci->behaviour == VX_BEH_TOWNSFOLK;
    f64 ddx = p->pos.x - m->pos.x, ddy = p->pos.y - m->pos.y, ddz = p->pos.z - m->pos.z;
    // at dawn the night's creatures leave the surface, out of sight first
    b32 dawn = !vx_cr_night(g->day_time) && m->pos.y >= VX_UNDERGROUND_Y - 8 &&
               (m->kind == VX_CR_ZOMBIE || m->kind == VX_CR_DEMON_EYE || m->kind == VX_CR_SERVANT);
    // the Eye stays for its fight wherever it flies, and leaves once it has fled far enough
    b32 boss = ci->behaviour == VX_BEH_BOSS;
    f64 reach = boss ? (m->boss_state == VX_EOC_FLEE ? 90.0 : 1e9) : dawn ? 40.0 : VX_CR_DESPAWN;
    if ((!friendly && ddx * ddx + ddy * ddy + ddz * ddz > reach * reach) || m->hp <= 0) {
      if (boss && m->hp > 0 && g->scripted) log_info("creatures: % has gone", fmt_cstr(vx_creature_name(m)));
      cs->items[i] = cs->items[--cs->count];
      continue;
    }
    // creatures wait while their ground is not loaded
    if (!boss && !vx_world_is_ready(w, m->pos.x, m->pos.z)) {
      i++;
      continue;
    }
    f32 half, h;
    vx_creature_size(m, &half, &h);
    m->age += dt;
    m->flash = m_maxf(0, m->flash - dt * 6);
    m->immune = m_maxf(0, m->immune - dt);
    m->show_hp = m_maxf(0, m->show_hp - dt);
    v3 to = {(f32)ddx, (f32)(p->pos.y + 1.4 - (m->pos.y + h * 0.5)), (f32)ddz};
    b32 aggro = alive && !dawn && !friendly && v3_length(to) < VX_CR_AGGRO;
    m->aggro = (u8)aggro;
    b32 flyer = ci->behaviour == VX_BEH_SWOOPER || ci->behaviour == VX_BEH_FLUTTERER || boss;
    if (!flyer) m->vel.y = m_maxf(m->vel.y - VX_CR_GRAVITY * dt, -VX_CR_MAX_FALL);
    b32 was_ground = m->on_ground;
    f32 fall_v = m->vel.y;
    u32 blocked = boss ? 0 : vx_cr_move(w, m, dt);
    m->on_ground = (u8)((blocked & 2u) != 0);
    if (blocked & 10u) m->vel.y = flyer ? m->vel.y : 0;
    if (ci->behaviour == VX_BEH_HOPPER) {
      if (m->on_ground && !was_ground && fall_v < -4) m->squash = -0.38f;
      vx_cr_hopper(cs, m, to, aggro, dt);
    } else if (ci->behaviour == VX_BEH_WALKER) {
      vx_cr_walker(w, cs, m, to, aggro, blocked, dt);
      // zombies moan now and then, louder the nearer they are
      f32 d = v3_length(to);
      m->voice_t -= dt;
      if (m->kind == VX_CR_ZOMBIE && m->voice_t <= 0) {
        m->voice_t = random_f32_range(&cs->rng, 5.0f, 12.0f);
        vx_audio_play_vol(&g->audio, VX_SND_ZOMBIE, 0.75f * m_saturatef(1 - d / 26));
      }
    } else if (friendly) {
      vx_cr_townsfolk(g, m, blocked, g->talk_seed == m->seed, dt);
      i++;
      continue;
    } else if (boss) {
      m->aggro = (u8)alive;
      if (m->kind == VX_CR_EMPRESS) vx_cr_empress(g, m, to, dt);
      else vx_cr_boss(g, m, to, dt);
    } else {
      vx_cr_flyer(cs, m, to, aggro, blocked, dt);
    }
    // contact damage: the creature's box against the player's
    f32 shrink = 0.1f;
    b32 touch = alive && m_absf((f32)ddx) < half + VX_PLAYER_HALF - shrink && m_absf((f32)ddz) < half + VX_PLAYER_HALF - shrink &&
                m->pos.y + shrink < p->pos.y + VX_PLAYER_HEIGHT && p->pos.y < m->pos.y + h - shrink;
    if (touch) {
      f32 dmg = m_maxf(1, m_roundf(vx_cr_damage(m) - defense * 0.5f));
      if (vx_vitals_damage(g, dmg, vx_cr_death_msg(m))) {
        f32 l = m_sqrtf((f32)(ddx * ddx + ddz * ddz));
        f32 nx = l > 1e-3f ? (f32)ddx / l : -m_sinf(m->yaw), nz = l > 1e-3f ? (f32)ddz / l : -m_cosf(m->yaw);
        p->vel.x = nx * 9.0f;
        p->vel.z = nz * 9.0f;
        p->vel.y = m_maxf(p->vel.y, 7.0f);
        p->knock_t = 0.3f;
        vx_world_text_add(g, VX_WT_DAMAGE, (VxV3d){p->pos.x, p->pos.y + VX_PLAYER_HEIGHT + 0.3, p->pos.z}, 0, (u32)dmg);
        // a flyer that lands a hit is thrown back off the player
        if (flyer && !boss) m->vel = (v3){-nx * 6.0f, 3.0f, -nz * 6.0f};
      }
    }
    i++;
  }
}

// copper value of a kill, Terraria's per enemy
hz_internal u32 vx_cr_value(const VxCreature *m) {
  hz_internal const u32 slime[VX_SLIME_COUNT] = {25, 50, 75, 75, 150, 500};
  switch (m->kind) {
  case VX_CR_SLIME: return slime[m->variant % VX_SLIME_COUNT];
  case VX_CR_ZOMBIE: return 60;
  case VX_CR_SKELETON: return 130;
  case VX_CR_DEMON_EYE: return 75;
  case VX_CR_EATER: return 90;
  case VX_CR_EYE_OF_CTHULHU: return 30000;
  case VX_CR_EMPRESS: return 250000;
  case VX_CR_SERVANT: return 0;
  default: return m->variant == VX_BAT_HELL ? 200 : 90;
  }
}

hz_internal void vx_cr_loot(VxGame *g, const VxCreature *m) {
  VxCreatures *cs = &g->mobs;
  f32 half, h;
  vx_creature_size(m, &half, &h);
  VxV3d at = {m->pos.x, m->pos.y + h * 0.5, m->pos.z};
  switch (m->kind) {
  case VX_CR_SLIME:
    vx_entities_spawn_drop(&g->ents, at, I_GEL,
                           m->variant == VX_SLIME_PINKY ? random_u32_range(&cs->rng, 8, 12) : random_u32_range(&cs->rng, 1, 3));
    break;
  case VX_CR_DEMON_EYE:
    if (random_f32(&cs->rng) < 0.33f) vx_entities_spawn_drop(&g->ents, at, I_LENS, 1);
    break;
  case VX_CR_EMPRESS: vx_entities_spawn_drop(&g->ents, at, I_HEALING_POTION, random_u32_range(&cs->rng, 5, 15)); break;
  case VX_CR_EYE_OF_CTHULHU: {
    u32 ore = random_u32_range(&cs->rng, 30, 87), potions = random_u32_range(&cs->rng, 5, 15);
    vx_entities_spawn_drop(&g->ents, at, B_DEMONITE_ORE, ore);
    vx_entities_spawn_drop(&g->ents, at, I_HEALING_POTION, potions);
    if (g->scripted)
      log_info("creatures: Eye of Cthulhu dropped % Demonite Ore and % Lesser Healing Potions at % % %", fmt_u32(ore),
               fmt_u32(potions), fmt_f64_p(at.x, 1), fmt_f64_p(at.y, 1), fmt_f64_p(at.z, 1));
  } break;
  default: break;
  }
  // whatever a slime carried falls out of it
  if (m->held && m->held != I_GEL) {
    VxItemId it = (VxItemId)m->held;
    u32 n = it == I_COPPER_COIN ? random_u32_range(&cs->rng, 3, 12)
          : it == I_WOODEN_ARROW || vx_item_is_block(it) || it == I_GLOWSTICK ? random_u32_range(&cs->rng, 2, 6)
                                                                              : 1;
    vx_entities_spawn_drop(&g->ents, at, it, n);
  }
  // coins: the value give or take a fifth, in gold, silver and copper
  u32 value = (u32)((f32)vx_cr_value(m) * random_f32_range(&cs->rng, 0.8f, 1.2f));
  if (value / 10000) vx_entities_spawn_drop(&g->ents, at, I_GOLD_COIN, value / 10000);
  if (value / 100 % 100) vx_entities_spawn_drop(&g->ents, at, I_SILVER_COIN, value / 100 % 100);
  if (value % 100) vx_entities_spawn_drop(&g->ents, at, I_COPPER_COIN, value % 100);
  if (g->scripted && value >= 10000) log_info("creatures: % dropped % copper in coins", fmt_cstr(vx_creature_name(m)), fmt_u32(value));
}

// chunks in the creature's own colours burst from where it died
hz_internal void vx_cr_gore(VxGame *g, const VxCreature *m, v3 dir) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 *pal = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, u32, 4);
  u32 n = 0;
  switch (m->kind) {
  case VX_CR_SLIME: {
    u32 c = VX_SLIME_COLORS[m->variant % VX_SLIME_COUNT];
    pal[n++] = c, pal[n++] = c, pal[n++] = ((c >> 1) & 0x7F7F7F), pal[n++] = (c | 0x404040);
  } break;
  case VX_CR_ZOMBIE: pal[n++] = 0x7FA866, pal[n++] = 0x3E5C8E, pal[n++] = 0x5C4632, pal[n++] = 0x8A1A1A; break;
  case VX_CR_SKELETON:
    pal[n++] = 0xE8DEC4, pal[n++] = 0xBDB091, pal[n++] = 0xE8DEC4;
    if (m->variant & 1) pal[n++] = 0xB8642E;
    break;
  case VX_CR_DEMON_EYE:
  case VX_CR_SERVANT: pal[n++] = 0xF2EEE6, pal[n++] = 0xC0242A, pal[n++] = 0x8A1A1A, pal[n++] = 0x3C78D8; break;
  case VX_CR_EYE_OF_CTHULHU: pal[n++] = 0xF2EEE6, pal[n++] = 0xF2EEE6, pal[n++] = 0xC0242A, pal[n++] = 0x5A0C14; break;
  case VX_CR_EMPRESS: pal[n++] = 0xFFF2FA, pal[n++] = 0xFF8CE6, pal[n++] = 0x7AE6FF, pal[n++] = 0xFFE07A; break;
  case VX_CR_EATER: pal[n++] = 0x6E4A80, pal[n++] = 0x4A3058, pal[n++] = 0x2A0E1A; break;
  default: {
    u32 c = VX_BAT_COLORS[m->variant % VX_BAT_COUNT];
    pal[n++] = c, pal[n++] = ((c >> 1) & 0x7F7F7F), pal[n++] = 0x8A1A1A;
  } break;
  }
  f32 half, h;
  vx_creature_size(m, &half, &h);
  VxV3d at = {m->pos.x, m->pos.y + h * 0.5, m->pos.z};
  u32 count = (u32)(10 + 10 * half * h);
  vx_entities_debris(&g->ents, at, v3_add(v3_scale(dir, 3.5f), (v3){0, 5.0f, 0}), 4.5f, count, 0.22f + half * 0.12f, pal, n);
  u32 c = pal[0];
  vx_entities_death_burst(&g->ents, at, m_maxf(half, h * 0.5f),
                          ((c >> 16) & 255) | (c & 0xFF00) | ((c & 255) << 16) | 0xFF000000u);
  tctx_temp_allocator_end(tmp);
}

b32 vx_creature_hurt(VxGame *g, u32 i, f32 damage, v3 dir, f32 knockback, b32 crit, const char *by) {
  VxCreatures *cs = &g->mobs;
  if (i >= cs->count) return false;
  VxCreature *m = &cs->items[i];
  if (m->immune > 0 || m->hp <= 0) return false;
  const VxCreatureInfo *ci = &vx_creature_info[m->kind];
  f32 dmg = m_maxf(1, m_roundf(damage - vx_cr_defense(m) * 0.5f)) * (crit ? 2.0f : 1.0f);
  m->hp -= dmg;
  m->immune = VX_CR_HIT_IMMUNE;
  m->flash = 1;
  m->show_hp = 4;
  m->aggro = 1;
  // what knockback does not move keeps its own flight: the Eye's dash is not stopped by a blow
  f32 kb = knockback * (1 - ci->knock_resist);
  if (kb > 0.01f) {
    m->vel.x = dir.x * kb;
    m->vel.z = dir.z * kb;
    m->vel.y = m_maxf(m->vel.y, kb * 0.55f + dir.y * kb);
    m->on_ground = false;
  }
  f32 half, h;
  vx_creature_size(m, &half, &h);
  vx_world_text_add(g, crit ? VX_WT_CRIT : VX_WT_DAMAGE, (VxV3d){m->pos.x, m->pos.y + h + 0.3, m->pos.z}, 0, (u32)dmg);
  if (g->scripted)
    log_info("creatures: % hit by % for %% (life % / %)", fmt_cstr(vx_creature_name(m)), fmt_cstr(by),
             fmt_f32_p(dmg, 0), fmt_cstr(crit ? " crit" : ""), fmt_f32_p(m_maxf(m->hp, 0), 0), fmt_f32_p(m->hp_max, 0));
  // Terraria's split: a hit sound per body, a death sound in its place on the killing blow
  b32 slime = m->kind == VX_CR_SLIME;
  vx_audio_play(&g->audio, m->hp <= 0 ? (slime ? VX_SND_KILL_SLIME : VX_SND_KILL)
                           : slime ? VX_SND_HIT_SLIME : m->kind == VX_CR_SKELETON ? VX_SND_HIT_BONE : VX_SND_HIT);
  if (crit) {
    vx_hitstop(g, 0.055f);
    vx_shake(g, 0.1f);
  }
  if (m->hp <= 0) {
    cs->kills++;
    b32 boss = vx_creature_info[m->kind].behaviour == VX_BEH_BOSS;
    vx_hitstop(g, boss ? 0.2f : 0.075f);
    vx_shake(g, boss ? 0.8f : 0.16f);
    vx_cr_loot(g, m);
    vx_cr_gore(g, m, dir);
    if (g->scripted) log_info("creatures: % killed", fmt_cstr(vx_creature_name(m)));
    if (m->kind == VX_CR_EYE_OF_CTHULHU) {
      g->boss_defeated = true;
      vx_event_rgb(g, STR_FROM_CSTR("Eye of Cthulhu has been defeated!"), 0xAF4BFF);
      vx_audio_play(&g->audio, VX_SND_ROAR);
      // the eyeball bursts: a wide flash, blood and a ring of embers
      VxV3d at = {m->pos.x, m->pos.y + 2.5, m->pos.z};
      vx_entities_death_burst(&g->ents, at, 3.6f, vx_cr_abgr(0xFF3040, 1));
      vx_entities_burst(&g->ents, VX_MOTE_DROPLET, at, (v3){0, 4, 0}, 7.0f, 70, 1.2f, 0.18f, vx_cr_abgr(0xA0101A, 1));
      for (u32 k = 0; k < 24; k++) {
        f32 a = (f32)k / 24.0f * 2 * PI;
        vx_entities_streak(&g->ents, at, (v3){m_cosf(a) * 16.0f, random_f32_range(&cs->rng, -4, 8), m_sinf(a) * 16.0f}, 0.5f,
                           0.5f, vx_cr_abgr(k & 1 ? 0xFFE0C8 : 0xFF4030, 1));
      }
    } else if (m->kind == VX_CR_EMPRESS) {
      g->empress_defeated = true;
      vx_event_rgb(g, STR_FROM_CSTR("Empress of Light has been defeated!"), 0xAF4BFF);
      vx_audio_play(&g->audio, VX_SND_ROAR);
      // she breaks into light: a white flash and a ring of every colour
      VxV3d at = {m->pos.x, m->pos.y + VX_EMP_CORE, m->pos.z};
      vx_entities_death_burst(&g->ents, at, 3.6f, vx_cr_abgr(0xFFC8F4, 1));
      for (u32 k = 0; k < 36; k++) {
        f32 a = (f32)k / 36.0f * 2 * PI;
        vx_entities_streak(&g->ents, at, (v3){m_cosf(a) * 15.0f, random_f32_range(&cs->rng, -5, 9), m_sinf(a) * 15.0f}, 0.6f,
                           0.55f, vx_cr_abgr(vx_hue_hex((f32)k / 36.0f, 0.25f), 1));
      }
    }
  }
  return true;
}

// ---- art: box puppets on the shared puppet grain texture, front along -z ----

typedef struct {
  VxEntities *e;
  u32 layer;
  f32 flash;
} VxCrArt;

force_inline void vx_cr_box(const VxCrArt *a, m4 xf, v3 lo, v3 hi, v3 tint) {
  vx_ebox_ex(a->e, xf, lo, hi, a->layer, tint, VX_BOX_NEAR_FADE, a->flash);
}
force_inline void vx_cr_glow(const VxCrArt *a, m4 xf, v3 lo, v3 hi, v3 tint) {
  vx_ebox_ex(a->e, xf, lo, hi, a->layer, tint, VX_BOX_SELF_LIT | VX_BOX_NEAR_FADE, a->flash);
}

// a translucent jelly dome, forward shaded, with what it swallowed turning slowly inside and its eyes on the surface
hz_internal void vx_cr_slime(const VxCrArt *a, m4 base, const VxCreature *m, const VxWorld *w) {
  u32 c = VX_SLIME_COLORS[m->variant % VX_SLIME_COUNT];
  v3 body = vx_cr_rgb(c);
  f32 sy = 1 + m->squash, sxz = 1 - m->squash * 0.55f;
  f32 s = m->variant == VX_SLIME_PINKY ? 0.55f : 1.0f;
  m4 b = m4_scaled(base, (v3){sxz * s, sy * s, sxz * s});
  u32 glow = m->variant == VX_SLIME_LAVA ? VX_BOX_SELF_LIT : 0;
  vx_ejelly(a->e, b, (v3){-0.8f, 0.0f, -0.68f}, (v3){0.8f, 0.9f, 0.68f}, body, glow, a->flash);
  vx_ejelly(a->e, b, (v3){-0.62f, 0.9f, -0.52f}, (v3){0.62f, 1.22f, 0.52f}, body, glow, a->flash);
  if (m->held) {
    f32 sky, blk;
    vx_light_at(w, (VxV3d){m->pos.x, m->pos.y + 0.5, m->pos.z}, &sky, &blk);
    VxItemId it = (VxItemId)m->held;
    b32 cube = vx_item_is_block(it) && vx_shape[it] != VX_SHAPE_CROSS && vx_shape[it] != VX_SHAPE_TORCH;
    f32 is = cube ? 0.36f : 0.62f;
    m4 x = m4_translated(b, (v3){0, cube ? 0.3f : 0.2f, 0});
    x = m4_rotate_y(x, m->age * 0.9f + (f32)(m->seed & 255));
    x = m4_rotate_z(x, m_sinf(m->age * 1.3f) * 0.25f);
    x = m4_scaled(x, (v3){is, is, is});
    x = m4_translated(x, cube ? (v3){-0.5f, 0, -0.5f} : (v3){-0.5f, 0, 0});
    vx_emit_item(a->e, x, it, sky, blk, 0);
  }
  for (u32 k = 0; k < 2; k++) {
    f32 o = k ? 1.0f : -1.0f;
    v3 lo = {o > 0 ? 0.14f : -0.38f, 0.5f, -0.72f}, hi = {o > 0 ? 0.38f : -0.14f, 0.78f, -0.69f};
    vx_cr_box(a, b, lo, hi, (v3){0.02f, 0.02f, 0.03f});
  }
}

// zombies and skeletons: a humanoid on the player's proportions, arms out, a shambling walk
hz_internal void vx_cr_humanoid(const VxCrArt *a, m4 base, const VxCreature *m) {
  b32 skel = m->kind == VX_CR_SKELETON;
  f32 sw = m_sinf(m->anim) * 0.7f;
  f32 bob = m_absf(m_cosf(m->anim)) * 0.06f;
  base = m4_translated(base, (v3){0, bob, 0});
  v3 bone = vx_cr_rgb(0xE8DEC4), bone2 = vx_cr_rgb(0xBDB091), hole = {0.03f, 0.025f, 0.03f};
  v3 skin = vx_cr_rgb(0x7FA866), shirt = vx_cr_rgb(0x3E5C8E), pants = vx_cr_rgb(0x5C4632);
  b32 armour = skel && (m->variant & 1);
  v3 plate = vx_cr_rgb(0xB8642E), plate2 = vx_cr_rgb(0x7E3E1C);

  for (u32 s = 0; s < 2; s++) {
    f32 sx = s ? 0.2f : -0.2f;
    m4 leg = m4_translated(base, (v3){sx, 1.0f, 0});
    leg = m4_rotate_x(leg, s ? sw : -sw);
    if (skel) {
      vx_cr_box(a, leg, (v3){-0.08f, -0.94f, -0.08f}, (v3){0.08f, 0.0f, 0.08f}, bone);
      vx_cr_box(a, leg, (v3){-0.12f, -0.52f, -0.12f}, (v3){0.12f, -0.4f, 0.12f}, bone2);
      vx_cr_box(a, leg, (v3){-0.13f, -1.0f, -0.24f}, (v3){0.13f, -0.88f, 0.12f}, bone);
    } else {
      vx_cr_box(a, leg, (v3){-0.18f, -0.8f, -0.19f}, (v3){0.18f, 0.02f, 0.19f}, pants);
      vx_cr_box(a, leg, (v3){-0.17f, -1.0f, -0.24f}, (v3){0.17f, -0.8f, 0.18f}, skin);
    }
  }
  m4 torso = m4_translated(base, (v3){0, 1.0f, 0});
  torso = m4_rotate_x(torso, skel ? -0.08f : -0.18f);
  if (skel) {
    vx_cr_box(a, torso, (v3){-0.3f, 0.0f, -0.12f}, (v3){0.3f, 0.14f, 0.12f}, bone2);
    vx_cr_box(a, torso, (v3){-0.07f, 0.0f, -0.05f}, (v3){0.07f, 0.92f, 0.09f}, bone);
    for (u32 r = 0; r < 3; r++) {
      f32 y = 0.34f + 0.18f * (f32)r;
      vx_cr_box(a, torso, (v3){-0.36f, y, -0.2f}, (v3){0.36f, y + 0.1f, 0.2f}, bone);
    }
    vx_cr_box(a, torso, (v3){-0.4f, 0.8f, -0.18f}, (v3){0.4f, 0.92f, 0.18f}, bone);
    if (armour) {
      vx_cr_box(a, torso, (v3){-0.42f, 0.3f, -0.24f}, (v3){0.42f, 0.86f, 0.22f}, plate);
      vx_cr_box(a, torso, (v3){-0.3f, 0.42f, -0.26f}, (v3){0.3f, 0.5f, -0.23f}, plate2);
    }
  } else {
    vx_cr_box(a, torso, (v3){-0.42f, 0.0f, -0.22f}, (v3){0.42f, 0.92f, 0.22f}, shirt);
    // torn cloth showing skin
    vx_cr_box(a, torso, (v3){-0.3f, 0.1f, -0.24f}, (v3){-0.06f, 0.34f, -0.21f}, skin);
    vx_cr_box(a, torso, (v3){0.12f, 0.52f, -0.24f}, (v3){0.32f, 0.7f, -0.21f}, skin);
    vx_cr_box(a, torso, (v3){-0.42f, -0.06f, -0.22f}, (v3){-0.1f, 0.0f, 0.22f}, v3_scale(shirt, 0.7f));
  }
  m4 head = m4_translated(torso, (v3){0, 0.92f, 0});
  head = m4_rotate_z(head, m_sinf(m->anim * 0.5f + (f32)(m->seed & 7)) * 0.08f);
  if (skel) {
    vx_cr_box(a, head, (v3){-0.38f, 0.08f, -0.38f}, (v3){0.38f, 0.8f, 0.38f}, bone);
    vx_cr_box(a, head, (v3){-0.3f, -0.04f, -0.36f}, (v3){0.3f, 0.12f, 0.2f}, bone2);
    for (u32 s = 0; s < 2; s++) {
      f32 o = s ? 1.0f : -1.0f;
      vx_cr_box(a, head, (v3){o > 0 ? 0.06f : -0.28f, 0.34f, -0.41f}, (v3){o > 0 ? 0.28f : -0.06f, 0.56f, -0.37f}, hole);
    }
    vx_cr_box(a, head, (v3){-0.05f, 0.2f, -0.41f}, (v3){0.05f, 0.3f, -0.37f}, hole);
    for (u32 t = 0; t < 4; t++) {
      f32 x = -0.22f + 0.12f * (f32)t;
      vx_cr_box(a, head, (v3){x, 0.04f, -0.39f}, (v3){x + 0.06f, 0.12f, -0.36f}, hole);
    }
    if (armour) vx_cr_box(a, head, (v3){-0.42f, 0.6f, -0.42f}, (v3){0.42f, 0.9f, 0.42f}, plate);
  } else {
    vx_cr_box(a, head, (v3){-0.4f, 0, -0.4f}, (v3){0.4f, 0.8f, 0.4f}, skin);
    vx_cr_box(a, head, (v3){-0.42f, 0.62f, -0.42f}, (v3){0.42f, 0.84f, 0.42f}, vx_cr_rgb(0x3A2E22));
    for (u32 s = 0; s < 2; s++) {
      f32 o = s ? 1.0f : -1.0f;
      vx_cr_box(a, head, (v3){o > 0 ? 0.06f : -0.26f, 0.32f, -0.43f}, (v3){o > 0 ? 0.26f : -0.06f, 0.48f, -0.39f}, hole);
      vx_cr_glow(a, head, (v3){o > 0 ? 0.12f : -0.18f, 0.36f, -0.44f}, (v3){o > 0 ? 0.18f : -0.12f, 0.43f, -0.42f},
                 vx_cr_rgb(0xD83020));
    }
    vx_cr_box(a, head, (v3){-0.16f, 0.12f, -0.42f}, (v3){0.16f, 0.2f, -0.39f}, v3_scale(skin, 0.45f));
  }
  // arms reach for the player
  for (u32 s = 0; s < 2; s++) {
    f32 sx = s ? 0.56f : -0.56f;
    m4 arm = m4_translated(torso, (v3){sx, 0.84f, 0});
    arm = m4_rotate_x(arm, -PI * 0.45f + (s ? sw : -sw) * 0.25f);
    if (skel) {
      vx_cr_box(a, arm, (v3){-0.08f, -0.86f, -0.08f}, (v3){0.08f, 0.08f, 0.08f}, bone);
      vx_cr_box(a, arm, (v3){-0.11f, -0.48f, -0.11f}, (v3){0.11f, -0.38f, 0.11f}, bone2);
    } else {
      vx_cr_box(a, arm, (v3){-0.16f, -0.44f, -0.16f}, (v3){0.16f, 0.1f, 0.16f}, shirt);
      vx_cr_box(a, arm, (v3){-0.14f, -0.86f, -0.14f}, (v3){0.14f, -0.44f, 0.14f}, skin);
    }
  }
}

hz_internal void vx_cr_demon_eye(const VxCrArt *a, m4 base, const VxCreature *m) {
  // the eye looks along its flight
  f32 sp = v3_length(m->vel);
  f32 pitch = sp > 0.1f ? m_asinf(m_clampf(m->vel.y / sp, -1, 1)) : 0;
  m4 b = m4_translated(base, (v3){0, 0.5f, 0});
  b = m4_rotate_x(b, pitch);
  v3 white = vx_cr_rgb(0xF2EEE6), vein = vx_cr_rgb(0xC0242A), iris = vx_cr_rgb(0x3C78D8);
  vx_cr_box(a, b, (v3){-0.46f, -0.46f, -0.46f}, (v3){0.46f, 0.46f, 0.46f}, white);
  vx_cr_box(a, b, (v3){-0.3f, -0.3f, -0.5f}, (v3){0.3f, 0.3f, -0.46f}, iris);
  vx_cr_box(a, b, (v3){-0.15f, -0.15f, -0.52f}, (v3){0.15f, 0.15f, -0.49f}, (v3){0.02f, 0.02f, 0.02f});
  vx_cr_box(a, b, (v3){-0.26f, 0.08f, -0.53f}, (v3){-0.16f, 0.18f, -0.5f}, (v3){1, 1, 1});
  // veins wrap the sides
  vx_cr_box(a, b, (v3){-0.48f, -0.04f, -0.3f}, (v3){-0.46f, 0.04f, 0.3f}, vein);
  vx_cr_box(a, b, (v3){0.46f, 0.1f, -0.2f}, (v3){0.48f, 0.18f, 0.36f}, vein);
  vx_cr_box(a, b, (v3){-0.2f, 0.46f, -0.1f}, (v3){0.24f, 0.48f, -0.02f}, vein);
  // the trailing optic nerve, whipping with the flight
  m4 t = m4_translated(b, (v3){0, 0, 0.46f});
  for (u32 k = 0; k < 4; k++) {
    t = m4_rotate_y(t, m_sinf(m->anim * 9 - (f32)k * 0.9f) * 0.35f);
    t = m4_rotate_x(t, m_cosf(m->anim * 7 - (f32)k) * 0.2f);
    f32 r = 0.16f - 0.03f * (f32)k;
    vx_cr_box(a, t, (v3){-r, -r, 0}, (v3){r, r, 0.3f}, v3_scale(vein, 1 - 0.1f * (f32)k));
    t = m4_translated(t, (v3){0, 0, 0.28f});
  }
}

// the eyeball's voxel cells, n either side of the centre; the second form carves a mouth cone out of the front
#define VX_EOC_N 6
hz_internal b32 vx_cr_eoc_solid(i32 i, i32 j, i32 k, f32 open) {
  f32 rr = (f32)(i * i + j * j + k * k), R = (f32)VX_EOC_N + 0.45f;
  if (rr > R * R) return false;
  if (open >= 1) return true;
  f32 r = m_sqrtf(rr);
  return !(r > R * 0.42f && -(f32)k / m_maxf(r, 1e-3f) > open);
}

hz_internal void vx_cr_eoc(const VxCrArt *a, m4 base, const VxCreature *m) {
  m4 b = m4_translated(base, (v3){0, 2.5f, 0});
  b = m4_rotate_x(b, m->pitch);
  b = m4_rotate_z(b, m->spin);
  f32 c = 2 * VX_EOC_R / (2 * VX_EOC_N + 1), R = (f32)VX_EOC_N + 0.45f;
  b32 p2 = m->boss_phase == 2;
  // the mouth's rim as the cosine off the front axis: wider as it gapes
  f32 open = p2 ? 1 - 0.42f * m->mouth : 1;
  v3 sclera = vx_cr_rgb(0xF2EEE6), sclera2 = vx_cr_rgb(0xE6D8CC), vein = vx_cr_rgb(0xC0242A), back = vx_cr_rgb(0x9A1E28);
  v3 iris = vx_cr_rgb(0x3C78D8), iris_rim = vx_cr_rgb(0x24489A), pupil = vx_cr_rgb(0x0A0A0E);
  v3 flesh = vx_cr_rgb(0xB42234), throat = vx_cr_rgb(0x5A0C16), tooth = vx_cr_rgb(0xF4ECD2), tooth_tip = vx_cr_rgb(0xFFFAEA);
  for (i32 k = -VX_EOC_N; k <= VX_EOC_N; k++)
    for (i32 j = -VX_EOC_N; j <= VX_EOC_N; j++)
      for (i32 i = -VX_EOC_N; i <= VX_EOC_N; i++) {
        if (!vx_cr_eoc_solid(i, j, k, open)) continue;
        // only cells with an open face are drawn
        if (vx_cr_eoc_solid(i + 1, j, k, open) && vx_cr_eoc_solid(i - 1, j, k, open) && vx_cr_eoc_solid(i, j + 1, k, open) &&
            vx_cr_eoc_solid(i, j - 1, k, open) && vx_cr_eoc_solid(i, j, k + 1, open) && vx_cr_eoc_solid(i, j, k - 1, open))
          continue;
        f32 r = m_sqrtf((f32)(i * i + j * j + k * k)), inv = 1.0f / m_maxf(r, 1e-3f);
        f32 front = -(f32)k * inv, rear = (f32)k * inv, phi = m_atan2f((f32)j, (f32)i);
        u32 h = (u32)(i * 73856093) ^ (u32)(j * 19349663) ^ (u32)(k * 83492791);
        v3 col;
        if (p2 && r < R - 0.9f) {
          // inside the mouth: raw flesh darkening down the throat
          col = v3_lerp(throat, flesh, m_saturatef((r - R * 0.42f) / (R * 0.5f)));
        } else if (!p2 && front > 0.965f) {
          col = pupil;
        } else if (!p2 && front > 0.8f) {
          col = front < 0.845f ? iris_rim : iris;
        } else {
          // veins run from the bloody back toward the front in wandering lines
          f32 f = phi * 7.0f / (2 * PI) + 0.35f * m_sinf(rear * 5.0f + phi * 2.0f);
          b32 is_vein = front < 0.62f && m_absf(f - m_floorf(f) - 0.5f) < 0.15f;
          col = rear > 0.78f ? back : is_vein ? vein : (h & 3u) == 0 ? sclera2 : sclera;
        }
        v3 p = {(f32)i * c, (f32)j * c, (f32)k * c};
        vx_cr_box(a, b, v3_sub(p, (v3){c * 0.5f, c * 0.5f, c * 0.5f}), v3_add(p, (v3){c * 0.5f, c * 0.5f, c * 0.5f}), col);
        // fangs ring the mouth's lip, tapering in toward its middle
        if (p2 && r >= R - 0.9f && front > open - 0.16f) {
          f32 rl = m_sqrtf((f32)(i * i + j * j));
          v3 in = rl > 1e-3f ? (v3){-(f32)i / rl, -(f32)j / rl, 0} : (v3){0, 0, 0};
          v3 base_c = v3_add(p, v3_add(v3_scale(in, c * 0.6f), (v3){0, 0, -c * 0.35f}));
          v3 tip_c = v3_add(p, v3_add(v3_scale(in, c * 1.15f), (v3){0, 0, -c * 0.45f}));
          vx_cr_box(a, b, v3_sub(base_c, (v3){c * 0.3f, c * 0.3f, c * 0.3f}), v3_add(base_c, (v3){c * 0.3f, c * 0.3f, c * 0.3f}), tooth);
          vx_cr_box(a, b, v3_sub(tip_c, (v3){c * 0.15f, c * 0.15f, c * 0.18f}), v3_add(tip_c, (v3){c * 0.15f, c * 0.15f, c * 0.18f}),
                    tooth_tip);
        }
      }
  // the wet glint on the iris
  if (!p2) {
    v3 g = v3_scale(v3_normalize((v3){-0.32f, 0.38f, -0.87f}), VX_EOC_R + 0.02f);
    vx_cr_glow(a, b, v3_sub(g, (v3){c * 0.4f, c * 0.4f, c * 0.2f}), v3_add(g, (v3){c * 0.4f, c * 0.4f, c * 0.2f}), (v3){1, 1, 1});
  }
  // six tendrils trail from the back, whipping with its flight
  for (u32 t = 0; t < 6; t++) {
    f32 th = (f32)t / 6.0f * 2 * PI + 0.3f;
    v3 root = v3_scale((v3){0.62f * m_cosf(th), 0.62f * m_sinf(th), 0.78f}, VX_EOC_R * 0.86f);
    m4 s = m4_translated(b, root);
    s = m4_rotate_x(s, -m_sinf(th) * 0.55f);
    s = m4_rotate_y(s, m_cosf(th) * 0.55f);
    for (u32 q = 0; q < 6; q++) {
      s = m4_rotate_y(s, m_sinf(m->anim * 6.0f - (f32)q * 0.8f + (f32)t * 1.7f) * 0.22f);
      s = m4_rotate_x(s, m_cosf(m->anim * 5.0f - (f32)q * 0.9f + (f32)t) * 0.18f);
      f32 rad = 0.32f - 0.04f * (f32)q, len = 0.78f;
      vx_cr_box(a, s, (v3){-rad, -rad, 0}, (v3){rad, rad, len}, v3_lerp(vein, vx_cr_rgb(0x5A0C14), (f32)q / 6.0f));
      s = m4_translated(s, (v3){0, 0, len * 0.92f});
    }
  }
}
#undef VX_EOC_N

#define VX_EMP_CELL 0.12f
#define VX_EMP_SCALE 1.5f // the art is authored at 4.6 blocks tall and drawn half again as big
// one wing in its plane, laid along its own axis (a out to the tip, b across): a teardrop of stained-glass cells, rim and veins leaded
hz_internal void vx_cr_emp_wing(const VxCrArt *a, m4 w, f32 side, b32 upper, f32 shimmer) {
  f32 L = upper ? 3.6f : 2.4f, W = upper ? 1.1f : 0.85f, c = VX_EMP_CELL;
  v3 lead = vx_cr_rgb(0x2A1636);
  i32 na = (i32)(L / c) + 1, nb = (i32)(W / c) + 1;
  for (i32 i = 0; i < na; i++)
    for (i32 j = -nb; j <= nb; j++) {
      f32 u = ((f32)i + 0.5f) * c, v = ((f32)j + 0.5f) * c, t = u / L;
      // narrow at the root, broadest past the middle, round at the tip
      f32 hw = W * m_powf(m_sinf(PI * m_powf(t, 0.65f)), 0.8f);
      if (m_absf(v) > hw || hw < c * 0.5f) continue;
      f32 q = v / hw;
      b32 vein = hw - m_absf(v) < c * 0.9f || (t > 0.12f && (m_absf(q + 0.4f) * hw < c * 0.6f || m_absf(q - 0.35f) * hw < c * 0.6f ||
                                                              m_absf(t - 0.48f) * L < c * 0.6f));
      f32 x0 = side * (u - c * 0.5f), x1 = side * (u + c * 0.5f);
      v3 lo = {m_minf(x0, x1), v - c * 0.5f, -0.025f}, hi = {m_maxf(x0, x1), v + c * 0.5f, 0.025f};
      if (vein) {
        lo.z = -0.035f, hi.z = 0.035f;
        vx_cr_box(a, w, lo, hi, lead);
        continue;
      }
      // cyan at the root through gold to pink at the upper tips and gold at the lower, pinker on the leading edge
      f32 hue = (upper ? 0.55f - 0.7f * t : 0.56f - 0.42f * t) + 0.1f * q + 0.03f * m_sinf(u * 4.0f - shimmer);
      u32 hex = vx_hue_hex(hue, 0.2f + 0.3f * (1 - t) + 0.15f * (1 - m_absf(q)));
      vx_cr_glow(a, w, lo, hi, v3_scale(vx_cr_rgb(hex), 1.2f));
    }
}

// the Empress of Light: a luminous gowned figure with a sunburst crown and four stained-glass wings, arms raised to cast
hz_internal void vx_cr_empress_art(const VxCrArt *a, m4 base, const VxCreature *m) {
  m4 b = m4_scaled(m4_translated(base, (v3){0, 0.18f * m_sinf(m->anim * 2.2f), 0}), (v3){VX_EMP_SCALE, VX_EMP_SCALE, VX_EMP_SCALE});
  v3 white = vx_cr_rgb(0xFFF4FA);
  // the gown swells from the waist and trails away to a wisp, paling into the colour wheel toward its end
  for (u32 i = 0; i < 12; i++) {
    f32 t = (f32)i / 11.0f, y0 = 0.1f + (f32)i * 0.19f, h = 0.08f + 0.5f * m_sinf(PI * (0.15f + 0.75f * t)) * m_powf(t, 0.35f);
    h = m_minf(h, 0.2f + (1 - t) * 1.2f);
    v3 col = v3_lerp(vx_cr_rgb(vx_hue_hex(0.55f + 0.35f * t + 0.05f * m_sinf(m->anim), 0.55f)), white, m_saturatef(t * 1.6f - 0.4f));
    vx_cr_glow(a, b, (v3){-h, y0, -h * 0.8f}, (v3){h, y0 + 0.2f, h * 0.8f}, v3_scale(col, 0.95f));
  }
  vx_cr_glow(a, b, (v3){-0.2f, 2.38f, -0.12f}, (v3){0.2f, 2.95f, 0.12f}, v3_scale(white, 0.95f));
  vx_cr_glow(a, b, (v3){-0.22f, 3.0f, -0.22f}, (v3){0.22f, 3.46f, 0.22f}, v3_scale(vx_cr_rgb(0xFFEEEA), 0.9f));
  // long pale-gold hair down her back
  v3 hair = v3_scale(vx_cr_rgb(0xFFE6B8), 0.85f);
  vx_cr_glow(a, b, (v3){-0.25f, 2.45f, 0.1f}, (v3){0.25f, 3.5f, 0.3f}, hair);
  vx_cr_glow(a, b, (v3){-0.25f, 3.42f, -0.24f}, (v3){0.25f, 3.56f, 0.3f}, hair);
  v3 eye = v3_scale(vx_cr_rgb(0x5A8CFF), 1.4f);
  vx_cr_glow(a, b, (v3){-0.14f, 3.19f, -0.235f}, (v3){-0.05f, 3.26f, -0.22f}, eye);
  vx_cr_glow(a, b, (v3){0.05f, 3.19f, -0.235f}, (v3){0.14f, 3.26f, -0.22f}, eye);
  // a sunburst crown: gold rays fanning above the head, the middle ones longest
  for (i32 k = -3; k <= 3; k++) {
    m4 s = m4_rotate_z(m4_translated(b, (v3){0, 3.5f, 0.04f}), (f32)k * 0.3f);
    f32 len = 0.62f - 0.09f * (f32)(k < 0 ? -k : k);
    vx_cr_glow(a, s, (v3){-0.035f, 0, -0.035f}, (v3){0.035f, len, 0.035f}, (v3){2.0f, 1.6f, 0.7f});
  }
  // arms hang at rest and rise overhead as she casts
  for (i32 sd = -1; sd <= 1; sd += 2) {
    f32 side = (f32)sd;
    m4 s = m4_rotate_z(m4_translated(b, (v3){side * 0.25f, 2.9f, 0}), side * (0.22f + m->mouth * 2.5f));
    vx_cr_glow(a, s, (v3){-0.05f, -0.68f, -0.05f}, (v3){0.05f, 0, 0.05f}, v3_scale(white, 0.9f));
    vx_cr_glow(a, s, (v3){-0.08f, -0.8f, -0.08f}, (v3){0.08f, -0.64f, 0.08f}, (v3){1.6f, 1.5f, 1.7f});
  }
  // four wings beating slowly, the lower pair a beat behind
  f32 flap = 0.32f + 0.24f * m_sinf(m->anim * 4.2f), flap2 = 0.3f + 0.2f * m_sinf(m->anim * 4.2f - 0.7f);
  for (i32 sd = -1; sd <= 1; sd += 2) {
    f32 side = (f32)sd;
    // the upper wings rise steeply out, the lower ones droop
    m4 wu = m4_rotate_y(m4_translated(b, (v3){side * 0.2f, 2.8f, 0.16f}), -side * flap);
    vx_cr_emp_wing(a, m4_rotate_z(wu, side * 0.72f), side, true, m->anim * 2.0f);
    m4 wl = m4_rotate_y(m4_translated(b, (v3){side * 0.2f, 2.5f, 0.16f}), -side * flap2);
    vx_cr_emp_wing(a, m4_rotate_z(wl, -side * 0.7f), side, false, m->anim * 2.0f);
  }
}

hz_internal void vx_cr_eater(const VxCrArt *a, m4 base, const VxCreature *m) {
  v3 hide = vx_cr_rgb(0x6E4A80), hide2 = vx_cr_rgb(0x4A3058), maw = vx_cr_rgb(0x2A0E1A), fang = vx_cr_rgb(0xD8C8B0);
  m4 b = m4_translated(base, (v3){0, 0.55f, 0});
  vx_cr_box(a, b, (v3){-0.42f, -0.36f, -0.5f}, (v3){0.42f, 0.36f, 0.2f}, hide);
  vx_cr_box(a, b, (v3){-0.28f, -0.22f, -0.53f}, (v3){0.28f, 0.22f, -0.5f}, maw);
  // mandibles snap open and shut
  f32 open = 0.25f + m_sinf(m->anim * 10) * 0.2f;
  for (u32 s = 0; s < 2; s++) {
    f32 o = s ? 1.0f : -1.0f;
    m4 j = m4_translated(b, (v3){o * 0.3f, 0, -0.45f});
    j = m4_rotate_y(j, o * open);
    vx_cr_box(a, j, (v3){o > 0 ? -0.06f : -0.06f, -0.08f, -0.42f}, (v3){0.06f, 0.08f, 0}, fang);
  }
  for (u32 s = 0; s < 2; s++) {
    f32 o = s ? 1.0f : -1.0f;
    vx_cr_glow(a, b, (v3){o > 0 ? 0.14f : -0.28f, 0.22f, -0.52f}, (v3){o > 0 ? 0.28f : -0.14f, 0.32f, -0.49f},
               vx_cr_rgb(0xB8F040));
  }
  m4 t = m4_translated(b, (v3){0, 0, 0.2f});
  for (u32 k = 0; k < 3; k++) {
    t = m4_rotate_y(t, m_sinf(m->anim * 6 - (f32)k * 1.1f) * 0.3f);
    f32 r = 0.32f - 0.07f * (f32)k;
    vx_cr_box(a, t, (v3){-r, -r, 0}, (v3){r, r, 0.38f}, k & 1 ? hide : hide2);
    t = m4_translated(t, (v3){0, 0, 0.36f});
  }
}

hz_internal void vx_cr_bat(const VxCrArt *a, m4 base, const VxCreature *m) {
  v3 fur = vx_cr_rgb(VX_BAT_COLORS[m->variant % VX_BAT_COUNT]), skin = v3_scale(fur, 0.7f);
  m4 b = m4_translated(base, (v3){0, 0.35f + m_sinf(m->anim * 22) * 0.05f, 0});
  vx_cr_box(a, b, (v3){-0.18f, -0.22f, -0.16f}, (v3){0.18f, 0.2f, 0.16f}, fur);
  vx_cr_box(a, b, (v3){-0.16f, 0.2f, -0.18f}, (v3){0.16f, 0.46f, 0.12f}, fur);
  for (u32 s = 0; s < 2; s++) {
    f32 o = s ? 1.0f : -1.0f;
    vx_cr_box(a, b, (v3){o > 0 ? 0.06f : -0.16f, 0.46f, -0.06f}, (v3){o > 0 ? 0.16f : -0.06f, 0.62f, 0.02f}, skin);
    vx_cr_glow(a, b, (v3){o > 0 ? 0.04f : -0.11f, 0.32f, -0.2f}, (v3){o > 0 ? 0.11f : -0.04f, 0.38f, -0.18f},
               vx_cr_rgb(m->variant == VX_BAT_HELL ? 0xFFC030 : 0xE02828));
    // two-jointed wings flap through a wide arc
    f32 flap = m_sinf(m->anim * 22) * 0.9f;
    m4 w = m4_translated(b, (v3){o * 0.16f, 0.1f, 0});
    w = m4_rotate_z(w, o * flap);
    vx_cr_box(a, w, (v3){o > 0 ? 0 : -0.42f, -0.02f, -0.12f}, (v3){o > 0 ? 0.42f : 0, 0.02f, 0.2f}, skin);
    m4 w2 = m4_translated(w, (v3){o * 0.42f, 0, 0});
    w2 = m4_rotate_z(w2, o * flap * 0.6f);
    vx_cr_box(a, w2, (v3){o > 0 ? 0 : -0.36f, -0.02f, -0.1f}, (v3){o > 0 ? 0.36f : 0, 0.02f, 0.14f}, skin);
  }
}

// each townsperson's look, as the reference's market: the Merchant's beard and brown coat, the Clothier's top hat and red coat, the Dryad's green hood and robe, the Mechanic's ginger hair and yellow work trousers
hz_internal VxPuppetColors vx_cr_role_look(u32 role) {
  switch (role) {
  case VX_ROLE_MERCHANT:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xE8B48C), .hair = vx_cr_rgb(0xD8D2C8), .eyes = vx_cr_rgb(0x4A3A2A),
                            .shirt = vx_cr_rgb(0x7A5232), .undershirt = vx_cr_rgb(0xE6D8B8), .pants = vx_cr_rgb(0x4A3A2C),
                            .shoes = vx_cr_rgb(0x2E241C), .hair_style = VX_HAIR_SHORT, .body_gear = VX_BODY_COAT, .beard = true};
  case VX_ROLE_CLOTHIER:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xF0C8A4), .hair = vx_cr_rgb(0x241C1A), .eyes = vx_cr_rgb(0x2A2522),
                            .shirt = vx_cr_rgb(0xB8262A), .undershirt = vx_cr_rgb(0xF2ECE2), .pants = vx_cr_rgb(0x221E1E),
                            .shoes = vx_cr_rgb(0x161414), .head_c = vx_cr_rgb(0x1E1A1A), .body_c = vx_cr_rgb(0xC0282C),
                            .hair_style = VX_HAIR_SHORT, .head_gear = VX_HEAD_TOP_HAT, .body_gear = VX_BODY_COAT};
  case VX_ROLE_DRYAD:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xF0D0B0), .hair = vx_cr_rgb(0x5CC048), .eyes = vx_cr_rgb(0x2E7A2A),
                            .shirt = vx_cr_rgb(0x4EAE44), .undershirt = vx_cr_rgb(0x8ADC6A), .pants = vx_cr_rgb(0x3E8E38),
                            .shoes = vx_cr_rgb(0x2E5E28), .head_c = vx_cr_rgb(0x56B84A), .hair_style = VX_HAIR_LONG,
                            .head_gear = VX_HEAD_HOOD, .body_gear = VX_BODY_ROBE};
  case VX_ROLE_DEMOLITIONIST:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xF0B890), .hair = vx_cr_rgb(0xC8501E), .eyes = vx_cr_rgb(0x3C5E8C),
                            .shirt = vx_cr_rgb(0x6A4A2E), .undershirt = vx_cr_rgb(0x8A6A48), .pants = vx_cr_rgb(0x4A3A2A),
                            .shoes = vx_cr_rgb(0x2A201A), .head_c = vx_cr_rgb(0xE8B040), .hair_style = VX_HAIR_SHORT,
                            .head_gear = VX_HEAD_MINER, .beard = true};
  case VX_ROLE_MECHANIC:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xF3D2B8), .hair = vx_cr_rgb(0xD8562A), .eyes = vx_cr_rgb(0x3C5E8C),
                            .shirt = vx_cr_rgb(0xC8302C), .undershirt = vx_cr_rgb(0xE8E0D8), .pants = vx_cr_rgb(0xE8C030),
                            .shoes = vx_cr_rgb(0x2E5EC8), .hair_style = VX_HAIR_LONG};
  default:
    return (VxPuppetColors){.skin = vx_cr_rgb(0xE8B48C), .hair = vx_cr_rgb(0x6A4024), .eyes = vx_cr_rgb(0x3C5E8C),
                            .shirt = vx_cr_rgb(0xB88A50), .undershirt = vx_cr_rgb(0xEADCB8), .pants = vx_cr_rgb(0x5C4430),
                            .shoes = vx_cr_rgb(0x3A2A1E), .hair_style = VX_HAIR_SHORT};
  }
}

// a townsperson in the player's build, dressed for its role: a stride, a wave, its head turned to the player
hz_internal void vx_cr_guide(VxEntities *e, const VxWorld *w, VxV3d cam, const VxCreature *m) {
  VxPuppetColors pc = vx_cr_role_look(m->variant);
  f32 hs = m_sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
  f32 walk = m_minf(1, hs / 2.2f), breathe = m_sinf(m->age * 2.2f);
  VxPose q = {0};
  q.leg_lx = m_sinf(m->anim) * 0.7f * walk;
  q.leg_rx = -q.leg_lx;
  q.arm_lx = -q.leg_lx * 0.8f;
  q.arm_rx = q.leg_lx * 0.8f;
  q.arm_lz = 0.06f + breathe * 0.02f * (1 - walk);
  q.arm_rz = -q.arm_lz;
  q.lean = 0.06f * walk;
  q.bounce = (m_absf(m_cosf(m->anim)) - 0.5f) * 0.07f * walk + breathe * 0.012f * (1 - walk);
  q.head_y = m->look;
  q.head_x = -0.08f;
  q.wing_fold = 1;
  if (m->wave > 0) {
    // the right arm up and swaying, eased in and out
    f32 k = m_saturatef(m_minf(m->wave, 1.4f - m->wave) * 5);
    q.arm_rx = m_lerpf(q.arm_rx, 2.9f, k);
    q.arm_rz = m_lerpf(q.arm_rz, -0.25f - 0.35f * m_sinf(m->wave * 14.0f), k);
  }
  VxV3d feet = m->pos;
  vx_puppet_body(e, w, cam, feet, m->yaw, &q, &pc, 0, false);
}

void vx_creatures_build(VxEntities *e, const VxWorld *w, const VxCreatures *c, VxV3d cam, f32 time) {
  UNUSED(time);
  VxCrArt a = {.e = e, .layer = vx_tex_layer[T_PUPPET]};
  for (u32 i = 0; i < c->count; i++) {
    const VxCreature *m = &c->items[i];
    a.flash = m->flash;
    m4 base = m4_translated(m4_identity(), (v3){(f32)(m->pos.x - cam.x), (f32)(m->pos.y - cam.y), (f32)(m->pos.z - cam.z)});
    base = m4_rotate_y(base, m->yaw);
    switch (m->kind) {
    case VX_CR_SLIME: vx_cr_slime(&a, base, m, w); break;
    case VX_CR_ZOMBIE:
    case VX_CR_SKELETON: vx_cr_humanoid(&a, base, m); break;
    case VX_CR_DEMON_EYE: vx_cr_demon_eye(&a, base, m); break;
    case VX_CR_EATER: vx_cr_eater(&a, base, m); break;
    case VX_CR_BAT: vx_cr_bat(&a, base, m); break;
    case VX_CR_GUIDE: vx_cr_guide(e, w, cam, m); break;
    case VX_CR_EYE_OF_CTHULHU: vx_cr_eoc(&a, base, m); break;
    // a Servant is a small demon eye, tendril and all
    case VX_CR_SERVANT: vx_cr_demon_eye(&a, m4_scaled(base, (v3){0.62f, 0.62f, 0.62f}), m); break;
    case VX_CR_EMPRESS: vx_cr_empress_art(&a, base, m); break;
    default: break;
    }
  }
}

#undef VX_CR_GRAVITY
#undef VX_CR_MAX_FALL
#undef VX_CR_EPS
#undef VX_CR_DESPAWN
#undef VX_CR_AGGRO
#undef VX_CR_HIT_IMMUNE
#undef VX_CR_SPAWN_MIN
#undef VX_CR_SPAWN_MAX
#undef VX_CR_SPAWN_EVERY
#undef VX_TALK_REACH
#undef VX_EOC_R
#undef VX_EOC_TRANSFORM_S
#undef VX_EMP_CORE
#undef VX_EMP_CELL
#undef VX_EMP_SCALE
#undef VX_TIP_FIRST

#include "voxaria/vx_entity_geo.h"

// dynamic geometry drawn into the g-buffer: particles, fireflies, the puppet, the held item

#define VX_XYUV 4
#define VX_SPRITE_SIDES 2
// entity vertex flags: 1 cutout, 4 self-lit (emissive), 128 held item
#define VX_ENT_SELF_LIT 4u

// sprite quad corners (x, y, u, v), drawn front and back
hz_internal const u8 VX_SPRITE_QUAD[VX_ECORNERS][VX_XYUV] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {1, 1, 1, 0}, {0, 1, 0, 0}};
hz_internal const u8 VX_SPRITE_ORDER[VX_SPRITE_SIDES][VX_ECORNERS] = {{0, 1, 2, 3}, {1, 0, 3, 2}};

void vx_entities_init(VxEntities *e, Allocator *alloc, u64 seed) {
  memzero_struct(e);
  e->particles = ALLOC_ARRAY(alloc, VxParticle, VX_MAX_PARTICLES);
  e->fireflies = ALLOC_ARRAY(alloc, VxFirefly, VX_MAX_FIREFLIES);
  e->motes = ALLOC_ARRAY(alloc, VxMote, VX_MAX_MOTES);
  e->emitters = ALLOC_ARRAY(alloc, VxMoteEmitter, VX_MAX_EMITTERS);
  e->parts = ALLOC_ARRAY_NO_ZERO(alloc, VxParticleGpu, VX_PARTICLE_CAP);
  e->verts = ALLOC_ARRAY(alloc, f32, VX_ENTITY_MAX_VERTS * VX_ENTITY_STRIDE);
  e->boxes = ALLOC_ARRAY(alloc, VxBoxGpu, VX_BOX_CAP);
  e->jelly = ALLOC_ARRAY(alloc, VxBoxGpu, VX_JELLY_CAP);
  e->vfx = ALLOC_ARRAY(alloc, VxVfxGpu, VX_PARTICLE_CAP);
  e->debris = ALLOC_ARRAY(alloc, VxDebris, VX_MAX_DEBRIS);
  e->drops = ALLOC_ARRAY(alloc, VxDrop, VX_MAX_DROPS);
  e->swing_t = 1;
  e->swing_rate = 1.0f / 0.28f;
  e->equip = 1;
  e->last_item = -1;
  e->rng = random_create(seed);
}

void vx_entities_trigger_swing(VxEntities *e, f32 duration) {
  e->swing_t = 0;
  e->swing_rate = 1.0f / m_maxf(duration, 0.08f);
}

hz_internal f32 vx_rand(VxEntities *e) { return random_f32(&e->rng); }

void vx_entities_spawn_break(VxEntities *e, u8 id, i32 x, i32 y, i32 z) {
  vx_entities_dust(e, id, x, y, z, (vx_shape[id] == VX_SHAPE_CROSS || vx_shape[id] == VX_SHAPE_TORCH) ? 10 : 26);
}

// chips of the block's own texture bursting from it: a few per hit, a shower when it breaks
void vx_entities_dust(VxEntities *e, u8 id, i32 x, i32 y, i32 z, u32 n) {
  if (id == B_AIR || vx_shape[id] == VX_SHAPE_AIR) return;
  u32 layer = vx_tex_layer[vx_face_tex[id][4]];
  v3 tint = (v3){1, 1, 1};
  u32 cut = vx_layer[id] == VX_LAYER_CUTOUT ? 1 : 0;
  for (u32 i = 0; i < n; i++) {
    // keep the newest particles when the pool is full
    if (e->particle_count == VX_MAX_PARTICLES) {
      mem_move(e->particles, e->particles + 1, sizeof(VxParticle) * (VX_MAX_PARTICLES - 1));
      e->particle_count--;
    }
    f64 px = x + 0.15 + vx_rand(e) * 0.7, py = y + 0.15 + vx_rand(e) * 0.7, pz = z + 0.15 + vx_rand(e) * 0.7;
    f32 life = 0.6f + vx_rand(e) * 0.7f;
    VxParticle *q = &e->particles[e->particle_count++];
    q->p = (VxV3d){px, py, pz};
    q->v = (v3){(f32)(px - x - 0.5) * 4 + (vx_rand(e) - 0.5f), 1.5f + vx_rand(e) * 2.5f,
                (f32)(pz - z - 0.5) * 4 + (vx_rand(e) - 0.5f)};
    q->life = life;
    q->max_life = life;
    q->layer = layer;
    q->u = m_floorf(vx_rand(e) * 12);
    q->vv = m_floorf(vx_rand(e) * 12);
    q->size = 0.07f + vx_rand(e) * 0.06f;
    q->tint = tint;
    q->cut = cut;
  }
}

force_inline b32 vx_solid_block(const VxWorld *w, f64 x, f64 y, f64 z) {
  return vx_solid[vx_world_block(w, vx_floor_i(x), vx_floor_i(y), vx_floor_i(z))] != 0;
}

void vx_entities_update_fireflies(VxEntities *e, f32 dt, const VxWorld *w, VxV3d cam, b32 active) {
  e->time += dt;
  u32 want = active ? VX_MAX_FIREFLIES : 0;
  // spawn near the ground around the camera
  for (u32 tries = 0; e->firefly_count < want && tries < 4; tries++) {
    f32 a = vx_rand(e) * PI * 2;
    f32 r = 3 + vx_rand(e) * 13;
    f64 x = cam.x + m_cosf(a) * r, z = cam.z + m_sinf(a) * r;
    i32 bx = vx_floor_i(x), bz = vx_floor_i(z);
    i32 y = vx_floor_i(cam.y) + 6;
    while (y > cam.y - 12 && !vx_solid[vx_world_block(w, bx, y, bz)]) y--;
    if (!vx_solid[vx_world_block(w, bx, y, bz)]) continue;
    if (vx_world_block(w, bx, y + 1, bz) == B_WATER) continue;
    VxFirefly *f = &e->fireflies[e->firefly_count++];
    *f = (VxFirefly){.p = {x, y + 1.2 + vx_rand(e) * 2, z}, .phase = vx_rand(e) * 10};
  }
  for (i32 i = (i32)e->firefly_count - 1; i >= 0; i--) {
    VxFirefly *f = &e->fireflies[i];
    f->life = m_minf(1, f->life + dt * (active ? 0.5f : -0.5f));
    f64 dx = f->p.x - cam.x, dz = f->p.z - cam.z;
    if ((!active && f->life <= 0) || dx * dx + dz * dz > 20 * 20) {
      e->fireflies[i] = e->fireflies[--e->firefly_count];
      continue;
    }
    // gentle wandering
    f32 t = e->time + f->phase;
    f->v.x += (m_sinf(t * 0.9f) * 0.6f - f->v.x) * dt;
    f->v.y += (m_sinf(t * 1.3f + 1.7f) * 0.25f - f->v.y) * dt;
    f->v.z += (m_cosf(t * 0.7f + 0.5f) * 0.6f - f->v.z) * dt;
    f->p.x += f->v.x * dt;
    f->p.y += f->v.y * dt;
    f->p.z += f->v.z * dt;
    if (vx_solid_block(w, f->p.x, f->p.y, f->p.z)) f->p.y += 1;
  }
}

void vx_entities_update(VxEntities *e, f32 dt, const VxWorld *w) {
  if (e->swing_t < 1) e->swing_t = m_minf(1, e->swing_t + dt * e->swing_rate);
  e->swing = e->swing_t < 1 ? m_sinf(e->swing_t * PI) : 0;
  e->equip = m_minf(1, e->equip + dt / 0.18f);
  u32 kept = 0;
  for (u32 i = 0; i < e->particle_count; i++) {
    VxParticle q = e->particles[i];
    q.life -= dt;
    if (q.life <= 0) continue;
    q.v.y -= 16 * dt;
    f32 drag = m_expf(-1.5f * dt);
    q.v.x *= drag;
    q.v.z *= drag;
    f64 nx = q.p.x + q.v.x * dt, ny = q.p.y + q.v.y * dt, nz = q.p.z + q.v.z * dt;
    if (vx_solid_block(w, nx, ny - q.size, nz)) {
      q.v.y = 0;
      q.v.x *= 0.6f;
      q.v.z *= 0.6f;
      q.p.y = m_floor(ny - q.size) + 1 + q.size;
    } else {
      q.p.y = ny;
    }
    if (!vx_solid_block(w, nx, q.p.y, q.p.z)) q.p.x = nx;
    if (!vx_solid_block(w, q.p.x, q.p.y, nz)) q.p.z = nz;
    e->particles[kept++] = q;
  }
  e->particle_count = kept;
  // debris bounces off the ground, rolls to a stop and shrinks away
  kept = 0;
  for (u32 i = 0; i < e->debris_count; i++) {
    VxDebris d = e->debris[i];
    d.life -= dt;
    if (d.life <= 0) continue;
    d.v.y = m_maxf(d.v.y - 24.0f * dt, -30.0f);
    f64 ny = d.p.y + d.v.y * dt;
    if (vx_solid_block(w, d.p.x, ny - d.size * 0.5, d.p.z)) {
      if (d.v.y < 0) d.p.y = m_floor(ny - d.size * 0.5) + 1 + d.size * 0.5;
      d.v.y = m_absf(d.v.y) > 2.5f ? -d.v.y * 0.35f : 0;
      d.v.x *= 0.55f, d.v.z *= 0.55f, d.spin *= 0.5f;
    } else {
      d.p.y = ny;
    }
    f64 nx = d.p.x + d.v.x * dt, nz = d.p.z + d.v.z * dt;
    if (!vx_solid_block(w, nx, d.p.y, d.p.z)) d.p.x = nx;
    else d.v.x = -d.v.x * 0.4f;
    if (!vx_solid_block(w, d.p.x, d.p.y, nz)) d.p.z = nz;
    else d.v.z = -d.v.z * 0.4f;
    d.ang += d.spin * dt;
    e->debris[kept++] = d;
  }
  e->debris_count = kept;
}

void vx_entities_debris(VxEntities *e, VxV3d p, v3 v, f32 spread, u32 count, f32 size, const u32 *palette, u32 palette_count) {
  for (u32 i = 0; i < count; i++) {
    // a full pool replaces the oldest
    if (e->debris_count == VX_MAX_DEBRIS) {
      mem_move(e->debris, e->debris + 1, sizeof(VxDebris) * (VX_MAX_DEBRIS - 1));
      e->debris_count--;
    }
    v3 d = {vx_rand(e) * 2 - 1, vx_rand(e) * 1.4f - 0.2f, vx_rand(e) * 2 - 1};
    e->debris[e->debris_count++] = (VxDebris){
        .p = {p.x + d.x * 0.3, p.y + d.y * 0.3, p.z + d.z * 0.3},
        .v = v3_add(v, v3_scale(d, spread)),
        .life = 2.2f + vx_rand(e) * 1.6f,
        .size = size * (0.6f + vx_rand(e) * 0.7f),
        .ang = vx_rand(e) * PI * 2,
        .spin = (vx_rand(e) * 2 - 1) * 14.0f,
        .tint = palette[(u32)(vx_rand(e) * (f32)palette_count) % palette_count],
        .axis = (u8)(vx_rand(e) * 2.99f),
    };
  }
}

hz_internal void vx_debris_build(VxEntities *e, VxV3d cam) {
  u32 L = vx_tex_layer[T_PUPPET];
  for (u32 i = 0; i < e->debris_count; i++) {
    const VxDebris *d = &e->debris[i];
    f32 s = d->size * m_minf(1, d->life / 0.5f) * 0.5f;
    if (s < 0.01f) continue;
    m4 m = m4_translated(m4_identity(), (v3){(f32)(d->p.x - cam.x), (f32)(d->p.y - cam.y), (f32)(d->p.z - cam.z)});
    m = m4_rotate_y(m, d->ang);
    m = d->axis == 0 ? m4_rotate_x(m, d->ang * 0.7f) : d->axis == 1 ? m4_rotate_z(m, d->ang * 0.7f) : m;
    u32 c = d->tint;
    v3 lin = {m_powf((f32)((c >> 16) & 255) / 255.0f, 2.2f), m_powf((f32)((c >> 8) & 255) / 255.0f, 2.2f),
              m_powf((f32)(c & 255) / 255.0f, 2.2f)};
    vx_ebox_ex(e, m, (v3){-s, -s, -s}, (v3){s, s, s}, L, lin, 0, 0);
  }
}

// ---- ambient motes ----

// each kind's share of a 16384 pool, scaled to the platform's, so dense leaves never starve the mist or the embers
hz_internal const u16 VX_MOTE_QUOTA[VX_MOTE_KIND_COUNT] = {1400, 400, 200, 600, 1200, 300, 1500, 6000,
                                                          400, 600, 300, 600, 32, 32, 200, 300};

force_inline u32 vx_rgba8(f32 r, f32 g, f32 b, f32 a) {
  return (u32)(m_clampf(r, 0, 1) * 255.0f + 0.5f) | (u32)(m_clampf(g, 0, 1) * 255.0f + 0.5f) << 8 |
         (u32)(m_clampf(b, 0, 1) * 255.0f + 0.5f) << 16 | (u32)(m_clampf(a, 0, 1) * 255.0f + 0.5f) << 24;
}

// a falling leaf's colour, by the leaves it came from
hz_internal u32 vx_leaf_tint(u8 id) {
  switch (id) {
  case B_BOREAL_LEAVES: return vx_rgba8(0.2f, 0.5f, 0.36f, 1);
  case B_JUNGLE_LEAVES: return vx_rgba8(0.42f, 0.78f, 0.16f, 1);
  case B_PALM_LEAVES: return vx_rgba8(0.55f, 0.78f, 0.22f, 1);
  case B_CORRUPT_LEAVES: return vx_rgba8(0.55f, 0.38f, 0.75f, 1);
  default: return vx_rgba8(0.4f, 0.72f, 0.22f, 1);
  }
}

hz_internal void vx_mote_add(VxEntities *e, VxMoteKind kind, VxV3d p, v3 v, f32 life, f32 size, u32 tint) {
  if (e->mote_count == VX_MAX_MOTES || e->mote_kind_count[kind] >= (u32)VX_MOTE_QUOTA[kind] * VX_MAX_MOTES / 16384u) return;
  e->mote_kind_count[kind]++;
  e->motes[e->mote_count++] =
      (VxMote){.p = p, .v = v, .life = life, .max_life = life, .size = size, .phase = vx_rand(e) * 10, .tint = tint,
               .kind = (u8)kind};
}

void vx_entities_burst(VxEntities *e, VxMoteKind kind, VxV3d p, v3 v, f32 spread, u32 count, f32 life, f32 size, u32 tint) {
  for (u32 i = 0; i < count; i++) {
    v3 d = {vx_rand(e) * 2 - 1, vx_rand(e) * 2 - 1, vx_rand(e) * 2 - 1};
    f32 k = 0.67f + vx_rand(e) * 0.66f;
    vx_mote_add(e, kind, p, v3_add(v, v3_scale(d, spread)), life * k, size * (0.67f + vx_rand(e) * 0.66f), tint);
  }
}

// the foot of a falling column: water under water, a column beside air above, landing on ground or a pool
hz_internal b32 vx_is_fall_foot(const VxWorld *w, i32 x, i32 y, i32 z) {
  if (vx_world_block(w, x, y, z) != B_WATER || vx_world_block(w, x, y + 1, z) != B_WATER) return false;
  b32 column = false, landed = vx_solid[vx_world_block(w, x, y - 1, z)] != 0;
  for (u32 k = 0; k < 4; k++) {
    i32 nx = x + (k == 0) - (k == 1), nz = z + (k == 2) - (k == 3);
    if (vx_world_block(w, nx, y + 1, nz) == B_AIR) column = true;
    if (vx_world_block(w, nx, y, nz) != B_AIR) landed = true;
  }
  return column && landed;
}

force_inline b32 vx_is_lava_top(const VxWorld *w, i32 x, i32 y, i32 z) {
  return vx_world_block(w, x, y, z) == B_LAVA && vx_world_block(w, x, y + 1, z) == B_AIR;
}

// a ring of cloud under the feet: the double jump's visible kick
void vx_entities_cloud_puff(VxEntities *e, VxV3d feet) {
  for (u32 i = 0; i < 14; i++) {
    f32 a = (f32)i / 14.0f * PI * 2 + vx_rand(e) * 0.3f, s = 2.2f + vx_rand(e) * 1.2f;
    vx_mote_add(e, VX_MOTE_MIST, (VxV3d){feet.x + m_cosf(a) * 0.4, feet.y + 0.1, feet.z + m_sinf(a) * 0.4},
                (v3){m_cosf(a) * s, -0.4f + vx_rand(e) * 0.4f, m_sinf(a) * s}, 0.5f + vx_rand(e) * 0.3f,
                0.7f + vx_rand(e) * 0.3f, vx_rgba8(1, 1, 1, 0.9f));
  }
}

hz_internal void vx_emitter_add(VxEntities *e, VxMoteKind kind, i32 x, i32 y, i32 z) {
  for (u32 i = 0; i < e->emitter_count; i++) {
    VxMoteEmitter *m = &e->emitters[i];
    if (m->x == x && m->y == y && m->z == z) return;
  }
  if (e->emitter_count == VX_MAX_EMITTERS) return;
  e->emitters[e->emitter_count++] = (VxMoteEmitter){x, y, z, (u8)kind, vx_rand(e), 1.0f};
}

// one tongue of a torch's flame at the stick's tip, now and then a wisp of smoke or a rising ember
hz_internal void vx_torch_flame(VxEntities *e, const VxWorld *w, i32 x, i32 y, i32 z) {
  u8 id = vx_world_block(w, x, y, z), side = vx_world_state(w, x, y, z);
  b32 wall = side != VX_SIDE_FLOOR && side <= VX_SIDE_NZ;
  f64 tx = x + 0.5 + (side == VX_SIDE_PX ? 0.375 : side == VX_SIDE_NX ? -0.375 : 0);
  f64 tz = z + 0.5 + (side == VX_SIDE_PZ ? 0.375 : side == VX_SIDE_NZ ? -0.375 : 0);
  VxV3d tip = {tx + (vx_rand(e) - 0.5f) * 0.05f, y + (wall ? 13.0 : 10.0) / 16.0 + 0.08, tz + (vx_rand(e) - 0.5f) * 0.05f};
  b32 ice = id == B_ICE_TORCH;
  u32 flame = ice ? vx_rgba8(0.45f, 0.8f, 1.0f, 1) : vx_rgba8(1.0f, 0.62f, 0.2f, 1);
  vx_mote_add(e, VX_MOTE_FLAME, tip, (v3){0, 0.7f + vx_rand(e) * 0.4f, 0}, 0.28f + vx_rand(e) * 0.12f, 0.36f + vx_rand(e) * 0.1f, flame);
  f32 r = vx_rand(e);
  if (r < 0.14f) {
    vx_mote_add(e, VX_MOTE_SMOKE, (VxV3d){tip.x, tip.y + 0.25, tip.z}, (v3){(vx_rand(e) - 0.5f) * 0.1f, 0.55f, (vx_rand(e) - 0.5f) * 0.1f},
                1.6f + vx_rand(e) * 0.8f, 0.32f, vx_rgba8(0.42f, 0.4f, 0.38f, 0.28f));
  } else if (r < 0.19f && !ice) {
    vx_mote_add(e, VX_MOTE_EMBER, tip, (v3){(vx_rand(e) - 0.5f) * 0.5f, 1.0f + vx_rand(e) * 0.8f, (vx_rand(e) - 0.5f) * 0.5f},
                0.7f + vx_rand(e) * 0.6f, 0.1f, flame);
  }
}

void vx_entities_step_puff(VxEntities *e, VxV3d feet, v3 vel, u32 rgb) {
  f32 r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
  // dust is the ground's colour lifted towards a dry pale
  u32 tint = vx_rgba8(r * 0.6f + 0.32f, g * 0.6f + 0.3f, b * 0.6f + 0.26f, 0.5f);
  for (u32 i = 0; i < 3; i++) {
    f32 a = vx_rand(e) * PI * 2;
    vx_mote_add(e, VX_MOTE_SMOKE, (VxV3d){feet.x + m_cosf(a) * 0.2, feet.y + 0.08, feet.z + m_sinf(a) * 0.2},
                (v3){-vel.x * 0.15f + m_cosf(a) * 0.5f, 0.35f + vx_rand(e) * 0.3f, -vel.z * 0.15f + m_sinf(a) * 0.5f},
                0.45f + vx_rand(e) * 0.25f, 0.34f + vx_rand(e) * 0.12f, tint);
  }
}

void vx_entities_splash(VxEntities *e, VxV3d at, f32 speed) {
  f32 k = m_clampf(speed / 12.0f, 0.3f, 1.5f);
  u32 drops = (u32)(14 + 26 * k), water = vx_rgba8(0.78f, 0.9f, 1.0f, 0.9f);
  for (u32 i = 0; i < drops; i++) {
    f32 a = vx_rand(e) * PI * 2, s = (1.0f + vx_rand(e) * 2.2f) * k, up = (3.5f + vx_rand(e) * 4.5f) * k;
    vx_mote_add(e, VX_MOTE_DROPLET, (VxV3d){at.x + m_cosf(a) * 0.3, at.y + 0.05, at.z + m_sinf(a) * 0.3},
                (v3){m_cosf(a) * s, up, m_sinf(a) * s}, 0.7f + vx_rand(e) * 0.4f, 0.09f + vx_rand(e) * 0.06f, water);
  }
  for (u32 i = 0; i < 10; i++) {
    f32 a = (f32)i / 10.0f * PI * 2, s = 1.4f * k;
    vx_mote_add(e, VX_MOTE_MIST, (VxV3d){at.x + m_cosf(a) * 0.4, at.y + 0.15, at.z + m_sinf(a) * 0.4},
                (v3){m_cosf(a) * s, 0.8f * k, m_sinf(a) * s}, 0.6f + vx_rand(e) * 0.3f, 0.6f * k + 0.2f,
                vx_rgba8(0.9f, 0.96f, 1.0f, 0.7f));
  }
}

void vx_entities_streak(VxEntities *e, VxV3d p, v3 v, f32 life, f32 size, u32 tint) {
  vx_mote_add(e, VX_MOTE_STREAK, p, v, life, size, tint);
}

void vx_entities_death_burst(VxEntities *e, VxV3d at, f32 radius, u32 tint) {
  vx_mote_add(e, VX_MOTE_FLASH, at, (v3){0, 0, 0}, 0.16f, radius * 2.6f, vx_rgba8(1, 1, 1, 1));
  vx_mote_add(e, VX_MOTE_RING, at, (v3){0, 0, 0}, 0.32f, radius * 3.2f, tint);
  for (u32 i = 0; i < 10; i++) {
    v3 d = v3_normalize((v3){vx_rand(e) * 2 - 1, vx_rand(e) * 1.4f - 0.2f, vx_rand(e) * 2 - 1});
    vx_mote_add(e, VX_MOTE_SPARKLE, at, v3_scale(d, 3.0f + vx_rand(e) * 3.0f), 0.35f + vx_rand(e) * 0.25f,
                0.18f + vx_rand(e) * 0.1f, vx_rgba8(1, 1, 1, 1));
  }
}

void vx_entities_update_motes(VxEntities *e, f32 dt, const VxWorld *w, const VxMoteEnv *env) {
  VxV3d cam = env->cam;
  i32 cy = vx_floor_i(cam.y);
  u32 white = vx_rgba8(1, 1, 1, 1);
  // the 36x36 columns around the camera are swept in turn for emitters and canopy, the whole square every 20 frames
  for (u32 c = 0; c < (env->active ? 65u : 0u); c++) {
    u32 k = e->scan_cursor++ % (36u * 36u);
    i32 x = vx_floor_i(cam.x) - 18 + (i32)(k % 36u), z = vx_floor_i(cam.z) - 18 + (i32)(k / 36u);
    for (i32 y = cy + 10; y >= cy - 24; y--) {
      u8 b = vx_world_block(w, x, y, z);
      if (b == B_AIR) continue;
      if (b == B_LAVA) {
        if (vx_is_lava_top(w, x, y, z)) vx_emitter_add(e, VX_MOTE_EMBER, x, y, z);
      } else if (b == B_WATER) {
        if (vx_is_fall_foot(w, x, y, z)) vx_emitter_add(e, VX_MOTE_MIST, x, y, z);
      } else if (vx_shape[b] == VX_SHAPE_TORCH) {
        vx_emitter_add(e, VX_MOTE_FLAME, x, y, z);
      } else if (vx_is_leaves(b) && vx_world_block(w, x, y - 1, z) == B_AIR && vx_rand(e) < 0.055f) {
        vx_mote_add(e, VX_MOTE_LEAF, (VxV3d){x + vx_rand(e), y - 0.05, z + vx_rand(e)}, (v3){0, -0.6f, 0}, 7.0f,
                    0.34f, vx_leaf_tint(b));
      } else if ((b == B_MUSHROOM_GRASS || b == B_GLOW_MUSHROOM) && y > cy - 10 && vx_rand(e) < 0.065f) {
        // only into open air near the camera's height, so caves out of sight never take the spores' share
        f64 sy = y + 1.1 + vx_rand(e) * 2;
        if (vx_world_block(w, x, vx_floor_i(sy), z) != B_AIR) continue;
        vx_mote_add(e, VX_MOTE_SPORE, (VxV3d){x + vx_rand(e), sy, z + vx_rand(e)},
                    (v3){0, 0.25f + vx_rand(e) * 0.2f, 0}, 4.0f + vx_rand(e) * 3, 0.22f,
                    vx_rgba8(0.6f, 0.9f, 1.0f, 1));
      }
    }
  }
  // emitters: rechecked once a second, dropped when gone or far away
  for (i32 i = (i32)e->emitter_count - 1; i >= 0; i--) {
    VxMoteEmitter *m = &e->emitters[i];
    f64 dx = m->x + 0.5 - cam.x, dz = m->z + 0.5 - cam.z;
    m->check -= dt;
    b32 drop = !env->active || dx * dx + dz * dz > 30.0 * 30.0;
    if (!drop && m->check <= 0) {
      m->check = 1.0f;
      drop = m->kind == VX_MOTE_EMBER   ? !vx_is_lava_top(w, m->x, m->y, m->z)
             : m->kind == VX_MOTE_FLAME ? vx_shape[vx_world_block(w, m->x, m->y, m->z)] != VX_SHAPE_TORCH
                                        : !vx_is_fall_foot(w, m->x, m->y, m->z);
    }
    if (drop) {
      e->emitters[i] = e->emitters[--e->emitter_count];
      continue;
    }
    m->acc += dt * (m->kind == VX_MOTE_EMBER ? 0.7f : m->kind == VX_MOTE_FLAME ? 16.0f : 22.0f);
    for (; m->acc >= 1; m->acc -= 1) {
      if (m->kind == VX_MOTE_FLAME) {
        vx_torch_flame(e, w, m->x, m->y, m->z);
      } else if (m->kind == VX_MOTE_EMBER) {
        vx_mote_add(e, VX_MOTE_EMBER, (VxV3d){m->x + vx_rand(e), m->y + 0.95, m->z + vx_rand(e)},
                    (v3){(vx_rand(e) - 0.5f) * 0.4f, 1.2f + vx_rand(e) * 1.6f, (vx_rand(e) - 0.5f) * 0.4f},
                    1.4f + vx_rand(e) * 1.6f, 0.26f + vx_rand(e) * 0.14f, white);
      } else {
        f32 a = vx_rand(e) * PI * 2, s = 0.8f + vx_rand(e) * 1.6f;
        vx_mote_add(e, VX_MOTE_MIST,
                    (VxV3d){m->x + 0.5 + m_cosf(a) * 0.62, m->y + 0.9 + vx_rand(e) * 0.3, m->z + 0.5 + m_sinf(a) * 0.62},
                    (v3){m_cosf(a) * s, 0.9f + vx_rand(e) * 1.2f, m_sinf(a) * s}, 0.7f + vx_rand(e) * 0.6f,
                    0.55f + vx_rand(e) * 0.4f, vx_rgba8(0.92f, 0.97f, 1.0f, 0.8f));
      }
    }
  }
  // free-floating kinds: flurries under an open snowy sky, dust in lit caves, ash and embers over the underworld
  f32 rate_snow = env->active && env->biome == VX_BIOME_SNOW && env->underground < 0.5f ? 40.0f : 0.0f;
  f32 rate_dust = env->active ? 10.0f * env->underground * (1 - env->underworld) : 0.0f;
  f32 rate_ember = env->active ? 110.0f * env->underworld : 0.0f;
  f32 n_snow = rate_snow * dt, n_dust = rate_dust * dt, n_ember = rate_ember * dt;
  for (f32 n = n_snow + vx_rand(e); n >= 1; n -= 1) {
    VxV3d p = {cam.x + (vx_rand(e) - 0.5f) * 30, cam.y + 6 + vx_rand(e) * 6, cam.z + (vx_rand(e) - 0.5f) * 30};
    u16 l = vx_world_light(w, vx_floor_i(p.x), vx_floor_i(p.y), vx_floor_i(p.z));
    if ((l >> VX_LIGHT_SKY_SHIFT) < 14) continue;
    vx_mote_add(e, VX_MOTE_SNOW, p, (v3){0, -1.1f - vx_rand(e) * 0.5f, 0}, 9.0f, 0.26f + vx_rand(e) * 0.1f, white);
  }
  for (f32 n = n_dust + vx_rand(e); n >= 1; n -= 1) {
    VxV3d p = {cam.x + (vx_rand(e) - 0.5f) * 14, cam.y + (vx_rand(e) - 0.5f) * 8, cam.z + (vx_rand(e) - 0.5f) * 14};
    if (vx_world_block(w, vx_floor_i(p.x), vx_floor_i(p.y), vx_floor_i(p.z)) != B_AIR) continue;
    vx_mote_add(e, VX_MOTE_DUST, p, (v3){(vx_rand(e) - 0.5f) * 0.1f, (vx_rand(e) - 0.5f) * 0.06f, (vx_rand(e) - 0.5f) * 0.1f},
                5.0f + vx_rand(e) * 4, 0.12f, vx_rgba8(0.95f, 0.88f, 0.72f, 0.75f));
  }
  for (f32 n = n_ember + vx_rand(e); n >= 1; n -= 1) {
    VxV3d p = {cam.x + (vx_rand(e) - 0.5f) * 34, cam.y - 6 + vx_rand(e) * 8, cam.z + (vx_rand(e) - 0.5f) * 34};
    if (vx_world_block(w, vx_floor_i(p.x), vx_floor_i(p.y), vx_floor_i(p.z)) != B_AIR) continue;
    vx_mote_add(e, VX_MOTE_EMBER, p, (v3){(vx_rand(e) - 0.5f) * 0.6f, 0.8f + vx_rand(e) * 1.4f, (vx_rand(e) - 0.5f) * 0.6f},
                2.0f + vx_rand(e) * 2, 0.24f + vx_rand(e) * 0.14f, white);
  }

  // motion: embers and spores rise and waver, leaves and snow tumble down, mist bursts and slows, dust hangs
  f32 t = e->time;
  u32 kept = 0;
  mem_zero(e->mote_kind_count, sizeof(e->mote_kind_count));
  for (u32 i = 0; i < e->mote_count; i++) {
    VxMote m = e->motes[i];
    m.life -= dt;
    if (m.life <= 0) continue;
    f32 sway = m_sinf(t * 1.7f + m.phase * 2.3f), sway2 = m_cosf(t * 1.3f + m.phase * 1.7f);
    v3 v = m.v;
    switch (m.kind) {
    case VX_MOTE_EMBER: v.x += sway * 0.6f * dt, v.z += sway2 * 0.6f * dt; break;
    case VX_MOTE_SPORE: v.x = sway * 0.18f, v.z = sway2 * 0.18f; break;
    case VX_MOTE_LEAF: v.x = sway * 0.7f, v.z = sway2 * 0.5f; break;
    case VX_MOTE_SNOW: v.x = sway * 0.35f + 0.2f, v.z = sway2 * 0.3f; break;
    case VX_MOTE_MIST: {
      f32 drag = m_expf(-2.5f * dt);
      v.x *= drag, v.z *= drag, v.y = v.y * drag + 0.15f * dt;
    } break;
    case VX_MOTE_SPARK: v = v3_scale(v, m_expf(-1.5f * dt)), v.y -= 14.0f * dt; break;
    case VX_MOTE_TRAIL: v = v3_scale(v, m_expf(-7.0f * dt)); break;
    case VX_MOTE_GORE: v.y -= 22.0f * dt; break;
    case VX_MOTE_SMOKE: {
      f32 drag = m_expf(-1.2f * dt);
      v.x = v.x * drag + sway * 0.25f * dt, v.z = v.z * drag + sway2 * 0.25f * dt, v.y = v.y * drag + 0.5f * dt;
    } break;
    case VX_MOTE_FLAME: v.x = sway * 0.12f, v.z = sway2 * 0.12f; break;
    case VX_MOTE_DROPLET: v.y -= 20.0f * dt; break;
    case VX_MOTE_STREAK: v = v3_scale(v, m_expf(-0.8f * dt)); break;
    case VX_MOTE_SPARKLE: v = v3_scale(v, m_expf(-3.0f * dt)); break;
    default: break;
    }
    m.v = v;
    VxV3d np = {m.p.x + v.x * dt, m.p.y + v.y * dt, m.p.z + v.z * dt};
    if (vx_solid_block(w, np.x, np.y, np.z)) {
      // falling kinds settle and fade where they land, flames and flashes pass through, the rest just end
      if (m.kind == VX_MOTE_FLAME || m.kind == VX_MOTE_FLASH || m.kind == VX_MOTE_RING) {
        m.p = np;
      } else if (m.kind != VX_MOTE_LEAF && m.kind != VX_MOTE_SNOW && m.kind != VX_MOTE_GORE) {
        continue;
      } else {
        m.v = (v3){0, 0, 0};
        m.life = m_minf(m.life, 0.8f);
      }
    } else {
      m.p = np;
    }
    f64 dx = m.p.x - cam.x, dz = m.p.z - cam.z;
    if (dx * dx + dz * dz > 34.0 * 34.0) continue;
    e->mote_kind_count[m.kind]++;
    e->motes[kept++] = m;
  }
  e->mote_count = kept;
}

f32 vx_entities_lava_heat(const VxEntities *e, VxV3d cam) {
  f32 heat = 0;
  for (u32 i = 0; i < e->emitter_count; i++) {
    const VxMoteEmitter *m = &e->emitters[i];
    if (m->kind != VX_MOTE_EMBER) continue;
    f64 dx = m->x + 0.5 - cam.x, dy = m->y + 1 - cam.y, dz = m->z + 0.5 - cam.z;
    heat += m_maxf(0, 1 - (f32)m_sqrt(dx * dx + dy * dy + dz * dz) / 12.0f);
  }
  return m_minf(1, heat / 5.0f);
}

hz_internal void vx_vfx(VxEntities *e, VxV3d p, VxV3d cam, f32 size, u32 tint, u32 sprite, u32 flags, f32 spin, f32 glow) {
  if (e->vfx_count == VX_PARTICLE_CAP) return;
  e->vfx[e->vfx_count++] = (VxVfxGpu){.x = (f32)(p.x - cam.x), .y = (f32)(p.y - cam.y), .z = (f32)(p.z - cam.z),
                                      .size = size, .tint = tint, .misc = sprite | flags << 8, .spin = spin, .glow = glow};
}

// a glowing smear trailing len blocks behind p along dir
hz_internal void vx_vfx_streak(VxEntities *e, VxV3d p, VxV3d cam, f32 size, u32 tint, u32 sprite, v3 dir, f32 len, f32 glow) {
  if (e->vfx_count == VX_PARTICLE_CAP) return;
  e->vfx[e->vfx_count++] = (VxVfxGpu){.x = (f32)(p.x - cam.x), .y = (f32)(p.y - cam.y), .z = (f32)(p.z - cam.z),
                                      .size = size, .dx = dir.x, .dy = dir.y, .dz = dir.z, .stretch = len, .tint = tint,
                                      .misc = sprite | VX_VFX_GLOW << 8, .glow = glow};
}

// pylon gems: a glowing crystal stood on its point, spinning and bobbing over each pylon near the camera, shards on its pins
hz_internal void vx_pylons_build(VxEntities *e, const VxWorld *w, VxV3d cam) {
  // forest pink, ocean blue, snow cyan, desert gold, jungle green, cavern teal, hallow violet, underworld orange (linear)
  hz_internal const v3 gem[8] = {{1.0f, 0.16f, 0.62f}, {0.07f, 0.26f, 1.0f}, {0.2f, 0.88f, 1.0f}, {1.0f, 0.7f, 0.07f},
                                 {0.2f, 0.87f, 0.1f},  {0.04f, 0.75f, 0.45f}, {0.45f, 0.2f, 1.0f}, {1.0f, 0.33f, 0.04f}};
  u32 L = vx_tex_layer[T_PUPPET];
  i32 ccx = vx_floor_i(cam.x) >> 4, ccz = vx_floor_i(cam.z) >> 4;
  for (i32 cz = ccz - 5; cz <= ccz + 5; cz++)
    for (i32 cx = ccx - 5; cx <= ccx + 5; cx++) {
      const VxChunk *c = vx_world_chunk(w, cx, cz);
      if (!c || c->state == VX_CHUNK_GENERATING) continue;
      for (u32 k = 0; k < c->pylon_count; k++) {
        u32 i = c->pylons[k];
        i32 x = cx * 16 + (i32)(i & 15u), y = (i32)(i >> 8), z = cz * 16 + (i32)((i >> 4) & 15u);
        f32 dx = (f32)(x + 0.5 - cam.x), dy = (f32)(y - cam.y), dz = (f32)(z + 0.5 - cam.z);
        if (dx * dx + dy * dy + dz * dz > 72.0f * 72.0f) continue;
        f32 ph = (f32)(vx_hash3i(x, y, z, 0x9e3779b9u) & 1023u) / 1023.0f * PI * 2;
        v3 col = gem[vx_world_state(w, x, y, z) & 7u], core = v3_lerp(col, (v3){1, 1, 1}, 0.35f);
        m4 g = m4_translated(m4_identity(), (v3){dx, dy + 1.2f + m_sinf(e->time * 1.6f + ph) * 0.06f, dz});
        g = m4_rotate_y(g, e->time * 0.9f + ph);
        g = m4_scaled(g, (v3){1, 1.6f, 1});
        g = m4_rotate_x(g, -0.61548f);
        g = m4_rotate_z(g, PI * 0.25f);
        vx_ejelly(e, g, (v3){-0.24f, -0.24f, -0.24f}, (v3){0.24f, 0.24f, 0.24f}, col, 0, 0);
        vx_ebox_ex(e, g, (v3){-0.15f, -0.15f, -0.15f}, (v3){0.15f, 0.15f, 0.15f}, L, core, VX_BOX_SELF_LIT, 0);
        for (u32 s = 0; s < 4; s++) {
          f32 ox = (s & 1) ? 0.3125f : -0.3125f, oz = (s & 2) ? 0.3125f : -0.3125f;
          m4 sh = m4_translated(m4_identity(), (v3){dx + ox, dy + 0.94f, dz + oz});
          sh = m4_rotate_y(sh, PI * 0.25f);
          sh = m4_scaled(sh, (v3){1, 1.8f, 1});
          sh = m4_rotate_x(sh, -0.61548f);
          sh = m4_rotate_z(sh, PI * 0.25f);
          vx_ebox_ex(e, sh, (v3){-0.05f, -0.05f, -0.05f}, (v3){0.05f, 0.05f, 0.05f}, L, col, VX_BOX_SELF_LIT, 0);
        }
        // three twinkles circling the gem, each flaring now and then
        for (u32 j = 0; j < 3; j++) {
          f32 a = e->time * 1.1f + ph + (f32)j * 2.094f, fl = m_sinf(e->time * 2.7f + (f32)j * 1.7f + ph);
          if (fl <= 0) continue;
          VxV3d p = {x + 0.5 + m_cosf(a) * 0.42f, y + 1.2 + m_sinf(e->time * 0.8f + (f32)j * 2.0f) * 0.3f, z + 0.5 + m_sinf(a) * 0.42f};
          vx_vfx(e, p, cam, 0.16f, vx_rgba8(col.x * 0.5f + 0.5f, col.y * 0.5f + 0.5f, col.z * 0.5f + 0.5f, fl * fl), VX_VFX_SQUARE,
                 VX_VFX_GLOW, 0, 3.0f);
        }
      }
    }
}

force_inline u32 vx_with_alpha(u32 tint, f32 a) { return (tint & 0xFFFFFFu) | (u32)(m_clampf(a, 0, 1) * 255.0f + 0.5f) << 24; }

hz_internal void vx_part(VxEntities *e, VxV3d p, VxV3d cam, f32 size, u32 tint, u32 tex, u32 flags, f32 spin) {
  if (e->part_count == VX_PARTICLE_CAP) return;
  e->parts[e->part_count++] = (VxParticleGpu){.x = (f32)(p.x - cam.x), .y = (f32)(p.y - cam.y), .z = (f32)(p.z - cam.z),
                                              .size = size, .tint = tint,
                                              .misc = vx_tex_layer[tex] | flags << 12, .spin = spin};
}

// motes and fireflies: glows, smoke and splashes as forward sprites; leaves, snow, dust and gore as lit pixel-art cards
hz_internal void vx_entities_build_particles(VxEntities *e, VxV3d cam) {
  e->part_count = 0;
  e->vfx_count = 0;
  for (u32 i = 0; i < e->mote_count; i++) {
    const VxMote *m = &e->motes[i];
    f32 k = m_minf(1, m->life / m_minf(0.6f, m->max_life * 0.4f)) * m_minf(1, (m->max_life - m->life) * 6 + 0.2f);
    f32 s = m->size * k, age = 1 - m->life / m->max_life;
    switch (m->kind) {
    case VX_MOTE_EMBER: {
      f32 fl = 0.75f + 0.25f * m_sinf(e->time * 13 + m->phase * 5);
      // white-hot when fresh, cooling through orange to a dull red
      u32 tint = vx_rgba8(1.0f, 0.3f + 0.55f * (1 - age), 0.08f + 0.3f * (1 - age) * (1 - age), k);
      vx_vfx(e, m->p, cam, s * 0.8f, tint, VX_VFX_SQUARE, VX_VFX_GLOW, m->phase, 3.6f * fl);
    } break;
    case VX_MOTE_SPORE:
      vx_vfx(e, m->p, cam, s * (0.9f + 0.3f * m_sinf(e->time * 2.1f + m->phase * 3)), vx_with_alpha(m->tint, k), VX_VFX_SQUARE,
             VX_VFX_GLOW, 0, 3.0f);
      break;
    case VX_MOTE_MIST:
    case VX_MOTE_SMOKE: {
      b32 smoke = m->kind == VX_MOTE_SMOKE;
      f32 sz = m->size * (smoke ? 0.5f + age * 1.6f : 0.6f + age * 1.4f);
      f32 a = ((m->tint >> 24) / 255.0f) * m_clampf((1 - age) * 1.4f, 0, 1) * m_minf(1, age * 8 + 0.2f);
      vx_vfx(e, m->p, cam, sz * 1.6f, vx_with_alpha(m->tint, a), PG_SPRITE_PUFF, 0, m->phase + age * 0.8f, 0);
    } break;
    case VX_MOTE_FLAME: {
      f32 fl = 0.8f + 0.2f * m_sinf(e->time * 23 + m->phase * 7);
      vx_vfx(e, m->p, cam, m->size * (1 - age * 0.6f) * fl, vx_with_alpha(m->tint, 1 - age), PG_SPRITE_FLAME, VX_VFX_GLOW, 0,
             2.6f);
    } break;
    case VX_MOTE_SPARK: vx_vfx(e, m->p, cam, m->size * m_minf(1, m->life / m->max_life * 1.5f), m->tint, VX_VFX_SQUARE, VX_VFX_GLOW, 0, 2.4f); break;
    case VX_MOTE_TRAIL: {
      f32 k2 = m->life / m->max_life;
      vx_vfx(e, m->p, cam, m->size * (0.35f + 0.65f * k2), vx_with_alpha(m->tint, k2 * 1.6f), VX_VFX_SQUARE, VX_VFX_GLOW, 0,
             2.2f);
    } break;
    case VX_MOTE_DROPLET: vx_vfx(e, m->p, cam, s, m->tint, VX_VFX_SQUARE, 0, 0, 0); break;
    case VX_MOTE_FLASH:
      vx_vfx(e, m->p, cam, m->size * (0.6f + age * 0.8f), vx_with_alpha(m->tint, 1 - age), PG_SPRITE_FLASH, VX_VFX_GLOW, m->phase, 3.0f);
      break;
    case VX_MOTE_RING:
      vx_vfx(e, m->p, cam, m->size * (0.3f + age * 1.7f), vx_with_alpha(m->tint, (1 - age) * 0.9f), PG_SPRITE_RING, VX_VFX_GLOW,
             m->phase, 2.0f);
      break;
    case VX_MOTE_STREAK: {
      f32 sp = v3_length(m->v);
      v3 dir = sp > 1e-3f ? v3_scale(m->v, 1.0f / sp) : (v3){0, -1, 0};
      vx_vfx_streak(e, m->p, cam, m->size, vx_with_alpha(m->tint, 1 - age), PG_SPRITE_STREAK, dir, m_minf(sp * 0.15f, 7.0f), 4.0f);
    } break;
    case VX_MOTE_SPARKLE: {
      // a quick twinkle: grows, peaks bright, shrinks
      f32 tw = m_sinf(age * PI);
      vx_vfx(e, m->p, cam, m->size * tw, m->tint, VX_VFX_SQUARE, VX_VFX_GLOW, PI * 0.25f, 3.0f * tw);
    } break;
    case VX_MOTE_LEAF: vx_part(e, m->p, cam, s, m->tint, T_PART_LEAF, 0, e->time * 1.4f + m->phase * 2); break;
    case VX_MOTE_SNOW: vx_part(e, m->p, cam, s, m->tint, T_PART_FLAKE, 0, m->phase); break;
    case VX_MOTE_GORE: vx_part(e, m->p, cam, m->size * m_minf(1, m->life / 0.5f), m->tint, T_PART_MOTE, 0, m->phase); break;
    default: vx_part(e, m->p, cam, s, m->tint, T_PART_MOTE, 0, 0); break;
    }
  }
  for (u32 i = 0; i < e->firefly_count; i++) {
    const VxFirefly *f = &e->fireflies[i];
    f32 blink = m_maxf(0, m_sinf(e->time * 1.7f + f->phase * 3.1f));
    f32 s = 0.42f * m_powf(blink, 0.6f) * f->life;
    if (s < 0.02f) continue;
    vx_vfx(e, f->p, cam, s, vx_rgba8(0.75f, 1.0f, 0.35f, 1), PG_SPRITE_MOTE, VX_VFX_GLOW, 0, 2.0f * blink);
  }
}

// the grappling hook's chain, link by link from the hand, and its claw
hz_internal void vx_hook_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxPlayer *p) {
  if (p->hook_state == VX_HOOK_NONE) return;
  VxV3d hand = vx_puppet_hook_hand(p);
  v3 a = {(f32)(hand.x - cam.x), (f32)(hand.y - cam.y), (f32)(hand.z - cam.z)};
  v3 b = {(f32)(p->hook_pos.x - cam.x), (f32)(p->hook_pos.y - cam.y), (f32)(p->hook_pos.z - cam.z)};
  v3 d = v3_sub(b, a);
  f32 len = v3_length(d);
  if (len < 0.05f) return;
  v3 z = v3_scale(d, 1.0f / len);
  v3 up = m_absf(z.y) > 0.95f ? (v3){1, 0, 0} : (v3){0, 1, 0};
  v3 x = v3_normalize(v3_cross(up, z)), y = v3_cross(z, x);
  f32 sky, blk;
  vx_light_at(w, p->hook_pos, &sky, &blk);
  u32 L = vx_tex_layer[T_PUPPET];
  v3 steel = {0.42f, 0.44f, 0.5f}, dark = {0.22f, 0.23f, 0.27f};
  u32 links = (u32)(len / 0.26f);
  for (u32 i = 0; i <= links; i++) {
    v3 c = v3_add(a, v3_scale(z, (f32)i * 0.26f));
    // alternate links turn a quarter, as a chain's do
    v3 lx = i & 1 ? y : x, ly = i & 1 ? v3_scale(x, -1) : y;
    m4 m = {0};
    m.m[0][0] = lx.x, m.m[0][1] = lx.y, m.m[0][2] = lx.z;
    m.m[1][0] = ly.x, m.m[1][1] = ly.y, m.m[1][2] = ly.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = c.x, m.m[3][1] = c.y, m.m[3][2] = c.z, m.m[3][3] = 1;
    vx_ebox(e, m, (v3){-0.035f, -0.08f, -0.02f}, (v3){0.035f, 0.08f, 0.2f}, L, i & 1 ? dark : steel, sky, blk);
  }
  m4 h = {0};
  h.m[0][0] = x.x, h.m[0][1] = x.y, h.m[0][2] = x.z;
  h.m[1][0] = y.x, h.m[1][1] = y.y, h.m[1][2] = y.z;
  h.m[2][0] = z.x, h.m[2][1] = z.y, h.m[2][2] = z.z;
  h.m[3][0] = b.x, h.m[3][1] = b.y, h.m[3][2] = b.z, h.m[3][3] = 1;
  vx_ebox(e, h, (v3){-0.08f, -0.08f, -0.25f}, (v3){0.08f, 0.08f, 0.05f}, L, steel, sky, blk);
  vx_ebox(e, h, (v3){-0.2f, -0.04f, -0.06f}, (v3){0.2f, 0.04f, 0.04f}, L, dark, sky, blk);
  vx_ebox(e, h, (v3){-0.04f, -0.2f, -0.06f}, (v3){0.04f, 0.2f, 0.04f}, L, dark, sky, blk);
}

void vx_entities_spawn_drop(VxEntities *e, VxV3d p, VxItemId item, u32 count) {
  if (!item || !count) return;
  // a full pool drops the oldest
  if (e->drop_count == VX_MAX_DROPS) {
    mem_move(e->drops, e->drops + 1, sizeof(VxDrop) * (VX_MAX_DROPS - 1));
    e->drop_count--;
  }
  f32 a = vx_rand(e) * PI * 2;
  e->drops[e->drop_count++] = (VxDrop){.p = p, .v = {m_cosf(a) * 1.6f, 3.5f + vx_rand(e) * 1.5f, m_sinf(a) * 1.6f},
                                       .item = item, .count = (u16)MIN(count, 65535u), .spin = vx_rand(e) * PI * 2};
}

#define VX_DROP_DELAY 0.55f // seconds before a fresh drop can be collected, so it is seen to pop
#define VX_DROP_REACH 5.25f // Terraria's item grab range in tiles
#define VX_DROP_GRAB 1.1f

void vx_entities_update_drops(VxEntities *e, f32 dt, const VxWorld *w, VxV3d chest, VxInventory *inv) {
  e->pickup_count = 0;
  // neighbours of the same item merge a few times a second
  e->merge_t -= dt;
  if (e->merge_t <= 0) {
    e->merge_t = 0.25f;
    for (u32 i = 0; i < e->drop_count; i++) {
      VxDrop *a = &e->drops[i];
      if (!a->count) continue;
      u32 cap = vx_item(a->item)->stack;
      for (u32 j = i + 1; j < e->drop_count; j++) {
        VxDrop *b = &e->drops[j];
        if (b->item != a->item || !b->count || (u32)a->count + b->count > cap) continue;
        f64 dx = a->p.x - b->p.x, dy = a->p.y - b->p.y, dz = a->p.z - b->p.z;
        if (dx * dx + dy * dy + dz * dz > 1.2 * 1.2) continue;
        a->count = (u16)(a->count + b->count);
        b->count = 0;
      }
    }
  }
  u32 kept = 0;
  for (u32 i = 0; i < e->drop_count; i++) {
    VxDrop d = e->drops[i];
    if (!d.count) continue;
    d.age += dt;
    d.spin += dt * 1.6f;
    f64 dx = chest.x - d.p.x, dy = chest.y - d.p.y, dz = chest.z - d.p.z;
    f64 dist = m_sqrt(dx * dx + dy * dy + dz * dz);
    b32 fits = vx_inventory_can_take(inv, d.item);
    if (d.age > VX_DROP_DELAY && dist < VX_DROP_REACH && fits) {
      // flies to the player, faster the closer it gets
      f32 sp = 7.0f + (f32)(VX_DROP_REACH - dist) * 3.0f;
      d.v = (v3){(f32)(dx / dist) * sp, (f32)(dy / dist) * sp, (f32)(dz / dist) * sp};
      d.p = (VxV3d){d.p.x + d.v.x * dt, d.p.y + d.v.y * dt, d.p.z + d.v.z * dt};
      // a short glittering wake while it flies in
      if (vx_rand(e) < dt * 40.0f)
        vx_mote_add(e, VX_MOTE_TRAIL, (VxV3d){d.p.x, d.p.y + 0.2, d.p.z}, (v3){0, 0, 0}, 0.22f, 0.16f, vx_rgba8(1, 0.95f, 0.75f, 0.8f));
      if (dist < VX_DROP_GRAB) {
        u32 left = vx_inventory_add(inv, d.item, d.count);
        if (left < d.count && e->pickup_count < VX_MAX_PICKUPS)
          e->pickups[e->pickup_count++] = (VxStack){d.item, (u16)(d.count - left)};
        d.count = (u16)left;
        if (!d.count) continue;
      }
    } else {
      // precious things glint where they lie, in their rarity's colour
      u32 rare = vx_item(d.item)->rarity;
      b32 coin = vx_is_coin(d.item);
      if ((rare || coin) && vx_rand(e) < dt * (1.2f + rare * 0.6f)) {
        hz_internal const f32 RARE_RGB[5][3] = {{1, 0.9f, 0.5f}, {0.55f, 0.7f, 1}, {0.5f, 1, 0.55f}, {1, 0.7f, 0.3f}, {1, 0.55f, 0.55f}};
        const f32 *c = RARE_RGB[MIN(rare, 4u)];
        VxV3d at = {d.p.x + (vx_rand(e) - 0.5f) * 0.5, d.p.y + 0.1 + vx_rand(e) * 0.45, d.p.z + (vx_rand(e) - 0.5f) * 0.5};
        vx_mote_add(e, VX_MOTE_SPARKLE, at, (v3){0, 0.25f, 0}, 0.5f, 0.2f, vx_rgba8(c[0], c[1], c[2], 1));
      }
      d.v.y -= 22.0f * dt;
      f32 drag = m_expf(-2.0f * dt);
      d.v.x *= drag, d.v.z *= drag;
      VxV3d n = {d.p.x + d.v.x * dt, d.p.y + d.v.y * dt, d.p.z + d.v.z * dt};
      if (vx_solid_block(w, n.x, d.p.y, d.p.z)) n.x = d.p.x, d.v.x = 0;
      if (vx_solid_block(w, d.p.x, d.p.y, n.z)) n.z = d.p.z, d.v.z = 0;
      if (vx_solid_block(w, n.x, n.y - 0.12, n.z)) {
        n.y = m_floor(n.y - 0.12) + 1.12;
        d.v.y = 0;
        d.v.x *= 0.5f, d.v.z *= 0.5f;
      }
      // a drop buried by a placed block pops out on top
      if (vx_solid_block(w, n.x, n.y, n.z)) n.y = m_floor(n.y) + 1.12;
      d.p = n;
    }
    e->drops[kept++] = d;
  }
  e->drop_count = kept;
}

#undef VX_DROP_DELAY
#undef VX_DROP_REACH
#undef VX_DROP_GRAB

#define VX_ITEM_SLICES 4

// a model block's boxes fitted into [0,1]^3, each face showing its part of the tile
hz_internal void vx_emit_model_item(VxEntities *e, m4 xf, u8 id, f32 sky, f32 blk, u32 flags) {
  const VxBox *boxes;
  u32 count;
  f32 W = 16, H = 16;
  if (vx_is_furniture(id)) {
    VxFurniture fu = vx_furniture[id];
    boxes = vx_furn_boxes + fu.first, count = fu.count;
    W = (f32)fu.w * 16, H = (f32)fu.h * 16;
  } else {
    boxes = vx_boxes + vx_model[id].first, count = vx_model[id].count;
  }
  f32 sc = 1.0f / m_maxf(W, H), offx = (1 - W * sc) * 0.5f, offz = (1 - 16 * sc) * 0.5f;
  v3 tint = (v3){1, 1, 1};
  for (u32 i = 0; i < count; i++) {
    const VxBox *b = &boxes[i];
    v3 lo = {b->lo[0] * sc + offx, b->lo[1] * sc, b->lo[2] * sc + offz};
    v3 hi = {b->hi[0] * sc + offx, b->hi[1] * sc, b->hi[2] * sc + offz};
    for (u32 f = 0; f < VX_EFACES; f++) {
      u32 t = b->tex[f] != VX_BOX_TEX_BLOCK ? b->tex[f] : vx_face_tex[id][f];
      u32 layer = vx_tex_layer[t];
      // the face's extent along its two tile axes, wrapped into one tile
      u32 ua = f < 2 ? 2 : 0, va = f >= 2 && f < 4 ? 2 : 1;
      f32 u0 = (f32)(b->lo[ua] & 15) / 16.0f, us = m_minf((f32)(b->hi[ua] - b->lo[ua]), 16.0f - (b->lo[ua] & 15)) / 16.0f;
      f32 vs = m_minf((f32)(b->hi[va] - b->lo[va]), 16.0f) / 16.0f;
      v3 n = v3_normalize(m4_mulv3(xf, VX_FACE_N[f], 0.0f));
      for (u32 k = 0; k < VX_ECORNERS; k++) {
        const u8 *c = VX_ENT_CORNERS[f][k];
        v3 p = {c[0] ? hi.x : lo.x, c[1] ? hi.y : lo.y, c[2] ? hi.z : lo.z};
        f32 u = u0 + VX_ENT_UV[k][0] * us, v = 1 - (1 - VX_ENT_UV[k][1]) * vs;
        vx_evtx(e, m4_mulv3(xf, p, 1.0f), u, v, layer, f, n, sky, blk, flags, tint);
      }
    }
  }
}

// an item over [0,1]: a block's cube, a plant's two-sided sprite, or other items' pixel art extruded in slices
void vx_emit_item(VxEntities *e, m4 xf, VxItemId item, f32 sky, f32 blk, u32 flags) {
  v3 tint = (v3){1, 1, 1};
  if (!vx_item_is_block(item)) {
    u32 layer = item - B_COUNT;
    v3 n = v3_normalize(m4_mulv3(xf, (v3){0, 0, 1}, 0.0f));
    for (u32 sl = 0; sl < VX_ITEM_SLICES; sl++) {
      f32 z = ((f32)sl - (VX_ITEM_SLICES - 1) * 0.5f) / (16.0f * (VX_ITEM_SLICES - 1));
      for (u32 o = 0; o < VX_SPRITE_SIDES; o++) {
        for (u32 k = 0; k < VX_ECORNERS; k++) {
          const u8 *q = VX_SPRITE_QUAD[VX_SPRITE_ORDER[o][k]];
          v3 p = m4_mulv3(xf, (v3){q[0], q[1], z}, 1.0f);
          vx_evtx(e, p, q[2], q[3], layer, VX_FACE_PZ, o ? v3_scale(n, -1) : n, sky, blk, 1 | 8 | flags, tint);
        }
      }
    }
    return;
  }
  u8 id = (u8)item;
  b32 sprite = vx_shape[id] == VX_SHAPE_CROSS || vx_shape[id] == VX_SHAPE_TORCH;
  if (sprite) {
    u32 layer = vx_tex_layer[vx_face_tex[id][0]];
    v3 n = v3_normalize(m4_mulv3(xf, (v3){0, 0, 1}, 0.0f));
    for (u32 o = 0; o < VX_SPRITE_SIDES; o++) {
      for (u32 k = 0; k < VX_ECORNERS; k++) {
        const u8 *q = VX_SPRITE_QUAD[VX_SPRITE_ORDER[o][k]];
        v3 p = m4_mulv3(xf, (v3){q[0], q[1], 0}, 1.0f);
        vx_evtx(e, p, q[2], q[3], layer, VX_FACE_PZ, o ? v3_scale(n, -1) : n, sky, blk, 1 | flags, tint);
      }
    }
    return;
  }
  if (vx_shape[id] == VX_SHAPE_MODEL) {
    vx_emit_model_item(e, xf, id, sky, blk, flags);
    return;
  }
  u32 cut = vx_layer[id] == VX_LAYER_CUTOUT ? 1 : 0;
  b32 liquid = vx_shape[id] == VX_SHAPE_LIQUID;
  for (u32 f = 0; f < VX_EFACES; f++) {
    u32 layer = vx_tex_layer[vx_face_tex[id][f]];
    v3 n = v3_normalize(m4_mulv3(xf, VX_FACE_N[f], 0.0f));
    for (u32 k = 0; k < VX_ECORNERS; k++) {
      const u8 *c = VX_ENT_CORNERS[f][k];
      v3 p = m4_mulv3(xf, (v3){c[0], (liquid && c[1]) ? 0.875f : c[1], c[2]}, 1.0f);
      vx_evtx(e, p, VX_ENT_UV[k][0], VX_ENT_UV[k][1], layer, f, n, sky, blk, cut | flags, tint);
    }
  }
}

// dropped items bob and turn; blocks are small cubes, items their sprite standing up
hz_internal void vx_drops_build(VxEntities *e, const VxWorld *w, VxV3d cam, f32 time) {
  for (u32 i = 0; i < e->drop_count; i++) {
    const VxDrop *d = &e->drops[i];
    f32 sky, blk;
    vx_light_at(w, d->p, &sky, &blk);
    f32 bob = m_sinf(time * 2.4f + (f32)i * 1.7f) * 0.06f;
    m4 m = m4_translated(m4_identity(), (v3){(f32)(d->p.x - cam.x), (f32)(d->p.y - cam.y) + bob + 0.05f,
                                            (f32)(d->p.z - cam.z)});
    m = m4_rotate_y(m, d->spin);
    b32 cube = vx_item_is_block(d->item) && vx_shape[d->item] != VX_SHAPE_CROSS && vx_shape[d->item] != VX_SHAPE_TORCH;
    f32 s = cube ? 0.34f : 0.62f;
    m = m4_scaled(m, (v3){s, s, s});
    m = m4_translated(m, cube ? (v3){-0.5f, 0, -0.5f} : (v3){-0.5f, 0, 0});
    vx_emit_item(e, m, d->item, sky, blk, 0);
  }
}

// cracks over blocks being mined: the crack texture's three stages as a cutout shell just outside the block
hz_internal void vx_cracks_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxTileHit *hits, u32 n) {
  for (u32 i = 0; i < n; i++) {
    const VxTileHit *h = &hits[i];
    u32 stage = (u32)m_clampf(h->damage / 34.0f, 0, 2);
    u32 layer = vx_tex_layer_of(T_CRACK, stage);
    f32 sky, blk;
    vx_light_at(w, (VxV3d){h->x + 0.5, h->y + 1.5, h->z + 0.5}, &sky, &blk);
    v3 b = {(f32)(h->x - cam.x) - 0.004f, (f32)(h->y - cam.y) - 0.004f, (f32)(h->z - cam.z) - 0.004f};
    f32 s = 1.008f;
    for (u32 f = 0; f < VX_EFACES; f++) {
      for (u32 k = 0; k < VX_ECORNERS; k++) {
        const u8 *c = VX_ENT_CORNERS[f][k];
        vx_evtx(e, (v3){b.x + c[0] * s, b.y + c[1] * s, b.z + c[2] * s}, VX_ENT_UV[k][0], VX_ENT_UV[k][1], layer, f,
                VX_FACE_N[f], sky, blk, 1, (v3){1, 1, 1});
      }
    }
  }
}

VxEntityGeometry vx_entities_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxEntityView *view) {
  e->vert_count = 0;
  e->box_count = 0;
  e->jelly_count = 0;
  // particles: small axis-aligned cubes
  for (u32 i = 0; i < e->particle_count; i++) {
    const VxParticle *q = &e->particles[i];
    f32 sky, blk;
    vx_light_at(w, q->p, &sky, &blk);
    f32 s = q->size * m_minf(1, q->life / (q->max_life * 0.3f));
    v3 b = {(f32)(q->p.x - cam.x) - s / 2, (f32)(q->p.y - cam.y) - s / 2, (f32)(q->p.z - cam.z) - s / 2};
    for (u32 f = 0; f < VX_EFACES; f++) {
      for (u32 k = 0; k < VX_ECORNERS; k++) {
        const u8 *c = VX_ENT_CORNERS[f][k];
        vx_evtx(e, (v3){b.x + c[0] * s, b.y + c[1] * s, b.z + c[2] * s}, (q->u + VX_ENT_UV[k][0] * 3) / 16.0f,
                (q->vv + VX_ENT_UV[k][1] * 3) / 16.0f, q->layer, f, VX_FACE_N[f], sky, blk, q->cut, q->tint);
      }
    }
  }
  vx_entities_build_particles(e, cam);
  vx_drops_build(e, w, cam, e->time);
  if (view->cracks) vx_cracks_build(e, w, cam, view->cracks, view->crack_count);
  if (view->player) vx_hook_build(e, w, cam, view->player);
  if (view->creatures) vx_creatures_build(e, w, view->creatures, cam, e->time);
  vx_armoury_build(e, w, cam);
  vx_pylons_build(e, w, cam);
  if (view->combat) vx_combat_build(e, view->combat, cam);
  vx_debris_build(e, cam);
  VxEntityGeometry out = {.puppet_start = e->vert_count, .parts = e->parts, .part_count = e->part_count,
                          .box_puppet_start = e->box_count};
  // the puppet is built in first person too: it is not drawn there, but it still casts its shadow
  if (view->player) {
    vx_puppet_build(e, w, cam, view->player, &e->puppet, view->colors, view->first_person ? 0 : view->item);
    out.puppet_drawn = !view->first_person;
  }
  out.hand_start = e->vert_count;
  VxItemId item = view->item;
  if (view->show_hand && item > 0) {
    if ((i32)item != e->last_item) {
      e->equip = 0;
      e->last_item = item;
    }
    f32 sky, blk;
    vx_light_at(w, cam, &sky, &blk);
    m4 rot = m4_identity();
    rot = m4_rotate_y(rot, view->yaw);
    rot = m4_rotate_x(rot, view->pitch);
    f32 sw = e->swing, eq = 1 - e->equip, bob = view->bob, bob_amount = view->bob_amount;
    f32 bx = m_cosf(bob) * 0.018f * bob_amount;
    f32 by = -m_absf(m_sinf(bob)) * 0.022f * bob_amount;
    rot = m4_translated(rot, (v3){0.5f + bx - sw * 0.12f, -0.48f + by - eq * 0.4f + sw * 0.08f - sw * sw * 0.12f,
                                  -0.82f - sw * 0.18f});
    rot = m4_rotate_x(rot, -sw * 0.9f);
    rot = m4_rotate_y(rot, sw * 0.35f);
    if (!vx_item_is_block(item)) {
      // a tool or weapon held out ahead, blade or head up and away
      rot = m4_rotate_y(rot, -0.5f);
      rot = m4_rotate_z(rot, 0.1f);
      rot = m4_scaled(rot, (v3){0.4f, 0.4f, 0.4f});
      rot = m4_translated(rot, (v3){-0.35f, -0.1f, 0});
    } else if (vx_shape[item] == VX_SHAPE_CROSS || vx_shape[item] == VX_SHAPE_TORCH) {
      rot = m4_rotate_y(rot, -0.35f);
      rot = m4_rotate_z(rot, 0.25f);
      rot = m4_scaled(rot, (v3){0.5f, 0.5f, 0.5f});
      rot = m4_translated(rot, (v3){-0.5f, -0.3f, 0});
    } else {
      rot = m4_rotate_y(rot, 0.78f);
      rot = m4_rotate_x(rot, 0.08f);
      rot = m4_scaled(rot, (v3){0.36f, 0.36f, 0.36f});
      rot = m4_translated(rot, (v3){-0.5f, -0.5f, -0.5f});
    }
    vx_emit_item(e, rot, item, sky, blk, 128);
  }
  out.verts = e->verts;
  out.vert_count = e->vert_count;
  out.boxes = e->boxes;
  out.box_count = e->box_count;
  out.jelly = e->jelly;
  out.jelly_count = e->jelly_count;
  out.vfx = e->vfx;
  out.vfx_count = e->vfx_count;
  e->hand_start = out.hand_start;
  return out;
}

#undef VX_XYUV
#undef VX_SPRITE_SIDES

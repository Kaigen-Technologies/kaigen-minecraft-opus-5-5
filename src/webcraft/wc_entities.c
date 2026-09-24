#include "webcraft/wc_game.h"

// dynamic geometry drawn into the g-buffer: particles, fireflies, the held block

#define WC_EFACES 6
#define WC_ECORNERS 4
#define WC_XYZ 3
#define WC_UV 2
#define WC_XYUV 4
#define WC_SPRITE_SIDES 2

hz_internal const u8 WC_ENT_CORNERS[WC_EFACES][WC_ECORNERS][WC_XYZ] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
};
hz_internal const u8 WC_ENT_UV[WC_ECORNERS][WC_UV] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
// sprite quad corners (x, y, u, v), drawn front and back
hz_internal const u8 WC_SPRITE_QUAD[WC_ECORNERS][WC_XYUV] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {1, 1, 1, 0}, {0, 1, 0, 0}};
hz_internal const u8 WC_SPRITE_ORDER[WC_SPRITE_SIDES][WC_ECORNERS] = {{0, 1, 2, 3}, {1, 0, 3, 2}};

hz_internal v3 wc_entity_tint(u8 id) {
  switch (wc_tint[id]) {
  case WC_TINT_GRASS: return (v3){0.52f, 0.74f, 0.36f};
  case WC_TINT_FOLIAGE: return (v3){0.4f, 0.64f, 0.24f};
  case WC_TINT_WATER: return (v3){0.25f, 0.45f, 0.9f};
  case WC_TINT_BIRCH: return (v3){0.5f, 0.66f, 0.34f};
  case WC_TINT_SPRUCE: return (v3){0.36f, 0.56f, 0.38f};
  default: return (v3){1, 1, 1};
  }
}

void wc_entities_init(WcEntities *e, Allocator *alloc, u64 seed) {
  memzero_struct(e);
  e->particles = ALLOC_ARRAY(alloc, WcParticle, WC_MAX_PARTICLES);
  e->fireflies = ALLOC_ARRAY(alloc, WcFirefly, WC_MAX_FIREFLIES);
  e->verts = ALLOC_ARRAY(alloc, f32, WC_ENTITY_MAX_VERTS * WC_ENTITY_STRIDE);
  e->swing_t = 1;
  e->equip = 1;
  e->last_item = -1;
  e->rng = random_create(seed);
}

void wc_entities_trigger_swing(WcEntities *e) { e->swing_t = 0; }

hz_internal f32 wc_rand(WcEntities *e) { return random_f32(&e->rng); }

void wc_entities_spawn_break(WcEntities *e, u8 id, i32 x, i32 y, i32 z) {
  if (id == B_AIR || wc_shape[id] == WC_SHAPE_AIR) return;
  u32 layer = wc_face_tex[id][4];
  v3 tint = wc_entity_tint(id);
  u32 cut = wc_layer[id] == WC_LAYER_CUTOUT ? 1 : 0;
  u32 n = (wc_shape[id] == WC_SHAPE_CROSS || wc_shape[id] == WC_SHAPE_TORCH) ? 10 : 26;
  for (u32 i = 0; i < n; i++) {
    // keep the newest particles when the pool is full
    if (e->particle_count == WC_MAX_PARTICLES) {
      mem_move(e->particles, e->particles + 1, sizeof(WcParticle) * (WC_MAX_PARTICLES - 1));
      e->particle_count--;
    }
    f64 px = x + 0.15 + wc_rand(e) * 0.7, py = y + 0.15 + wc_rand(e) * 0.7, pz = z + 0.15 + wc_rand(e) * 0.7;
    f32 life = 0.6f + wc_rand(e) * 0.7f;
    WcParticle *q = &e->particles[e->particle_count++];
    q->p = (WcV3d){px, py, pz};
    q->v = (v3){(f32)(px - x - 0.5) * 4 + (wc_rand(e) - 0.5f), 1.5f + wc_rand(e) * 2.5f,
                (f32)(pz - z - 0.5) * 4 + (wc_rand(e) - 0.5f)};
    q->life = life;
    q->max_life = life;
    q->layer = layer;
    q->u = m_floorf(wc_rand(e) * 12);
    q->vv = m_floorf(wc_rand(e) * 12);
    q->size = 0.07f + wc_rand(e) * 0.06f;
    q->tint = tint;
    q->cut = cut;
  }
}

force_inline b32 wc_solid_block(const WcWorld *w, f64 x, f64 y, f64 z) {
  return wc_solid[wc_world_block(w, wc_floor_i(x), wc_floor_i(y), wc_floor_i(z))] != 0;
}

void wc_entities_update_fireflies(WcEntities *e, f32 dt, const WcWorld *w, WcV3d cam, b32 active) {
  e->time += dt;
  u32 want = active ? WC_MAX_FIREFLIES : 0;
  // spawn near the ground around the camera
  for (u32 tries = 0; e->firefly_count < want && tries < 4; tries++) {
    f32 a = wc_rand(e) * PI * 2;
    f32 r = 3 + wc_rand(e) * 13;
    f64 x = cam.x + m_cosf(a) * r, z = cam.z + m_sinf(a) * r;
    i32 bx = wc_floor_i(x), bz = wc_floor_i(z);
    i32 y = wc_floor_i(cam.y) + 6;
    while (y > cam.y - 12 && !wc_solid[wc_world_block(w, bx, y, bz)]) y--;
    if (!wc_solid[wc_world_block(w, bx, y, bz)]) continue;
    if (wc_world_block(w, bx, y + 1, bz) == B_WATER) continue;
    WcFirefly *f = &e->fireflies[e->firefly_count++];
    *f = (WcFirefly){.p = {x, y + 1.2 + wc_rand(e) * 2, z}, .phase = wc_rand(e) * 10};
  }
  for (i32 i = (i32)e->firefly_count - 1; i >= 0; i--) {
    WcFirefly *f = &e->fireflies[i];
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
    if (wc_solid_block(w, f->p.x, f->p.y, f->p.z)) f->p.y += 1;
  }
}

void wc_entities_update(WcEntities *e, f32 dt, const WcWorld *w) {
  if (e->swing_t < 1) e->swing_t = m_minf(1, e->swing_t + dt / 0.28f);
  e->swing = e->swing_t < 1 ? m_sinf(e->swing_t * PI) : 0;
  e->equip = m_minf(1, e->equip + dt / 0.18f);
  u32 kept = 0;
  for (u32 i = 0; i < e->particle_count; i++) {
    WcParticle q = e->particles[i];
    q.life -= dt;
    if (q.life <= 0) continue;
    q.v.y -= 16 * dt;
    f32 drag = m_expf(-1.5f * dt);
    q.v.x *= drag;
    q.v.z *= drag;
    f64 nx = q.p.x + q.v.x * dt, ny = q.p.y + q.v.y * dt, nz = q.p.z + q.v.z * dt;
    if (wc_solid_block(w, nx, ny - q.size, nz)) {
      q.v.y = 0;
      q.v.x *= 0.6f;
      q.v.z *= 0.6f;
      q.p.y = m_floor(ny - q.size) + 1 + q.size;
    } else {
      q.p.y = ny;
    }
    if (!wc_solid_block(w, nx, q.p.y, q.p.z)) q.p.x = nx;
    if (!wc_solid_block(w, q.p.x, q.p.y, nz)) q.p.z = nz;
    e->particles[kept++] = q;
  }
  e->particle_count = kept;
}

force_inline void wc_evtx(WcEntities *e, v3 p, f32 u, f32 v, u32 layer, u32 face, f32 sky, f32 blk, u32 flags, v3 t) {
  if (e->vert_count >= WC_ENTITY_MAX_VERTS) return;
  f32 *d = e->verts + e->vert_count * WC_ENTITY_STRIDE;
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
  e->vert_count++;
}

hz_internal void wc_light_at(const WcWorld *w, WcV3d p, f32 *sky, f32 *blk) {
  u8 l = wc_world_light(w, wc_floor_i(p.x), wc_floor_i(p.y), wc_floor_i(p.z));
  *sky = (f32)(l >> 4) / 15.0f;
  *blk = (f32)(l & 15) / 15.0f;
}

WcEntityGeometry wc_entities_build(WcEntities *e, const WcWorld *w, WcV3d cam, f32 yaw, f32 pitch, u8 item, f32 bob,
                                   f32 bob_amount, b32 show_hand) {
  e->vert_count = 0;
  // particles: small axis-aligned cubes
  for (u32 i = 0; i < e->particle_count; i++) {
    const WcParticle *q = &e->particles[i];
    f32 sky, blk;
    wc_light_at(w, q->p, &sky, &blk);
    f32 s = q->size * m_minf(1, q->life / (q->max_life * 0.3f));
    v3 b = {(f32)(q->p.x - cam.x) - s / 2, (f32)(q->p.y - cam.y) - s / 2, (f32)(q->p.z - cam.z) - s / 2};
    for (u32 f = 0; f < WC_EFACES; f++) {
      for (u32 k = 0; k < WC_ECORNERS; k++) {
        const u8 *c = WC_ENT_CORNERS[f][k];
        wc_evtx(e, (v3){b.x + c[0] * s, b.y + c[1] * s, b.z + c[2] * s}, (q->u + WC_ENT_UV[k][0] * 3) / 16.0f,
                (q->vv + WC_ENT_UV[k][1] * 3) / 16.0f, q->layer, f, sky, blk, q->cut, q->tint);
      }
    }
  }
  // fireflies: tiny blinking emissive cubes
  u32 firefly_layer = wc_face_tex[B_GLOWSTONE][0];
  for (u32 i = 0; i < e->firefly_count; i++) {
    const WcFirefly *f = &e->fireflies[i];
    f32 blink = m_maxf(0, m_sinf(e->time * 1.7f + f->phase * 3.1f));
    f32 s = 0.07f * m_powf(blink, 0.6f) * f->life;
    if (s < 0.01f) continue;
    v3 b = {(f32)(f->p.x - cam.x) - s / 2, (f32)(f->p.y - cam.y) - s / 2, (f32)(f->p.z - cam.z) - s / 2};
    for (u32 fc = 0; fc < WC_EFACES; fc++) {
      for (u32 k = 0; k < WC_ECORNERS; k++) {
        const u8 *c = WC_ENT_CORNERS[fc][k];
        wc_evtx(e, (v3){b.x + c[0] * s, b.y + c[1] * s, b.z + c[2] * s}, 0.4f + WC_ENT_UV[k][0] * 0.1f,
                0.4f + WC_ENT_UV[k][1] * 0.1f, firefly_layer, fc, 0, 0, 0, (v3){0.75f, 1.0f, 0.35f});
      }
    }
  }
  // held item, in camera orientation
  WcEntityGeometry out = {.hand_start = e->vert_count, .hand_rot = m4_identity()};
  m4 rot = m4_identity();
  if (show_hand && item > 0) {
    if ((i32)item != e->last_item) {
      e->equip = 0;
      e->last_item = item;
    }
    f32 sky, blk;
    wc_light_at(w, cam, &sky, &blk);
    v3 tint = wc_entity_tint(item);
    rot = m4_rotate_y(rot, yaw);
    rot = m4_rotate_x(rot, pitch);
    f32 sw = e->swing, eq = 1 - e->equip;
    f32 bx = m_cosf(bob) * 0.018f * bob_amount;
    f32 by = -m_absf(m_sinf(bob)) * 0.022f * bob_amount;
    rot = m4_translated(rot, (v3){0.5f + bx - sw * 0.12f, -0.48f + by - eq * 0.4f + sw * 0.08f - sw * sw * 0.12f,
                                  -0.82f - sw * 0.18f});
    rot = m4_rotate_x(rot, -sw * 0.9f);
    rot = m4_rotate_y(rot, sw * 0.35f);
    b32 sprite = wc_shape[item] == WC_SHAPE_CROSS || wc_shape[item] == WC_SHAPE_TORCH;
    if (sprite) {
      rot = m4_rotate_y(rot, -0.35f);
      rot = m4_rotate_z(rot, 0.25f);
      rot = m4_scaled(rot, (v3){0.5f, 0.5f, 0.5f});
      rot = m4_translated(rot, (v3){-0.5f, -0.3f, 0});
      u32 layer = wc_face_tex[item][0];
      for (u32 o = 0; o < WC_SPRITE_SIDES; o++) {
        for (u32 k = 0; k < WC_ECORNERS; k++) {
          const u8 *q = WC_SPRITE_QUAD[WC_SPRITE_ORDER[o][k]];
          v3 p = m4_mulv3(rot, (v3){q[0], q[1], 0}, 1.0f);
          wc_evtx(e, p, q[2], q[3], layer, WC_FACE_PZ, sky, blk, 1 | 128, tint);
        }
      }
    } else {
      rot = m4_rotate_y(rot, 0.78f);
      rot = m4_rotate_x(rot, 0.08f);
      rot = m4_scaled(rot, (v3){0.36f, 0.36f, 0.36f});
      rot = m4_translated(rot, (v3){-0.5f, -0.5f, -0.5f});
      u32 cut = wc_layer[item] == WC_LAYER_CUTOUT ? 1 : 0;
      b32 liquid = wc_shape[item] == WC_SHAPE_LIQUID;
      for (u32 f = 0; f < WC_EFACES; f++) {
        u32 layer = wc_face_tex[item][f];
        for (u32 k = 0; k < WC_ECORNERS; k++) {
          const u8 *c = WC_ENT_CORNERS[f][k];
          v3 p = m4_mulv3(rot, (v3){c[0], (liquid && c[1]) ? 0.875f : c[1], c[2]}, 1.0f);
          wc_evtx(e, p, WC_ENT_UV[k][0], WC_ENT_UV[k][1], layer, f, sky, blk, cut | 128, tint);
        }
      }
    }
  }
  // normal rotation: the item transform's upper 3x3, orthonormalised
  for (u32 c = 0; c < 3; c++) {
    v3 col = {rot.m[c][0], rot.m[c][1], rot.m[c][2]};
    f32 l = v3_length(col);
    if (l == 0) l = 1;
    out.hand_rot.m[c][0] = col.x / l;
    out.hand_rot.m[c][1] = col.y / l;
    out.hand_rot.m[c][2] = col.z / l;
  }
  out.verts = e->verts;
  out.vert_count = e->vert_count;
  e->hand_start = out.hand_start;
  return out;
}

#undef WC_EFACES
#undef WC_ECORNERS
#undef WC_XYZ
#undef WC_UV
#undef WC_XYUV
#undef WC_SPRITE_SIDES

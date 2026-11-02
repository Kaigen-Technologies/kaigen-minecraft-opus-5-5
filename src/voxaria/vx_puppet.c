#include "voxaria/vx_entity_geo.h"

// the player's voxel puppet: boxes on a few pivots, posed from the player's state, tinted from the chosen colours

hz_internal const u32 VX_SKIN_TONES[] = {0xF3D2B8, 0xE8B796, 0xC98F62, 0xA8784C, 0x7C4E30, 0x553321};
hz_internal const char *const VX_SKIN_NAMES[] = {"Pale", "Fair", "Tan", "Olive", "Brown", "Deep"};
hz_internal const u32 VX_DYES[] = {0x2A2522, 0x6B4126, 0x8E4E25, 0xE6C46E, 0xC0602A, 0x9A9A9E, 0xF2F0EA, 0xC23A32,
                                   0xE07F2C, 0xE8C840, 0x4E9A3A, 0x2E9C94, 0x3866C8, 0x263E78, 0x7A44B0, 0xE07AA8};
hz_internal const char *const VX_DYE_NAMES[] = {"Black", "Brown",  "Chestnut", "Blonde", "Ginger", "Grey",
                                                "White", "Red",    "Orange",   "Yellow", "Green",  "Teal",
                                                "Blue",  "Navy",   "Purple",   "Pink"};
hz_internal const char *const VX_HAIR_STYLE_NAMES[] = {"Short", "Long", "Spiky", "Ponytail"};

static_assert(ARRAY_SIZE(VX_SKIN_TONES) == ARRAY_SIZE(VX_SKIN_NAMES), "skin tables");
static_assert(ARRAY_SIZE(VX_DYES) == ARRAY_SIZE(VX_DYE_NAMES), "dye tables");
static_assert(ARRAY_SIZE(VX_HAIR_STYLE_NAMES) == VX_HAIR_STYLE_COUNT, "hair styles");

u32 vx_puppet_option_count(VxPuppetPart part) {
  return part == VX_PP_SKIN ? ARRAY_SIZE(VX_SKIN_NAMES)
         : part == VX_PP_HAIR_STYLE ? VX_HAIR_STYLE_COUNT
                                    : ARRAY_SIZE(VX_DYE_NAMES);
}

const char *const *vx_puppet_option_names(VxPuppetPart part) {
  return part == VX_PP_SKIN ? VX_SKIN_NAMES : part == VX_PP_HAIR_STYLE ? VX_HAIR_STYLE_NAMES : VX_DYE_NAMES;
}

hz_internal f32 vx_pp_lin(u32 c8) {
  f32 c = (f32)c8 / 255.0f;
  return c <= 0.04045f ? c / 12.92f : m_powf((c + 0.055f) / 1.055f, 2.4f);
}

// tints multiply the albedo after its srgb decode, so they are linear too
hz_internal v3 vx_pp_rgb(u32 c) { return (v3){vx_pp_lin((c >> 16) & 255), vx_pp_lin((c >> 8) & 255), vx_pp_lin(c & 255)}; }

VxPuppetColors vx_puppet_colors(const VxSettings *s) {
  u32 nd = ARRAY_SIZE(VX_DYES), ns = ARRAY_SIZE(VX_SKIN_TONES);
  return (VxPuppetColors){
      .skin = vx_pp_rgb(VX_SKIN_TONES[s->skin_tone % ns]),
      .hair = vx_pp_rgb(VX_DYES[s->hair_color % nd]),
      .eyes = vx_pp_rgb(VX_DYES[s->eye_color % nd]),
      .shirt = vx_pp_rgb(VX_DYES[s->shirt_color % nd]),
      .undershirt = vx_pp_rgb(VX_DYES[s->undershirt_color % nd]),
      .pants = vx_pp_rgb(VX_DYES[s->pants_color % nd]),
      .shoes = vx_pp_rgb(VX_DYES[s->shoe_color % nd]),
      .hair_style = s->hair_style % VX_HAIR_STYLE_COUNT,
  };
}

// a hit: every part pulled toward red by k
void vx_puppet_hurt_tint(VxPuppetColors *pc, f32 k) {
  v3 m = {1, 1 - k, 1 - k};
  pc->skin = v3_mul(pc->skin, m);
  pc->hair = v3_mul(pc->hair, m);
  pc->eyes = v3_mul(pc->eyes, m);
  pc->shirt = v3_mul(pc->shirt, m);
  pc->undershirt = v3_mul(pc->undershirt, m);
  pc->pants = v3_mul(pc->pants, m);
  pc->shoes = v3_mul(pc->shoes, m);
  pc->head_c = v3_mul(pc->head_c, m);
  pc->body_c = v3_mul(pc->body_c, m);
  pc->legs_c = v3_mul(pc->legs_c, m);
}

// an armour piece's plate colour: gold reads orange in the reference's warm light
hz_internal v3 vx_pp_metal(VxItemId item) {
  switch (vx_item(item)->mat) {
  case VX_IM_COPPER: return vx_pp_rgb(0xD07840);
  case VX_IM_TIN: return vx_pp_rgb(0xB8B090);
  case VX_IM_SILVER: return vx_pp_rgb(0xD8E0EA);
  case VX_IM_GOLD: return vx_pp_rgb(0xF4A43A);
  default: return vx_pp_rgb(0x9CA6B2);
  }
}

void vx_puppet_armor(VxPuppetColors *pc, const VxStack *armor) {
  VxItemId head = armor[0].item, body = armor[1].item, legs = armor[2].item;
  if (head) {
    pc->head_c = vx_pp_metal(head);
    pc->head_gear = head == I_MINING_HELMET ? VX_HEAD_MINER : vx_item(head)->mat == VX_IM_GOLD ? VX_HEAD_CROWN : VX_HEAD_HELMET;
    if (head == I_MINING_HELMET) pc->head_c = vx_pp_rgb(0xE8C040);
  }
  if (body) pc->body_c = vx_pp_metal(body), pc->body_gear = VX_BODY_ARMOR;
  if (legs) pc->legs_c = vx_pp_metal(legs), pc->leg_armor = true;
}

// while the hook is out the body turns to it and the right arm points along the chain
hz_internal f32 vx_hook_facing(const VxPlayer *p) {
  return m_atan2f(-(f32)(p->hook_pos.x - p->pos.x), -(f32)(p->hook_pos.z - p->pos.z));
}

hz_internal VxV3d vx_hook_shoulder(const VxPlayer *p) {
  f32 f = vx_hook_facing(p);
  return (VxV3d){p->pos.x + m_cosf(f) * 0.58, p->pos.y + p->step_smooth + 1.84, p->pos.z - m_sinf(f) * 0.58};
}

// the right hand of a puppet holding the hook's chain
VxV3d vx_puppet_hook_hand(const VxPlayer *p) {
  VxV3d s = vx_hook_shoulder(p);
  v3 d = {(f32)(p->hook_pos.x - s.x), (f32)(p->hook_pos.y - s.y), (f32)(p->hook_pos.z - s.z)};
  f32 l = v3_length(d);
  if (l < 1e-3f) return s;
  return (VxV3d){s.x + d.x / l * 0.86f, s.y + d.y / l * 0.86f, s.z + d.z / l * 0.86f};
}

hz_internal f32 vx_ease_to(f32 cur, f32 target, f32 k) { return cur + (target - cur) * k; }

void vx_puppet_animate(VxPuppet *pp, const VxPlayer *p, VxItemId held, f32 swing_t, f32 dt) {
  pp->time += dt;
  f32 t = pp->time;
  f32 hs = m_sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
  VxPose g = {0};
  b32 fly = p->flying || p->flapping || p->gliding;
  if (p->in_water && !p->on_ground) {
    // swimming: stretched out, arms sweeping and legs kicking
    g.tilt = 0.5f + 0.7f * m_minf(1, hs / 4.0f);
    g.arm_lx = 2.0f + m_sinf(t * 5.0f) * 0.9f;
    g.arm_rx = 2.0f + m_sinf(t * 5.0f + PI) * 0.9f;
    g.arm_lz = 0.35f, g.arm_rz = -0.35f;
    g.leg_lx = m_sinf(t * 8.0f) * 0.45f;
    g.leg_rx = -g.leg_lx;
  } else if (fly) {
    // flight: leaning into the travel, legs trailing, arms swept back
    g.tilt = 0.15f + 0.55f * m_minf(1, hs / 14.0f);
    g.leg_lx = -0.3f + m_sinf(t * 3.0f) * 0.05f;
    g.leg_rx = -0.15f + m_sinf(t * 3.0f + 1.3f) * 0.05f;
    g.leg_lz = 0.06f, g.leg_rz = -0.06f;
    g.arm_lx = -0.45f, g.arm_rx = -0.45f;
    g.arm_lz = 0.3f, g.arm_rz = -0.3f;
  } else if (!p->on_ground && p->hook_state != VX_HOOK_ATTACHED) {
    if (p->vel.y > 0.5f) {
      // rising: one knee up, an arm thrown overhead
      g.leg_lx = 0.85f, g.leg_rx = -0.3f;
      g.arm_rx = 2.7f, g.arm_lx = 0.5f;
      g.arm_rz = -0.15f, g.arm_lz = 0.25f;
      g.lean = 0.05f;
    } else {
      // falling: arms up and flailing, legs loose
      f32 w = m_sinf(t * 13.0f) * 0.18f;
      g.arm_lx = 2.4f + w, g.arm_rx = 2.4f - w;
      g.arm_lz = 0.45f, g.arm_rz = -0.45f;
      g.leg_lx = 0.25f, g.leg_rx = -0.2f;
      g.leg_lz = 0.12f, g.leg_rz = -0.12f;
    }
  } else if (p->hook_state == VX_HOOK_ATTACHED) {
    // hanging from the hook: legs loose, the free arm out for balance
    g.arm_lx = 0.3f, g.arm_lz = 0.2f;
    g.leg_lx = 0.2f, g.leg_rx = -0.1f;
  } else {
    // ground: a stride from the distance walked, a breath when standing
    f32 walk = m_minf(1, hs / 8.5f);
    f32 ph = p->stride * 1.3f;
    f32 breathe = m_sinf(t * 2.2f);
    g.leg_lx = m_sinf(ph) * 0.8f * walk;
    g.leg_rx = -g.leg_lx;
    g.arm_lx = -g.leg_lx * 0.85f;
    g.arm_rx = g.leg_lx * 0.85f;
    g.arm_lz = 0.06f + breathe * 0.02f * (1 - walk);
    g.arm_rz = -g.arm_lz;
    g.lean = 0.1f * walk;
    g.bounce = (m_absf(m_cosf(ph)) - 0.5f) * 0.09f * walk + breathe * 0.012f * (1 - walk);
  }
  // tools, weapons and torches: upper arm 30 degrees forward, forearm bent about 50 more, the fist at the chest
  b32 holding = held > 0 && (!vx_item_is_block(held) || vx_shape[held] == VX_SHAPE_TORCH) && !(p->in_water && !p->on_ground);
  if (holding) {
    g.arm_rx = 0.52f + g.arm_rx * 0.12f;
    g.arm_rz = 0.12f; // positive turns the right arm out
    g.elbow_r = 0.85f;
  }
  f32 k = 1 - m_expf(-16.0f * dt);
  VxPose *c = &pp->pose;
  c->tilt = vx_ease_to(c->tilt, g.tilt, 1 - m_expf(-6.0f * dt));
  c->lean = vx_ease_to(c->lean, g.lean, k);
  c->bounce = vx_ease_to(c->bounce, g.bounce, k);
  c->arm_lx = vx_ease_to(c->arm_lx, g.arm_lx, k);
  c->arm_rx = vx_ease_to(c->arm_rx, g.arm_rx, k);
  c->arm_lz = vx_ease_to(c->arm_lz, g.arm_lz, k);
  c->arm_rz = vx_ease_to(c->arm_rz, g.arm_rz, k);
  c->elbow_r = vx_ease_to(c->elbow_r, g.elbow_r, k);
  c->leg_lx = vx_ease_to(c->leg_lx, g.leg_lx, k);
  c->leg_rx = vx_ease_to(c->leg_rx, g.leg_rx, k);
  c->leg_lz = vx_ease_to(c->leg_lz, g.leg_lz, k);
  c->leg_rz = vx_ease_to(c->leg_rz, g.leg_rz, k);
  // wings: spread and beating while lifting, held open in a glide, folded along the back otherwise
  b32 open = !p->on_ground && (p->flapping || p->gliding || p->flying);
  c->wing_fold = vx_ease_to(c->wing_fold, open ? 0.0f : p->in_water ? 1.0f : 0.12f, 1 - m_expf(-10.0f * dt));
  f32 flap = p->flapping || p->flying ? m_sinf(t * 17.0f) * 0.6f : p->gliding ? 0.15f : open ? 0.0f : -0.78f + m_sinf(t * 1.6f) * 0.04f;
  c->wing_flap = vx_ease_to(c->wing_flap, flap, 1 - m_expf(-30.0f * dt));
  // a swing is driven directly: overhead and behind, down through the front
  pp->swinging = swing_t < 1;
  if (pp->swinging) {
    f32 s = swing_t * swing_t * (3 - 2 * swing_t);
    c->arm_rx = 3.0f + (-0.1f - 3.0f) * s;
    c->arm_rz = -0.12f;
    c->elbow_r = 0;
  }
  // head follows the view, within what a neck allows
  c->head_x = vx_ease_to(c->head_x, m_clampf(p->pitch, -0.6f, 0.7f), k);
  f32 face = pp->swinging ? p->yaw : p->body_yaw;
  f32 d = m_fmodf(p->yaw - face + PI * 3, PI * 2) - PI;
  c->head_y = vx_ease_to(c->head_y, m_clampf(d, -0.9f, 0.9f), k);
  if (pp->swinging) pp->facing = p->yaw;
  else pp->facing = p->body_yaw;
  // a shot holds the weapon out along the view, the off hand steadying it
  pp->aim_t = m_maxf(0, pp->aim_t - dt);
  if (pp->aim_t > 0) {
    c->arm_rx = PI * 0.5f + m_clampf(p->pitch, -1.2f, 1.2f);
    c->arm_rz = 0.05f;
    c->arm_lx = c->arm_rx;
    c->arm_lz = -0.45f;
    c->elbow_r = 0;
    pp->facing = p->yaw;
  }
  if (p->hook_state != VX_HOOK_NONE) {
    pp->facing = vx_hook_facing(p);
    VxV3d s = vx_hook_shoulder(p);
    f64 dx = p->hook_pos.x - s.x, dy = p->hook_pos.y - s.y, dz = p->hook_pos.z - s.z;
    c->arm_rx = m_atan2f((f32)m_sqrt(dx * dx + dz * dz), (f32)-dy);
    c->arm_rz = 0;
    c->elbow_r = 0;
  }
  c->hold = holding && !pp->swinging && pp->aim_t <= 0 && p->hook_state == VX_HOOK_NONE;
}

force_inline u32 vx_srgb8(f32 lin) {
  f32 c = m_saturatef(lin);
  return (u32)(m_powf(c, 1.0f / 2.2f) * 255.0f + 0.5f);
}

hz_internal void vx_box_record(VxBoxGpu *b, m4 xf, v3 lo, v3 hi, u32 layer, v3 tint, u32 flags, f32 flash) {
  v3 sz = v3_sub(hi, lo);
  v3 cx = v3_scale(m4_mulv3(xf, (v3){1, 0, 0}, 0.0f), sz.x);
  v3 cy = v3_scale(m4_mulv3(xf, (v3){0, 1, 0}, 0.0f), sz.y);
  v3 cz = v3_scale(m4_mulv3(xf, (v3){0, 0, 1}, 0.0f), sz.z);
  v3 o = m4_mulv3(xf, lo, 1.0f);
  b->r0 = (v4){cx.x, cy.x, cz.x, o.x};
  b->r1 = (v4){cx.y, cy.y, cz.y, o.y};
  b->r2 = (v4){cx.z, cy.z, cz.z, o.z};
  b->tint = vx_srgb8(tint.x) | (vx_srgb8(tint.y) << 8) | (vx_srgb8(tint.z) << 16) |
            ((u32)(m_saturatef(flash) * 255.0f + 0.5f) << 24);
  b->misc = (layer & 4095u) | ((flags & 255u) << 12);
  b->pad0 = b->pad1 = 0;
}

void vx_ebox_ex(VxEntities *e, m4 xf, v3 lo, v3 hi, u32 layer, v3 tint, u32 flags, f32 flash) {
  if (e->box_count >= VX_BOX_CAP) return;
  vx_box_record(&e->boxes[e->box_count++], xf, lo, hi, layer, tint, flags, flash);
}

void vx_ejelly(VxEntities *e, m4 xf, v3 lo, v3 hi, v3 tint, u32 flags, f32 flash) {
  if (e->jelly_count >= VX_JELLY_CAP) return;
  vx_box_record(&e->jelly[e->jelly_count++], xf, lo, hi, 0, tint, flags, flash);
}

void vx_ebox(VxEntities *e, m4 xf, v3 lo, v3 hi, u32 layer, v3 tint, f32 sky, f32 blk) {
  UNUSED(sky);
  UNUSED(blk);
  vx_ebox_ex(e, xf, lo, hi, layer, tint, 0, 0);
}

hz_internal void vx_puppet_hair(VxEntities *e, m4 head, const VxPuppetColors *pc, u32 layer, f32 sky, f32 blk) {
  v3 h = pc->hair, h2 = v3_scale(pc->hair, 0.82f);
  if (pc->beard) {
    vx_ebox(e, head, (v3){-0.42f, -0.14f, -0.47f}, (v3){0.42f, 0.16f, -0.36f}, layer, h2, sky, blk);
    vx_ebox(e, head, (v3){-0.28f, 0.14f, -0.48f}, (v3){0.28f, 0.24f, -0.4f}, layer, h, sky, blk);
  }
  if (pc->head_gear != VX_HEAD_HAIR && pc->head_gear != VX_HEAD_TOP_HAT) return;
  vx_ebox(e, head, (v3){-0.44f, 0.56f, -0.44f}, (v3){0.44f, 0.88f, 0.44f}, layer, h, sky, blk);
  vx_ebox(e, head, (v3){-0.44f, 0.3f, 0.26f}, (v3){0.44f, 0.6f, 0.46f}, layer, h2, sky, blk);
  vx_ebox(e, head, (v3){-0.46f, 0.36f, -0.12f}, (v3){-0.4f, 0.6f, 0.3f}, layer, h2, sky, blk);
  vx_ebox(e, head, (v3){0.4f, 0.36f, -0.12f}, (v3){0.46f, 0.6f, 0.3f}, layer, h2, sky, blk);
  vx_ebox(e, head, (v3){-0.44f, 0.6f, -0.47f}, (v3){0.1f, 0.78f, -0.42f}, layer, h, sky, blk);
  switch (pc->hair_style) {
  case VX_HAIR_LONG:
    vx_ebox(e, head, (v3){-0.44f, -0.42f, 0.24f}, (v3){0.44f, 0.32f, 0.48f}, layer, h2, sky, blk);
    vx_ebox(e, head, (v3){-0.47f, -0.1f, -0.06f}, (v3){-0.41f, 0.4f, 0.3f}, layer, h2, sky, blk);
    vx_ebox(e, head, (v3){0.41f, -0.1f, -0.06f}, (v3){0.47f, 0.4f, 0.3f}, layer, h2, sky, blk);
    break;
  case VX_HAIR_SPIKY:
    vx_ebox(e, head, (v3){-0.32f, 0.88f, -0.3f}, (v3){-0.08f, 1.04f, -0.06f}, layer, h, sky, blk);
    vx_ebox(e, head, (v3){0.04f, 0.88f, -0.2f}, (v3){0.28f, 1.1f, 0.04f}, layer, h, sky, blk);
    vx_ebox(e, head, (v3){-0.18f, 0.88f, 0.1f}, (v3){0.1f, 1.0f, 0.36f}, layer, h2, sky, blk);
    vx_ebox(e, head, (v3){-0.44f, 0.66f, -0.52f}, (v3){-0.2f, 0.84f, -0.42f}, layer, h, sky, blk);
    break;
  case VX_HAIR_PONYTAIL:
    vx_ebox(e, head, (v3){-0.12f, 0.5f, 0.46f}, (v3){0.12f, 0.72f, 0.58f}, layer, h2, sky, blk);
    vx_ebox(e, head, (v3){-0.1f, -0.12f, 0.5f}, (v3){0.1f, 0.54f, 0.66f}, layer, h, sky, blk);
    break;
  default: break;
  }
}

// a helmet, crown, miner's lamp, top hat or hood over the head
hz_internal void vx_puppet_headgear(VxEntities *e, m4 head, const VxPuppetColors *pc, u32 L, f32 sky, f32 blk) {
  v3 c = pc->head_c, dark = v3_scale(pc->head_c, 0.72f);
  switch (pc->head_gear) {
  case VX_HEAD_HELMET:
  case VX_HEAD_CROWN:
  case VX_HEAD_MINER: {
    // a cap down to the brow, a neck guard behind and cheek plates to the jaw, the face left open
    vx_ebox(e, head, (v3){-0.47f, 0.5f, -0.47f}, (v3){0.47f, 0.94f, 0.47f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.47f, -0.02f, 0.28f}, (v3){0.47f, 0.5f, 0.47f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.47f, 0.06f, -0.42f}, (v3){-0.4f, 0.5f, 0.28f}, L, dark, sky, blk);
    vx_ebox(e, head, (v3){0.4f, 0.06f, -0.42f}, (v3){0.47f, 0.5f, 0.28f}, L, dark, sky, blk);
    vx_ebox(e, head, (v3){-0.49f, 0.5f, -0.5f}, (v3){0.49f, 0.6f, -0.44f}, L, dark, sky, blk);
    if (pc->visor) {
      // the face plate and two glowing eye slits
      vx_ebox(e, head, (v3){-0.44f, 0.02f, -0.48f}, (v3){0.44f, 0.5f, -0.4f}, L, c, sky, blk);
      vx_ebox(e, head, (v3){-0.3f, 0.3f, -0.5f}, (v3){0.3f, 0.42f, -0.47f}, L, v3_scale(c, 0.3f), sky, blk);
      vx_ebox_ex(e, head, (v3){-0.26f, 0.32f, -0.51f}, (v3){-0.06f, 0.4f, -0.48f}, L, pc->eyes, VX_BOX_SELF_LIT, 0);
      vx_ebox_ex(e, head, (v3){0.06f, 0.32f, -0.51f}, (v3){0.26f, 0.4f, -0.48f}, L, pc->eyes, VX_BOX_SELF_LIT, 0);
    }
    if (pc->head_gear == VX_HEAD_MINER) {
      vx_ebox_ex(e, head, (v3){-0.11f, 0.62f, -0.56f}, (v3){0.11f, 0.84f, -0.46f}, L, (v3){1.0f, 0.95f, 0.7f}, VX_BOX_SELF_LIT, 0);
    } else if (pc->head_gear == VX_HEAD_CROWN) {
      // a ring of glowing rays leaning out from the crown, long and short in turn
      for (u32 k = 0; k < 14; k++) {
        f32 a = (f32)k * (2 * PI / 14.0f), len = k & 1 ? 0.3f : 0.48f;
        m4 r = m4_rotate_y(head, a);
        r = m4_translated(r, (v3){0, 0.9f, -0.36f});
        r = m4_rotate_x(r, -0.55f);
        vx_ebox_ex(e, r, (v3){-0.035f, 0, -0.035f}, (v3){0.035f, len, 0.035f}, L, (v3){1.0f, 0.74f, 0.1f}, VX_BOX_HOT_GLOW, 0);
      }
    }
  } break;
  case VX_HEAD_TOP_HAT:
    vx_ebox(e, head, (v3){-0.58f, 0.86f, -0.58f}, (v3){0.58f, 0.94f, 0.58f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.38f, 0.94f, -0.38f}, (v3){0.38f, 1.52f, 0.38f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.4f, 0.96f, -0.4f}, (v3){0.4f, 1.1f, 0.4f}, L, pc->body_c, sky, blk);
    break;
  case VX_HEAD_HOOD:
    // over the crown and down the back and sides, with a peak over the brow
    vx_ebox(e, head, (v3){-0.48f, 0.78f, -0.46f}, (v3){0.48f, 0.98f, 0.5f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.48f, -0.08f, 0.32f}, (v3){0.48f, 0.78f, 0.52f}, L, dark, sky, blk);
    vx_ebox(e, head, (v3){-0.5f, 0.0f, -0.42f}, (v3){-0.4f, 0.78f, 0.32f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){0.4f, 0.0f, -0.42f}, (v3){0.5f, 0.78f, 0.32f}, L, c, sky, blk);
    vx_ebox(e, head, (v3){-0.48f, 0.68f, -0.52f}, (v3){0.48f, 0.84f, -0.44f}, L, dark, sky, blk);
    break;
  default: break;
  }
}

// golden feathered wings from the shoulder blades, as S4: a raised leading edge, a row of coverts, and eleven feathers fanning from straight down at the body to out along the edge at the tip
hz_internal void vx_puppet_wings(VxEntities *e, m4 torso, const VxPose *q, const VxPuppetColors *pc, f32 sky, f32 blk) {
  u32 L = vx_tex_layer[T_PUPPET];
  // s_14/s_16: saturated orange roots, feather tips glowing yellow past the bloom threshold
  v3 edge_c = {0.5f, 0.14f, 0.01f}, covert = {0.82f, 0.24f, 0.0f}, root_c = {1.0f, 0.32f, 0.0f}, tip_c = {1.0f, 0.74f, 0.1f};
  u32 tip_flags = VX_BOX_HOT_GLOW;
  if (v3_length(pc->wing_tip) > 0) {
    root_c = pc->wing_root, tip_c = pc->wing_tip, tip_flags = 0;
    edge_c = v3_scale(root_c, 0.6f), covert = v3_scale(root_c, 0.85f);
  }
  f32 open = 1 - q->wing_fold, edge_len = 1.55f;
  for (u32 s = 0; s < 2; s++) {
    f32 o = s ? 1.0f : -1.0f;
    m4 root = m4_translated(torso, (v3){o * 0.16f, 0.82f, 0.26f});
    // folded, the wing turns back against the spine and its edge hangs down, so the feathers lie flat along the back
    root = m4_rotate_y(root, -o * q->wing_fold * 1.5f);
    m4 edge = m4_rotate_z(root, o * (open * (0.42f + q->wing_flap) - q->wing_fold * 1.25f));
    vx_ebox(e, edge, (v3){s ? 0.0f : -edge_len, -0.07f, -0.02f}, (v3){s ? edge_len : 0.0f, 0.09f, 0.1f}, L, edge_c, sky, blk);
    for (u32 k = 0; k < 3; k++) {
      f32 x0 = 0.05f + 0.3f * (f32)k, x1 = x0 + 0.36f;
      vx_ebox(e, edge, (v3){s ? x0 : -x1, -0.42f + 0.06f * (f32)k, 0.0f}, (v3){s ? x1 : -x0, -0.02f, 0.085f}, L, covert, sky, blk);
    }
    for (u32 k = 0; k < 11; k++) {
      f32 u = 0.1f + 0.9f * (f32)k / 10.0f;
      f32 len = 0.62f + 1.0f * m_powf(u, 0.8f), w = 0.1f + 0.05f * u;
      // the fan closes as the wing folds, so folded feathers hang straight down the back
      f32 fan = (0.12f + 1.2f * u * u) * (1 - 0.85f * q->wing_fold);
      m4 f = m4_translated(edge, (v3){o * u * edge_len, 0.0f, 0.03f + 0.014f * (f32)k});
      f = m4_rotate_z(f, o * fan);
      vx_ebox(e, f, (v3){-w, -len * 0.5f, 0.0f}, (v3){w, 0.02f, 0.05f}, L, root_c, sky, blk);
      vx_ebox_ex(e, f, (v3){-w * 0.85f, -len, 0.005f}, (v3){w * 0.85f, -len * 0.5f, 0.045f}, L, tip_c, tip_flags, 0);
    }
  }
}

// the player's puppet and the item in its hand, camera-relative
void vx_puppet_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxPlayer *p, const VxPuppet *pp,
                     const VxPuppetColors *pc, VxItemId item) {
  VxV3d feet = {p->pos.x, p->pos.y + p->step_smooth, p->pos.z};
  vx_puppet_body(e, w, cam, feet, pp->facing, &pp->pose, pc, item, p->wing_time_max > 0);
}

// a humanoid in the player's build at feet, posed by q; townsfolk share it
void vx_puppet_body(VxEntities *e, const VxWorld *w, VxV3d cam, VxV3d feet, f32 facing, const VxPose *q,
                    const VxPuppetColors *pc, VxItemId item, b32 wings) {
  VxV3d chest = {feet.x, feet.y + 1.5, feet.z};
  f32 sky, blk;
  vx_light_at(w, chest, &sky, &blk);
  u32 L = vx_tex_layer[T_PUPPET];
  m4 base = m4_translated(m4_identity(), (v3){(f32)(feet.x - cam.x), (f32)(feet.y - cam.y), (f32)(feet.z - cam.z)});
  base = m4_rotate_y(base, facing);
  // whole-body tilt pivots at the middle so flight and swimming lie the puppet down
  base = m4_translated(base, (v3){0, 1.35f + q->bounce, 0});
  base = m4_rotate_x(base, -q->tilt);
  base = m4_translated(base, (v3){0, -1.35f, 0});

  // legs: trousers over shoes, from the hip
  for (u32 s = 0; s < 2; s++) {
    f32 sx = s ? 0.2f : -0.2f;
    m4 leg = m4_translated(base, (v3){sx, 1.0f, 0});
    leg = m4_rotate_x(leg, s ? q->leg_rx : q->leg_lx);
    leg = m4_rotate_z(leg, s ? q->leg_rz : q->leg_lz);
    v3 pants = pc->leg_armor ? pc->legs_c : pc->pants, shoes = pc->leg_armor ? v3_scale(pc->legs_c, 0.7f) : pc->shoes;
    vx_ebox(e, leg, (v3){-0.18f, -0.8f, -0.19f}, (v3){0.18f, 0.02f, 0.19f}, L, pants, sky, blk);
    vx_ebox(e, leg, (v3){-0.19f, -1.0f, -0.27f}, (v3){0.19f, -0.8f, 0.2f}, L, shoes, sky, blk);
    if (pc->leg_armor) vx_ebox(e, leg, (v3){-0.15f, -0.5f, -0.23f}, (v3){0.15f, -0.3f, -0.18f}, L, v3_scale(pc->legs_c, 1.15f), sky, blk);
  }

  // torso leans from the hips
  m4 torso = m4_translated(base, (v3){0, 1.0f, 0});
  torso = m4_rotate_x(torso, -q->lean);
  b32 armor = pc->body_gear == VX_BODY_ARMOR;
  v3 shirt = armor ? pc->body_c : pc->shirt;
  vx_ebox(e, torso, (v3){-0.42f, 0.0f, -0.22f}, (v3){0.42f, 0.12f, 0.22f}, L, v3_scale(armor ? pc->body_c : pc->pants, 0.8f), sky, blk);
  vx_ebox(e, torso, (v3){-0.42f, 0.12f, -0.22f}, (v3){0.42f, 0.92f, 0.22f}, L, shirt, sky, blk);
  if (armor) {
    // a breastplate standing proud of the mail
    vx_ebox(e, torso, (v3){-0.36f, 0.34f, -0.27f}, (v3){0.36f, 0.88f, -0.2f}, L, v3_scale(pc->body_c, 1.12f), sky, blk);
    if (v3_length(pc->emblem) > 0)
      vx_ebox_ex(e, torso, (v3){-0.12f, 0.5f, -0.3f}, (v3){0.12f, 0.74f, -0.26f}, L, pc->emblem, VX_BOX_SELF_LIT, 0);
  } else {
    vx_ebox(e, torso, (v3){-0.13f, 0.56f, -0.25f}, (v3){0.13f, 0.92f, -0.21f}, L, pc->undershirt, sky, blk);
  }
  if (pc->body_gear == VX_BODY_COAT) {
    // coat tails: a back panel and two front flaps open over the shirt
    v3 tail = v3_scale(pc->shirt, 0.9f);
    vx_ebox(e, torso, (v3){-0.44f, -0.56f, 0.12f}, (v3){0.44f, 0.12f, 0.25f}, L, tail, sky, blk);
    vx_ebox(e, torso, (v3){-0.44f, -0.46f, -0.25f}, (v3){-0.14f, 0.12f, -0.2f}, L, tail, sky, blk);
    vx_ebox(e, torso, (v3){0.14f, -0.46f, -0.25f}, (v3){0.44f, 0.12f, -0.2f}, L, tail, sky, blk);
  } else if (pc->body_gear == VX_BODY_ROBE) {
    vx_ebox(e, torso, (v3){-0.45f, -0.8f, -0.26f}, (v3){0.45f, 0.12f, 0.26f}, L, v3_scale(pc->shirt, 0.92f), sky, blk);
  }

  if (wings) vx_puppet_wings(e, torso, q, pc, sky, blk);

  // head: skin, eyes, mouth, then hair over it
  m4 head = m4_translated(torso, (v3){0, 0.92f, 0});
  head = m4_rotate_y(head, q->head_y);
  head = m4_rotate_x(head, q->head_x);
  vx_ebox(e, head, (v3){-0.4f, 0, -0.4f}, (v3){0.4f, 0.8f, 0.4f}, L, pc->skin, sky, blk);
  v3 white = {0.96f, 0.95f, 0.92f};
  for (u32 s = 0; s < 2; s++) {
    f32 o = s ? 1.0f : -1.0f;
    v3 a = {o > 0 ? 0.06f : -0.24f, 0.32f, -0.43f}, b = {o > 0 ? 0.24f : -0.06f, 0.46f, -0.39f};
    vx_ebox(e, head, a, b, L, white, sky, blk);
    v3 ia = {o > 0 ? 0.06f : -0.15f, 0.32f, -0.44f}, ib = {o > 0 ? 0.15f : -0.06f, 0.44f, -0.42f};
    vx_ebox(e, head, ia, ib, L, pc->eyes, sky, blk);
  }
  vx_ebox(e, head, (v3){-0.1f, 0.15f, -0.42f}, (v3){0.1f, 0.2f, -0.39f}, L, v3_scale(pc->skin, 0.62f), sky, blk);
  vx_puppet_hair(e, head, pc, L, sky, blk);
  vx_puppet_headgear(e, head, pc, L, sky, blk);

  // arms: sleeves over skin, from the shoulder; the right hand holds the item
  for (u32 s = 0; s < 2; s++) {
    f32 sx = s ? 0.58f : -0.58f;
    m4 arm = m4_translated(torso, (v3){sx, 0.84f, 0});
    arm = m4_rotate_x(arm, s ? q->arm_rx : q->arm_lx);
    arm = m4_rotate_z(arm, s ? q->arm_rz : q->arm_lz);
    vx_ebox(e, arm, (v3){-0.16f, -0.44f, -0.16f}, (v3){0.16f, 0.1f, 0.16f}, L, shirt, sky, blk);
    // armour runs to gauntlets; a robe's sleeves to the wrist
    v3 fore = armor ? v3_scale(pc->body_c, 0.88f) : pc->body_gear == VX_BODY_ROBE ? pc->shirt : pc->skin;
    // the forearm turns at the elbow, its box still authored in the arm's space
    f32 bend = s ? q->elbow_r : 0;
    m4 lower = m4_translated(m4_rotate_x(m4_translated(arm, (v3){0, -0.44f, 0}), bend), (v3){0, 0.44f, 0});
    vx_ebox(e, lower, (v3){-0.14f, -0.86f, -0.14f}, (v3){0.14f, -0.44f, 0.14f}, L, fore, sky, blk);
    if (armor) vx_ebox(e, arm, (v3){-0.21f, -0.06f, -0.21f}, (v3){0.21f, 0.2f, 0.21f}, L, v3_scale(pc->body_c, 1.1f), sky, blk);
    if (s && item > 0) {
      m4 hand = m4_translated(lower, (v3){0, -0.8f, -0.04f});
      // held between swings: the arm's and elbow's turns undone, so the item stands straight up out of the fist
      m4 up = m4_rotate_x(m4_rotate_z(m4_rotate_x(hand, -bend), -q->arm_rz), -q->arm_rx);
      if (q->hold && !vx_item_is_block(item)) {
        // the art's diagonal from grip to tip turned to vertical, its face to the side, the tip leaned 20 degrees forward
        m4 t = m4_rotate_z(m4_rotate_y(m4_rotate_x(up, -0.35f), PI * 0.5f), PI * 0.25f);
        t = m4_scaled(t, (v3){1.1f, 1.1f, 1.1f});
        vx_emit_item(e, m4_translated(t, (v3){-0.12f, -0.12f, 0}), item, sky, blk, 0);
      } else if (!vx_item_is_block(item)) {
        // the art's grip (its lower-left corner) sits in the fist, the head points along the forearm's swing
        m4 t = m4_rotate_y(hand, PI * 0.5f);
        t = m4_rotate_z(t, -PI * 0.75f);
        t = m4_scaled(t, (v3){1.1f, 1.1f, 1.1f});
        vx_emit_item(e, m4_translated(t, (v3){-0.12f, -0.12f, 0}), item, sky, blk, 0);
      } else if (vx_shape[item] == VX_SHAPE_CROSS || vx_shape[item] == VX_SHAPE_TORCH) {
        // a sprite points out of the fist, crossed so it reads from any side
        m4 sp = q->hold ? up : m4_rotate_x(hand, -PI * 0.5f);
        for (u32 c = 0; c < 2; c++) {
          m4 x = c ? m4_rotate_y(sp, PI * 0.5f) : sp;
          x = m4_translated(x, (v3){-0.3f, -0.12f, 0});
          vx_emit_item(e, m4_scaled(x, (v3){0.6f, 0.6f, 0.6f}), item, sky, blk, 0);
        }
      } else {
        m4 b = m4_translated(hand, (v3){-0.17f, -0.2f, -0.24f});
        vx_emit_item(e, m4_scaled(b, (v3){0.34f, 0.34f, 0.34f}), item, sky, blk, 0);
      }
    }
  }
}

// an armour set on a stand: plate head to foot, a closed visor, a glowing emblem, wings where the set has them
hz_internal VxPuppetColors vx_armour_set(u32 plate, u32 eyes, u32 emblem, u32 wing_root, u32 wing_tip, u32 head) {
  v3 c = vx_pp_rgb(plate);
  return (VxPuppetColors){.skin = c, .hair = c, .eyes = vx_pp_rgb(eyes), .shirt = c, .undershirt = c, .pants = c, .shoes = c,
                          .head_c = c, .body_c = c, .legs_c = c, .emblem = vx_pp_rgb(emblem),
                          .wing_root = wing_root ? vx_pp_rgb(wing_root) : (v3){0}, .wing_tip = wing_tip ? vx_pp_rgb(wing_tip) : (v3){0},
                          .head_gear = head, .body_gear = VX_BODY_ARMOR, .leg_armor = true, .visor = true};
}

// a wooden plaque on a wall with a weapon laid across it; frame: the wall face, +z into the wall
hz_internal void vx_armoury_plaque(VxEntities *e, m4 frame, VxItemId item, f32 sky, f32 blk) {
  vx_ebox(e, frame, (v3){-1.1f, -0.4f, -0.1f}, (v3){1.1f, 0.4f, 0.0f}, vx_tex_layer[T_PLANKS], (v3){0.5f, 0.32f, 0.22f}, sky, blk);
  vx_ebox(e, frame, (v3){-0.98f, -0.3f, -0.13f}, (v3){0.98f, 0.3f, -0.1f}, vx_tex_layer[T_PLANKS], (v3){0.62f, 0.4f, 0.26f}, sky, blk);
  m4 t = m4_translated(frame, (v3){0, 0, -0.16f});
  t = m4_rotate_z(t, -PI * 0.25f);
  t = m4_scaled(t, (v3){1.45f, 1.45f, 1.45f});
  vx_emit_item(e, m4_translated(t, (v3){-0.5f, -0.5f, 0}), item, sky, blk, 0);
}

void vx_armoury_build(VxEntities *e, const VxWorld *w, VxV3d cam) {
  VxTownRoom r;
  if (!vx_town_armoury(&w->terrain, &r)) return;
  f64 cx = r.ox + r.ux * r.w * 0.5 + r.vx * r.d * 0.5, cz = r.oz + r.uz * r.w * 0.5 + r.vz * r.d * 0.5;
  if ((cx - cam.x) * (cx - cam.x) + (cz - cam.z) * (cz - cam.z) > 64.0 * 64.0) return;
  // the room frame: x along u, z along v, the block-centre grid's origin on the floor
  f32 yaw = m_atan2f(r.vx, r.vz);
  m4 room = m4_translated(m4_identity(), (v3){(f32)(r.ox - cam.x), (f32)(r.y - cam.y), (f32)(r.oz - cam.z)});
  room = m4_rotate_y(room, yaw);
  f32 sky, blk;
  vx_light_at(w, (VxV3d){cx, r.y + 2.0, cz}, &sky, &blk);
  // sun, vortex, nebula and stardust, as s_18-s_20
  VxPuppetColors sets[4] = {
      vx_armour_set(0xE88A28, 0xFFF4C8, 0xFFF2C0, 0, 0, VX_HEAD_CROWN),
      vx_armour_set(0x1C4C4A, 0x70FFF0, 0x60FFE0, 0, 0, VX_HEAD_HELMET),
      vx_armour_set(0x7A2A62, 0xFFC8F4, 0xFF50C8, 0xB0389A, 0xF4A0E4, VX_HEAD_HELMET),
      vx_armour_set(0x283C80, 0x90E8FF, 0x70C8FF, 0x7AB8E8, 0xDCF4FF, VX_HEAD_HELMET),
  };
  const VxItemId held[4] = {I_GOLD_BROADSWORD, I_WOODEN_BOW, 0, I_WAND_OF_SPARKING};
  f32 span = (f32)r.w - 5.6f;
  for (u32 k = 0; k < 4; k++) {
    // u runs right to left seen from the door, so the sun set stands leftmost
    f32 u = 2.3f + span * (f32)(3 - k) / 3.0f, v = (f32)r.d - 3.6f;
    m4 stand = m4_translated(room, (v3){u, 0, v});
    vx_ebox(e, stand, (v3){-0.5f, 0, -0.5f}, (v3){0.5f, 0.12f, 0.5f}, vx_tex_layer[T_PLANKS], (v3){0.7f, 0.5f, 0.34f}, sky, blk);
    VxV3d feet = {r.ox + r.ux * u + r.vx * v, r.y + 0.12, r.oz + r.uz * u + r.vz * v};
    VxPose q = {.arm_rx = held[k] ? 0.55f : 0.1f, .arm_lz = 0.18f, .arm_rz = -0.06f, .wing_fold = 0.1f, .wing_flap = -0.6f};
    vx_puppet_body(e, w, cam, feet, yaw, &q, &sets[k], held[k], v3_length(sets[k].wing_tip) > 0 || k == 0);
  }
  // plaques: two on the back wall between the stands, two on the left wall, one on the right
  f32 back = (f32)r.d - 2.5f, mid = ((f32)r.w - 1) * 0.5f;
  vx_armoury_plaque(e, m4_translated(room, (v3){mid, 3.5f, back}), I_ENCHANTED_SWORD, sky, blk);
  vx_armoury_plaque(e, m4_translated(room, (v3){mid, 2.2f, back}), I_NIGHTS_EDGE, sky, blk);
  m4 left = m4_rotate_y(m4_translated(room, (v3){1.5f, 0, 0}), -PI * 0.5f);
  vx_armoury_plaque(e, m4_translated(left, (v3){(f32)r.d - 5.5f, 3.2f, 0}), I_ICE_BLADE, sky, blk);
  vx_armoury_plaque(e, m4_translated(left, (v3){5.8f, 2.0f, 0}), I_GOLD_BROADSWORD, sky, blk);
  m4 right = m4_rotate_y(m4_translated(room, (v3){(f32)r.w - 2.5f, 0, 0}), PI * 0.5f);
  vx_armoury_plaque(e, m4_translated(right, (v3){-((f32)r.d - 4.5f), 3.2f, 0}), I_SILVER_BROADSWORD, sky, blk);
}

#ifndef VX_GAME_H
#define VX_GAME_H

#include "voxaria/vx_render.h"
#include "voxaria/vx_settings.h"
#include "voxaria/vx_items.h"
#include "lib/random.h"
#include "audio.h"
#include "procgen/pg_audio.h"
#include "assets/assets.h"
#include "ui/ui.h"

// ---- player ----

#define VX_PLAYER_HEIGHT 2.7 // Terraria's player is about 2.7 tiles tall and needs a 3-tall, 2-wide gap
#define VX_PLAYER_HALF 0.55 // half the player's width

typedef struct {
  b32 forward, back, left, right;
  b32 jump, down; // down: descend while flying, drop through platforms
  b32 jump_pressed, fly_pressed, hook_pressed;
  // analog move from the touch stick, strafe right / back positive, length <= 1
  f32 stick_x, stick_z;
  v3 aim; // the view direction, for the hook
} VxPlayerInput;

typedef enum {
  VX_HOOK_NONE,
  VX_HOOK_FLYING,
  VX_HOOK_ATTACHED,
} VxHookState;

// what happened to the player this update, for sounds and effects
enum {
  VX_PEV_JUMP = 1u << 0,
  VX_PEV_DOUBLE_JUMP = 1u << 1,
  VX_PEV_LAND = 1u << 2,
  VX_PEV_SPLASH = 1u << 3,
  VX_PEV_HOOK_FIRE = 1u << 4,
  VX_PEV_HOOK_HIT = 1u << 5,
  VX_PEV_STEP_UP = 1u << 6,
  VX_PEV_COYOTE = 1u << 7, // the jump left from the air, within the coyote window
};

typedef struct {
  VxV3d pos;
  v3 vel;
  f32 yaw, pitch;
  f32 knock_t; // seconds left in which a knockback outweighs steering
  f32 body_yaw; // where the character faces: it turns toward where it runs
  b32 on_ground, flying, in_water, in_lava, head_in_water;
  f32 sensitivity;
  f32 eye_offset, step_smooth; // step_smooth: the rest of a step-up the view still eases through (<= 0)
  f32 bob, bob_amount, fov_boost, stride;
  // jumping and falling
  f32 coyote, jump_buffer, jump_hold;
  b32 jumping, double_used, flapping, gliding;
  f64 fall_peak;
  // abilities, from equipped accessories
  b32 has_double_jump, has_hook;
  f32 run_mult;
  f32 wing_time_max, wing_time;
  VxHookState hook_state;
  VxV3d hook_pos;
  v3 hook_vel;
  f32 hook_len; // rope length: it only shortens, so the player swings under the hook
  u32 events;      // VX_PEV_* this update
  f32 landed_fall; // blocks fallen, on the update that landed
} VxPlayer;

void vx_player_init(VxPlayer *p);
VxV3d vx_player_eye(const VxPlayer *p);
// eye position including subtle view bobbing
VxV3d vx_player_camera(const VxPlayer *p, b32 bobbing);
v3 vx_player_forward(const VxPlayer *p);
void vx_player_look(VxPlayer *p, f32 dx, f32 dy);
void vx_player_update(VxPlayer *p, f32 dt, const VxPlayerInput *in, const VxWorld *w, f32 now);
b32 vx_player_intersects_block(const VxPlayer *p, i32 x, i32 y, i32 z);

typedef struct {
  b32 hit;
  i32 x, y, z;
  i32 nx, ny, nz;
  f32 dist;
  u8 id;
  v3 dir; // the ray it was found along, so a townsperson on the same line can be picked first
} VxRayHit;

// voxel dda (Amanatides & Woo); liquids are transparent to it
VxRayHit vx_raycast(const VxWorld *w, VxV3d o, v3 d, f32 max_dist);

// ---- map ----

#define VX_MAP_SIZE (VX_WORLD_HALF * 2)

typedef struct {
  u8 *px;     // VX_MAP_SIZE^2 srgb rgba, alpha 0 where unexplored
  u32 *stamp; // per chunk: the content stamp its texels were drawn from
  u32 *colors; // per block id
  GpuTexture tex;
  b32 upload_all;
} VxMap;

void vx_map_init(VxMap *m, Allocator *alloc, const VxTextureData *tex);
// forgets everything explored; the texture follows on the next update
void vx_map_clear(VxMap *m);
// redraws loaded chunks whose blocks or light changed, nearest first, a bounded number per call (main thread)
void vx_map_update(VxMap *m, const VxWorld *w, VxV3d around);

// ---- event log ----

#define VX_SCENES 20 // the reference video's scenes, Page Down / Page Up in play
#define VX_EVENTS 8
#define VX_EVENT_LEN 96
#define VX_EVENT_LIFE 10.0f

// ---- chests ----

#define VX_CHEST_SLOTS 40
#define VX_MAX_CHESTS 1024

typedef struct {
  i32 x, y, z;
  VxStack slot[VX_CHEST_SLOTS];
} VxChest;

// ---- world-anchored text ----

#define VX_MAX_WORLD_TEXT 32
#define VX_WORLD_TEXT_LIFE 1.6f

typedef enum {
  VX_WT_PICKUP, // item and amount: "Dirt Block (5)"
  VX_WT_DAMAGE,
  VX_WT_CRIT,
  VX_WT_HEAL,
} VxWorldTextKind;

typedef struct {
  VxV3d p;
  f32 t;
  u8 kind;
  VxItemId item;
  u32 amount;
} VxWorldText;

// ---- vitals ----

#define VX_RESPAWN_TIME 5.0f

typedef struct {
  f32 hp, hp_max, mana, mana_max;
  f32 breath, breath_max; // seconds of air
  f32 since_hurt, immune, mana_idle, potion_cd;
  f32 hurt_flash; // 1 on a hit, fading
  f32 last_damage;
  u32 hits;
  b32 dead;
  f32 death_t;
  const char *death_msg;
} VxVitals;

typedef enum {
  VX_HUD_HEART,
  VX_HUD_HEART_EMPTY,
  VX_HUD_STAR,
  VX_HUD_STAR_EMPTY,
  VX_HUD_BUBBLE,
  VX_HUD_ICON_COUNT,
} VxHudIcon;

#define VX_HUD_ICON_SIZE 64

void vx_vitals_reset(VxVitals *v);
b32 vx_vitals_use_mana(VxVitals *v, f32 amount);
void vx_hud_icons_make(GpuTexture *out);
// 4x nearest-upscaled item art for the ui, into out[B_COUNT..I_COUNT)
void vx_item_icons_make(GpuTexture *out);

// ---- camera ----

typedef enum {
  VX_CAM_THIRD,
  VX_CAM_FIRST,
} VxCamMode;

#define VX_REACH 6.0f // blocks from the player's chest, as Terraria measures it from the player

typedef struct {
  VxCamMode mode;
  f32 dist;     // wanted orbit distance
  f32 cur_dist; // after collision; eases back out
  f32 orbit; // yaw offset around the player, for looking at the character
  VxV3d pos;
  v3 fwd;
} VxCamera;

void vx_camera_init(VxCamera *c);
void vx_camera_update(VxCamera *c, const VxPlayer *p, const VxWorld *w, f32 dt, b32 bobbing);
VxV3d vx_player_reach_origin(const VxPlayer *p);
VxV3d vx_camera_ray_start(const VxCamera *cam, VxV3d o, v3 d, const VxPlayer *p);
VxRayHit vx_camera_target(const VxCamera *c, VxV3d o, v3 d, const VxPlayer *p, const VxWorld *w, f32 reach, b32 smart);

// ---- touch controls ----

typedef enum {
  VX_TOUCH_FREE,
  VX_TOUCH_STICK,  // left thumb: the movement stick, centred where it landed
  VX_TOUCH_WORLD,  // undecided: a tap places, a hold breaks, a drag looks
  VX_TOUCH_LOOK,
  VX_TOUCH_BREAK,
  VX_TOUCH_BUTTON,
  VX_TOUCH_IGNORED, // began outside play, or on nothing
} VxTouchRole;

typedef enum {
  VX_TBTN_JUMP,
  VX_TBTN_SNEAK,
  VX_TBTN_INVENTORY,
  VX_TBTN_PAUSE,
  VX_TBTN_MAP,
  VX_TBTN_HOOK,
  VX_TBTN_HEAL,
  VX_TBTN_VIEW,
  VX_TBTN_HOTBAR, // + slot index
} VxTouchButton;

typedef struct {
  u32 id;
  VxTouchRole role;
  u32 button;
} VxTouchTrack;

typedef struct {
  VxTouchTrack tracks[MAX_TOUCHES];
  v2 stick_center, stick_knob; // ui px
  f32 stick_x, stick_z;
  b32 jump_held, jump_pressed, hook_pressed;
  b32 sneak_held, sneak_on;
  VxRayHit target; // the block under the breaking finger
} VxTouch;

// ---- dynamic entities: held item, block-break particles, fireflies ----

#define VX_MAX_PARTICLES 600
#define VX_MAX_FIREFLIES 42

typedef struct {
  VxV3d p;
  v3 v;
  f32 life, max_life, size;
  u32 layer;
  f32 u, vv;
  v3 tint;
  u32 cut;
} VxParticle;

typedef struct {
  VxV3d p;
  v3 v;
  f32 phase, life;
} VxFirefly;

// ambient motes: drifting specks the world gives off around the camera
#define VX_MAX_MOTES VX_PARTICLE_CAP
#define VX_MAX_EMITTERS 128

typedef enum {
  VX_MOTE_EMBER,
  VX_MOTE_MIST,
  VX_MOTE_LEAF,
  VX_MOTE_SPORE,
  VX_MOTE_SNOW,
  VX_MOTE_DUST,
  VX_MOTE_SPARK, // glowing, falls and dies on contact
  VX_MOTE_TRAIL, // glowing, hangs and shrinks: weapon swings and projectile wakes
  VX_MOTE_GORE,  // lit chunks that fall and settle
  VX_MOTE_SMOKE,   // a lit puff that rises, swells and thins
  VX_MOTE_FLAME,   // a flickering tongue of fire over a torch
  VX_MOTE_DROPLET, // a lit drop of water under gravity
  VX_MOTE_FLASH,   // the white-hot instant of a death or impact
  VX_MOTE_RING,    // an expanding shockwave shell
  VX_MOTE_STREAK,  // a glowing smear along its velocity
  VX_MOTE_SPARKLE, // a twinkling square on a precious item
  VX_MOTE_KIND_COUNT,
} VxMoteKind;

typedef struct {
  VxV3d p;
  v3 v;
  f32 life, max_life, size, phase;
  u32 tint; // rgba8
  u8 kind;
} VxMote;

// a cell that keeps giving off motes: a lava surface or the foot of a waterfall
typedef struct {
  i32 x, y, z;
  u8 kind;
  f32 acc, check;
} VxMoteEmitter;

typedef struct {
  VxV3d cam;
  u8 biome;
  f32 underground, underworld;
  b32 active;
} VxMoteEnv;

// ---- character ----

typedef enum {
  VX_PP_SKIN,
  VX_PP_HAIR,
  VX_PP_HAIR_STYLE,
  VX_PP_EYES,
  VX_PP_SHIRT,
  VX_PP_UNDERSHIRT,
  VX_PP_PANTS,
  VX_PP_SHOES,
  VX_PP_COUNT,
} VxPuppetPart;

typedef enum {
  VX_HAIR_SHORT,
  VX_HAIR_LONG,
  VX_HAIR_SPIKY,
  VX_HAIR_PONYTAIL,
  VX_HAIR_STYLE_COUNT,
} VxHairStyle;

// what covers the head: hair alone, or a helmet, hat or hood over it
typedef enum {
  VX_HEAD_HAIR,
  VX_HEAD_HELMET,
  VX_HEAD_CROWN,
  VX_HEAD_MINER,
  VX_HEAD_TOP_HAT,
  VX_HEAD_HOOD,
} VxHeadGear;

// what the torso wears over the shirt
typedef enum {
  VX_BODY_SHIRT,
  VX_BODY_ARMOR,
  VX_BODY_COAT,
  VX_BODY_ROBE,
} VxBodyGear;

typedef struct {
  v3 skin, hair, eyes, shirt, undershirt, pants, shoes;
  v3 head_c, body_c, legs_c; // the helmet or hat, the chest armour or hat band, the greaves
  v3 emblem;                 // a glowing chest emblem, none while black
  v3 wing_root, wing_tip;    // feather colours, gold while black
  u32 hair_style, head_gear, body_gear;
  b32 leg_armor, beard, visor; // visor: the helmet closes over the face, the eyes glowing slits
} VxPuppetColors;

// joint angles in radians; limbs swing forward for positive x, outward for positive z on the left
typedef struct {
  f32 tilt, lean, bounce;
  f32 wing_fold, wing_flap; // 1 folded along the back, 0 spread; flap angle
  f32 head_x, head_y;
  f32 arm_lx, arm_lz, arm_rx, arm_rz;
  f32 elbow_r; // the right forearm's bend forward from the upper arm
  f32 leg_lx, leg_lz, leg_rx, leg_rz;
  b32 hold; // the right fist raised with its item straight up, as the reference holds a sword between swings
  f32 item_up; // 1 the item stands straight up out of the fist, 0 it lies along the forearm; blended so it never flips
} VxPose;

typedef struct {
  VxPose pose;
  f32 time, facing;
  f32 aim_t; // seconds left holding a ranged weapon out along the view
  f32 last_swing_t, from_arm, from_elbow; // where the arm was when the attack began: the wind-up starts there
  b32 swinging;
} VxPuppet;

// a block being mined: damage builds over hits and heals when left alone
typedef struct {
  i32 x, y, z;
  f32 damage, idle;
} VxTileHit;

#define VX_TILE_HITS 16

// ---- creatures: box puppets with one of four behaviours ----

typedef enum {
  VX_BEH_HOPPER,
  VX_BEH_WALKER,
  VX_BEH_SWOOPER,
  VX_BEH_FLUTTERER,
  VX_BEH_TOWNSFOLK, // friendly: never hurts, is never hurt, never despawns
  VX_BEH_BOSS,      // its own fight: hovers, dashes and changes form, through blocks
} VxBehaviour;

typedef enum {
  VX_CR_SLIME,
  VX_CR_ZOMBIE,
  VX_CR_SKELETON,
  VX_CR_DEMON_EYE,
  VX_CR_EATER,
  VX_CR_BAT,
  VX_CR_GUIDE,
  VX_CR_EYE_OF_CTHULHU,
  VX_CR_SERVANT, // the Eye's small eyes, sent out of its pupil
  VX_CR_EMPRESS, // the Empress of Light: a rainbow-winged fairy that fills the dusk sky with prismatic bolts
  VX_CR_COUNT,
} VxCreatureKind;

// slime and bat variants by biome
typedef enum {
  VX_SLIME_GREEN,
  VX_SLIME_BLUE,
  VX_SLIME_ICE,
  VX_SLIME_JUNGLE,
  VX_SLIME_LAVA,
  VX_SLIME_PINKY,
  VX_SLIME_COUNT,
} VxSlimeVariant;

typedef enum {
  VX_BAT_CAVE,
  VX_BAT_ICE,
  VX_BAT_JUNGLE,
  VX_BAT_HELL,
  VX_BAT_COUNT,
} VxBatVariant;

typedef struct {
  const char *name;
  const char *death_msg; // shown when it kills the player
  u8 behaviour;
  f32 half, height; // collision box: half width, height
  f32 hp, damage, defense, knock_resist; // knock_resist: 0 takes all knockback, 1 none
  f32 speed;
} VxCreatureInfo;

extern const VxCreatureInfo vx_creature_info[VX_CR_COUNT];

// night by the day clock: creatures, the boss and the music all agree on it
force_inline b32 vx_is_night(f32 day_time) { return day_time > 0.52f && day_time < 0.98f; }

force_inline b32 vx_creature_friendly(u8 kind) { return vx_creature_info[kind].behaviour == VX_BEH_TOWNSFOLK; }

#define VX_MAX_CREATURES 96

typedef struct {
  VxV3d pos; // feet, centred
  v3 vel;
  f32 yaw, hp, hp_max;
  f32 timer, anim, squash, flash, immune, age, show_hp;
  f32 jitter_a, jitter_b; // a flutterer's or townsperson's wander heading, and a flutterer's bob phase
  f32 voice_t;            // seconds until a zombie's next groan
  f32 look, wave;         // a townsperson's head turn toward the player and its greeting wave, seconds left
  u32 seed;
  u8 kind, variant;
  u8 on_ground, aggro;
  u8 greeted, strolling; // townsfolk: has waved at the player since they came near; walking, not standing
  u8 boss_phase, boss_state, dashes; // the Eye: 1 or 2, what it is doing, dashes left in the volley
  f32 pitch, spin, mouth;            // the Eye: facing tilt, roll while it transforms, how wide its mouth gapes
  u32 hit_swing; // the last melee swing that hit it
  u16 held;      // the item a slime carries inside its jelly, dropped when it dies
} VxCreature;

typedef struct {
  VxCreature *items; // VX_MAX_CREATURES
  u32 count;
  u32 kills;
  f32 spawn_timer;
  b32 natural_off; // test tooling: only scripted spawns and the boss's own
  Random rng;
} VxCreatures;

// ---- combat: melee swings and projectiles ----

typedef enum {
  VX_PROJ_ARROW,
  VX_PROJ_SPARK, // the wand's rainbow bolt
  VX_PROJ_BEAM,  // the Enchanted Sword's flying blade of light
  VX_PROJ_FROST, // the Ice Blade's shard
  VX_PROJ_STAR,  // a fallen star on its way down
  VX_PROJ_PRISM, // the Empress's bolt: flies out in a fan, then turns on the player
  VX_PROJ_LANCE, // the Empress's crescent, thrown straight at the player
} VxProjectileKind;

// the kinds from here on hurt the player and pass through blocks
#define VX_PROJ_HOSTILE VX_PROJ_PRISM

// a colour round the colour wheel (0 red, 1/3 green, 2/3 blue) as srgb 0xRRGGBB, washed toward white by w
force_inline u32 vx_hue_hex(f32 h, f32 w) {
  h -= m_floorf(h);
  f32 r = m_saturatef(m_absf(h * 6 - 3) - 1), g = m_saturatef(2 - m_absf(h * 6 - 2)), b = m_saturatef(2 - m_absf(h * 6 - 4));
  r += (1 - r) * w, g += (1 - g) * w, b += (1 - b) * w;
  return (u32)(r * 255 + 0.5f) << 16 | (u32)(g * 255 + 0.5f) << 8 | (u32)(b * 255 + 0.5f);
}

#define VX_MAX_PROJECTILES 256

#define VX_ATK_WIND 0.62f   // share of an attack spent winding up
#define VX_ATK_STRIKE 0.2f  // share spent on the strike, three times faster; the rest follows through

// one curve for every attack, so the arm and the hit agree: 0..1 through the wind-up (easing out), 1..2 through the strike (accelerating), 2 after
force_inline f32 vx_attack_phase(f32 u) {
  if (u < VX_ATK_WIND) {
    f32 s = u / VX_ATK_WIND;
    return 1 - (1 - s) * (1 - s);
  }
  if (u < VX_ATK_WIND + VX_ATK_STRIKE) {
    f32 s = (u - VX_ATK_WIND) / VX_ATK_STRIKE;
    return 1 + s * s;
  }
  return 2;
}

// the sword arm's angle from hanging (0) through forward (pi/2) to overhead (pi) at an attack phase
#define VX_ATK_HOLD 0.52f
#define VX_ATK_RAISED 2.75f
#define VX_ATK_DOWN 0.1f
force_inline f32 vx_slash_arm(f32 ph) {
  return ph <= 1 ? VX_ATK_HOLD + (VX_ATK_RAISED - VX_ATK_HOLD) * ph : VX_ATK_RAISED + (VX_ATK_DOWN - VX_ATK_RAISED) * (ph - 1);
}

typedef struct {
  VxV3d p;
  v3 v;
  f32 life, damage, knock;
  f32 age, hue; // hostile kinds: seconds flown, and the colour round the colour wheel
  u8 kind;
} VxProjectile;

typedef struct {
  f32 t, dur; // seconds left in the current swing, and its length
  f32 reach, damage, knock;
  const char *weapon; // the swinging weapon's name
  u32 swing_id;
  u32 trail_tint;
  u32 glint_tint; // the special blades' dust off the tip, 0 for none
  const VxItemInfo *weapon_info; // the swinging weapon, for what it throws at the strike
  b32 released;   // the strike has begun: hits, trail and thrown bolts
  const VxItemInfo *pending; // a shot waiting for the strike of its draw or flick
  f32 pending_t;
  b32 stab;       // a shortsword thrusts along the view instead of swinging
  v3 tip_prev; // the blade tip last frame, relative to the chest
  b32 tip_valid;
  VxProjectile *proj; // VX_MAX_PROJECTILES
  u32 proj_count;
  u32 hits, shots;
  Random rng;
} VxCombat;

typedef struct {
  const VxPlayer *player; // null: no puppet
  const VxPuppetColors *colors;
  b32 first_person, show_hand;
  VxItemId item;
  const VxTileHit *cracks;
  u32 crack_count;
  f32 yaw, pitch, bob, bob_amount;
  const VxCreatures *creatures;
  const VxCombat *combat;
} VxEntityView;

// ---- debris: tumbling lit boxes from kills and impacts ----

#define VX_MAX_DEBRIS 320

typedef struct {
  VxV3d p;
  v3 v;
  f32 life, size, ang, spin;
  u32 tint; // srgb 0xRRGGBB
  u8 axis;  // the tumble's second axis
} VxDebris;

// ---- dropped items ----

#define VX_MAX_DROPS 384
#define VX_MAX_PICKUPS 16

typedef struct {
  VxV3d p;
  v3 v;
  VxItemId item;
  u16 count;
  f32 age, spin;
} VxDrop;

typedef struct {
  VxParticle *particles;
  u32 particle_count;
  VxDrop *drops;
  u32 drop_count;
  f32 merge_t;
  VxStack pickups[VX_MAX_PICKUPS]; // collected this update
  u32 pickup_count;
  VxMote *motes;
  u32 mote_count;
  u16 mote_kind_count[VX_MOTE_KIND_COUNT];
  VxMoteEmitter *emitters;
  u32 emitter_count;
  u32 scan_cursor; // the next column of the emitter sweep
  VxParticleGpu *parts;
  u32 part_count;
  VxFirefly *fireflies;
  u32 firefly_count;
  f32 *verts;
  u32 vert_count;
  u32 hand_start;
  VxBoxGpu *boxes; // VX_BOX_CAP
  VxBoxGpu *jelly; // VX_JELLY_CAP
  u32 jelly_count;
  VxVfxGpu *vfx; // VX_PARTICLE_CAP
  u32 vfx_count;
  u32 box_count;
  VxDebris *debris;
  u32 debris_count;
  f32 time;
  f32 swing, swing_t, swing_rate, equip;
  i32 last_item;
  VxPuppet puppet;
  Random rng;
} VxEntities;

u32 vx_puppet_option_count(VxPuppetPart part);
const char *const *vx_puppet_option_names(VxPuppetPart part);
VxPuppetColors vx_puppet_colors(const VxSettings *s);
void vx_puppet_hurt_tint(VxPuppetColors *pc, f32 k);
// dresses the puppet in the worn head, body and leg armour
void vx_puppet_armor(VxPuppetColors *pc, const VxStack *armor);
VxV3d vx_puppet_hook_hand(const VxPlayer *p);
void vx_puppet_animate(VxPuppet *pp, const VxPlayer *p, VxItemId held, f32 swing_t, f32 dt);
void vx_puppet_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxPlayer *p, const VxPuppet *pp,
                     const VxPuppetColors *pc, VxItemId item);
// the armoury's armour stands and weapon plaques, when the camera is near it
void vx_armoury_build(VxEntities *e, const VxWorld *w, VxV3d cam);
void vx_puppet_body(VxEntities *e, const VxWorld *w, VxV3d cam, VxV3d feet, f32 facing, const VxPose *q,
                    const VxPuppetColors *pc, VxItemId item, b32 wings);

// ---- held light: a flood-filled window around the player (VX_DYN_SIZE^3 rgba8 texels) ----

typedef struct {
  u32 *texels;
  u8 *dist, *opacity, *queued;
  u32 *queue;
  iv3 origin;
  u32 version;
  b32 lit;
  iv3 key_cell;
  u8 key_item;
  u32 key_edits;
} VxDynLight;

void vx_dynlight_init(VxDynLight *d, Allocator *alloc);

// ---- the Terraria view: side-on, cut away in front of the player's slab ----

#define VX_CUT_FOV 40.0f

typedef struct {
  b32 on;
  i32 z;               // the player's slab; everything past it is cut away
  i32 facing;          // +1 toward +x, -1 toward -x
  f32 view_x, view_y;  // the camera's smoothed aim on the plane
  u32 *light;          // VX_CUT_W x VX_HEIGHT rgba8 light map of the slab from light_x0
  f32 *work;
  i32 light_x0;
  u32 light_version, light_edits, light_dyn;
  f32 light_t;
  b32 scripted_cursor; // test tooling: a pointer fixed in the world instead of the mouse
  b32 touch_cursor;    // on touch the last finger on the world is the pointer
  f64 cursor_x, cursor_y;
} VxCut;

// impact feedback: trauma drives the view's shake, hit-stop slows the simulation for a few frames
typedef struct {
  f32 trauma, hitstop, t;
} VxJuice;
// refills the window when the source cell, the item or the world changed; false when the item gives no light
b32 vx_dynlight_update(VxDynLight *d, const VxWorld *w, VxV3d src, u8 item);

void vx_entities_init(VxEntities *e, Allocator *alloc, u64 seed);
void vx_entities_trigger_swing(VxEntities *e, f32 duration);
void vx_entities_cloud_puff(VxEntities *e, VxV3d feet);
// a scuff of the ground's colour (rgb 0xRRGGBB) kicked up behind a running foot
void vx_entities_step_puff(VxEntities *e, VxV3d feet, v3 vel, u32 rgb);
// droplets and spray thrown up where something hits water at speed
void vx_entities_splash(VxEntities *e, VxV3d at, f32 speed);
// a glowing smear moving with v: falling stars and dashes
void vx_entities_streak(VxEntities *e, VxV3d p, v3 v, f32 life, f32 size, u32 tint);
// the white flash and shockwave of a death
void vx_entities_death_burst(VxEntities *e, VxV3d at, f32 radius, u32 tint);
void vx_entities_spawn_drop(VxEntities *e, VxV3d p, VxItemId item, u32 count);
// drops fall, merge and fly to the player once they are older than their pickup delay; collected stacks land in pickups
void vx_entities_update_drops(VxEntities *e, f32 dt, const VxWorld *w, VxV3d chest, VxInventory *inv);
void vx_entities_dust(VxEntities *e, u8 id, i32 x, i32 y, i32 z, u32 count);
// count chunks thrown from p around v, coloured from a palette of srgb colours
void vx_entities_debris(VxEntities *e, VxV3d p, v3 v, f32 spread, u32 count, f32 size, const u32 *palette, u32 palette_count);
// count motes of a kind at p with velocity v plus a random spread in every direction, life and size varied by a third
void vx_entities_burst(VxEntities *e, VxMoteKind kind, VxV3d p, v3 v, f32 spread, u32 count, f32 life, f32 size, u32 tint);
void vx_entities_spawn_break(VxEntities *e, u8 id, i32 x, i32 y, i32 z);
void vx_entities_update_fireflies(VxEntities *e, f32 dt, const VxWorld *w, VxV3d cam, b32 active);
void vx_entities_update(VxEntities *e, f32 dt, const VxWorld *w);
void vx_entities_update_motes(VxEntities *e, f32 dt, const VxWorld *w, const VxMoteEnv *env);
// 0..1: how much open lava surrounds the camera, from the ember emitters found near it
f32 vx_entities_lava_heat(const VxEntities *e, VxV3d cam);
VxEntityGeometry vx_entities_build(VxEntities *e, const VxWorld *w, VxV3d cam, const VxEntityView *view);
void vx_emit_item(VxEntities *e, m4 xf, VxItemId item, f32 sky, f32 blk, u32 flags);

// ---- procedural sound ----

typedef enum {
  VX_MAT_STONE,
  VX_MAT_DIRT,
  VX_MAT_GRASS,
  VX_MAT_WOOD,
  VX_MAT_SAND,
  VX_MAT_GLASS,
  VX_MAT_PLANT,
  VX_MAT_WATER,
  VX_MAT_WOOL,
  VX_MAT_SNOW,
  VX_MAT_COUNT,
} VxMaterial;

#define VX_THUNDER_VARIANTS 3
#define VX_MAX_PENDING_THUNDER 8

typedef struct {
  f32 delay;
  f32 strength;
  u32 variant;
} VxPendingThunder;

// one-shot sounds beyond the per-material break, place and step sets
#define VX_SOUND_LIST(X)                                                                                               \
  X(SWING, "swing")                                                                                                    \
  X(JUMP, "jump")                                                                                                      \
  X(LAND, "land")                                                                                                      \
  X(DOUBLE_JUMP, "double jump")                                                                                        \
  X(HOOK_FIRE, "hook fire")                                                                                            \
  X(HOOK_LATCH, "hook latch")                                                                                          \
  X(WINGS, "wing flap")                                                                                                \
  X(HURT, "hurt")                                                                                                      \
  X(DEATH, "death")                                                                                                    \
  X(DRINK, "drink")                                                                                                    \
  X(SHIMMER, "shimmer")                                                                                                \
  X(CRAFT, "craft")                                                                                                    \
  X(CHEST, "chest open")                                                                                               \
  X(DOOR_OPEN, "door open")                                                                                            \
  X(DOOR_CLOSE, "door close")                                                                                          \
  X(TREE_FALL, "tree fall")                                                                                            \
  X(PICKUP, "item pickup")                                                                                             \
  X(HIT, "hit")                                                                                                        \
  X(HIT_SLIME, "slime hit")                                                                                            \
  X(HIT_BONE, "bone hit")                                                                                              \
  X(KILL, "kill")                                                                                                      \
  X(KILL_SLIME, "slime kill")                                                                                          \
  X(BOW, "bow")                                                                                                        \
  X(ARROW_HIT, "arrow hit")                                                                                            \
  X(SPARK, "spark cast")                                                                                               \
  X(ZOMBIE, "zombie groan")                                                                                            \
  X(ROAR, "boss roar")                                                                                                 \
  X(BOSS_DASH, "boss dash")

typedef enum {
#define VX_X(id, name) VX_SND_##id,
  VX_SOUND_LIST(VX_X)
#undef VX_X
  VX_SND_COUNT
} VxSound;

// ambience beds, crossfaded by where the camera is and when
typedef enum {
  VX_AMB_DAY,
  VX_AMB_NIGHT,
  VX_AMB_CAVE,
  VX_AMB_UNDERWORLD,
  VX_AMB_OCEAN,
  VX_AMB_COUNT,
} VxAmbLayer;

typedef struct {
  f32 w[VX_AMB_COUNT]; // 0..1 per layer
} VxAmbienceMix;

#define VX_CRICKETS 4

typedef struct {
  f32 freq, period, t, phase; // carrier Hz, seconds per chirp, time into the chirp, carrier phase
  u32 pulses;
} VxCricket;

// a one-shot grain inside a bed: a cave drip, a lava blorp
typedef struct {
  f32 age, f0, phase;
  b32 on;
} VxAmbGrain;

// music: six loops, each a pattern song played by a small synth band
typedef enum {
  VX_SONG_TITLE,
  VX_SONG_DAY,
  VX_SONG_NIGHT,
  VX_SONG_UNDERGROUND,
  VX_SONG_UNDERWORLD,
  VX_SONG_BOSS,
  VX_SONG_COUNT,
} VxSongId;

typedef struct {
  PgOsc o1, o2;
  f32 freq, target, t, rel_from, rel_t;
  b32 gate;
} VxMusicVoice;

// one song's playback; two of these crossfade
typedef struct {
  i32 song; // -1 when silent
  f32 gain, want;
  u32 step;
  f32 step_left; // samples until the next step
  VxMusicVoice lead, bass, arp, pad[3];
  PgOsc kick_osc, snare_osc;
  f32 kick_t, snare_t, hat_t;
  PgButter lead_lp, bass_lp, pad_lp, arp_lp, snare_bp, hat_hp;
  f32 *echo; // a feedback delay of three sixteenths
  u32 echo_len, echo_pos;
  Random rng;
} VxMusicPlayer;

typedef struct {
  VxMusicPlayer pl[2];
  u32 cur; // the player fading in or playing
  HzHandleT(AudioSource) stream;
  HzHandleT(AudioVoice) voice;
  u32 target_frames;
} VxMusic;

typedef struct {
  PgAmbience day, night; // the engine's room tone: wind and chirping birds by day, a low bed under the crickets at night
  VxCricket crickets[VX_CRICKETS];
  PgButter drone_lp, rumble_lp, surf_lp;
  VxAmbGrain drip, blorp;
  f32 *echo; // the cave's drip echo, one delay line
  u32 echo_len, echo_pos;
  f64 t;
  f32 rendered[VX_AMB_COUNT], gain[VX_AMB_COUNT], want[VX_AMB_COUNT];
  i32 dominant;
  HzHandleT(AudioSource) stream;
  HzHandleT(AudioVoice) voice;
  u32 target_frames;
  Random rng;
} VxAmbience;

typedef struct {
  AudioSystem sys;
  Allocator alloc;
  b32 trace; // scripted runs log every sound by name
  VxAmbience amb;
  VxMusic music;
  HzHandleT(AudioSource) *sounds; // VX_SND_COUNT
  HzHandleT(AudioSource) *brk;   // per material
  HzHandleT(AudioSource) *place; // per material
  HzHandleT(AudioSource) *step;  // per material
  HzHandleT(AudioSource) splash;
  HzHandleT(AudioSource) rain;
  HzHandleT(AudioSource) *thunder; // VX_THUNDER_VARIANTS
  HzHandleT(AudioVoice) rain_voice;
  f32 rain_gain;
  VxPendingThunder *pending;
  u32 pending_count;
  Random rng;
} VxAudio;

// main lane
void vx_audio_init(VxAudio *a, Allocator *alloc);
void vx_audio_update(VxAudio *a, f32 dt, f32 volume, f32 rain, f32 outdoors, const VxAmbienceMix *mix, i32 music);
// the music: a tracker playing one of VX_SONG_COUNT loops into a live stream, crossfading when the wanted song changes (-1 for none)
void vx_music_init(VxMusic *m, AudioSystem *sys, Allocator *alloc);
void vx_music_update(VxMusic *m, AudioSystem *sys, b32 trace, i32 want);
VxMaterial vx_material_of(u8 id);
void vx_audio_break(VxAudio *a, u8 id);
void vx_audio_place(VxAudio *a, u8 id);
void vx_audio_step(VxAudio *a, u8 id);
void vx_audio_splash(VxAudio *a);
void vx_audio_pickup(VxAudio *a);
// mining a block: the material's break sound, quieter and higher
void vx_audio_dig(VxAudio *a, u8 id);
void vx_audio_play(VxAudio *a, VxSound s);
// a sound from something in the world, at a volume its distance gives it
void vx_audio_play_vol(VxAudio *a, VxSound s, f32 volume);
void vx_audio_thunder(VxAudio *a, f32 delay, f32 strength);

// ---- block icons for the hud and inventory ----

// every lane: its share of the isometric block icons, into pixels (B_COUNT icons of size^2 srgb rgba8)
void vx_icons_raster(u8 *pixels, const VxTextureData *tex, u32 size);
void vx_icons_raster_range(u8 *pixels, const VxTextureData *tex, u32 size, u32 first, u32 end);
// contact sheet of every item's icon (desktop only)
b32 vx_icons_dump(const VxTextureData *tex, const char *path);
// main lane: one icon texture per block id from the rasterised pixels (invalid for air)
void vx_icons_upload(GpuTexture *out, u8 *pixels, u32 size);

// ---- settings ----

void vx_settings_defaults(VxSettings *s);
void vx_settings_apply_preset(VxSettings *s, VxPreset preset);
VxRenderSettings vx_settings_render(const VxSettings *s);

// ---- game ----

typedef enum {
  VX_MODE_LOADING,
  VX_MODE_MENU,
  VX_MODE_PLAYING,
  VX_MODE_PAUSED,
  VX_MODE_INVENTORY,
  VX_MODE_SETTINGS,
  VX_MODE_ERROR,
  VX_MODE_WARDROBE,
} VxMode;

typedef enum {
  VX_BOOT_START,
  VX_BOOT_GENERATING,
  VX_BOOT_DONE,
} VxBoot;

// ---- liquids: a budgeted cellular automaton over the active cells ----

#define VX_LIQ_RING_CAP (1u << 15)
#define VX_LIQ_SET_CAP (1u << 16)
#define VX_LIQ_HZ 20.0f
#define VX_LIQ_BUDGET 512

typedef struct {
  i32 x, y, z;
} VxLiqCell;

typedef struct {
  u64 *set; // active cells, so a cell is queued once
  u32 set_cap;
  VxLiqCell *ring;
  u32 head, count;
  u32 tick;
  u32 changes, reactions, dropped; // running totals, for logs
} VxLiquids;

void vx_liquids_init(VxLiquids *l, Allocator *alloc);
void vx_liquids_clear(VxLiquids *l);
void vx_liquids_activate(VxLiquids *l, i32 x, i32 y, i32 z);
void vx_liquids_touch(VxLiquids *l, const VxWorld *w, i32 x, i32 y, i32 z);
void vx_liquids_step(VxLiquids *l, VxWorld *w, u32 budget);

#define VX_PERF_SAMPLES 150
#define VX_TOAST_MAX 96
#define VX_SETTINGS_SLIDERS 9
#define VX_UI_BUTTONS 16

typedef enum {
  VX_SLIDER_RENDER_DISTANCE,
  VX_SLIDER_RENDER_SCALE,
  VX_SLIDER_SHADOW_DISTANCE,
  VX_SLIDER_FOV,
  VX_SLIDER_SENSITIVITY,
  VX_SLIDER_VOLUME,
  VX_SLIDER_DAY_LENGTH,
  VX_SLIDER_CLOUD_COVERAGE,
  VX_SLIDER_TIME_OF_DAY,
} VxSliderId;

typedef enum {
  VX_DROPDOWN_PRESET,
  VX_DROPDOWN_SHADOW_RES,
  VX_DROPDOWN_CLOUDS,
  VX_DROPDOWN_WEATHER,
  VX_DROPDOWN_LIGHTING,
  VX_DROPDOWN_CHARACTER, // one per VxPuppetPart from here
  VX_DROPDOWN_COUNT = VX_DROPDOWN_CHARACTER + VX_PP_COUNT,
} VxDropdownId;

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  AssetSystem *assets;
  UISystem *ui;
  InputSystem input;

  VxTextureData tex;
  VxEditStore edits;
  VxMeshStore meshes;
  VxWorld world;
  VxRenderer renderer;
  VxEntities ents;
  VxDynLight dyn;
  VxAudio audio;
  GpuTexture *icons; // per item id
  u8 *icon_pixels;   // app_init only: rasterised icons awaiting upload, on main's temp arena
  GpuTexture scrim;  // screen backdrop vignette
  b32 world_created;

  u32 seed;

  // game
  VxSettings settings;
  VxPlayer player;
  VxVitals vitals;
  VxCreatures mobs;
  VxCombat combat;
  VxV3d spawn_pos;
  GpuTexture *hud_icons;
  VxCamera cam;
  b32 smart_cursor;
  VxMode mode;
  VxBoot boot;
  VxMode settings_return;
  f32 day_time, time_scale, elapsed;
  VxInventory inv;
  VxMap map;
  b32 show_map;
  char *events; // VX_EVENTS lines of VX_EVENT_LEN, oldest first
  f32 event_t[VX_EVENTS];
  u32 event_rgb[VX_EVENTS]; // a line's colour as 0xRRGGBB, 0 for the usual white
  u32 event_count;
  VxChest *chests; // contents of every chest opened so far, by position
  u32 chest_count;
  i32 open_chest; // index into chests, -1 when none is open
  u32 talk_seed;  // the townsperson being talked to, by seed; 0 when none
  u32 talk_line, talk_count;
  b32 night_hint; // the Guide's first-night warning has been given
  b32 boss_defeated, boss_was_night; // the Eye of Cthulhu is beaten; it was night last frame, for the dusk roll
  b32 empress_defeated;
  f32 boss_pending;                  // seconds until the boss arrives after its dusk warning, 0 when not coming
  u8 boss_pending_kind;
  i32 song, song_next;               // the music playing, and a context waiting out its dwell
  f32 song_next_t;
  VxTileHit tile_hits[VX_TILE_HITS];
  u32 tile_hit_count;
  f32 use_cd, wing_beat;
  VxWorldText texts[VX_MAX_WORLD_TEXT];
  u32 text_count;
  b32 forced_double, forced_hook;
  f32 forced_wings;
  VxRayHit target;
  b32 show_debug, hide_hud, screenshot_requested;
  f32 eye_sky;
  f32 fps, fps_time;
  u32 fps_frames;
  f32 debug_timer;
  char *toast;
  f32 toast_t;
  b32 started;
  b32 played;                   // left the title screen once; until then the menu is the title flyover
  f32 title_t, title_y;         // flyover clock and the smoothed camera height
  VxV3d view_pos;               // the camera the frame is drawn from, when it is not the play camera (title, cutaway)
  f32 view_yaw, view_pitch;
  VxCut cut;
  VxJuice juice;
  b32 spawn_ready; // the player's column and the start radius are meshed
  u32 ready_frames;
  // streaming budget inputs (main lane)
  u64 frame_t0;         // start of this frame's update
  f32 frame_period_ms;  // frame interval while not cpu-bound: the cadence of vsync, the browser or the gpu
  f32 frame_cpu_ms;     // last frame: update start to the end of its job phase
  f32 main_ms;          // main lane's update + render time
  f32 gpu_ms;           // whole-frame gpu time, smoothed; 0 where gpu timers are unavailable
  VxLiquids liquids;
  f32 liq_timer, liq_log_timer;
  u32 liq_log_changes;
  f32 step_dist;
  b32 was_in_water;
  f32 rain, wetness, snow, snow_cover, flash;
  f32 flash_timer, climate_timer, coldness, rain_target, weather_timer;
  u8 biome; // where the player is, refreshed with the climate
  // the look follows the player: smoothed biome weights, how far underground and into the underworld
  f32 biome_w[VX_BIOME_COUNT];
  f32 underground, underworld;
  f32 lava_heat;
  u32 day; // days passed, for the moon's phase
  // the falling star crossing the sky (progress < 0: none) and the wait until the next one
  v3 star_start, star_heading;
  f32 star_progress, star_timer;
  f32 star_forced; // test tooling: seconds until a star falls in front of the camera
  f32 landstar_timer, landstar_forced; // stars that land in the world at night; forced is test tooling
  u32 flash_pulses;
  b32 weather_frozen;
  Random rng;
  b32 auto_quality;
  f32 *perf_samples;
  u32 perf_count;
  f32 cpu_ms, cpu_max, cpu_accum, cpu_peak;
  u32 cpu_frames;
  b32 has_pending_above;
  b32 has_pending_cave;
  f32 pending_cave_top; // test tooling: find an unlit cave pocket below this y near the camera
  u8 pending_cave_floor; // when set, the pocket's floor must be this block
  f32 pending_cave_open;  // blocks of open air the pocket needs ahead of the camera
  u8 pending_find;        // test tooling: view the nearest loaded block of this id
  u32 pending_stage;     // test tooling: 1 places coloured lights around the camera once it lands, 2 torches only
  u32 trace_uploads;     // frames left to log light volume uploads (after a scripted edit)
  f32 pending_above;
  char *error_text;
  char *debug_text;

  // retained ui state
  UIInteract_Slider *sliders;
  UIInteract_Dropdown *dropdowns;
  UIInteractAnim *button_anims;
  UIInteract_Modal new_world_modal;
  VxItemId inv_hover_item; // under the pointer in the inventory screen, this frame
  i32 inv_hover_recipe;     // likewise a recipe, or -1
  u32 stations;             // a bit per VxStation within reach
  OsCursor cursor;
  b32 test_play; // scripted runs play without grabbing the mouse
  VxTouch touch;
  f32 input_now; // the clock touch start times are on; a still finger sends no events to advance its own
  b32 touch_mode; // the last input was a touch: touch hud, no mouse lock
  b32 scripted;  // scripted runs keep the quality they were given
  b32 has_forced_seed;
  u32 forced_seed;
  u32 scene; // 1-based reference scene being set up, 0 none
  u32 scene_pos; // offset of the scene script's next step
  f32 scene_t;
  b32 scene_resume; // the scene's world load ends in play, not the menu
} VxGame;

void vx_cut_init(VxCut *c, Allocator *alloc);
// turns the Terraria view on (at the player's slab) or off
void vx_cut_set(VxGame *g, b32 on);
// side-on movement: A and D walk the plane, S drops, the player turns to walk and to face the pointer while using
void vx_cut_steer(VxGame *g, VxPlayerInput *in);
// keeps play on the slab, moves the side camera, refreshes the light map and aims the play camera at the pointer
void vx_cut_update(VxGame *g, f32 dt);
// holds the player on the slab after anything this frame moved it
void vx_cut_hold(VxGame *g);
// the tile on the plane under a screen point, which also becomes the pointer the aim follows
VxRayHit vx_cut_pick(VxGame *g, f32 sx, f32 sy);
void vx_shake(VxGame *g, f32 amount);
void vx_hitstop(VxGame *g, f32 seconds);
// advances the juice and returns the simulation's step for this frame
f32 vx_juice_update(VxGame *g, f32 dt);
void vx_juice_view(const VxGame *g, VxV3d *pos, f32 *yaw, f32 *pitch, b32 first);

// before the first play the menu is the title: a golden-hour flyover with the logo
force_inline b32 vx_title_active(const VxGame *g) { return g->started && !g->played && g->mode != VX_MODE_PLAYING; }


void vx_game_toast(VxGame *g, const char *text);
// reference scenes S1-S20 set up inside a running game, input stays live
void vx_scene_start(VxGame *g, u32 n);
void vx_scene_update(VxGame *g, f32 dt);
// the wardrobe's actions: wear or take off an armour piece or accessory, hold a weapon in the selected slot, strip to the bare character
void vx_wardrobe_pick(VxGame *g, VxItemId item);
void vx_wardrobe_clear(VxGame *g);
void vx_game_apply_settings(VxGame *g);
void vx_game_new_world(VxGame *g);
void vx_break_block(VxGame *g, VxRayHit h);
// the held item, on the target: held is the button down, pressed its first frame
void vx_use_item(VxGame *g, b32 held, b32 pressed, VxRayHit h);
void vx_use_update(VxGame *g, f32 dt);
void vx_give_starting_kit(VxGame *g);
void vx_world_text_add(VxGame *g, VxWorldTextKind kind, VxV3d p, VxItemId item, u32 amount);
// a line in the event log, bottom left (and the log when scripted)
void vx_event(VxGame *g, String text);
// an event line in a colour (0xRRGGBB), as Terraria's boss and invasion messages
void vx_event_rgb(VxGame *g, String text, u32 rgb);
// drinks a healing potion from the bag, as Terraria's quick heal; true if one was drunk
b32 vx_quick_heal(VxGame *g);
// right click on a block: doors open and close, chests open; true if the block took the click
b32 vx_interact(VxGame *g, VxRayHit h);
// the chest at a position, created empty on first use; NULL when every chest record is taken
VxChest *vx_chest_at(VxGame *g, i32 x, i32 y, i32 z, b32 create);
void vx_chest_close(VxGame *g);
// moves every stack of the open chest that fits into the bag, as its Loot All button
void vx_chest_loot_all(VxGame *g);

// one playing frame of touch input: finger roles, stick, look, taps and buttons
void vx_touch_update(VxGame *g, f32 dt);
// drops every finger and toggle, for leaving play
void vx_touch_reset(VxTouch *t);
// touch devices are played in landscape; portrait shows a rotate prompt
b32 vx_touch_portrait(const VxGame *g);
b32 vx_touch_stick_active(const VxTouch *t);
void vx_game_lock(VxGame *g);
void vx_game_set_mode(VxGame *g, VxMode m);
b32 vx_vitals_damage(VxGame *g, f32 amount, const char *cause);
void vx_vitals_update(VxGame *g, f32 dt);
void vx_game_select(VxGame *g, u32 slot);
// clock text for a 0..1 day time (0 = 06:00)
String vx_clock_string(f32 day_time, Allocator *alloc);

// ui for every screen plus the hud; main lane, between ui_begin_frame and ui_end_frame
void vx_ui_frame(VxGame *g, f32 dt);

b32 vx_aabb_collides(const VxWorld *w, f64 px, f64 py, f64 pz, f64 half, f64 height);
void vx_creatures_init(VxCreatures *c, Allocator *alloc, u64 seed);
// the name a variant goes by: Blue Slime, Hellbat
const char *vx_creature_name(const VxCreature *m);
// the collision box in blocks; slimes scale with their variant
void vx_creature_size(const VxCreature *m, f32 *half, f32 *height);
// a new creature with its feet at p, lifted clear of blocks; NULL when every slot is taken
VxCreature *vx_creature_spawn(VxCreatures *c, const VxWorld *w, VxCreatureKind kind, u32 variant, VxV3d p);
// behaviour, physics, contact damage to the player and despawning
void vx_creatures_update(VxGame *g, f32 dt);
// a hit from the player: defense, knockback along dir, damage number, death and loot; true if it landed
b32 vx_creature_hurt(VxGame *g, u32 i, f32 damage, v3 dir, f32 knockback, b32 crit, const char *by);
void vx_creatures_build(VxEntities *e, const VxWorld *w, const VxCreatures *c, VxV3d cam, f32 time);
// a townsperson on the ray from o along d, unblocked and near the player, starts or continues a talk; true if one did
b32 vx_creature_talk(VxGame *g, VxV3d o, v3 d);
// a boss (the Eye of Cthulhu or the Empress of Light) flies in at night, or at any hour when forced; NULL when one is already here or it is day
VxCreature *vx_boss_summon(VxGame *g, VxCreatureKind kind, b32 force);
// the living boss's index in the creature list, -1 when there is none
i32 vx_boss_index(const VxGame *g);
// the townsperson being talked to, or NULL
const VxCreature *vx_talk_partner(const VxGame *g);
const char *vx_talk_text(const VxGame *g);
void vx_combat_init(VxCombat *c, Allocator *alloc, u64 seed);
// a sword swing: hits each creature in its arc once over its length
void vx_combat_swing(VxGame *g, const VxItemInfo *weapon, f32 dur);
// a projectile weapon's shot along the camera's aim; false when out of ammo or mana
b32 vx_combat_shoot(VxGame *g, const VxItemInfo *weapon);
// true when the weapon has its arrow or mana; the shot itself leaves at the strike of the draw or flick
b32 vx_combat_can_shoot(const VxGame *g, const VxItemInfo *weapon);
void vx_combat_ready(VxGame *g, const VxItemInfo *weapon, f32 dur);
void vx_combat_update(VxGame *g, f32 dt);
// a boss's projectile that hurts the player (kind VX_PROJ_PRISM or VX_PROJ_LANCE)
void vx_combat_hostile(VxGame *g, VxProjectileKind kind, VxV3d p, v3 v, f32 hue, f32 life, f32 damage);
// a fallen star from the night sky toward a ground point, travelling along heading in the ground plane (zero: any)
void vx_combat_star(VxGame *g, VxV3d target, v3 heading);
// world loot: pot contents, generated chest contents by depth and biome, fallen stars at night
void vx_loot_pot(VxGame *g, i32 x, i32 y, i32 z);
void vx_loot_chest_fill(VxGame *g, VxChest *c, u8 id);
void vx_loot_update(VxGame *g, f32 dt);
void vx_combat_build(VxEntities *e, const VxCombat *c, VxV3d cam);

#endif

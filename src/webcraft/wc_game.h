#ifndef WC_GAME_H
#define WC_GAME_H

#include "webcraft/wc_render.h"
#include "webcraft/wc_save.h"
#include "lib/random.h"
#include "audio.h"
#include "assets/assets.h"
#include "ui/ui.h"

// ---- player ----

typedef struct {
  b32 forward, back, left, right;
  b32 jump, sneak, sprint_key;
  b32 jump_pressed, forward_pressed, fly_pressed;
  f32 look_dx, look_dy;
} WcPlayerInput;

typedef struct {
  WcV3d pos;
  v3 vel;
  f32 yaw, pitch;
  b32 on_ground, flying, in_water, head_in_water, sprinting, sneaking;
  f32 sensitivity;
  f32 last_space, last_w;
  f32 eye_offset;
  f32 bob, bob_amount, fov_boost;
} WcPlayer;

void wc_player_init(WcPlayer *p);
WcV3d wc_player_eye(const WcPlayer *p);
// eye position including subtle view bobbing
WcV3d wc_player_camera(const WcPlayer *p, b32 bobbing);
v3 wc_player_forward(const WcPlayer *p);
void wc_player_look(WcPlayer *p, f32 dx, f32 dy);
void wc_player_update(WcPlayer *p, f32 dt, const WcPlayerInput *in, const WcWorld *w, f32 now);
b32 wc_player_intersects_block(const WcPlayer *p, i32 x, i32 y, i32 z);

typedef struct {
  b32 hit;
  i32 x, y, z;
  i32 nx, ny, nz;
  f32 dist;
  u8 id;
} WcRayHit;

// voxel dda (Amanatides & Woo); liquids are transparent to it
WcRayHit wc_raycast(const WcWorld *w, WcV3d o, v3 d, f32 max_dist);

// ---- dynamic entities: held item, block-break particles, fireflies ----

#define WC_MAX_PARTICLES 600
#define WC_MAX_FIREFLIES 42

typedef struct {
  WcV3d p;
  v3 v;
  f32 life, max_life, size;
  u32 layer;
  f32 u, vv;
  v3 tint;
  u32 cut;
} WcParticle;

typedef struct {
  WcV3d p;
  v3 v;
  f32 phase, life;
} WcFirefly;

typedef struct {
  WcParticle *particles;
  u32 particle_count;
  WcFirefly *fireflies;
  u32 firefly_count;
  f32 *verts;
  u32 vert_count;
  u32 hand_start;
  f32 time;
  f32 swing, swing_t, equip;
  i32 last_item;
  Random rng;
} WcEntities;

void wc_entities_init(WcEntities *e, Allocator *alloc, u64 seed);
void wc_entities_trigger_swing(WcEntities *e);
void wc_entities_spawn_break(WcEntities *e, u8 id, i32 x, i32 y, i32 z);
void wc_entities_update_fireflies(WcEntities *e, f32 dt, const WcWorld *w, WcV3d cam, b32 active);
void wc_entities_update(WcEntities *e, f32 dt, const WcWorld *w);
WcEntityGeometry wc_entities_build(WcEntities *e, const WcWorld *w, WcV3d cam, f32 yaw, f32 pitch, u8 item, f32 bob,
                                   f32 bob_amount, b32 show_hand);

// ---- procedural sound ----

typedef enum {
  WC_MAT_STONE,
  WC_MAT_DIRT,
  WC_MAT_GRASS,
  WC_MAT_WOOD,
  WC_MAT_SAND,
  WC_MAT_GLASS,
  WC_MAT_PLANT,
  WC_MAT_WATER,
  WC_MAT_WOOL,
  WC_MAT_SNOW,
  WC_MAT_COUNT,
} WcMaterial;

#define WC_THUNDER_VARIANTS 3
#define WC_MAX_PENDING_THUNDER 8

typedef struct {
  f32 delay;
  f32 strength;
  u32 variant;
} WcPendingThunder;

typedef struct {
  AudioSystem sys;
  Allocator alloc;
  HzHandleT(AudioSource) *brk;   // per material
  HzHandleT(AudioSource) *place; // per material
  HzHandleT(AudioSource) *step;  // per material
  HzHandleT(AudioSource) splash;
  HzHandleT(AudioSource) rain;
  HzHandleT(AudioSource) *thunder; // WC_THUNDER_VARIANTS
  HzHandleT(AudioVoice) rain_voice;
  f32 rain_gain;
  WcPendingThunder *pending;
  u32 pending_count;
  Random rng;
} WcAudio;

// main lane
void wc_audio_init(WcAudio *a, Allocator *alloc);
void wc_audio_update(WcAudio *a, f32 dt, f32 volume, f32 rain, f32 outdoors);
WcMaterial wc_material_of(u8 id);
void wc_audio_break(WcAudio *a, u8 id);
void wc_audio_place(WcAudio *a, u8 id);
void wc_audio_step(WcAudio *a, u8 id);
void wc_audio_splash(WcAudio *a);
void wc_audio_thunder(WcAudio *a, f32 delay, f32 strength);

// ---- block icons for the hud and inventory ----

// every lane: its share of the isometric block icons, into pixels (B_COUNT icons of size^2 srgb rgba8)
void wc_icons_raster(u8 *pixels, const WcTextureData *tex, u32 size);
// main lane: one icon texture per block id from the rasterised pixels (invalid for air)
void wc_icons_upload(GpuTexture *out, u8 *pixels, u32 size);

// ---- settings and persistence ----

void wc_settings_defaults(WcSettings *s);
void wc_settings_apply_preset(WcSettings *s, WcPreset preset);
WcRenderSettings wc_settings_render(const WcSettings *s);
b32 wc_settings_write(const WcSettings *s, const char *path);
b32 wc_settings_read(const u8 *buf, u32 len, WcSettings *out);

typedef struct {
  u32 seed;
  b32 has_player;
  WcV3d pos;
  f32 yaw, pitch;
  b32 flying;
  f32 day_time;
  const u8 *hotbar;
} WcWorldMeta;

b32 wc_world_save_write(const char *path, const WcWorldMeta *meta, const WcEditStore *edits);
// validates, fills meta and replaces the edit store's contents
b32 wc_world_save_read(const u8 *buf, u32 len, WcWorldMeta *meta, u8 *hotbar_out, WcEditStore *edits);

// ---- game ----

typedef enum {
  WC_MODE_LOADING,
  WC_MODE_MENU,
  WC_MODE_PLAYING,
  WC_MODE_PAUSED,
  WC_MODE_INVENTORY,
  WC_MODE_SETTINGS,
  WC_MODE_ERROR,
} WcMode;

typedef enum {
  WC_BOOT_READ_SAVES,
  WC_BOOT_GENERATING,
  WC_BOOT_DONE,
} WcBoot;

typedef struct {
  i32 x, y, z, d;
} WcWaterCell;

#define WC_WATER_QUEUE_MAX 400
#define WC_PERF_SAMPLES 150
#define WC_TOAST_MAX 96
#define WC_SETTINGS_SLIDERS 9
#define WC_SETTINGS_DROPDOWNS 4
#define WC_UI_BUTTONS 8

typedef enum {
  WC_SLIDER_RENDER_DISTANCE,
  WC_SLIDER_RENDER_SCALE,
  WC_SLIDER_SHADOW_DISTANCE,
  WC_SLIDER_FOV,
  WC_SLIDER_SENSITIVITY,
  WC_SLIDER_VOLUME,
  WC_SLIDER_DAY_LENGTH,
  WC_SLIDER_CLOUD_COVERAGE,
  WC_SLIDER_TIME_OF_DAY,
} WcSliderId;

typedef enum {
  WC_DROPDOWN_PRESET,
  WC_DROPDOWN_SHADOW_RES,
  WC_DROPDOWN_CLOUDS,
  WC_DROPDOWN_WEATHER,
} WcDropdownId;

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  AssetSystem *assets;
  UISystem *ui;
  InputSystem input;

  WcTextureData tex;
  WcEditStore edits;
  WcMeshStore meshes;
  WcWorld world;
  WcRenderer renderer;
  WcEntities ents;
  WcAudio audio;
  GpuTexture *icons; // per block id
  u8 *icon_pixels;   // app_init only: rasterised icons awaiting upload, on main's temp arena
  GpuTexture scrim;  // screen backdrop vignette
  b32 world_created;

  // persistence
  String settings_path, world_path;
  OsFileOp *settings_op, *world_op;
  b32 settings_were_saved;
  u32 seed;
  WcWorldMeta pending_meta;
  b32 has_pending_meta;
  f32 save_debounce;
  f32 meta_timer;

  // game
  WcSettings settings;
  WcPlayer player;
  WcMode mode;
  WcBoot boot;
  WcMode settings_return;
  f32 day_time, time_scale, elapsed;
  u8 *hotbar;
  u32 selected;
  WcRayHit target;
  f32 break_cd, place_cd;
  b32 show_debug, hide_hud, screenshot_requested;
  f32 eye_sky;
  f32 fps, fps_time;
  u32 fps_frames;
  f32 debug_timer;
  char *toast;
  f32 toast_t;
  b32 started;
  b32 spawn_ready; // the player's column and the start radius are meshed
  u32 ready_frames;
  // streaming budget inputs (main lane)
  u64 frame_t0;         // start of this frame's update
  f32 frame_period_ms;  // frame interval while not cpu-bound: the cadence of vsync, the browser or the gpu
  f32 frame_cpu_ms;     // last frame: update start to the end of its job phase
  f32 main_ms;          // main lane's update + render time
  f32 gpu_ms;           // whole-frame gpu time, smoothed; 0 where gpu timers are unavailable
  WcWaterCell *water_q;
  u32 water_count;
  f32 water_timer;
  f32 step_dist;
  b32 was_in_water;
  f32 rain, wetness, snow, snow_cover, flash;
  f32 flash_timer, climate_timer, coldness, rain_target, weather_timer;
  u32 flash_pulses;
  b32 weather_frozen;
  Random rng;
  b32 auto_quality;
  f32 *perf_samples;
  u32 perf_count;
  f32 cpu_ms, cpu_max, cpu_accum, cpu_peak;
  u32 cpu_frames;
  b32 has_pending_above;
  f32 pending_above;
  char *error_text;
  char *debug_text;

  // retained ui state
  UIInteract_Slider *sliders;
  UIInteract_Dropdown *dropdowns;
  UIInteractAnim *button_anims;
  UIInteract_Modal new_world_modal;
  u32 inv_hover;
  OsCursor cursor;
  b32 test_play; // scripted runs play without grabbing the mouse
  b32 no_save;   // scripted worlds never overwrite the player's save
  b32 has_forced_seed;
  u32 forced_seed;
} WcGame;

void wc_game_toast(WcGame *g, const char *text);
void wc_game_apply_settings(WcGame *g, b32 save);
void wc_game_new_world(WcGame *g);
void wc_game_lock(WcGame *g);
void wc_game_set_mode(WcGame *g, WcMode m);
void wc_game_select(WcGame *g, u32 slot);
// clock text for a 0..1 day time (0 = 06:00)
String wc_clock_string(f32 day_time, Allocator *alloc);

// ui for every screen plus the hud; main lane, between ui_begin_frame and ui_end_frame
void wc_ui_frame(WcGame *g, f32 dt);

#endif

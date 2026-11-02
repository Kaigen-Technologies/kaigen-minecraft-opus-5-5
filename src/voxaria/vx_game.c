#include "voxaria/vx_game.h"
#include "app/app.h"
#include "input.h"
#include "lib/string_builder.h"
#include "hz_engine.h"
#include "hz_command_registry.h"
#include "generated/types_app.gen.h"

const HzAppConfig hz_app_config = {
    .window_title = "Voxaria",
    .window_width = 1280,
    .window_height = 720,
    // uncapped: the stats panel shows the real frame rate (the browser build stays at the display's refresh)
    .vsync = HZ_VSYNC_OFF,
    .target_frame_time_ms = -1,
};

#define VX_ICON_SIZE 64

// ---- helpers used by the ui ----

void vx_game_toast(VxGame *g, const char *text) {
  u32 n = cstr_len(text);
  if (n >= VX_TOAST_MAX) n = VX_TOAST_MAX - 1;
  mem_cpy(g->toast, text, n);
  g->toast[n] = 0;
  g->toast_t = 0;
  if (g->scripted) log_info("toast: %", fmt_cstr(text));
}

String vx_clock_string(f32 day_time, Allocator *alloc) {
  f32 hours = m_fmodf(day_time * 24 + 6, 24);
  u32 h = (u32)hours;
  u32 m = (u32)((hours - (f32)h) * 60);
  return str_format(alloc, "%%:%%", fmt_u32(h / 10), fmt_u32(h % 10), fmt_u32(m / 10), fmt_u32(m % 10));
}

// leaving the bag puts the held stack back, or drops what does not fit
hz_internal void vx_game_close_inventory(VxGame *g) {
  vx_chest_close(g);
  u32 left = vx_inventory_stow_cursor(&g->inv);
  if (left) {
    VxV3d p = g->player.pos;
    vx_entities_spawn_drop(&g->ents, (VxV3d){p.x, p.y + 1.5, p.z}, g->inv.cursor.item, left);
    g->inv.cursor = (VxStack){0};
  }
}

void vx_game_set_mode(VxGame *g, VxMode m) {
  if (g->mode == VX_MODE_INVENTORY && m != VX_MODE_INVENTORY) vx_game_close_inventory(g);
  g->mode = m;
  if (m != VX_MODE_PLAYING && os_is_mouse_locked()) os_lock_mouse(false);
}

void vx_game_lock(VxGame *g) {
  if (!g->started) return;
  g->played = true;
  // the Terraria view plays with a free pointer
  if (!g->test_play && !g->touch_mode && !g->cut.on) os_lock_mouse(true);
  if (g->mode == VX_MODE_INVENTORY) vx_game_close_inventory(g);
  g->mode = VX_MODE_PLAYING;
}

void vx_game_select(VxGame *g, u32 slot) {
  g->inv.selected = slot;
  VxItemId item = g->inv.slot[slot].item;
  if (item) vx_game_toast(g, vx_item(item)->name);
}

void vx_game_apply_settings(VxGame *g) {
  g->renderer.settings = vx_settings_render(&g->settings);
  g->world.render_distance = (i32)g->settings.render_distance;
  g->player.sensitivity = 0.0022f * g->settings.sensitivity;
  vx_render_invalidate_history(&g->renderer);
}

hz_internal void vx_stage_lights(VxGame *g, b32 torches_only);
hz_internal void vx_view_find(VxGame *g, u8 id);
hz_internal void vx_update_falling_star(VxGame *g, f32 dt);
hz_internal void vx_launch_falling_star(VxGame *g, f32 yaw);

// resets the player and camera state for the current seed
hz_internal void vx_start_world(VxGame *g) {
  // a new world starts unexplored
  if (g->map.px) vx_map_clear(&g->map);
  g->event_count = 0;
  if (!g->world_created) {
    vx_world_init(&g->world, g->seed, &g->edits, &g->meshes);
    g->world_created = true;
  } else {
    vx_world_reset(&g->world, g->seed);
  }
  g->world.render_distance = (i32)g->settings.render_distance;
  f32 sens = g->player.sensitivity;
  vx_player_init(&g->player);
  g->player.sensitivity = sens;
  v3 sp = vx_find_spawn(&g->world.terrain);
  g->player.pos = (VxV3d){sp.x, sp.y + 0.01, sp.z};
  g->player.fall_peak = g->player.pos.y;
  g->spawn_pos = g->player.pos;
  vx_vitals_reset(&g->vitals);
  g->player.yaw = PI * 0.75f;
  g->player.pitch = -0.05f;
  vx_camera_init(&g->cam);
  log_info("voxaria: world seed %", fmt_u32(g->seed));
  vx_liquids_clear(&g->liquids);
  g->mobs.count = 0;
  g->talk_seed = 0;
  g->talk_count = 0;
  g->night_hint = false;
  g->boss_defeated = false;
  g->empress_defeated = false;
  g->boss_was_night = false;
  g->boss_pending = 0;
  g->combat.proj_count = 0;
  g->combat.t = 0;
  g->started = false;
  g->spawn_ready = false;
  g->ready_frames = 0;
  g->boot = VX_BOOT_GENERATING;
  vx_game_set_mode(g, VX_MODE_LOADING);
  vx_render_invalidate_history(&g->renderer);
}

void vx_game_new_world(VxGame *g) {
  vx_edits_clear(&g->edits);
  g->seed = random_u32(&g->rng) & 0x7fffffffu;
  vx_give_starting_kit(g);
  g->day_time = 0.08f;
  vx_start_world(g);
}

// first update: settings applied, then the world from the scripted or a random seed
hz_internal void vx_boot_start(VxGame *g) {
  if (g->boot != VX_BOOT_START) return;
  g->auto_quality = !g->scripted;
  vx_game_apply_settings(g);
  g->seed = g->has_forced_seed ? g->forced_seed : random_u32(&g->rng) & 0x7fffffffu;
  vx_start_world(g);
}

// ---- weather ----

hz_internal f32 vx_grand(VxGame *g) { return random_f32(&g->rng); }

hz_internal void vx_update_weather(VxGame *g, f32 dt) {
  if (g->settings.weather == VX_WEATHER_CLEAR) {
    g->rain_target = 0;
  } else if (g->settings.weather == VX_WEATHER_RAIN) {
    g->rain_target = 1;
  } else {
    g->weather_timer -= dt;
    if (g->weather_timer <= 0) {
      g->rain_target = g->rain_target > 0.5f ? 0.0f : 1.0f;
      g->weather_timer = g->rain_target > 0 ? 150 + vx_grand(g) * 240 : 420 + vx_grand(g) * 720;
    }
  }
  // clouds build up first, rain follows
  g->rain += (g->rain_target - g->rain) * (1 - m_expf(-dt / 12));
  // precipitation turns to snow in cold biomes and high up
  g->climate_timer -= dt;
  if (g->climate_timer <= 0 && g->world_created) {
    g->climate_timer = 0.5f;
    VxV3d p = g->player.pos;
    g->biome = vx_biome_at(&g->world.terrain, vx_floor_i(p.x), vx_floor_i(p.y), vx_floor_i(p.z));
    g->coldness = vx_sample_column(&g->world.terrain, vx_floor_i(p.x), vx_floor_i(p.z)).biome == VX_BIOME_SNOW ? 1.0f : 0.0f;
  }
  g->snow += (g->coldness - g->snow) * (1 - m_expf(-dt / 3));
  f32 falling = g->rain > 0.35f ? 1.0f : 0.0f;
  f32 wet_target = falling * (1 - g->snow);
  g->wetness += (wet_target - g->wetness) * (1 - m_expf(-dt / (wet_target > g->wetness ? 18.0f : 70.0f)));
  // thunderstorms: double-flicker lightning then thunder
  if (g->rain > 0.8f && g->snow < 0.5f) {
    g->flash_timer -= dt;
    if (g->flash_timer <= 0) {
      g->flash_timer = 9 + vx_grand(g) * 28;
      g->flash_pulses = 2 + (u32)(vx_grand(g) * 2);
      g->flash = 1;
      f32 dist = 0.4f + vx_grand(g) * 2.6f;
      vx_audio_thunder(&g->audio, dist, m_maxf(0.3f, 1.2f - dist * 0.3f));
      vx_shake(g, m_maxf(0, 0.35f - dist * 0.15f));
    }
  }
  g->flash *= m_expf(-dt * 9);
  if (g->flash < 0.15f && g->flash_pulses > 1 && vx_grand(g) < dt * 12) {
    g->flash = 0.7f + vx_grand(g) * 0.3f;
    g->flash_pulses--;
  }
  f32 cover_target = falling * g->snow;
  g->snow_cover +=
      (cover_target - g->snow_cover) * (1 - m_expf(-dt / (cover_target > g->snow_cover ? 40.0f : 150.0f)));
}

// ---- input ----

force_inline b32 vx_down(const VxGame *g, App_InputButtonType k) { return g->input.buttons[k].is_pressed; }
force_inline b32 vx_pressed(const VxGame *g, App_InputButtonType k) { return g->input.buttons[k].pressed_this_frame; }

hz_internal void vx_handle_input(VxGame *g, f32 dt) {
  g->touch_mode = g->input.last_category == INPUT_CATEGORY_TOUCH;
  if (g->mode != VX_MODE_PLAYING || vx_touch_portrait(g)) vx_touch_reset(&g->touch);
  // reference scenes: stepping on while a scene's world still loads is allowed
  if (g->mode == VX_MODE_PLAYING || (g->mode == VX_MODE_LOADING && g->scene_resume)) {
    if (vx_pressed(g, KEY_PAGE_DOWN)) vx_scene_start(g, g->scene % VX_SCENES + 1);
    if (vx_pressed(g, KEY_PAGE_UP)) vx_scene_start(g, g->scene > 1 ? g->scene - 1 : VX_SCENES);
    if (vx_pressed(g, KEY_HOME) && g->scene) vx_scene_start(g, g->scene);
  }
  // modal keys outside play
  if (g->mode == VX_MODE_INVENTORY) {
    if (vx_pressed(g, KEY_E)) vx_game_lock(g);
    else if (vx_pressed(g, KEY_ESCAPE)) vx_game_set_mode(g, VX_MODE_PAUSED);
    return;
  }
  if (g->mode == VX_MODE_SETTINGS) {
    if (vx_pressed(g, KEY_ESCAPE)) {
      vx_game_apply_settings(g);
      vx_game_set_mode(g, g->settings_return == VX_MODE_MENU ? VX_MODE_MENU : VX_MODE_PAUSED);
    }
    return;
  }
  if (g->mode != VX_MODE_PLAYING) return;
  // the platform can drop the lock on its own (escape in a browser, focus loss); touch never takes it
  if (!g->test_play && !g->touch_mode && !g->cut.on && !os_is_mouse_locked()) {
    vx_game_set_mode(g, VX_MODE_PAUSED);
    return;
  }
  if (vx_pressed(g, KEY_ESCAPE)) {
    vx_game_set_mode(g, VX_MODE_PAUSED);
    return;
  }
  // touch looks with its own fingers; a touch-driven pointer is not a mouse
  if (!g->touch_mode && !g->cut.on) vx_player_look(&g->player, g->input.mouse_delta.x, g->input.mouse_delta.y);
  // 1-9 then 0, as Terraria's ten hotbar keys
  for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++)
    if (vx_pressed(g, (App_InputButtonType)(i < 9 ? KEY_1 + i : KEY_0))) vx_game_select(g, i);
  f32 wheel = g->input.scroll_delta.y;
  if (wheel != 0)
    vx_game_select(g, (g->inv.selected + (wheel < 0 ? 1 : VX_HOTBAR_SLOTS - 1)) % VX_HOTBAR_SLOTS);
  if (vx_pressed(g, KEY_F3)) g->show_debug = !g->show_debug;
  if (vx_pressed(g, KEY_F2)) g->screenshot_requested = true;
  if (vx_pressed(g, KEY_F1)) g->hide_hud = !g->hide_hud;
  if (vx_pressed(g, KEY_V)) g->cam.mode = g->cam.mode == VX_CAM_THIRD ? VX_CAM_FIRST : VX_CAM_THIRD;
  if (vx_pressed(g, KEY_C)) vx_cut_set(g, !g->cut.on);
  if (vx_pressed(g, KEY_LEFT_CONTROL)) g->smart_cursor = !g->smart_cursor;
  if (vx_pressed(g, KEY_M)) g->show_map = !g->show_map;
  if (vx_pressed(g, KEY_H)) vx_quick_heal(g);
  if (vx_pressed(g, KEY_E)) {
    vx_game_set_mode(g, VX_MODE_INVENTORY);
    return;
  }
  g->time_scale = 1;
  if (vx_down(g, KEY_T)) g->time_scale = (vx_down(g, KEY_LEFT_SHIFT) || vx_down(g, KEY_RIGHT_SHIFT)) ? -36.0f : 36.0f;

  // a touch also drives the ui's left button: in touch play the fingers act, not the mouse buttons
  if (g->touch_mode) {
    if (!vx_touch_portrait(g)) vx_touch_update(g, dt);
    return;
  }
  // Terraria's mouse: the left button uses the held item, the middle one picks the hotbar slot holding the target
  vx_use_item(g, vx_down(g, MOUSE_LEFT), vx_pressed(g, MOUSE_LEFT), g->target);
  if (vx_pressed(g, MOUSE_RIGHT)) vx_interact(g, g->target);
  if (vx_pressed(g, MOUSE_MIDDLE) && g->target.hit) {
    for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++)
      if (g->inv.slot[i].item == g->target.id) {
        vx_game_select(g, i);
        break;
      }
  }
}

hz_internal VxPlayerInput vx_player_input(const VxGame *g) {
  if (g->mode != VX_MODE_PLAYING) return (VxPlayerInput){0};
  VxPlayerInput in = {
      .forward = vx_down(g, KEY_W) || vx_down(g, KEY_UP),
      .back = vx_down(g, KEY_S) || vx_down(g, KEY_DOWN),
      .left = vx_down(g, KEY_A) || vx_down(g, KEY_LEFT),
      .right = vx_down(g, KEY_D) || vx_down(g, KEY_RIGHT),
      .jump = vx_down(g, KEY_SPACE),
      .down = vx_down(g, KEY_LEFT_SHIFT) || vx_down(g, KEY_RIGHT_SHIFT),
      .jump_pressed = vx_pressed(g, KEY_SPACE),
      .fly_pressed = vx_pressed(g, KEY_F),
      .hook_pressed = vx_pressed(g, KEY_G),
  };
  if (g->touch_mode) {
    const VxTouch *t = &g->touch;
    in.stick_x = t->stick_x;
    in.stick_z = t->stick_z;
    in.jump |= t->jump_held;
    in.jump_pressed |= t->jump_pressed;
    in.hook_pressed |= t->hook_pressed;
    in.down |= t->sneak_held;
  }
  const VxPlayer *p = &g->player;
  if (g->cut.on) {
    in.aim = g->cam.fwd;
    return in;
  }
  // the hook flies from the hand toward whatever is under the crosshair, so third-person parallax cannot miss
  VxV3d o = vx_camera_ray_start(&g->cam, g->cam.pos, g->cam.fwd, p);
  VxRayHit ah = vx_raycast(&g->world, o, g->cam.fwd, 40.0f);
  f32 t = ah.hit ? ah.dist : 40.0f;
  v3 to = {(f32)(o.x + g->cam.fwd.x * t - p->pos.x), (f32)(o.y + g->cam.fwd.y * t - p->pos.y - 1.6),
           (f32)(o.z + g->cam.fwd.z * t - p->pos.z)};
  in.aim = v3_length(to) > 0.01f ? v3_normalize(to) : g->cam.fwd;
  return in;
}

// after loading, watch the frame rate and step the preset down while it is too slow
hz_internal void vx_auto_tune(VxGame *g, f32 dt) {
  if (!g->auto_quality || !g->started) return;
  if (!vx_world_idle(&g->world)) {
    g->perf_count = 0;
    return;
  }
  g->perf_samples[g->perf_count++] = dt;
  if (g->perf_count < VX_PERF_SAMPLES) return;
  // median by counting: samples are few and this runs once per 150 frames
  f32 median = 0;
  for (u32 i = 0; i < VX_PERF_SAMPLES; i++) {
    u32 below = 0;
    for (u32 j = 0; j < VX_PERF_SAMPLES; j++)
      if (g->perf_samples[j] < g->perf_samples[i]) below++;
    if (below <= VX_PERF_SAMPLES / 2) median = m_maxf(median, g->perf_samples[i]);
  }
  g->perf_count = 0;
  VxPreset p = g->settings.preset;
  if (median > 1.0f / 38.0f && p != VX_PRESET_CUSTOM && p != VX_PRESET_LOW) {
    VxPreset next = (VxPreset)(p - 1);
    vx_settings_apply_preset(&g->settings, next);
    vx_game_apply_settings(g);
    Allocator fa = tctx_temp_allocator(NULL);
    String msg = str_format(&fa, "Graphics set to % for smoother performance",
                            fmt_cstr(next == VX_PRESET_LOW ? "low" : next == VX_PRESET_MEDIUM ? "medium" : "high"));
    vx_game_toast(g, msg.value);
  } else {
    g->auto_quality = false;
  }
}

#define VX_COMPASS_POINTS 8
hz_internal const char *const VX_COMPASS[VX_COMPASS_POINTS] = {"N", "NW", "W", "SW", "S", "SE", "E", "NE"};

hz_internal void vx_update_debug_text(VxGame *g) {
  const VxPlayer *p = &g->player;
  i32 bx = vx_floor_i(p->pos.x), by = vx_floor_i(p->pos.y), bz = vx_floor_i(p->pos.z);
  u8 biome = vx_biome_at(&g->world.terrain, bx, by, bz);
  u16 l = vx_world_light(&g->world, bx, vx_floor_i(p->pos.y + p->eye_offset), bz);
  f32 yaw_deg = m_fmodf(p->yaw * 180.0f / PI, 360.0f);
  if (yaw_deg < 0) yaw_deg += 360;
  const char *facing = VX_COMPASS[((u32)(yaw_deg / 45.0f + 0.5f)) % VX_COMPASS_POINTS];
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  StringBuilder sb = sb_create(1024, &tmp.allocator);
  sb_append_format(&sb, "Voxaria  % fps  cpu % ms (max %)\n", fmt_u32((u32)(g->fps + 0.5f)), fmt_f32_p(g->cpu_ms, 1),
                   fmt_f32_p(g->cpu_max, 1));
  sb_append_format(&sb, "XYZ: % / % / %\n", fmt_f64_p(p->pos.x, 2), fmt_f64_p(p->pos.y, 2), fmt_f64_p(p->pos.z, 2));
  sb_append_format(&sb, "Block: % % %  Chunk: % %\n", fmt_i32(bx), fmt_i32(by), fmt_i32(bz), fmt_i32(bx >> 4),
                   fmt_i32(bz >> 4));
  sb_append_format(&sb, "Facing: % (%\xc2\xb0)  pitch %\xc2\xb0\n", fmt_cstr(facing), fmt_u32((u32)yaw_deg),
                   fmt_i32((i32)(p->pitch * 180.0f / PI)));
  sb_append_format(&sb, "Biome: %  Light: sky % rgb % % %\n", fmt_cstr(vx_biome_name[biome]),
                   fmt_u32(l >> VX_LIGHT_SKY_SHIFT), fmt_u32(l & 15), fmt_u32((l >> 4) & 15), fmt_u32((l >> 8) & 15));
  String clock = vx_clock_string(g->day_time, &tmp.allocator);
  sb_append_format(&sb, "Time: %  %%  Rain: %%\n", fmt_str(clock),
                   fmt_cstr(p->flying ? "Flying" : p->on_ground ? "Ground" : "Air"), fmt_cstr(p->in_water ? " Water" : ""),
                   fmt_u32((u32)(g->rain * 100 + 0.5f)), fmt_char('%'));
  sb_append_format(&sb, "Chunks: %  sections drawn: %  pending gen % mesh %\n", fmt_u32(g->world.stats.chunks),
                   fmt_u32(g->renderer.stats.sections), fmt_u32(g->world.stats.pending_gen),
                   fmt_u32(g->world.stats.pending_mesh));
  sb_append_format(&sb, "Draw calls: % + shadow %  VRAM(mesh): % MB\n", fmt_u32(g->renderer.stats.draw_calls),
                   fmt_u32(g->renderer.stats.shadow_draw_calls),
                   fmt_f32_p((f32)(g->meshes.quads_used * VX_QUAD_BYTES) / 1048576.0f, 1));
  sb_append_format(&sb, "Seed: %", fmt_u32(g->seed));
  if (g->target.hit)
    sb_append_format(&sb, "\nTarget: % @ % % %", fmt_cstr(vx_block_label[g->target.id]), fmt_i32(g->target.x),
                     fmt_i32(g->target.y), fmt_i32(g->target.z));
  char *s = sb_get(&sb);
  u32 len = cstr_len(s);
  u32 n = len < 1023 ? len : 1023;
  mem_cpy(g->debug_text, s, n);
  g->debug_text[n] = 0;
  tctx_temp_allocator_end(tmp);
}

// ---- frame ----

#define VX_LOAD_BUDGET_MS 40.0f    // loading screen: the spawn area streams in one or two frames
#define VX_READY_BUDGET_MS 4.0f    // spawn meshed: keep the last loading frames short
#define VX_BURST_BUDGET_MS 20.0f   // the player's own column is missing
#define VX_BACKLOG_BUDGET_MS 12.0f // most of the view distance is missing, as after a teleport
#define VX_BACKLOG_PROGRESS 0.9f

// main lane, first thing in a frame: interval of frames that were not cpu-bound estimates the frame cadence
hz_internal void vx_frame_begin(VxGame *g) {
  u64 now = os_time_now();
  if (g->frame_t0) {
    f32 dt = (f32)os_ticks_to_ms(os_time_diff(now, g->frame_t0));
    if (g->frame_cpu_ms < dt * 0.75f) g->frame_period_ms += (dt - g->frame_period_ms) * 0.1f;
  }
  g->frame_t0 = now;
}

// job-phase length: bursts while the player's surroundings are missing, otherwise the frame's idle time
hz_internal f32 vx_stream_budget(VxGame *g) {
  VxPlayer *p = &g->player;
  f32 idle = m_clampf(g->frame_period_ms * 0.8f - g->main_ms, 1.5f, 12.0f);
  if (!g->started) return g->spawn_ready ? m_minf(idle, VX_READY_BUDGET_MS) : VX_LOAD_BUDGET_MS;
  if (!vx_world_is_ready(&g->world, p->pos.x, p->pos.z)) return VX_BURST_BUDGET_MS;
  if (vx_world_load_progress(&g->world, g->world.render_distance) < VX_BACKLOG_PROGRESS)
    return m_maxf(idle, VX_BACKLOG_BUDGET_MS);
  return idle;
}

// highest block of any kind in a column, leaves included
hz_internal i32 vx_title_top(const VxWorld *w, i32 x, i32 z) {
  i32 y = VX_HEIGHT - 1;
  while (y > 0 && vx_world_block(w, x, y, z) == B_AIR) y--;
  return y;
}

// high over the pylon avenue's far end, swaying slowly as it looks down the avenue into the town; round the spawn without a town
hz_internal void vx_title_camera(VxGame *g, f32 dt) {
  f32 radius = 34, speed = 0.03f, lead = 18, up = 24;
  g->title_t += dt;
  f32 a = 2.4f + g->title_t * speed;
  VxV3d c = g->spawn_pos;
  f64 ax, ay, az, half;
  if (vx_town_avenue(&g->world.terrain, &ax, &ay, &az, &half)) {
    c = (VxV3d){ax, ay, az};
    radius = 10.0f, lead = 16, up = 20;
    a = 0.12f * m_sinf(g->title_t * 0.05f);
  }
  f64 cx = c.x + m_cosf(a) * radius, cz = c.z + m_sinf(a) * radius;
  // every column within a crown's width of the camera, a few seconds ahead on the path too, so no canopy closes over it
  i32 top = (i32)c.y;
  for (u32 k = 0; k < 3; k++) {
    f32 ak = a + (f32)k * speed * 3.0f;
    i32 px = vx_floor_i(c.x + m_cosf(ak) * radius), pz = vx_floor_i(c.z + m_sinf(ak) * radius);
    for (i32 dz = -6; dz <= 6; dz += 2)
      for (i32 dx = -6; dx <= 6; dx += 2) top = MAX(top, vx_title_top(&g->world, px + dx, pz + dz));
  }
  f32 want = m_maxf((f32)top + 8.0f, (f32)c.y + up);
  if (g->title_y <= 0) g->title_y = want;
  g->title_y += (want - g->title_y) * (1 - m_expf(-dt * (want > g->title_y ? 2.0f : 0.25f)));
  // aim past the centre, so the ground near the camera sits in the lower third under the logo
  f64 tx = c.x - m_cosf(a) * lead, tz = c.z - m_sinf(a) * lead, ty = c.y;
  v3 d = v3_normalize((v3){(f32)(tx - cx), (f32)(ty - g->title_y), (f32)(tz - cz)});
  g->view_yaw = m_atan2f(-d.x, -d.z);
  g->view_pitch = m_asinf(d.y);
  g->cam.pos = g->view_pos = (VxV3d){cx, g->title_y, cz};
  g->cam.fwd = d;
}

hz_internal void vx_game_update(VxGame *g, AppMemory *memory) {
  u64 cpu0 = os_time_now();
  f32 dt = m_clampf(memory->dt, 0.0001f, 0.1f);
  g->elapsed += dt;
  input_update(&g->input, &memory->input_events, memory->total_time);
  g->input_now = memory->total_time;
  g->fps_frames++;
  g->fps_time += dt;
  if (g->fps_time >= 0.5f) {
    g->fps = g->fps_frames / g->fps_time;
    g->fps_frames = 0;
    g->fps_time = 0;
  }
  vx_boot_start(g);
  if (!g->world_created) return;

  VxPlayer *p = &g->player;
  vx_mesh_store_begin_frame(&g->meshes);
  vx_world_integrate(&g->world);
  // before the budget is chosen: the frame that finds the spawn area meshed must not run a full loading round
  if (!g->started) {
    f32 prog = vx_world_load_progress(&g->world, (i32)m_minf(4.0f, (f32)g->settings.render_distance));
    g->spawn_ready = vx_world_is_ready(&g->world, p->pos.x, p->pos.z) && prog >= 0.98f;
  }
  b32 near_first = g->started ? !vx_world_is_ready(&g->world, p->pos.x, p->pos.z) : !g->spawn_ready;
  vx_world_update(&g->world, p->pos.x, p->pos.z, vx_stream_budget(g), near_first);

  if (!g->started) {
    if (g->spawn_ready && vx_render_ready(&g->renderer) && ++g->ready_frames > 2) {
      g->started = true;
      g->boot = VX_BOOT_DONE;
      // make sure we are not stuck inside terrain
      i32 bx = vx_floor_i(p->pos.x), bz = vx_floor_i(p->pos.z);
      i32 y = vx_floor_i(p->pos.y);
      if (y < 1) y = 1;
      while (y < VX_HEIGHT - 2 &&
             (vx_solid[vx_world_block(&g->world, bx, y, bz)] || vx_solid[vx_world_block(&g->world, bx, y + 1, bz)]))
        y++;
      p->pos.y = y + 0.001;
      p->fall_peak = p->pos.y;
      g->spawn_pos = p->pos;
      if (g->mode == VX_MODE_LOADING && g->scene_resume) vx_game_lock(g);
      else if (g->mode == VX_MODE_LOADING) vx_game_set_mode(g, g->test_play ? VX_MODE_PLAYING : VX_MODE_MENU);
      g->scene_resume = false;
    }
  }
  if (g->has_pending_above && vx_world_is_ready(&g->world, p->pos.x, p->pos.z)) {
    // test tooling: place the camera a given height above the surface
    i32 bx = vx_floor_i(p->pos.x), bz = vx_floor_i(p->pos.z);
    i32 y = VX_HEIGHT - 1;
    while (y > 0 && !vx_solid[vx_world_block(&g->world, bx, y, bz)] && vx_world_block(&g->world, bx, y, bz) != B_WATER)
      y--;
    p->pos.y = y + 1 + g->pending_above;
    p->fall_peak = p->pos.y;
    g->has_pending_above = false;
    vx_render_invalidate_history(&g->renderer);
  }

  // test tooling: nearest sky-dark pocket below pending_cave_top, once all of reach is lit (platform-independent)
  i32 cave_reach = g->pending_cave_floor || g->pending_cave_open > 0 ? 176 : 48;
  i32 cave_chunks = MIN(cave_reach / 16 + 1, g->world.render_distance - 1);
  if (g->has_pending_cave && vx_world_load_progress(&g->world, cave_chunks) >= 1.0f && vx_world_idle(&g->world)) {
    i32 cx = vx_floor_i(p->pos.x), cz = vx_floor_i(p->pos.z);
    b32 found = false;
    i32 reach = MIN(cave_reach, (cave_chunks - 1) * 16);
    for (i32 r = 0; r <= reach && !found; r++) {
      for (i32 dz = -r; dz <= r && !found; dz++) {
        for (i32 dx = -r; dx <= r && !found; dx++) {
          if (MAX(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != r) continue;
          i32 x = cx + dx, z = cz + dz;
          if (!vx_world_is_ready(&g->world, x + 0.5f, z + 0.5f)) continue;
          // a band under the requested height: a deeper layer below an unlucky column must not win
          i32 y_top = (i32)g->pending_cave_top, y_low = MAX(y_top - 32, 4);
          for (i32 y = y_top; y > y_low && !found; y--) {
            u8 below = vx_world_block(&g->world, x, y - 1, z);
            if (!vx_solid[below] || (g->pending_cave_floor && below != g->pending_cave_floor)) continue;
            b32 open = true;
            for (i32 k = 0; k < 3; k++) {
              u8 b = vx_world_block(&g->world, x, y + k, z);
              if (b != B_AIR || (vx_world_light(&g->world, x, y + k, z) >> VX_LIGHT_SKY_SHIFT) != 0) open = false;
            }
            // open=<n>: the view along the camera's yaw stays clear for n blocks at eye height
            f64 fx = -m_sin(p->yaw), fz = -m_cos(p->yaw);
            for (f64 t = 1; open && t <= g->pending_cave_open; t += 0.5)
              if (vx_solid[vx_world_block(&g->world, vx_floor_i(x + 0.5 + fx * t), y + 1, vx_floor_i(z + 0.5 + fz * t))] ||
                  vx_solid[vx_world_block(&g->world, vx_floor_i(x + 0.5 + fx * t), y, vx_floor_i(z + 0.5 + fz * t))])
                open = false;
            if (!open) continue;
            p->pos = (VxV3d){x + 0.5, y + 0.001, z + 0.5};
            p->fall_peak = p->pos.y;
            found = true;
            log_info("vx_view cave: pocket at % % %", fmt_i32(x), fmt_i32(y), fmt_i32(z));
          }
        }
      }
    }
    if (!found) log_warn("vx_view cave: no pocket found");
    g->has_pending_cave = false;
    vx_render_invalidate_history(&g->renderer);
  }
  if (g->pending_find && !g->has_pending_cave && !g->has_pending_above &&
      vx_world_load_progress(&g->world, 6) >= 1.0f) {
    vx_view_find(g, g->pending_find);
    g->pending_find = 0;
    vx_render_invalidate_history(&g->renderer);
  }
  if (g->pending_stage && !g->has_pending_cave && !g->has_pending_above &&
      vx_world_is_ready(&g->world, p->pos.x, p->pos.z)) {
    vx_stage_lights(g, g->pending_stage == 2);
    g->pending_stage = 0;
  }

  vx_scene_update(g, dt);
  vx_handle_input(g, dt);
  if (g->mode == VX_MODE_PLAYING || g->mode == VX_MODE_MENU) vx_auto_tune(g, dt);
  // a hit-stop holds bodies, swings and particles; the clock, weather and interface run on
  f32 sdt = vx_juice_update(g, dt);
  if (g->started && !g->vitals.dead && (vx_world_is_ready(&g->world, p->pos.x, p->pos.z) || p->flying)) {
    VxPlayerInput pin = vx_player_input(g);
    if (g->cut.on) vx_cut_steer(g, &pin);
    vx_player_update(p, sdt, &pin, &g->world, g->elapsed);
  } else {
    p->events = 0;
  }
  if (g->started) {
    vx_vitals_update(g, dt);
    vx_use_update(g, dt);
    vx_creatures_update(g, sdt);
    vx_combat_update(g, sdt);
    vx_cut_hold(g);
    vx_loot_update(g, dt);
    vx_map_update(&g->map, &g->world, g->player.pos);
  }
  // liquids step at a fixed rate; a slow frame catches up at most three steps
  g->liq_timer = m_minf(g->liq_timer + dt, 3.0f / VX_LIQ_HZ);
  while (g->liq_timer >= 1.0f / VX_LIQ_HZ) {
    g->liq_timer -= 1.0f / VX_LIQ_HZ;
    u32 before = g->liquids.changes;
    b32 was_active = g->liquids.count > 0;
    vx_liquids_step(&g->liquids, &g->world, VX_LIQ_BUDGET);
    g->liq_log_changes += g->liquids.changes - before;
    if (g->scripted && was_active && g->liquids.count == 0)
      log_info("liquids: settled (% changes, % reactions in all)", fmt_u32(g->liquids.changes),
               fmt_u32(g->liquids.reactions));
  }
  g->liq_log_timer += dt;
  if (g->scripted && g->liq_log_timer > 1.0f) {
    if (g->liq_log_changes)
      log_info("liquids: % active cells, % changes in the last second", fmt_u32(g->liquids.count),
               fmt_u32(g->liq_log_changes));
    g->liq_log_timer = 0;
    g->liq_log_changes = 0;
  }
  vx_update_weather(g, dt);
  // footsteps and splashes
  if (g->started && p->on_ground && !p->flying) {
    g->step_dist += m_sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z) * dt;
    if (g->step_dist > 2.4f) {
      g->step_dist = 0;
      u8 ground = vx_world_block(&g->world, vx_floor_i(p->pos.x), vx_floor_i(p->pos.y - 0.1), vx_floor_i(p->pos.z));
      vx_audio_step(&g->audio, ground);
      if (p->vel.x * p->vel.x + p->vel.z * p->vel.z > 16.0f) vx_entities_step_puff(&g->ents, p->pos, p->vel, g->map.colors[ground]);
    }
  }
  if (p->in_water && !g->was_in_water && m_absf(p->vel.y) > 2) {
    vx_audio_splash(&g->audio);
    vx_entities_splash(&g->ents, (VxV3d){p->pos.x, m_floor(p->pos.y) + 0.9, p->pos.z}, m_absf(p->vel.y));
  }
  g->was_in_water = p->in_water;
  // day cycle
  vx_update_falling_star(g, dt);
  f32 next_time = g->day_time + dt * g->time_scale / (g->settings.day_length * 60.0f);
  if (next_time >= 1.0f) g->day++;
  g->day_time = m_fmodf(next_time + 1.0f, 1.0f);

  VxV3d eye = vx_player_eye(p);
  vx_camera_update(&g->cam, p, &g->world, dt, g->settings.bobbing);
  if (vx_title_active(g)) vx_title_camera(g, dt);
  if (g->cut.on) {
    vx_cut_update(g, dt);
  } else {
    if (g->mode != VX_MODE_PLAYING) g->target = (VxRayHit){0};
    else if (g->touch_mode) g->target = g->touch.target;
    else g->target = vx_camera_target(&g->cam, g->cam.pos, g->cam.fwd, p, &g->world, VX_REACH, g->smart_cursor);
    if (g->mode == VX_MODE_PLAYING && !g->touch_mode) g->target.dir = g->cam.fwd;
  }
  VxV3d cpos = g->cam.pos;
  // the camera's surroundings drive exposure, fog and the underwater look
  u16 el = vx_world_light(&g->world, vx_floor_i(cpos.x), vx_floor_i(cpos.y), vx_floor_i(cpos.z));
  g->eye_sky += ((f32)(el >> VX_LIGHT_SKY_SHIFT) / 15.0f - g->eye_sky) * (1 - m_expf(-2 * dt));
  f32 look_k = 1 - m_expf(-dt / 1.5f);
  for (u32 b = 0; b < VX_BIOME_COUNT; b++) g->biome_w[b] += ((b == g->biome ? 1.0f : 0.0f) - g->biome_w[b]) * look_k;
  f32 deep = m_clampf((f32)(VX_UNDERGROUND_Y - cpos.y) / 24.0f, 0, 1);
  g->underground += (m_maxf(1 - g->eye_sky, deep) - g->underground) * look_k;
  g->underworld += (m_clampf((f32)(VX_CAVERN_Y - 8 - cpos.y) / 20.0f, 0, 1) - g->underworld) * look_k;

  vx_entities_update(&g->ents, sdt, &g->world);
  vx_puppet_animate(&g->ents.puppet, p, g->inv.slot[g->inv.selected].item, g->ents.swing_t, sdt);
  if (p->events & VX_PEV_DOUBLE_JUMP) vx_entities_cloud_puff(&g->ents, p->pos);
  // movement sounds
  g->audio.trace = g->scripted;
  if (g->started) {
    VxAudio *au = &g->audio;
    if (p->events & VX_PEV_DOUBLE_JUMP) vx_audio_play(au, VX_SND_DOUBLE_JUMP);
    else if (p->events & VX_PEV_JUMP) vx_audio_play(au, VX_SND_JUMP);
    if ((p->events & VX_PEV_LAND) && p->landed_fall > 1.5f) vx_audio_play(au, VX_SND_LAND);
    if (p->events & VX_PEV_HOOK_FIRE) vx_audio_play(au, VX_SND_HOOK_FIRE);
    if (p->events & VX_PEV_HOOK_HIT) vx_audio_play(au, VX_SND_HOOK_LATCH);
    // a beat of the wings every 0.3 s while they lift
    g->wing_beat = p->flapping ? g->wing_beat - dt : 0;
    if (p->flapping && g->wing_beat <= 0) {
      vx_audio_play(au, VX_SND_WINGS);
      g->wing_beat = 0.3f;
    }
  }
  if (g->scripted && (p->events & VX_PEV_HOOK_FIRE))
    log_info("player: hook fired toward % % %", fmt_f32_p(p->hook_vel.x, 1), fmt_f32_p(p->hook_vel.y, 1),
             fmt_f32_p(p->hook_vel.z, 1));
  if (g->scripted && (p->events & (VX_PEV_JUMP | VX_PEV_DOUBLE_JUMP | VX_PEV_LAND | VX_PEV_HOOK_HIT | VX_PEV_STEP_UP)))
    log_info("player: %%%%%% at % % % (fell %)", fmt_cstr(p->events & VX_PEV_JUMP ? "jump " : ""),
             fmt_cstr(p->events & VX_PEV_COYOTE ? "(coyote) " : ""), fmt_cstr(p->events & VX_PEV_STEP_UP ? "step-up " : ""),
             fmt_cstr(p->events & VX_PEV_DOUBLE_JUMP ? "double-jump " : ""),
             fmt_cstr(p->events & VX_PEV_LAND ? "land " : ""), fmt_cstr(p->events & VX_PEV_HOOK_HIT ? "hook " : ""),
             fmt_f64_p(p->pos.x, 1), fmt_f64_p(p->pos.y, 1), fmt_f64_p(p->pos.z, 1), fmt_f32_p(p->landed_fall, 1));
  // a held torch lights the way, and so does a mining helmet's lamp
  VxItemId hi = g->inv.slot[g->inv.selected].item;
  u8 held = g->started && !g->vitals.dead && vx_item_is_block(hi) ? (u8)hi : B_AIR;
  if (held == B_AIR && g->started && !g->vitals.dead && g->inv.armor[0].item == I_MINING_HELMET) held = B_TORCH;
  vx_dynlight_update(&g->dyn, &g->world, eye, held);
  b32 warm_night = g->day_time > 0.53f && g->day_time < 0.97f && g->rain < 0.2f && g->snow < 0.2f;
  vx_entities_update_fireflies(&g->ents, dt, &g->world, cpos, warm_night && g->started);
  VxMoteEnv menv = {.cam = cpos, .biome = g->biome, .underground = g->underground,
                    .underworld = g->underworld, .active = g->started};
  vx_entities_update_motes(&g->ents, dt, &g->world, &menv);
  g->lava_heat += (vx_entities_lava_heat(&g->ents, menv.cam) - g->lava_heat) * (1 - m_expf(-dt / 0.8f));

  if (g->toast_t < 10.0f) g->toast_t += dt;
  g->debug_timer -= dt;
  if (g->show_debug && g->debug_timer <= 0) {
    g->debug_timer = 0.25f;
    vx_update_debug_text(g);
  }
  {
    TempAllocator gt = tctx_temp_allocator_begin(NULL);
    f32 *groups = ALLOC_ARRAY(&gt.allocator, f32, VX_GPU_GROUP_COUNT);
    f32 gpu_frame = 0;
    if (vx_render_gpu_times(&g->renderer, groups, &gpu_frame) && gpu_frame > 0)
      g->gpu_ms = g->gpu_ms > 0 ? g->gpu_ms + (gpu_frame - g->gpu_ms) * 0.1f : gpu_frame;
    tctx_temp_allocator_end(gt);
  }
  f32 cpu = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), cpu0));
  g->cpu_accum += cpu;
  g->cpu_peak = m_maxf(g->cpu_peak, cpu);
  if (++g->cpu_frames >= 60) {
    g->cpu_ms = g->cpu_accum / g->cpu_frames;
    g->cpu_max = g->cpu_peak;
    g->cpu_accum = 0;
    g->cpu_peak = 0;
    g->cpu_frames = 0;
  }
}

hz_internal void vx_game_render(VxGame *g, AppMemory *memory) {
  f32 dt = m_clampf(memory->dt, 0.0001f, 0.1f);
  VxPlayer *p = &g->player;
  b32 title = vx_title_active(g), view = title || g->cut.on;
  VxV3d cam = view ? g->view_pos : g->cam.pos;
  b32 first = g->cam.mode == VX_CAM_FIRST && !g->cut.on;
  f32 cam_yaw = view ? g->view_yaw : p->yaw + (first ? 0 : g->cam.orbit), cam_pitch = view ? g->view_pitch : p->pitch;
  if (!title) vx_juice_view(g, &cam, &cam_yaw, &cam_pitch, first);
  VxEntityGeometry ents = {0};
  VxPuppetColors pc = vx_puppet_colors(&g->settings);
  vx_puppet_armor(&pc, g->inv.armor);
  vx_puppet_hurt_tint(&pc, g->vitals.hurt_flash * 0.65f);
  // too close behind the head, the puppet would fill the view
  b32 puppet_view = g->cut.on || (!first && g->cam.cur_dist > 1.3f);
  VxEntityView ev = {.player = g->started && !g->vitals.dead && !title ? p : NULL, .colors = &pc, .first_person = !puppet_view,
                     .show_hand = first && !g->hide_hud && g->started && !g->vitals.dead && !title,
                     .item = g->inv.slot[g->inv.selected].item, .cracks = g->tile_hits, .crack_count = g->tile_hit_count,
                     .yaw = p->yaw, .pitch = p->pitch, .bob = p->bob,
                     .bob_amount = g->settings.bobbing ? p->bob_amount : 0, .creatures = &g->mobs, .combat = &g->combat};
  if (g->world_created) ents = vx_entities_build(&g->ents, &g->world, cam, &ev);
  u8 cam_block = vx_world_block(&g->world, vx_floor_i(cam.x), vx_floor_i(cam.y), vx_floor_i(cam.z));
  VxFrameInput in = {
      .cam = cam,
      .yaw = cam_yaw,
      .pitch = cam_pitch,
      .fov = g->cut.on ? VX_CUT_FOV : g->settings.fov * (1 + p->fov_boost),
      .time = g->elapsed,
      .day_time = title ? 0.56f : g->day_time,
      .dt = dt,
      .underwater = first || g->cut.on ? p->head_in_water : cam_block == B_WATER,
      .eye_sky = g->eye_sky,
      .has_selection = g->target.hit && !g->hide_hud,
      .sel = {g->target.x, g->target.y, g->target.z},
      .cloud_coverage = g->settings.cloud_coverage,
      .moon_phase = (f32)(g->day % 8) / 8.0f,
      .star_start = g->star_start,
      .star_heading = g->star_heading,
      .star_progress = g->star_progress,
      .underground = g->underground,
      .underworld = g->underworld,
      .lava_heat = g->lava_heat,
      .rain = g->rain,
      .wetness = g->wetness,
      .snow = g->snow,
      .snow_cover = g->snow_cover,
      .flash = g->flash,
      .crosshair = g->mode == VX_MODE_PLAYING && !g->hide_hud && !g->touch_mode && !g->cut.on,
      .dpr = memory->dpr,
      .cut = g->cut.on,
      .cut_plane = (f32)(g->cut.z + 1),
      .cut_x0 = g->cut.light_x0,
      .cut_light = g->cut.light,
      .cut_version = g->cut.light_version,
  };
  for (u32 b = 0; b < VX_BIOME_COUNT; b++) in.biome_w[b] = g->biome_w[b];
  if (g->dyn.lit) {
    in.dyn_light = g->dyn.texels;
    in.dyn_origin = g->dyn.origin;
    in.dyn_version = g->dyn.version;
  }
  // fireflies carry a small unshadowed glow each
  TempAllocator lights_tmp = tctx_temp_allocator_begin(NULL);
  VxPointLight *points = ALLOC_ARRAY_NO_ZERO(&lights_tmp.allocator, VxPointLight, VX_MAX_POINT_LIGHTS);
  u32 np = 0;
  // the Empress lights the sky round her, first so no firefly crowds her out
  i32 bi = vx_boss_index(g);
  if (bi >= 0 && g->mobs.items[bi].kind == VX_CR_EMPRESS) {
    const VxCreature *m = &g->mobs.items[bi];
    points[np++] = (VxPointLight){.pos = {m->pos.x, m->pos.y + 4.5, m->pos.z}, .radius = 28.0f, .color = {2.6f, 2.1f, 2.4f}};
  }
  for (u32 i = 0; i < g->ents.firefly_count && np < VX_MAX_POINT_LIGHTS; i++) {
    const VxFirefly *f = &g->ents.fireflies[i];
    f32 glow = m_maxf(0, m_sinf(g->ents.time * 1.7f + f->phase * 3.1f));
    points[np++] = (VxPointLight){.pos = f->p, .radius = 3.5f, .color = {0.35f * glow, 0.6f * glow, 0.12f * glow}};
  }
  // a spark in flight lights its surroundings
  for (u32 i = 0; i < g->combat.proj_count && np < VX_MAX_POINT_LIGHTS; i++) {
    const VxProjectile *pr = &g->combat.proj[i];
    if (pr->kind == VX_PROJ_SPARK) points[np++] = (VxPointLight){.pos = pr->p, .radius = 6.0f, .color = {1.6f, 0.7f, 0.18f}};
    if (pr->kind == VX_PROJ_STAR) points[np++] = (VxPointLight){.pos = pr->p, .radius = 12.0f, .color = {2.4f, 1.9f, 0.7f}};
    if (pr->kind == VX_PROJ_LANCE) points[np++] = (VxPointLight){.pos = pr->p, .radius = 9.0f, .color = {0.8f, 2.0f, 1.8f}};
  }
  // a fallen star on the ground glows until it is taken or dawn takes it
  for (u32 i = 0; i < g->ents.drop_count && np < VX_MAX_POINT_LIGHTS; i++)
    if (g->ents.drops[i].item == I_FALLEN_STAR)
      points[np++] = (VxPointLight){.pos = {g->ents.drops[i].p.x, g->ents.drops[i].p.y + 0.4, g->ents.drops[i].p.z}, .radius = 6.0f,
                                    .color = {1.3f, 1.05f, 0.4f}};
  in.points = points;
  in.point_count = np;
  // the swapchain is the logical canvas at the effective dpr
  vx_render_frame(&g->renderer, &g->world, &in, &ents, (u32)(memory->canvas_width * memory->dpr),
                  (u32)(memory->canvas_height * memory->dpr));
  tctx_temp_allocator_end(lights_tmp);
  if (g->world_created) vx_world_clear_mesh_changes(&g->world);
  if (g->trace_uploads) {
    g->trace_uploads--;
    log_info("light volume: % sections uploaded (% uniform)", fmt_u32(g->renderer.vol.uploaded_sections),
             fmt_u32(g->renderer.vol.uploaded_uniform));
  }
  // ambience: the surface by time of day, caves and the underworld by depth, surf by the sea; rain hushes the birds and crickets
  VxAmbienceMix mix = {0};
  if (g->started) {
    f32 t = g->day_time, night = m_saturatef((t - 0.5f) / 0.05f) * m_saturatef((0.99f - t) / 0.05f);
    f32 surface = (1 - g->underground) * (1 - g->underworld), sea = surface * g->biome_w[VX_BIOME_OCEAN];
    f32 land = (surface - 0.7f * sea) * (1 - 0.75f * g->rain);
    mix.w[VX_AMB_DAY] = land * (1 - night);
    mix.w[VX_AMB_NIGHT] = land * night;
    mix.w[VX_AMB_CAVE] = g->underground * (1 - g->underworld);
    mix.w[VX_AMB_UNDERWORLD] = g->underworld;
    mix.w[VX_AMB_OCEAN] = sea;
  }
  // music by context: the title before play, the boss over everything, then depth, then the hour
  i32 song = VX_SONG_TITLE;
  if (g->started && g->played && g->mode != VX_MODE_MENU) {
    song = vx_boss_index(g) >= 0  ? VX_SONG_BOSS
         : g->underworld > 0.5f  ? VX_SONG_UNDERWORLD
         : g->underground > 0.5f ? VX_SONG_UNDERGROUND
         : vx_is_night(g->day_time) ? VX_SONG_NIGHT
                                    : VX_SONG_DAY;
  }
  // a context must hold 0.75 s before the music follows, so a depth passing through a band does not start a song; the boss cuts in
  if (song != g->song_next) g->song_next_t = 0;
  g->song_next = song;
  g->song_next_t += dt;
  if (song == VX_SONG_BOSS || song == VX_SONG_TITLE || g->song_next_t > 0.75f) g->song = song;
  vx_audio_update(&g->audio, dt, g->settings.volume, g->rain, g->eye_sky, &mix, g->song);

  ui_begin_frame(g->ui, &g->input, dt);
  vx_ui_frame(g, dt);
  vx_render_timer_begin(&g->renderer, VX_GPU_UI);
  ui_end_frame(g->ui);
  vx_render_timer_end(&g->renderer);
  os_set_cursor(g->mode == VX_MODE_PLAYING ? OS_CURSOR_ARROW : g->cursor);

  if (g->screenshot_requested) {
    g->screenshot_requested = false;
    Allocator fa = tctx_temp_allocator(NULL);
    String path = str_format(&fa, "voxaria-%.png", fmt_u64(os_time_unix_ms()));
    if (hz_screenshot_request(path.value)) vx_game_toast(g, "Screenshot saved");
  }
}

// ---- test commands (--commands) ----

hz_internal b32 vx_arg_f32(CString args, const char *key, f32 *out) {
  u32 klen = cstr_len(key);
  for (const char *s = args; s && *s; s++) {
    if ((s == args || s[-1] == ' ' || s[-1] == ',') && str_equal(STR((char *)s, klen), STR((char *)key, klen)) &&
        s[klen] == '=') {
      String v = STR((char *)s + klen + 1, 0);
      while (v.value[v.len] && v.value[v.len] != ' ' && v.value[v.len] != ',') v.len++;
      f64 d = 0;
      if (!str_to_f64(v, &d)) return false;
      *out = (f32)d;
      return true;
    }
  }
  return false;
}

// the value of key=<word> in a vx_view argument string
hz_internal b32 vx_arg_word(CString args, const char *key, String *out) {
  u32 klen = cstr_len(key);
  for (const char *s = args; s && *s; s++) {
    if ((s == args || s[-1] == ' ' || s[-1] == ',') && str_equal(STR((char *)s, klen), STR((char *)key, klen)) &&
        s[klen] == '=') {
      String v = STR((char *)s + klen + 1, 0);
      while (v.value[v.len] && v.value[v.len] != ' ' && v.value[v.len] != ',') v.len++;
      *out = v;
      return true;
    }
  }
  return false;
}

// an item by its display name, case-insensitive, with _ for spaces; items win a name shared with a block (Life Crystal)
hz_internal VxItemId vx_item_by_name(String name) {
  for (u32 k = 1; k < I_COUNT; k++) {
    u32 id = k <= I_COUNT - B_COUNT ? k + B_COUNT - 1 : k - (I_COUNT - B_COUNT);
    const char *l = vx_item(id)->name;
    if (!l) continue;
    u32 i = 0;
    for (; i < name.len && l[i]; i++) {
      char a = name.value[i], b = l[i] == ' ' ? '_' : l[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b) break;
    }
    if (i == name.len && l[i] == 0) return (VxItemId)id;
  }
  return 0;
}

// a block by its label with spaces written as underscores, any case ("life_crystal")
hz_internal u8 vx_block_by_name(String name) {
  for (u32 id = 1; id < B_COUNT; id++) {
    const char *l = vx_block_label[id];
    u32 i = 0;
    for (; i < name.len && l[i]; i++) {
      char a = name.value[i], b = l[i] == ' ' ? '_' : l[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b) break;
    }
    if (i == name.len && !l[i]) return (u8)id;
  }
  return B_AIR;
}

// test tooling: the nearest loaded block of an id, seen from a few blocks away with a clear line of sight
hz_internal void vx_view_find(VxGame *g, u8 id) {
  VxPlayer *p = &g->player;
  const VxWorld *w = &g->world;
  i32 cx = vx_floor_i(p->pos.x), cz = vx_floor_i(p->pos.z);
  for (i32 r = 0; r <= 112; r++)
    for (i32 dz = -r; dz <= r; dz++)
      for (i32 dx = -r; dx <= r; dx++) {
        if (MAX(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != r) continue;
        i32 x = cx + dx, z = cz + dz;
        if (!vx_world_chunk(w, x >> 4, z >> 4)) continue;
        for (i32 y = 1; y < VX_HEIGHT - 3; y++) {
          if (vx_world_block(w, x, y, z) != id) continue;
          for (i32 dist = 3; dist <= 5; dist++)
            for (i32 a = 0; a < 8; a++)
              for (i32 dy = 0; dy <= 2; dy++) {
                f32 ang = (f32)a * PI * 0.25f;
                i32 fx = x + (i32)m_roundf(m_cosf(ang) * (f32)dist), fz = z + (i32)m_roundf(m_sinf(ang) * (f32)dist);
                i32 fy = y - 1 + dy;
                if (vx_solid[vx_world_block(w, fx, fy, fz)] || vx_solid[vx_world_block(w, fx, fy + 1, fz)] ||
                    vx_world_block(w, fx, fy, fz) != B_AIR)
                  continue;
                VxV3d eye = {fx + 0.5, fy + 1.62, fz + 0.5};
                v3 d = {(f32)(x + 0.5 - eye.x), (f32)(y + 0.5 - eye.y), (f32)(z + 0.5 - eye.z)};
                f32 len = v3_length(d);
                VxRayHit h = vx_raycast(w, eye, v3_scale(d, 1.0f / len), len + 1);
                if (!h.hit || h.x != x || h.y != y || h.z != z) continue;
                p->pos = (VxV3d){fx + 0.5, fy + 0.001, fz + 0.5};
                p->fall_peak = p->pos.y;
                p->yaw = m_atan2f(-d.x, -d.z);
                p->pitch = m_asinf(d.y / len);
                log_info("vx_view find: % at % % %", fmt_cstr(vx_block_label[id]), fmt_i32(x), fmt_i32(y), fmt_i32(z));
                return;
              }
        }
      }
  log_warn("vx_view find: no visible % nearby", fmt_cstr(vx_block_label[id]));
}

// look-dev shots (TERRARIA_3D_PLAN.md §4); positions are for the default look-dev seed
typedef struct VxPostcard {
  char *name;
  char *args;
} VxPostcard;

hz_internal const VxPostcard vx_postcards[] = {
    {"P1", "x=27.5 z=15.5 above=4 yaw=0.8 pitch=-0.1 time=0.25 rain=0 fly=1"},
    {"P2", "x=27.5 z=15.5 above=4 yaw=0.8 pitch=-0.1 time=0.53 rain=0 fly=1"},
    {"P3", "x=8.5 z=8.5 cave=100 yaw=0.6 pitch=-0.1 time=0.25 rain=0 fly=1 stage=1"},
    {"P4", "x=0.5 z=0.5 site=4 cave=88 mushroom=1 open=24 yaw=0.6 pitch=0.05 time=0.25 rain=0 fly=1"},
    {"P5", "x=0.5 z=0.5 site=3 yaw=1.57 pitch=-0.05 time=0.25 rain=0 fly=1"},
    {"P6", "x=8.5 z=8.5 above=1.2 yaw=3.99 pitch=0.62 time=0.68 day=3 fallstar=5.8 rain=0 fly=1"},
    {"P7", "x=0.5 z=0.5 site=2 yaw=1.57 pitch=-0.3 back=22 time=0.3 rain=0 fly=1"},
    {"P8", "x=300.5 z=0.5 above=4 yaw=4.71 pitch=-0.12 time=0.46 rain=0 fly=1"},
    {"P9", "biome=2 above=6 yaw=0.8 pitch=0.1 time=0.75 rain=0 fly=1"},
    {"P10", "biome=3 above=26 yaw=0.8 pitch=-0.3 time=0.3 rain=0 fly=1"},
    // the fight postcards: tools/postcards.sh adds the spawns, the skybridge and the swing before the shot
    {"P11", "x=27.5 z=15.5 above=0 time=0.545 rain=0 fly=0 cam=3"},
    {"P12", "x=27.5 z=15.5 above=0 yaw=0.8 pitch=-0.22 time=0.62 rain=0 fly=0 cam=3 dist=9 orbit=0.7"},
    {"P13", "x=27.5 z=15.5 above=0 yaw=0.8 pitch=-0.18 time=0.3 rain=0 fly=0 cam=3 dist=6 orbit=0.7"},
};

hz_internal void vx_view_apply(VxGame *g, CString args);

// vx_view:<postcard> | x=.. y=.. z=.. yaw=.. pitch=.. time=.. fly=1 debug=n rain=.. snow=.. flash=.. above=.. scale=..
hz_internal void vx_cmd_view(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String a = cstr_to_str(ctx->args);
  for (u32 i = 0; i < ARRAY_SIZE(vx_postcards); i++) {
    if (str_equal(a, cstr_to_str(vx_postcards[i].name))) {
      vx_view_apply(g, vx_postcards[i].args);
      return;
    }
  }
  vx_view_apply(g, ctx->args);
}

// vx_shaft:r=<radius> bottom=<y>: dig a wide shaft from the surface under the player down to bottom, as a player digs to hell
hz_internal void vx_cmd_shaft(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  VxWorld *w = &g->world;
  const VxSimplex *n = w->terrain.n;
  f32 r = 5.0f, bottom = 30.0f;
  vx_arg_f32(ctx->args, "r", &r);
  vx_arg_f32(ctx->args, "bottom", &bottom);
  i32 cx = vx_floor_i(g->player.pos.x), cz = vx_floor_i(g->player.pos.z);
  i32 top = VX_HEIGHT - 2;
  while (top > (i32)bottom && vx_solid[vx_world_block(w, cx, top, cz)] != 1) top--;
  i32 reach = (i32)r + 6, dug = 0;
  for (i32 y = top + 1; y >= (i32)bottom; y--) {
    // the walls step in and out as the digger's aim wandered, and the mouth flares
    f64 ry = r + vx_noise2(&n[VX_NOISE_DETAIL], y * 0.15, 7.3) * 0.9 + MAX(0, 5 - (top - y)) * 0.7;
    for (i32 dz = -reach; dz <= reach; dz++)
      for (i32 dx = -reach; dx <= reach; dx++) {
        f64 a = m_atan2((f64)dz, (f64)dx);
        f64 rr = ry * (1.0 + vx_noise2(&n[VX_NOISE_PATCH], m_cos(a) * 1.3 + y * 0.13, m_sin(a) * 1.3) * 0.07);
        if (dx * dx + dz * dz > rr * rr || vx_world_block(w, cx + dx, y, cz + dz) == B_AIR) continue;
        dug += vx_world_set_block(w, cx + dx, y, cz + dz, B_AIR, false) ? 1 : 0;
      }
  }
  log_info("vx_shaft: % blocks from % down to %", fmt_i32(dug), fmt_i32(top), fmt_i32((i32)bottom));
}

// vx_time:<0..1>, 0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight
hz_internal void vx_cmd_time(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f64 v = 0;
  if (!cstr_to_f64(ctx->args, &v)) return;
  g->scripted = true;
  g->day_time = m_fmodf((f32)v, 1.0f);
  vx_render_invalidate_history(&g->renderer);
}

hz_internal void vx_view_apply(VxGame *g, CString args) {
  VxPlayer *p = &g->player;
  g->scripted = true;
  f32 v;
  if (vx_arg_f32(args, "x", &v)) p->pos.x = v;
  if (vx_arg_f32(args, "y", &v)) p->pos.y = v;
  if (vx_arg_f32(args, "z", &v)) p->pos.z = v;
  if (vx_arg_f32(args, "yaw", &v)) p->yaw = p->body_yaw = v;
  if (vx_arg_f32(args, "pitch", &v)) p->pitch = v;
  if (vx_arg_f32(args, "time", &v)) g->day_time = v;
  if (vx_arg_f32(args, "fly", &v)) p->flying = v > 0.5f;
  p->fall_peak = p->pos.y;
  // a teleport lets go of the hook and keeps no momentum
  p->hook_state = VX_HOOK_NONE;
  p->vel = (v3){0, 0, 0};
  // postcards are composed from the eye; cam=3 views from behind the player, cam=1 forces the eye
  if (vx_arg_f32(args, "cam", &v)) g->cam.mode = v > 2 ? VX_CAM_THIRD : VX_CAM_FIRST;
  else if (vx_arg_f32(args, "yaw", &v)) g->cam.mode = VX_CAM_FIRST;
  g->cam.orbit = vx_arg_f32(args, "orbit", &v) ? v : 0;
  g->cam.dist = vx_arg_f32(args, "dist", &v) ? m_clampf(v, 2.0f, 12.0f) : 6.0f;
  g->cam.cur_dist = g->cam.dist;
  if (vx_arg_f32(args, "scale", &v)) {
    g->settings.render_scale = m_clampf(v, 0.5f, 1.0f);
    g->auto_quality = false;
    vx_game_apply_settings(g);
  }
  if (vx_arg_f32(args, "debug", &v)) g->renderer.debug_view = (u32)v;
  if (vx_arg_f32(args, "cave", &v)) {
    g->pending_cave_top = v;
    g->has_pending_cave = true;
    g->pending_cave_floor = vx_arg_f32(args, "mushroom", &v) && v > 0.5f ? B_MUSHROOM_GRASS : 0;
    g->pending_cave_open = vx_arg_f32(args, "open", &v) ? v : 0;
  }
  if (vx_arg_f32(args, "stage", &v)) g->pending_stage = (u32)(v + 0.5f);
  // probe=1: log the block under the crosshair
  if (vx_arg_f32(args, "probe", &v)) {
    VxRayHit h = vx_raycast(&g->world, vx_player_eye(p), vx_player_forward(p), 200);
    if (h.hit) log_info("vx_view probe: % at % % %", fmt_cstr(vx_block_label[h.id]), fmt_i32(h.x), fmt_i32(h.y), fmt_i32(h.z));
    else log_info("vx_view probe: nothing");
  }
  String word;
  if (vx_arg_word(args, "find", &word)) {
    g->pending_find = vx_block_by_name(word);
    if (!g->pending_find) log_warn("vx_view find: unknown block %", fmt_str(word));
  }
  // site=<kind>: stand at the nearest generated structure of a kind (VxSiteKind)
  if (vx_arg_f32(args, "site", &v)) {
    i32 sx, sy, sz;
    if (vx_find_site(&g->world.terrain, (u32)v, vx_floor_i(p->pos.x), vx_floor_i(p->pos.z), &sx, &sy, &sz)) {
      p->pos = (VxV3d){sx + 0.5, sy + 0.001, sz + 0.5};
      p->fall_peak = p->pos.y;
      log_info("vx_view site: % at % % %", fmt_u32((u32)v), fmt_i32(sx), fmt_i32(sy), fmt_i32(sz));
      // back=<n>: step back from the spot along the view and up a little, to frame the structure
      f32 back;
      if (vx_arg_f32(args, "back", &back)) {
        p->pos.x += m_sinf(p->yaw) * back;
        p->pos.z += m_cosf(p->yaw) * back;
        p->pos.y += back * 0.35f;
      }
    } else {
      log_warn("vx_view site: none of kind % nearby", fmt_u32((u32)v));
    }
  }
  // landmark=<i>: frame desert landmark i from dist blocks out along its radial direction turned by around, up above its base
  if (vx_arg_f32(args, "landmark", &v)) {
    const VxTerrain *T = &g->world.terrain;
    u32 i = (u32)v;
    if (i < T->landmark_count) {
      const VxLandmark *l = &T->landmarks[i];
      f32 dist = 60, up = 40, around = 0;
      vx_arg_f32(args, "dist", &dist);
      vx_arg_f32(args, "up", &up);
      vx_arg_f32(args, "around", &around);
      f32 a = m_atan2f(l->z, l->x) + around;
      p->pos = (VxV3d){l->x + m_cosf(a) * dist, (f64)(l->base + up), l->z + m_sinf(a) * dist};
      f32 dx = l->x - (f32)p->pos.x, dz = l->z - (f32)p->pos.z;
      p->yaw = p->body_yaw = m_atan2f(-dx, -dz);
      if (!vx_arg_f32(args, "cam", &v)) g->cam.mode = VX_CAM_FIRST;
      if (!vx_arg_f32(args, "pitch", &p->pitch))
        p->pitch = m_atan2f((f32)(l->base + l->h2 * 0.5f) - (f32)p->pos.y, m_sqrtf(dx * dx + dz * dz));
      p->fall_peak = p->pos.y;
      log_info("vx_view landmark: % kind % at % % base %", fmt_u32(i), fmt_u32(l->kind), fmt_f32(l->x), fmt_f32(l->z),
               fmt_i32(l->base));
    } else {
      log_warn("vx_view landmark: % of %", fmt_u32(i), fmt_u32(T->landmark_count));
    }
  }
  if (vx_arg_f32(args, "day", &v)) g->day = (u32)v;
  if (vx_arg_f32(args, "light", &v)) {
    g->settings.lighting = (VxLightMode)((u32)v % 4);
    vx_game_apply_settings(g);
  }
  // fallstar=<seconds>: a star falls in front of the camera that much later
  if (vx_arg_f32(args, "fallstar", &v)) g->star_forced = v > 0 ? v : 0.001f;
  // landstar=<seconds>: a fallen star lands a few blocks ahead of the camera that much later
  if (vx_arg_f32(args, "landstar", &v)) g->landstar_forced = v > 0 ? v : 0.001f;
  if (vx_arg_f32(args, "biome", &v)) {
    // the nearest surface column of that biome, away from its borders, searched in rings of 8 blocks
    u8 want = (u8)v;
    i32 x0 = vx_floor_i(p->pos.x), z0 = vx_floor_i(p->pos.z);
    b32 found = false;
    for (i32 r = 0; r <= 64 && !found; r++)
      for (i32 dz = -r; dz <= r && !found; dz++)
        for (i32 dx = -r; dx <= r && !found; dx++) {
          if (MAX(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != r) continue;
          i32 x = x0 + dx * 8, z = z0 + dz * 8;
          b32 inside = true;
          for (i32 k = 0; k < 4 && inside; k++)
            inside = vx_sample_column(&g->world.terrain, x + (k & 1 ? 24 : -24), z + (k & 2 ? 24 : -24)).biome == want;
          if (!inside || vx_sample_column(&g->world.terrain, x, z).biome != want) continue;
          p->pos.x = x + 0.5;
          p->pos.z = z + 0.5;
          found = true;
        }
    if (!found) log_warn("vx_view biome: none within reach");
  }
  if (vx_arg_f32(args, "above", &v)) {
    g->pending_above = v;
    g->has_pending_above = true;
  }
  if (vx_arg_f32(args, "snow", &v)) {
    g->snow = v;
    g->coldness = v;
    g->climate_timer = 1e9f;
  }
  if (vx_arg_f32(args, "flash", &v)) {
    g->flash = v;
    g->flash_pulses = 0;
  }
  if (vx_arg_f32(args, "rain", &v)) {
    g->flash_timer = 1e9f;
    g->rain = v;
    g->rain_target = v;
    g->wetness = v * (1 - g->snow);
    g->snow_cover = v * g->snow;
    g->settings.weather = v > 0.5f ? VX_WEATHER_RAIN : VX_WEATHER_CLEAR;
  }
  p->vel = (v3){0, 0, 0};
  vx_render_invalidate_history(&g->renderer);
}

// vx_seed:<n>: fresh world from a fixed seed
hz_internal void vx_cmd_seed(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f64 v = 0;
  if (!cstr_to_f64(ctx->args, &v)) return;
  g->scripted = true;
  g->auto_quality = false;
  g->forced_seed = (u32)v;
  g->has_forced_seed = true;
  if (g->boot == VX_BOOT_START) return;
  vx_edits_clear(&g->edits);
  vx_give_starting_kit(g);
  g->seed = g->forced_seed;
  vx_start_world(g);
}

// vx_hud:0|1
hz_internal void vx_cmd_hud(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  g->hide_hud = ctx->args && ctx->args[0] == '0';
}

// launches a falling star from bearing yaw (radians), high in the sky, heading across and down
hz_internal void vx_launch_falling_star(VxGame *g, f32 yaw) {
  f32 el = 0.45f + vx_grand(g) * 0.3f;
  g->star_start = (v3){-m_sinf(yaw) * m_cosf(el), m_sinf(el), -m_cosf(yaw) * m_cosf(el)};
  v3 side = v3_normalize(v3_cross(g->star_start, (v3){0, 1, 0}));
  if (vx_grand(g) < 0.5f) side = v3_scale(side, -1);
  g->star_heading = v3_normalize(v3_add(side, (v3){0, -0.45f, 0}));
  g->star_progress = 0;
}

// at night a star falls every few seconds, each crossing the sky in a little over a second
hz_internal void vx_update_falling_star(VxGame *g, f32 dt) {
  if (g->star_forced > 0) {
    g->star_forced -= dt;
    if (g->star_forced <= 0) vx_launch_falling_star(g, g->player.yaw);
  }
  if (g->star_progress >= 0) {
    g->star_progress += dt / 1.2f;
    if (g->star_progress >= 1) g->star_progress = -1;
  }
  b32 night = g->day_time > 0.56f && g->day_time < 0.94f;
  g->star_timer -= dt;
  if (g->star_timer <= 0 && g->star_progress < 0 && night) vx_launch_falling_star(g, vx_grand(g) * 6.2831853f);
  if (g->star_timer <= 0) g->star_timer = 6 + vx_grand(g) * 10;
}

// test tooling: differently coloured lights on the floor around the camera, for look-dev shots
typedef struct {
  f32 yaw, dist;
  u8 id;
} VxStageLight;

hz_internal const VxStageLight vx_stage_set[] = {
    {-0.6f, 5.0f, B_TORCH},         {0.6f, 6.0f, B_ICE_TORCH},       {0.0f, 9.0f, B_TORCH},
    {-0.25f, 3.5f, B_GLOW_MUSHROOM}, {0.3f, 3.0f, B_LIFE_CRYSTAL},
};

hz_internal void vx_stage_lights(VxGame *g, b32 torches_only) {
  VxV3d eye = vx_player_eye(&g->player);
  i32 ey = vx_floor_i(eye.y);
  for (u32 i = 0; i < ARRAY_SIZE(vx_stage_set); i++) {
    const VxStageLight *l = &vx_stage_set[i];
    u8 id = torches_only ? B_TORCH : l->id;
    f64 a = g->player.yaw + l->yaw;
    f64 dx = -m_sin(a), dz = -m_cos(a);
    // walk out at eye height, stopping short of any wall
    i32 px = vx_floor_i(eye.x), pz = vx_floor_i(eye.z);
    for (f64 t = 0.25; t <= l->dist; t += 0.25) {
      i32 x = vx_floor_i(eye.x + dx * t), z = vx_floor_i(eye.z + dz * t);
      if (vx_solid[vx_world_block(&g->world, x, ey, z)]) break;
      px = x, pz = z;
    }
    i32 y = ey;
    while (y > 1 && !vx_solid[vx_world_block(&g->world, px, y - 1, pz)]) y--;
    if (vx_world_block(&g->world, px, y, pz) != B_AIR) continue;
    vx_world_set_block(&g->world, px, y, pz, id, true);
    log_info("stage: % at % % %: % light writes, % sections remeshed (the block's own neighbourhood)",
             fmt_cstr(vx_block_label[id]), fmt_i32(px), fmt_i32(y), fmt_i32(pz), fmt_u32(g->world.stats.light_writes),
             fmt_u32(g->world.stats.dirty_sections));
    g->trace_uploads = 3;
  }
}

// vx_edit:x=.. y=.. z=.. block=<name> state=<n>: a recorded edit, as the player would make it
hz_internal void vx_cmd_edit(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 x, y, z, st = 0;
  String name;
  if (!vx_arg_f32(ctx->args, "x", &x) || !vx_arg_f32(ctx->args, "y", &y) || !vx_arg_f32(ctx->args, "z", &z) ||
      !vx_arg_word(ctx->args, "block", &name)) {
    log_warn("vx_edit: needs x= y= z= block=");
    return;
  }
  vx_arg_f32(ctx->args, "state", &st);
  u8 id = str_equal(name, STR_FROM_CSTR("air")) ? B_AIR : vx_block_by_name(name);
  b32 ok = vx_world_set_block_state(&g->world, (i32)x, (i32)y, (i32)z, id, (u8)st, true);
  if (ok) vx_liquids_touch(&g->liquids, &g->world, (i32)x, (i32)y, (i32)z);
  log_info("vx_edit: % at % % % state %: %", fmt_cstr(vx_block_label[id]), fmt_i32((i32)x), fmt_i32((i32)y),
           fmt_i32((i32)z), fmt_u32((u32)st), fmt_cstr(ok ? "set" : "not set"));
}

// test tooling: vx_cam:orbit= dist= moves only the third-person camera, mid-action
hz_internal void vx_cmd_cam(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 v;
  g->cam.mode = VX_CAM_THIRD;
  if (vx_arg_f32(ctx->args, "orbit", &v)) g->cam.orbit = v;
  if (vx_arg_f32(ctx->args, "dist", &v)) g->cam.dist = g->cam.cur_dist = m_clampf(v, 2.0f, 12.0f);
}

// vx_give:item=<name, spaces as _> count=<n> [slot=<n>] [equip=1]: puts items in the inventory, a hotbar slot, or on the body
hz_internal void vx_cmd_give(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  f32 n = 1, slot = -1;
  if (!vx_arg_word(ctx->args, "item", &name)) {
    log_warn("vx_give: needs item=");
    return;
  }
  vx_arg_f32(ctx->args, "count", &n);
  vx_arg_f32(ctx->args, "slot", &slot);
  VxItemId item = vx_item_by_name(name);
  if (!item) {
    log_warn("vx_give: no item named %", fmt_str(name));
    return;
  }
  u32 count = (u32)MAX(n, 1);
  f32 equip = 0;
  u8 kind = vx_item(item)->kind;
  if (vx_arg_f32(ctx->args, "equip", &equip) && equip > 0 && kind >= VX_IK_ACCESSORY && kind <= VX_IK_LEGS) {
    VxStack *to = kind == VX_IK_ACCESSORY ? &g->inv.accessory[0] : &g->inv.armor[kind - VX_IK_HEAD];
    for (u32 k = 0; kind == VX_IK_ACCESSORY && k < VX_ACCESSORY_SLOTS; k++)
      if (!g->inv.accessory[k].item) {
        to = &g->inv.accessory[k];
        break;
      }
    *to = (VxStack){item, 1};
  } else if (slot >= 0 && slot < VX_INV_SLOTS) {
    g->inv.slot[(u32)slot] = (VxStack){item, (u16)MIN(count, (u32)vx_item(item)->stack)};
  } else {
    u32 left = vx_inventory_add(&g->inv, item, count);
    if (left) log_warn("vx_give: % did not fit", fmt_u32(left));
  }
  log_info("vx_give: % x %", fmt_u32(count), fmt_cstr(vx_item(item)->name));
}

// vx_spawn:kind=<slime|zombie|skeleton|eye|eater|bat|guide|eoc|servant> variant=<n> x= z= [y=]: a creature on the ground at x z, or at y
hz_internal void vx_cmd_spawn(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  f32 x, z, y = -1, variant = 0;
  // natural=0|1: turns the spawner off for a clean scripted scene, or back on
  if (vx_arg_f32(ctx->args, "natural", &x)) {
    g->mobs.natural_off = x == 0;
    log_info("vx_spawn: natural spawns %", fmt_cstr(x == 0 ? "off" : "on"));
    return;
  }
  if (!vx_arg_word(ctx->args, "kind", &name) || !vx_arg_f32(ctx->args, "x", &x) || !vx_arg_f32(ctx->args, "z", &z)) {
    log_warn("vx_spawn: needs kind= x= z=");
    return;
  }
  vx_arg_f32(ctx->args, "variant", &variant);
  hz_internal const char *const names[VX_CR_COUNT] = {"slime", "zombie", "skeleton", "eye", "eater", "bat", "guide", "eoc", "servant"};
  u32 kind = VX_CR_COUNT;
  for (u32 k = 0; k < VX_CR_COUNT; k++)
    if (str_equal(name, cstr_to_str(names[k]))) kind = k;
  if (kind == VX_CR_COUNT) {
    log_warn("vx_spawn: no creature named %", fmt_str(name));
    return;
  }
  if (!vx_arg_f32(ctx->args, "y", &y)) {
    i32 bx = vx_floor_i(x), bz = vx_floor_i(z), top = VX_HEIGHT - 1;
    // the ground under any canopy
    for (; top > 0; top--) {
      u8 b = vx_world_block(&g->world, bx, top, bz);
      if (vx_solid[b] && !vx_is_leaves(b) && !vx_is_log(b)) break;
    }
    y = (f32)top + 1.01f;
  }
  VxCreature *m = vx_creature_spawn(&g->mobs, &g->world, (VxCreatureKind)kind, (u32)variant, (VxV3d){x, y, z});
  // hp=<life>: a weakened creature, so one blow shows its death
  f32 hp;
  if (m && vx_arg_f32(ctx->args, "hp", &hp) && hp > 0) m->hp = MIN(hp, m->hp_max);
  if (m)
    log_info("vx_spawn: % at % % %", fmt_cstr(vx_creature_name(m)), fmt_f64_p(m->pos.x, 1),
             fmt_f64_p(m->pos.y, 2), fmt_f64_p(m->pos.z, 1));
}

// vx_use: one press of the held item at the current target, as a left click
hz_internal void vx_cmd_use(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  VxGame *g = user;
  g->use_cd = 0;
  vx_use_item(g, false, true, g->target);
}

// vx_craft:item=<name>: crafts it once with the stations in reach, as the crafting panel's click does, and stows the result
hz_internal void vx_cmd_craft(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  VxItemId item = vx_arg_word(ctx->args, "item", &name) ? vx_item_by_name(name) : 0;
  for (u32 i = 0; i < vx_recipe_count && item; i++) {
    if (vx_recipes[i].out.item != item) continue;
    b32 made = vx_craft(&g->inv, &vx_recipes[i], g->stations);
    if (made) {
      g->inv.cursor.count = (u16)vx_inventory_add(&g->inv, g->inv.cursor.item, g->inv.cursor.count);
      if (!g->inv.cursor.count) g->inv.cursor.item = 0;
    }
    log_info("vx_craft: % %", fmt_cstr(vx_item(item)->name), fmt_cstr(made ? "made" : "not available"));
    return;
  }
  log_warn("vx_craft: no recipe makes that");
}

// vx_interact: one right click on whatever the crosshair is on (chests, doors)
hz_internal void vx_cmd_interact(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  vx_interact((VxGame *)user, ((VxGame *)user)->target);
}

// vx_boss[:kind=empress]: summons the Eye of Cthulhu (or the Empress of Light) at any hour; vx_boss:hurt=<damage> deals the living boss a blow
hz_internal void vx_cmd_boss(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 hurt;
  if (vx_arg_f32(ctx->args, "hurt", &hurt)) {
    i32 i = vx_boss_index(g);
    if (i < 0) {
      log_warn("vx_boss: no boss to hurt");
      return;
    }
    const VxCreature *m = &g->mobs.items[i];
    v3 d = {(f32)(m->pos.x - g->player.pos.x), 0, (f32)(m->pos.z - g->player.pos.z)};
    vx_creature_hurt(g, (u32)i, hurt, v3_length(d) > 1e-3f ? v3_normalize(d) : (v3){0, 0, -1}, 0, false, "vx_boss");
    return;
  }
  String kind;
  b32 emp = vx_arg_word(ctx->args, "kind", &kind) && str_equal(kind, STR_FROM_CSTR("empress"));
  VxCreature *m = vx_boss_summon(g, emp ? VX_CR_EMPRESS : VX_CR_EYE_OF_CTHULHU, true);
  if (m)
    log_info("vx_boss: % at % % %", fmt_cstr(vx_creature_name(m)), fmt_f64_p(m->pos.x, 1), fmt_f64_p(m->pos.y, 1),
             fmt_f64_p(m->pos.z, 1));
}

// vx_mobs: logs every creature's position, velocity and state
hz_internal void vx_cmd_mobs(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  VxGame *g = user;
  for (u32 i = 0; i < g->mobs.count; i++) {
    const VxCreature *m = &g->mobs.items[i];
    f64 dx = g->player.pos.x - m->pos.x, dz = g->player.pos.z - m->pos.z;
    log_info("vx_mobs: % age % at % % % vel % % % ground % aggro % to player %", fmt_cstr(vx_creature_name(m)),
             fmt_f32_p(m->age, 2), fmt_f64_p(m->pos.x, 1), fmt_f64_p(m->pos.y, 1), fmt_f64_p(m->pos.z, 1),
             fmt_f32_p(m->vel.x, 1), fmt_f32_p(m->vel.y, 1), fmt_f32_p(m->vel.z, 1), fmt_u32(m->on_ground),
             fmt_u32(m->aggro), fmt_f32_p((f32)m_sqrt(dx * dx + dz * dz), 1));
  }
  for (u32 i = 0; i < g->combat.proj_count; i++) {
    const VxProjectile *pr = &g->combat.proj[i];
    log_info("vx_mobs: projectile % at % % % vel % % % life %", fmt_cstr(pr->kind == VX_PROJ_ARROW ? "arrow" : pr->kind == VX_PROJ_SPARK ? "spark" : "star"),
             fmt_f64_p(pr->p.x, 2), fmt_f64_p(pr->p.y, 2), fmt_f64_p(pr->p.z, 2), fmt_f32_p(pr->v.x, 1),
             fmt_f32_p(pr->v.y, 1), fmt_f32_p(pr->v.z, 1), fmt_f32_p(pr->life, 2));
  }
}

// vx_drop:item=<name> count=<n> x= y= z=: drops items into the world
hz_internal void vx_cmd_drop(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  f32 n = 1, x, y, z;
  if (!vx_arg_word(ctx->args, "item", &name) || !vx_arg_f32(ctx->args, "x", &x) || !vx_arg_f32(ctx->args, "y", &y) ||
      !vx_arg_f32(ctx->args, "z", &z))
    return;
  vx_arg_f32(ctx->args, "count", &n);
  VxItemId item = vx_item_by_name(name);
  if (item) vx_entities_spawn_drop(&g->ents, (VxV3d){x, y, z}, item, (u32)MAX(n, 1));
}

// vx_point:inv=<i>|armor=<i>|acc=<i>|chest=<i>|recipe=<item>|trash=0: moves the pointer onto an inventory slot, as a mouse would
hz_internal void vx_cmd_point(const HzCommandCtx *ctx, void *user) {
  UNUSED(user);
  f32 i = 0;
  String name;
  HzUIElementId eid;
  if (vx_arg_f32(ctx->args, "inv", &i)) eid = ui_idi("InvSlot", (u32)i);
  else if (vx_arg_f32(ctx->args, "armor", &i)) eid = ui_idi("ArmorSlot", (u32)i);
  else if (vx_arg_f32(ctx->args, "acc", &i)) eid = ui_idi("AccSlot", (u32)i);
  else if (vx_arg_f32(ctx->args, "trash", &i)) eid = ui_id("TrashSlot");
  else if (vx_arg_f32(ctx->args, "chest", &i)) eid = ui_idi("ChestSlot", (u32)i);
  else if (vx_arg_word(ctx->args, "recipe", &name)) {
    // the first recipe making the named item
    VxItemId item = vx_item_by_name(name);
    u32 r = 0;
    while (r < vx_recipe_count && vx_recipes[r].out.item != item) r++;
    if (!item || r == vx_recipe_count) {
      log_warn("vx_point: no recipe for %", fmt_str(name));
      return;
    }
    eid = ui_idi("Recipe", r);
  }
  else return;
  HzUIElementData data = ui_get_element_data(eid);
  if (!data.found) {
    log_warn("vx_point: slot not on screen: %", fmt_cstr(ctx->args));
    return;
  }
  AppInputEvents *ev = ctx->events;
  f32 cx = data.boundingBox.x + data.boundingBox.width * 0.5f, cy = data.boundingBox.y + data.boundingBox.height * 0.5f;
  ev->mouse_dx += cx - ev->mouse_x;
  ev->mouse_dy += cy - ev->mouse_y;
  ev->mouse_x = cx;
  ev->mouse_y = cy;
}

// vx_find:block=<name> radius=<n>: logs where the named block sits on its own base (tree trunks: one line per tree)
hz_internal void vx_cmd_find(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  f32 r = 24;
  if (!vx_arg_word(ctx->args, "block", &name)) return;
  vx_arg_f32(ctx->args, "radius", &r);
  u8 id = vx_block_by_name(name);
  i32 px = vx_floor_i(g->player.pos.x), pz = vx_floor_i(g->player.pos.z), R = (i32)r, found = 0;
  for (i32 z = pz - R; z <= pz + R && found < 160; z++)
    for (i32 x = px - R; x <= px + R && found < 160; x++)
      for (i32 y = VX_HEIGHT - 2; y > 0; y--) {
        if (vx_world_block(&g->world, x, y, z) != id || vx_world_block(&g->world, x, y - 1, z) == id) continue;
        log_info("vx_find: % at % % % (on %)", fmt_cstr(vx_block_label[id]), fmt_i32(x), fmt_i32(y), fmt_i32(z),
                 fmt_cstr(vx_block_label[vx_world_block(&g->world, x, y - 1, z)]));
        found++;
        break;
      }
}

// vx_map:<0|1>: the full map
hz_internal void vx_cmd_map(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f64 v = 0;
  if (cstr_to_f64(ctx->args, &v)) g->show_map = v > 0.5;
}

// vx_select:<slot> or vx_select:item=<name>, the hotbar slot holding that item
hz_internal void vx_cmd_select(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f64 v = 0;
  String name;
  if (vx_arg_word(ctx->args, "item", &name)) {
    VxItemId item = vx_item_by_name(name);
    for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++)
      if (item && g->inv.slot[i].item == item) {
        vx_game_select(g, i);
        return;
      }
    log_warn("vx_select: no % in the hotbar", fmt_str(name));
  } else if (cstr_to_f64(ctx->args, &v) && v >= 0 && v < VX_HOTBAR_SLOTS) {
    vx_game_select(g, (u32)v);
  }
}

// vx_look:yaw= pitch= | guide=1 | boss=1, turns the view without moving the player, or onto the Guide or the Eye
hz_internal void vx_cmd_look(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 v;
  if (vx_arg_f32(ctx->args, "yaw", &v)) g->player.yaw = g->player.body_yaw = v;
  if (vx_arg_f32(ctx->args, "pitch", &v)) g->player.pitch = v;
  // chest=1: the view on the middle of the nearest chest within reach
  if (vx_arg_f32(ctx->args, "chest", &v)) {
    VxV3d e = vx_player_eye(&g->player);
    i32 ex = vx_floor_i(e.x), ey = vx_floor_i(e.y), ez = vx_floor_i(e.z);
    f32 best = 1e9f;
    for (i32 dy = -4; dy <= 4; dy++)
      for (i32 dz = -6; dz <= 6; dz++)
        for (i32 dx = -6; dx <= 6; dx++) {
          u8 id = vx_world_block(&g->world, ex + dx, ey + dy, ez + dz);
          if (id != B_CHEST && id != B_GOLD_CHEST) continue;
          f32 cx = (f32)(ex + dx + 0.5 - e.x), cy = (f32)(ey + dy + 0.4 - e.y), cz = (f32)(ez + dz + 0.5 - e.z);
          f32 d2 = cx * cx + cy * cy + cz * cz;
          if (d2 >= best) continue;
          best = d2;
          g->player.yaw = g->player.body_yaw = m_atan2f(-cx, -cz);
          g->player.pitch = m_atan2f(cy, m_sqrtf(cx * cx + cz * cz));
        }
    if (best > 1e8f) log_warn("vx_look: no chest within reach");
  }
  // guide=1 or boss=1: the view on that creature's middle
  b32 guide = vx_arg_f32(ctx->args, "guide", &v), boss = vx_arg_f32(ctx->args, "boss", &v);
  for (u32 i = 0; (guide || boss) && i < g->mobs.count; i++) {
    const VxCreature *m = &g->mobs.items[i];
    if (guide ? m->kind != VX_CR_GUIDE : vx_creature_info[m->kind].behaviour != VX_BEH_BOSS) continue;
    VxV3d e = vx_player_eye(&g->player);
    f32 dx = (f32)(m->pos.x - e.x), dy = (f32)(m->pos.y + (guide ? 2.0 : 2.5) - e.y), dz = (f32)(m->pos.z - e.z);
    g->player.yaw = g->player.body_yaw = m_atan2f(-dx, -dz);
    g->player.pitch = m_atan2f(dy, m_sqrtf(dx * dx + dz * dz));
    break;
  }
}

// vx_cut:on=1|0 [cx= cy=]: the Terraria view at the player's slab, with the pointer held at a world point
hz_internal void vx_cmd_cut(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 v, x, y;
  if (vx_arg_f32(ctx->args, "on", &v)) vx_cut_set(g, v > 0);
  if (vx_arg_f32(ctx->args, "cx", &x) && vx_arg_f32(ctx->args, "cy", &y)) {
    g->cut.scripted_cursor = true;
    g->cut.cursor_x = x;
    g->cut.cursor_y = y;
  }
}

// vx_chest:all=1, the open chest's Loot All
hz_internal void vx_cmd_chest(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  VxGame *g = user;
  if (g->open_chest < 0) log_warn("vx_chest: no chest is open");
  vx_chest_loot_all(g);
}

// vx_equip:item=<name>: moves an accessory or armour piece from the bag onto the body, as dragging it to its slot does
hz_internal void vx_cmd_equip(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String name;
  VxItemId item = vx_arg_word(ctx->args, "item", &name) ? vx_item_by_name(name) : 0;
  u8 kind = item ? vx_item(item)->kind : 0;
  if (!item || kind < VX_IK_ACCESSORY || kind > VX_IK_LEGS) {
    log_warn("vx_equip: needs item= naming an accessory or armour piece");
    return;
  }
  for (u32 i = 0; i < VX_INV_SLOTS; i++) {
    VxStack *s = &g->inv.slot[i];
    if (s->item != item) continue;
    VxStack *to = NULL;
    if (kind == VX_IK_ACCESSORY) {
      for (u32 k = 0; k < VX_ACCESSORY_SLOTS && !to; k++)
        if (!g->inv.accessory[k].item) to = &g->inv.accessory[k];
    } else if (!g->inv.armor[kind - VX_IK_HEAD].item) {
      to = &g->inv.armor[kind - VX_IK_HEAD];
    }
    if (!to) break;
    *to = (VxStack){item, 1};
    if (--s->count == 0) s->item = 0;
    log_info("vx_equip: % on", fmt_cstr(vx_item(item)->name));
    return;
  }
  log_warn("vx_equip: no free slot, or % is not in the bag", fmt_str(name));
}

// vx_inv: logs every filled inventory slot
hz_internal void vx_cmd_inv(const HzCommandCtx *ctx, void *user) {
  (void)ctx;
  VxGame *g = user;
  for (u32 i = 0; i < VX_INV_SLOTS; i++)
    if (g->inv.slot[i].item)
      log_info("vx_inv: slot % % x %", fmt_u32(i), fmt_u32(g->inv.slot[i].count),
               fmt_cstr(vx_item(g->inv.slot[i].item)->name));
}

// test tooling: vx_fill:x0= y0= z0= x1= y1= z1= block=<name|air>, a box of one block
hz_internal void vx_cmd_fill(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 x0, y0, z0, x1, y1, z1;
  String name;
  if (!vx_arg_f32(ctx->args, "x0", &x0) || !vx_arg_f32(ctx->args, "y0", &y0) || !vx_arg_f32(ctx->args, "z0", &z0) ||
      !vx_arg_f32(ctx->args, "x1", &x1) || !vx_arg_f32(ctx->args, "y1", &y1) || !vx_arg_f32(ctx->args, "z1", &z1) ||
      !vx_arg_word(ctx->args, "block", &name)) {
    log_warn("vx_fill: needs x0= y0= z0= x1= y1= z1= block=");
    return;
  }
  u8 id = str_equal(name, STR_FROM_CSTR("air")) ? B_AIR : vx_block_by_name(name);
  if (id == B_AIR && !str_equal(name, STR_FROM_CSTR("air"))) {
    log_warn("vx_fill: no block named %", fmt_str(name));
    return;
  }
  u32 n = 0;
  for (i32 y = (i32)MIN(y0, y1); y <= (i32)MAX(y0, y1); y++)
    for (i32 z = (i32)MIN(z0, z1); z <= (i32)MAX(z0, z1); z++)
      for (i32 x = (i32)MIN(x0, x1); x <= (i32)MAX(x0, x1); x++)
        if (vx_world_set_block(&g->world, x, y, z, id, true)) {
          vx_liquids_touch(&g->liquids, &g->world, x, y, z);
          n++;
        }
  log_info("vx_fill: % blocks of %", fmt_u32(n), fmt_cstr(vx_block_label[id]));
}

// vx_frame:x0= z0= x1= z1= y= h=: an open timber frame like a player's sky arena, beams at y on posts h tall
hz_internal void vx_cmd_frame(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 x0, z0, x1, z1, y, h;
  if (!vx_arg_f32(ctx->args, "x0", &x0) || !vx_arg_f32(ctx->args, "z0", &z0) || !vx_arg_f32(ctx->args, "x1", &x1) ||
      !vx_arg_f32(ctx->args, "z1", &z1) || !vx_arg_f32(ctx->args, "y", &y) || !vx_arg_f32(ctx->args, "h", &h)) {
    log_warn("vx_frame: needs x0= z0= x1= z1= y= h=");
    return;
  }
  i32 ax = (i32)MIN(x0, x1), bx = (i32)MAX(x0, x1), az = (i32)MIN(z0, z1), bz = (i32)MAX(z0, z1), top = (i32)y;
  b32 along_x = bx - ax >= bz - az;
  u32 n = 0;
  for (i32 z = az; z <= bz; z++)
    for (i32 x = ax; x <= bx; x++) {
      // rails every two blocks along the long side, cross beams every four, posts under the edges every six
      i32 l = along_x ? x - ax : z - az, s = along_x ? z - az : x - ax, ls = along_x ? bx - ax : bz - az, ss = along_x ? bz - az : bx - ax;
      b32 rail = s % 2 == 0 || s == ss, cross = l % 4 == 0 || l == ls, edge = s == 0 || s == ss;
      if (!rail && !cross) continue;
      u8 id = rail != along_x ? B_WOOD_BEAM_Z : B_WOOD_BEAM_X;
      if (rail && cross) id = edge ? B_WOOD_POST : (along_x ? B_WOOD_BEAM_Z : B_WOOD_BEAM_X);
      n += vx_world_set_block(&g->world, x, top, z, id, true);
      if (edge && (l % 6 == 0 || l == ls))
        for (i32 k = 1; k <= (i32)h; k++) n += vx_world_set_block(&g->world, x, top - k, z, B_WOOD_POST, true);
    }
  log_info("vx_frame: % blocks", fmt_u32(n));
}

// test tooling: vx_ability:double=1 hook=1 wings=<seconds of flight>, until accessories grant them
hz_internal void vx_cmd_ability(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  VxPlayer *p = &g->player;
  f32 v;
  UNUSED(p);
  if (vx_arg_f32(ctx->args, "double", &v)) g->forced_double = v > 0.5f;
  if (vx_arg_f32(ctx->args, "hook", &v)) g->forced_hook = v > 0.5f;
  if (vx_arg_f32(ctx->args, "wings", &v)) g->forced_wings = v;
}

// test tooling: vx_character:skin= hair= style= eyes= shirt= under= pants= shoes= (palette indices)
hz_internal void vx_cmd_character(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  VxSettings *s = &g->settings;
  f32 v;
  if (vx_arg_f32(ctx->args, "skin", &v)) s->skin_tone = (u32)v;
  if (vx_arg_f32(ctx->args, "hair", &v)) s->hair_color = (u32)v;
  if (vx_arg_f32(ctx->args, "style", &v)) s->hair_style = (u32)v;
  if (vx_arg_f32(ctx->args, "eyes", &v)) s->eye_color = (u32)v;
  if (vx_arg_f32(ctx->args, "shirt", &v)) s->shirt_color = (u32)v;
  if (vx_arg_f32(ctx->args, "under", &v)) s->undershirt_color = (u32)v;
  if (vx_arg_f32(ctx->args, "pants", &v)) s->pants_color = (u32)v;
  if (vx_arg_f32(ctx->args, "shoes", &v)) s->shoe_color = (u32)v;
}

// vx_edit_check:x=.. y=.. z=..: logs whether the block's chunk is loaded, and its block and state
hz_internal void vx_cmd_edit_check(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 x, y, z;
  if (!vx_arg_f32(ctx->args, "x", &x) || !vx_arg_f32(ctx->args, "y", &y) || !vx_arg_f32(ctx->args, "z", &z)) return;
  i32 ix = (i32)x, iy = (i32)y, iz = (i32)z;
  VxChunk *c = vx_world_chunk(&g->world, ix >> 4, iz >> 4);
  if (!c) {
    log_info("vx_edit_check: % % % chunk not loaded", fmt_i32(ix), fmt_i32(iy), fmt_i32(iz));
    return;
  }
  u8 id = vx_world_block(&g->world, ix, iy, iz);
  log_info("vx_edit_check: % % % chunk uid % holds % with state %", fmt_i32(ix), fmt_i32(iy), fmt_i32(iz),
           fmt_u32(c->uid), fmt_cstr(vx_block_label[id]), fmt_u32(vx_world_state(&g->world, ix, iy, iz)));
}

// vx_dump_textures[:path]: writes the tile texture contact sheet (desktop only)
hz_internal void vx_cmd_dump_textures(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  const char *path = ctx->args && ctx->args[0] ? ctx->args : "out/textures.png";
  if (vx_textures_dump(&g->tex, path)) log_info("texture sheet written to %", fmt_cstr(path));
  else log_warn("texture sheet not written to %", fmt_cstr(path));
}

// vx_dump_icons[:path]: writes the item icon contact sheet (desktop only)
hz_internal void vx_cmd_dump_icons(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  const char *path = ctx->args && ctx->args[0] ? ctx->args : "out/icons.png";
  if (vx_icons_dump(&g->tex, path)) log_info("icon sheet written to %", fmt_cstr(path));
  else log_warn("icon sheet not written to %", fmt_cstr(path));
}

// vx_play: enter play without grabbing the mouse (scripted runs)
hz_internal void vx_cmd_play(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  VxGame *g = user;
  g->test_play = true;
  g->played = true;
  if (g->started) g->mode = VX_MODE_PLAYING;
}

// vx_mode:menu|settings|inventory|paused
hz_internal void vx_cmd_mode(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String a = cstr_to_str(ctx->args);
  if (str_equal(a, STR_FROM_CSTR("menu"))) g->mode = VX_MODE_MENU;
  else if (str_equal(a, STR_FROM_CSTR("paused"))) g->mode = VX_MODE_PAUSED;
  else if (str_equal(a, STR_FROM_CSTR("inventory"))) g->mode = VX_MODE_INVENTORY;
  else if (str_equal(a, STR_FROM_CSTR("settings"))) {
    g->settings_return = VX_MODE_MENU;
    g->mode = VX_MODE_SETTINGS;
  }
}

// vx_preset:low|medium|high|ultra
hz_internal void vx_cmd_preset(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  String a = cstr_to_str(ctx->args);
  VxPreset p = str_equal(a, STR_FROM_CSTR("low"))      ? VX_PRESET_LOW
               : str_equal(a, STR_FROM_CSTR("medium")) ? VX_PRESET_MEDIUM
               : str_equal(a, STR_FROM_CSTR("ultra"))  ? VX_PRESET_ULTRA
                                                       : VX_PRESET_HIGH;
  vx_settings_apply_preset(&g->settings, p);
  g->auto_quality = false;
  vx_game_apply_settings(g);
}

// vx_setblock:x y z id
hz_internal void vx_cmd_setblock(const HzCommandCtx *ctx, void *user) {
  VxGame *g = user;
  f32 x, y, z, id;
  if (vx_arg_f32(ctx->args, "x", &x) && vx_arg_f32(ctx->args, "y", &y) && vx_arg_f32(ctx->args, "z", &z) &&
      vx_arg_f32(ctx->args, "id", &id) && id >= 0 && id < B_COUNT)
    vx_world_set_block(&g->world, (i32)x, (i32)y, (i32)z, (u8)id, true);
}

// css radial-gradient(ellipse at center, rgba(0,0,0,.25), rgba(0,0,0,.6)), stretched over the screen
hz_internal GpuTexture vx_make_scrim(void) {
  const u32 n = 128;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u8 *px = ALLOC_ARRAY(&tmp.allocator, u8, n * n * 4);
  for (u32 y = 0; y < n; y++) {
    for (u32 x = 0; x < n; x++) {
      f32 u = ((x + 0.5f) / n - 0.5f) * 2.0f, v = ((y + 0.5f) / n - 0.5f) * 2.0f;
      f32 t = m_minf(1.0f, m_sqrtf(u * u + v * v) / m_sqrtf(2.0f));
      px[(y * n + x) * 4 + 3] = (u8)((0.25f + (0.6f - 0.25f) * t) * 255.0f + 0.5f);
    }
  }
  GpuTexture tex = gpu_make_texture(&(GpuTextureDesc){
      .data = px, .size = n * n * 4, .data_type = GPU_TEXTURE_DATA_RGBA8, .width = n, .height = n});
  tctx_temp_allocator_end(tmp);
  return tex;
}

// ---- entry points ----

HZ_APP_API void app_init(AppMemory *memory) {
  VxGame *g = memory->state;
  if (is_main_thread()) {
    // render at 1 device px per logical px on every display
    hz_set_max_dpr(1.0f);
    vx_render_create_pipelines(&g->renderer);
    // compiles start now, not when init returns: the rest of init overlaps them
    gpu_flush();
    g->arena = arena_create(GB(1), MB(1));
    g->alloc = make_arena_allocator(g->arena);
    vx_textures_alloc(&g->tex, &g->alloc);
    Allocator icon_ta = tctx_temp_allocator(NULL);
    g->icon_pixels = ALLOC_ARRAY_NO_ZERO(&icon_ta, u8, B_COUNT * VX_ICON_SIZE * VX_ICON_SIZE * 4);
  }
  lane_sync();
  vx_textures_generate(&g->tex);
  lane_sync();
  vx_icons_raster(g->icon_pixels, &g->tex, VX_ICON_SIZE);
  lane_sync();
  if (!is_main_thread()) return;

  Allocator *a = &g->alloc;
  g->input = input_init();
  g->assets = asset_system_create(NULL);
  g->ui = ui_create(NULL, g->assets);
  g->rng = random_create_type(RANDOM_PCG32, os_time_unix_ms());
  vx_settings_defaults(&g->settings);
  g->renderer.settings = vx_settings_render(&g->settings);
  g->renderer.gpu_timing = true;
  vx_render_init(&g->renderer, a, &g->tex, (u32)(memory->canvas_width * memory->dpr),
                 (u32)(memory->canvas_height * memory->dpr), memory->dpr);
  g->icons = ALLOC_ARRAY(a, GpuTexture, MAX(VX_BLOCK_IDS, I_COUNT));
  vx_icons_upload(g->icons, g->icon_pixels, VX_ICON_SIZE);
  vx_item_icons_make(g->icons);
  g->icon_pixels = NULL;
  g->scrim = vx_make_scrim();
  g->hud_icons = ALLOC_ARRAY(a, GpuTexture, VX_HUD_ICON_COUNT);
  vx_hud_icons_make(g->hud_icons);
  vx_entities_init(&g->ents, a, os_time_unix_ms());
  vx_creatures_init(&g->mobs, a, os_time_unix_ms());
  vx_combat_init(&g->combat, a, os_time_unix_ms());
  vx_dynlight_init(&g->dyn, a);
  vx_cut_init(&g->cut, a);
  vx_audio_init(&g->audio, a);
  vx_edits_init(&g->edits);
  vx_mesh_store_init(&g->meshes, a);

  vx_give_starting_kit(g);
  g->chests = ALLOC_ARRAY(a, VxChest, VX_MAX_CHESTS);
  vx_map_init(&g->map, a, &g->tex);
  g->events = ALLOC_ARRAY(a, char, VX_EVENTS * VX_EVENT_LEN);
  g->open_chest = -1;
  g->toast = ALLOC_ARRAY(a, char, VX_TOAST_MAX);
  g->debug_text = ALLOC_ARRAY(a, char, 1024);
  vx_liquids_init(&g->liquids, a);
  g->perf_samples = ALLOC_ARRAY(a, f32, VX_PERF_SAMPLES);
  g->sliders = ALLOC_ARRAY(a, UIInteract_Slider, VX_SETTINGS_SLIDERS);
  g->dropdowns = ALLOC_ARRAY(a, UIInteract_Dropdown, VX_DROPDOWN_COUNT);
  g->button_anims = ALLOC_ARRAY(a, UIInteractAnim, VX_UI_BUTTONS);
  vx_player_init(&g->player);
  g->player.sensitivity = 0.0022f * g->settings.sensitivity;
  g->day_time = 0.08f;
  g->frame_period_ms = 16.667f;
  g->time_scale = 1;
  g->eye_sky = 1;
  g->star_progress = -1;
  g->flash_timer = 20;
  g->weather_timer = 240 + random_f32(&g->rng) * 360;
  g->mode = VX_MODE_LOADING;
  g->boot = VX_BOOT_START;

  hz_command_register("vx_view", vx_cmd_view, g);
  hz_command_register("vx_spawn", vx_cmd_spawn, g);
  hz_command_register("vx_mobs", vx_cmd_mobs, g);
  hz_command_register("vx_boss", vx_cmd_boss, g);
  hz_command_register("vx_frame", vx_cmd_frame, g);
  hz_command_register("vx_use", vx_cmd_use, g);
  hz_command_register("vx_craft", vx_cmd_craft, g);
  hz_command_register("vx_interact", vx_cmd_interact, g);
  hz_command_register("vx_play", vx_cmd_play, g);
  hz_command_register("vx_mode", vx_cmd_mode, g);
  hz_command_register("vx_preset", vx_cmd_preset, g);
  hz_command_register("vx_setblock", vx_cmd_setblock, g);
  hz_command_register("vx_seed", vx_cmd_seed, g);
  hz_command_register("vx_shaft", vx_cmd_shaft, g);
  hz_command_register("vx_hud", vx_cmd_hud, g);
  hz_command_register("vx_time", vx_cmd_time, g);
  hz_command_register("vx_dump_textures", vx_cmd_dump_textures, g);
  hz_command_register("vx_dump_icons", vx_cmd_dump_icons, g);
  hz_command_register("vx_edit", vx_cmd_edit, g);
  hz_command_register("vx_edit_check", vx_cmd_edit_check, g);
  hz_command_register("vx_character", vx_cmd_character, g);
  hz_command_register("vx_ability", vx_cmd_ability, g);
  hz_command_register("vx_fill", vx_cmd_fill, g);
  hz_command_register("vx_cam", vx_cmd_cam, g);
  hz_command_register("vx_give", vx_cmd_give, g);
  hz_command_register("vx_chest", vx_cmd_chest, g);
  hz_command_register("vx_cut", vx_cmd_cut, g);
  hz_command_register("vx_equip", vx_cmd_equip, g);
  hz_command_register("vx_drop", vx_cmd_drop, g);
  hz_command_register("vx_select", vx_cmd_select, g);
  hz_command_register("vx_map", vx_cmd_map, g);
  hz_command_register("vx_inv", vx_cmd_inv, g);
  hz_command_register("vx_look", vx_cmd_look, g);
  hz_command_register("vx_find", vx_cmd_find, g);
  hz_command_register("vx_point", vx_cmd_point, g);
}

HZ_APP_API void app_update_and_render(AppMemory *memory) {
  VxGame *g = memory->state;
  // collective: every lane participates
  asset_system_update(g->assets);
  ui_font_update(g->ui);
  b32 main = is_main_thread();
  if (main) {
    vx_frame_begin(g);
    vx_game_update(g, memory);
  }
  lane_sync();
  if (main) {
    vx_game_render(g, memory);
    f32 ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), g->frame_t0));
    g->main_ms += (ms - g->main_ms) * 0.1f;
  }
  // world_created is published by the sync above, so every lane agrees on entering the job rounds
  if (g->world_created) vx_world_run_jobs(&g->world);
  if (main) g->frame_cpu_ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), g->frame_t0));
}

HZ_APP_API size_t app_state_size(void) { return sizeof(VxGame); }

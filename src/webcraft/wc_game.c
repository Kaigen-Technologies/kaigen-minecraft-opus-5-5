#include "webcraft/wc_game.h"
#include "app/app.h"
#include "input.h"
#include "lib/string_builder.h"
#include "hz_engine.h"
#include "hz_command_registry.h"
#include "generated/types_app.gen.h"

const HzAppConfig hz_app_config = {
    .window_title = "WebCraft",
    .window_width = 1280,
    .window_height = 720,
    // uncapped: the stats panel shows the real frame rate (the browser build stays at the display's refresh)
    .vsync = HZ_VSYNC_OFF,
    .target_frame_time_ms = -1,
};

#define WC_ICON_SIZE 64
#define WC_SAVE_DEBOUNCE 2.0f
#define WC_META_INTERVAL 10.0f

// ---- helpers used by the ui ----

void wc_game_toast(WcGame *g, const char *text) {
  u32 n = cstr_len(text);
  if (n >= WC_TOAST_MAX) n = WC_TOAST_MAX - 1;
  mem_cpy(g->toast, text, n);
  g->toast[n] = 0;
  g->toast_t = 0;
}

String wc_clock_string(f32 day_time, Allocator *alloc) {
  f32 hours = m_fmodf(day_time * 24 + 6, 24);
  u32 h = (u32)hours;
  u32 m = (u32)((hours - (f32)h) * 60);
  return str_format(alloc, "%%:%%", fmt_u32(h / 10), fmt_u32(h % 10), fmt_u32(m / 10), fmt_u32(m % 10));
}

void wc_game_set_mode(WcGame *g, WcMode m) {
  g->mode = m;
  if (m != WC_MODE_PLAYING && os_is_mouse_locked()) os_lock_mouse(false);
}

void wc_game_lock(WcGame *g) {
  if (!g->started) return;
  if (!g->test_play) os_lock_mouse(true);
  g->mode = WC_MODE_PLAYING;
}

void wc_game_select(WcGame *g, u32 slot) {
  g->selected = slot;
  wc_game_toast(g, wc_block_label[g->hotbar[slot]]);
}

void wc_game_apply_settings(WcGame *g, b32 save) {
  g->renderer.settings = wc_settings_render(&g->settings);
  g->world.render_distance = (i32)g->settings.render_distance;
  g->player.sensitivity = 0.0022f * g->settings.sensitivity;
  wc_render_invalidate_history(&g->renderer);
  if (save && g->settings_path.len) wc_settings_write(&g->settings, g->settings_path.value);
}

// ---- persistence ----

hz_internal WcWorldMeta wc_current_meta(WcGame *g) {
  return (WcWorldMeta){.seed = g->seed, .has_player = true, .pos = g->player.pos, .yaw = g->player.yaw,
                       .pitch = g->player.pitch, .flying = g->player.flying, .day_time = g->day_time,
                       .hotbar = g->hotbar};
}

hz_internal void wc_save_world(WcGame *g) {
  if (!g->started || !g->world_path.len || g->no_save) return;
  WcWorldMeta meta = wc_current_meta(g);
  if (wc_world_save_write(g->world_path.value, &meta, &g->edits)) g->edits.dirty = false;
}

// resets the player and camera state for the current seed / meta
hz_internal void wc_start_world(WcGame *g, const WcWorldMeta *meta) {
  if (!g->world_created) {
    wc_world_init(&g->world, g->seed, &g->edits, &g->meshes);
    g->world_created = true;
  } else {
    wc_world_reset(&g->world, g->seed);
  }
  g->world.render_distance = (i32)g->settings.render_distance;
  f32 sens = g->player.sensitivity;
  wc_player_init(&g->player);
  g->player.sensitivity = sens;
  if (meta && meta->has_player) {
    g->player.pos = meta->pos;
    g->player.yaw = meta->yaw;
    g->player.pitch = meta->pitch;
    g->player.flying = meta->flying;
    g->day_time = meta->day_time;
  } else {
    v3 sp = wc_find_spawn(&g->world.terrain);
    g->player.pos = (WcV3d){sp.x, sp.y + 0.01, sp.z};
    g->player.yaw = PI * 0.75f;
    g->player.pitch = -0.05f;
  }
  log_info("webcraft: world seed % (%)", fmt_u32(g->seed), fmt_cstr(meta && meta->has_player ? "saved" : "new"));
  g->water_count = 0;
  g->started = false;
  g->spawn_ready = false;
  g->ready_frames = 0;
  g->boot = WC_BOOT_GENERATING;
  wc_game_set_mode(g, WC_MODE_LOADING);
  wc_render_invalidate_history(&g->renderer);
}

void wc_game_new_world(WcGame *g) {
  wc_edits_clear(&g->edits);
  g->seed = random_u32(&g->rng) & 0x7fffffffu;
  mem_cpy(g->hotbar, wc_default_hotbar, WC_HOTBAR_SLOTS);
  g->day_time = 0.08f;
  wc_start_world(g, NULL);
  WcWorldMeta meta = wc_current_meta(g);
  meta.has_player = false;
  wc_world_save_write(g->world_path.value, &meta, &g->edits);
}

hz_internal b32 wc_poll_file(OsFileOp **op, PlatformFileData *out, Allocator *alloc, b32 *done) {
  if (!*op) {
    *done = true;
    return false;
  }
  OsFileReadState st = os_check_read_file(*op);
  if (st == OS_FILE_READ_STATE_IN_PROGRESS) {
    *done = false;
    return false;
  }
  *done = true;
  b32 ok = false;
  if (st == OS_FILE_READ_STATE_COMPLETED) {
    ok = os_get_file_data(*op, out, alloc);
    if (!ok) os_release_file_op(*op);
  } else {
    os_release_file_op(*op);
  }
  *op = NULL;
  return ok && out->success;
}

// async boot: read settings and the world save, then start generating
hz_internal void wc_boot_poll(WcGame *g) {
  if (g->boot != WC_BOOT_READ_SAVES) return;
  b32 s_done, w_done;
  PlatformFileData sd = {0};
  if (wc_poll_file(&g->settings_op, &sd, &g->alloc, &s_done)) {
    if (wc_settings_read(sd.buffer, sd.buffer_len, &g->settings)) g->settings_were_saved = true;
  }
  PlatformFileData wd = {0};
  if (wc_poll_file(&g->world_op, &wd, &g->alloc, &w_done)) {
    WcWorldMeta meta;
    if (wc_world_save_read(wd.buffer, wd.buffer_len, &meta, g->hotbar, &g->edits)) {
      g->pending_meta = meta;
      g->has_pending_meta = true;
    }
  }
  if (!s_done || !w_done) return;
  g->auto_quality = !g->settings_were_saved && !g->no_save;
  wc_game_apply_settings(g, false);
  if (g->has_forced_seed) {
    wc_edits_clear(&g->edits);
    mem_cpy(g->hotbar, wc_default_hotbar, WC_HOTBAR_SLOTS);
    g->seed = g->forced_seed;
    wc_start_world(g, NULL);
  } else if (g->has_pending_meta) {
    g->seed = g->pending_meta.seed;
    wc_start_world(g, &g->pending_meta);
  } else {
    g->seed = random_u32(&g->rng) & 0x7fffffffu;
    wc_start_world(g, NULL);
  }
}

// ---- world interaction ----

hz_internal void wc_schedule_water(WcGame *g, i32 x, i32 y, i32 z, b32 is_source) {
  const WcWorld *w = &g->world;
  if (!is_source) {
    b32 adjacent = wc_world_block(w, x, y + 1, z) == B_WATER || wc_world_block(w, x + 1, y, z) == B_WATER ||
               wc_world_block(w, x - 1, y, z) == B_WATER || wc_world_block(w, x, y, z + 1) == B_WATER ||
               wc_world_block(w, x, y, z - 1) == B_WATER;
    if (!adjacent) return;
  }
  if (g->water_count < WC_WATER_QUEUE_MAX) g->water_q[g->water_count++] = (WcWaterCell){x, y, z, 0};
}

force_inline b32 wc_water_can_fill(u8 b) { return b == B_AIR || (wc_replaceable[b] && b != B_WATER); }

// simple fluid spread: water falls into holes, then runs a few blocks sideways
hz_internal void wc_tick_water(WcGame *g) {
  WcWorld *w = &g->world;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  WcWaterCell *next = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, WcWaterCell, 12 * 5);
  u32 next_count = 0, taken = 0;
  for (u32 budget = 12; taken < g->water_count && budget > 0; budget--) {
    WcWaterCell c = g->water_q[taken++];
    u8 cur = wc_world_block(w, c.x, c.y, c.z);
    if (cur != B_WATER) {
      if (cur != B_AIR && !wc_replaceable[cur]) continue;
      if (!wc_world_set_block(w, c.x, c.y, c.z, B_WATER, true)) continue;
    }
    if (wc_water_can_fill(wc_world_block(w, c.x, c.y - 1, c.z))) {
      next[next_count++] = (WcWaterCell){c.x, c.y - 1, c.z, c.d};
      continue;
    }
    if (c.d >= 4) continue;
    for (u32 k = 0; k < 4; k++) {
      i32 dx = k == 0 ? 1 : k == 1 ? -1 : 0, dz = k == 2 ? 1 : k == 3 ? -1 : 0;
      if (wc_water_can_fill(wc_world_block(w, c.x + dx, c.y, c.z + dz)))
        next[next_count++] = (WcWaterCell){c.x + dx, c.y, c.z + dz, c.d + 1};
    }
  }
  mem_move(g->water_q, g->water_q + taken, sizeof(WcWaterCell) * (g->water_count - taken));
  g->water_count -= taken;
  for (u32 i = 0; i < next_count && g->water_count < WC_WATER_QUEUE_MAX; i++) g->water_q[g->water_count++] = next[i];
  tctx_temp_allocator_end(tmp);
}

hz_internal void wc_break_block(WcGame *g, WcRayHit h) {
  wc_entities_trigger_swing(&g->ents);
  if (h.id == B_BEDROCK) return;
  WcWorld *w = &g->world;
  if (!wc_world_set_block(w, h.x, h.y, h.z, B_AIR, true)) return;
  wc_entities_spawn_break(&g->ents, h.id, h.x, h.y, h.z);
  wc_audio_break(&g->audio, h.id);
  // plants, torches and cactus need the block below them
  for (i32 y = h.y + 1; y < WC_HEIGHT; y++) {
    u8 above = wc_world_block(w, h.x, y, h.z);
    if (wc_shape[above] == WC_SHAPE_CROSS || wc_shape[above] == WC_SHAPE_TORCH || above == B_CACTUS)
      wc_world_set_block(w, h.x, y, h.z, B_AIR, true);
    else
      break;
  }
  wc_schedule_water(g, h.x, h.y, h.z, false);
  g->save_debounce = WC_SAVE_DEBOUNCE;
}

hz_internal void wc_place_block(WcGame *g, WcRayHit h) {
  u8 id = g->hotbar[g->selected];
  wc_entities_trigger_swing(&g->ents);
  i32 x = h.x + h.nx, y = h.y + h.ny, z = h.z + h.nz;
  // clicking a replaceable block (grass, flowers) replaces it directly
  if (wc_replaceable[h.id] && h.id != B_WATER) {
    x = h.x;
    y = h.y;
    z = h.z;
  }
  if (y < 0 || y >= WC_HEIGHT) return;
  WcWorld *w = &g->world;
  u8 cur = wc_world_block(w, x, y, z);
  if (cur != B_AIR && !wc_replaceable[cur]) return;
  if (wc_solid[id] && wc_player_intersects_block(&g->player, x, y, z)) return;
  if (wc_shape[id] == WC_SHAPE_CROSS || wc_shape[id] == WC_SHAPE_TORCH || id == B_SUGAR_CANE) {
    u8 below = wc_world_block(w, x, y - 1, z);
    if (!wc_occludes[below] && below != id && below != B_CACTUS) return;
  }
  if (!wc_world_set_block(w, x, y, z, id, true)) return;
  wc_audio_place(&g->audio, id);
  if (id == B_WATER) wc_schedule_water(g, x, y, z, true);
  g->save_debounce = WC_SAVE_DEBOUNCE;
}

// ---- weather ----

hz_internal f32 wc_grand(WcGame *g) { return random_f32(&g->rng); }

hz_internal void wc_update_weather(WcGame *g, f32 dt) {
  if (g->settings.weather == WC_WEATHER_CLEAR) {
    g->rain_target = 0;
  } else if (g->settings.weather == WC_WEATHER_RAIN) {
    g->rain_target = 1;
  } else {
    g->weather_timer -= dt;
    if (g->weather_timer <= 0) {
      g->rain_target = g->rain_target > 0.5f ? 0.0f : 1.0f;
      g->weather_timer = g->rain_target > 0 ? 150 + wc_grand(g) * 240 : 420 + wc_grand(g) * 720;
    }
  }
  // clouds build up first, rain follows
  g->rain += (g->rain_target - g->rain) * (1 - m_expf(-dt / 12));
  // precipitation turns to snow in cold biomes and high up
  g->climate_timer -= dt;
  if (g->climate_timer <= 0 && g->world_created) {
    g->climate_timer = 0.5f;
    WcV3d p = g->player.pos;
    WcColumnSample c = wc_sample_column(&g->world.terrain, wc_floor_i(p.x), wc_floor_i(p.z));
    f64 cold_t = c.temp - m_max(0.0, m_max((f64)c.height, p.y) - 95) * 0.014;
    g->coldness = (f32)wc_clamp((-0.3 - cold_t) / 0.2, 0, 1);
  }
  g->snow += (g->coldness - g->snow) * (1 - m_expf(-dt / 3));
  f32 falling = g->rain > 0.35f ? 1.0f : 0.0f;
  f32 wet_target = falling * (1 - g->snow);
  g->wetness += (wet_target - g->wetness) * (1 - m_expf(-dt / (wet_target > g->wetness ? 18.0f : 70.0f)));
  // thunderstorms: double-flicker lightning then thunder
  if (g->rain > 0.8f && g->snow < 0.5f) {
    g->flash_timer -= dt;
    if (g->flash_timer <= 0) {
      g->flash_timer = 9 + wc_grand(g) * 28;
      g->flash_pulses = 2 + (u32)(wc_grand(g) * 2);
      g->flash = 1;
      f32 dist = 0.4f + wc_grand(g) * 2.6f;
      wc_audio_thunder(&g->audio, dist, m_maxf(0.3f, 1.2f - dist * 0.3f));
    }
  }
  g->flash *= m_expf(-dt * 9);
  if (g->flash < 0.15f && g->flash_pulses > 1 && wc_grand(g) < dt * 12) {
    g->flash = 0.7f + wc_grand(g) * 0.3f;
    g->flash_pulses--;
  }
  f32 cover_target = falling * g->snow;
  g->snow_cover +=
      (cover_target - g->snow_cover) * (1 - m_expf(-dt / (cover_target > g->snow_cover ? 40.0f : 150.0f)));
}

// ---- input ----

force_inline b32 wc_down(const WcGame *g, App_InputButtonType k) { return g->input.buttons[k].is_pressed; }
force_inline b32 wc_pressed(const WcGame *g, App_InputButtonType k) { return g->input.buttons[k].pressed_this_frame; }

hz_internal void wc_handle_input(WcGame *g, f32 dt) {
  // modal keys outside play
  if (g->mode == WC_MODE_INVENTORY) {
    if (wc_pressed(g, KEY_E)) wc_game_lock(g);
    else if (wc_pressed(g, KEY_ESCAPE)) wc_game_set_mode(g, WC_MODE_PAUSED);
    return;
  }
  if (g->mode == WC_MODE_SETTINGS) {
    if (wc_pressed(g, KEY_ESCAPE)) {
      wc_game_apply_settings(g, true);
      wc_game_set_mode(g, g->settings_return == WC_MODE_MENU ? WC_MODE_MENU : WC_MODE_PAUSED);
    }
    return;
  }
  if (g->mode != WC_MODE_PLAYING) return;
  // the platform can drop the lock on its own (escape in a browser, focus loss)
  if (!g->test_play && !os_is_mouse_locked()) {
    wc_game_set_mode(g, WC_MODE_PAUSED);
    return;
  }
  if (wc_pressed(g, KEY_ESCAPE)) {
    wc_game_set_mode(g, WC_MODE_PAUSED);
    return;
  }
  wc_player_look(&g->player, g->input.mouse_delta.x, g->input.mouse_delta.y);
  for (u32 i = 0; i < WC_HOTBAR_SLOTS; i++)
    if (wc_pressed(g, (App_InputButtonType)(KEY_1 + i))) wc_game_select(g, i);
  f32 wheel = g->input.scroll_delta.y;
  if (wheel != 0) wc_game_select(g, (g->selected + (wheel < 0 ? 1 : WC_HOTBAR_SLOTS - 1)) % WC_HOTBAR_SLOTS);
  if (wc_pressed(g, KEY_F3)) g->show_debug = !g->show_debug;
  if (wc_pressed(g, KEY_F2)) g->screenshot_requested = true;
  if (wc_pressed(g, KEY_F1)) g->hide_hud = !g->hide_hud;
  if (wc_pressed(g, KEY_E)) {
    wc_game_set_mode(g, WC_MODE_INVENTORY);
    return;
  }
  g->time_scale = 1;
  if (wc_down(g, KEY_T)) g->time_scale = (wc_down(g, KEY_LEFT_SHIFT) || wc_down(g, KEY_RIGHT_SHIFT)) ? -36.0f : 36.0f;

  g->break_cd -= dt;
  g->place_cd -= dt;
  b32 click_l = wc_pressed(g, MOUSE_LEFT), click_r = wc_pressed(g, MOUSE_RIGHT), click_m = wc_pressed(g, MOUSE_MIDDLE);
  if (g->target.hit) {
    if (click_l || (wc_down(g, MOUSE_LEFT) && g->break_cd <= 0)) {
      wc_break_block(g, g->target);
      g->break_cd = click_l ? 0.3f : 0.22f;
    }
    if (click_r || (wc_down(g, MOUSE_RIGHT) && g->place_cd <= 0)) {
      wc_place_block(g, g->target);
      g->place_cd = click_r ? 0.3f : 0.2f;
    }
    if (click_m) {
      u8 id = g->target.id;
      u32 existing = WC_HOTBAR_SLOTS;
      for (u32 i = 0; i < WC_HOTBAR_SLOTS; i++)
        if (g->hotbar[i] == id) existing = i;
      if (existing < WC_HOTBAR_SLOTS) {
        wc_game_select(g, existing);
      } else if (wc_block_in_inventory[id]) {
        g->hotbar[g->selected] = id;
        wc_game_toast(g, wc_block_label[id]);
      }
    }
  }
}

hz_internal WcPlayerInput wc_player_input(const WcGame *g) {
  if (g->mode != WC_MODE_PLAYING) return (WcPlayerInput){0};
  return (WcPlayerInput){
      .forward = wc_down(g, KEY_W) || wc_down(g, KEY_UP),
      .back = wc_down(g, KEY_S) || wc_down(g, KEY_DOWN),
      .left = wc_down(g, KEY_A) || wc_down(g, KEY_LEFT),
      .right = wc_down(g, KEY_D) || wc_down(g, KEY_RIGHT),
      .jump = wc_down(g, KEY_SPACE),
      .sneak = wc_down(g, KEY_LEFT_SHIFT) || wc_down(g, KEY_RIGHT_SHIFT),
      .sprint_key = wc_down(g, KEY_LEFT_CONTROL) || wc_down(g, KEY_RIGHT_CONTROL),
      .jump_pressed = wc_pressed(g, KEY_SPACE),
      .forward_pressed = wc_pressed(g, KEY_W),
      .fly_pressed = wc_pressed(g, KEY_F),
  };
}

// after loading, watch the frame rate and step the preset down while it is too slow
hz_internal void wc_auto_tune(WcGame *g, f32 dt) {
  if (!g->auto_quality || !g->started) return;
  if (!wc_world_idle(&g->world)) {
    g->perf_count = 0;
    return;
  }
  g->perf_samples[g->perf_count++] = dt;
  if (g->perf_count < WC_PERF_SAMPLES) return;
  // median by counting: samples are few and this runs once per 150 frames
  f32 median = 0;
  for (u32 i = 0; i < WC_PERF_SAMPLES; i++) {
    u32 below = 0;
    for (u32 j = 0; j < WC_PERF_SAMPLES; j++)
      if (g->perf_samples[j] < g->perf_samples[i]) below++;
    if (below <= WC_PERF_SAMPLES / 2) median = m_maxf(median, g->perf_samples[i]);
  }
  g->perf_count = 0;
  WcPreset p = g->settings.preset;
  if (median > 1.0f / 38.0f && p != WC_PRESET_CUSTOM && p != WC_PRESET_LOW) {
    WcPreset next = (WcPreset)(p - 1);
    wc_settings_apply_preset(&g->settings, next);
    wc_game_apply_settings(g, false);
    Allocator fa = tctx_temp_allocator(NULL);
    String msg = str_format(&fa, "Graphics set to % for smoother performance",
                            fmt_cstr(next == WC_PRESET_LOW ? "low" : next == WC_PRESET_MEDIUM ? "medium" : "high"));
    wc_game_toast(g, msg.value);
  } else {
    g->auto_quality = false;
    wc_settings_write(&g->settings, g->settings_path.value);
  }
}

#define WC_COMPASS_POINTS 8
hz_internal const char *const WC_COMPASS[WC_COMPASS_POINTS] = {"N", "NW", "W", "SW", "S", "SE", "E", "NE"};

hz_internal void wc_update_debug_text(WcGame *g) {
  const WcPlayer *p = &g->player;
  i32 bx = wc_floor_i(p->pos.x), by = wc_floor_i(p->pos.y), bz = wc_floor_i(p->pos.z);
  WcColumnSample col = wc_sample_column(&g->world.terrain, bx, bz);
  u8 l = wc_world_light(&g->world, bx, wc_floor_i(p->pos.y + p->eye_offset), bz);
  f32 yaw_deg = m_fmodf(p->yaw * 180.0f / PI, 360.0f);
  if (yaw_deg < 0) yaw_deg += 360;
  const char *facing = WC_COMPASS[((u32)(yaw_deg / 45.0f + 0.5f)) % WC_COMPASS_POINTS];
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  StringBuilder sb = sb_create(1024, &tmp.allocator);
  sb_append_format(&sb, "WebCraft  % fps  cpu % ms (max %)\n", fmt_u32((u32)(g->fps + 0.5f)), fmt_f32_p(g->cpu_ms, 1),
                   fmt_f32_p(g->cpu_max, 1));
  sb_append_format(&sb, "XYZ: % / % / %\n", fmt_f64_p(p->pos.x, 2), fmt_f64_p(p->pos.y, 2), fmt_f64_p(p->pos.z, 2));
  sb_append_format(&sb, "Block: % % %  Chunk: % %\n", fmt_i32(bx), fmt_i32(by), fmt_i32(bz), fmt_i32(bx >> 4),
                   fmt_i32(bz >> 4));
  sb_append_format(&sb, "Facing: % (%\xc2\xb0)  pitch %\xc2\xb0\n", fmt_cstr(facing), fmt_u32((u32)yaw_deg),
                   fmt_i32((i32)(p->pitch * 180.0f / PI)));
  sb_append_format(&sb, "Biome: %  Light: sky % block %\n", fmt_cstr(wc_biome_name[col.biome]), fmt_u32(l >> 4),
                   fmt_u32(l & 15));
  String clock = wc_clock_string(g->day_time, &tmp.allocator);
  sb_append_format(&sb, "Time: %  %%  Rain: %%\n", fmt_str(clock),
                   fmt_cstr(p->flying ? "Flying" : p->on_ground ? "Ground" : "Air"), fmt_cstr(p->in_water ? " Water" : ""),
                   fmt_u32((u32)(g->rain * 100 + 0.5f)), fmt_char('%'));
  sb_append_format(&sb, "Chunks: %  sections drawn: %  pending gen % mesh %\n", fmt_u32(g->world.stats.chunks),
                   fmt_u32(g->renderer.stats.sections), fmt_u32(g->world.stats.pending_gen),
                   fmt_u32(g->world.stats.pending_mesh));
  sb_append_format(&sb, "Draw calls: % + shadow %  VRAM(mesh): % MB\n", fmt_u32(g->renderer.stats.draw_calls),
                   fmt_u32(g->renderer.stats.shadow_draw_calls),
                   fmt_f32_p((f32)(g->meshes.quads_used * WC_QUAD_BYTES) / 1048576.0f, 1));
  sb_append_format(&sb, "Seed: %", fmt_u32(g->seed));
  if (g->target.hit)
    sb_append_format(&sb, "\nTarget: % @ % % %", fmt_cstr(wc_block_label[g->target.id]), fmt_i32(g->target.x),
                     fmt_i32(g->target.y), fmt_i32(g->target.z));
  char *s = sb_get(&sb);
  u32 len = cstr_len(s);
  u32 n = len < 1023 ? len : 1023;
  mem_cpy(g->debug_text, s, n);
  g->debug_text[n] = 0;
  tctx_temp_allocator_end(tmp);
}

// ---- frame ----

#define WC_LOAD_BUDGET_MS 40.0f    // loading screen: the spawn area streams in one or two frames
#define WC_READY_BUDGET_MS 4.0f    // spawn meshed: keep the last loading frames short
#define WC_BURST_BUDGET_MS 20.0f   // the player's own column is missing
#define WC_BACKLOG_BUDGET_MS 12.0f // most of the view distance is missing, as after a teleport
#define WC_BACKLOG_PROGRESS 0.9f

// main lane, first thing in a frame: interval of frames that were not cpu-bound estimates the frame cadence
hz_internal void wc_frame_begin(WcGame *g) {
  u64 now = os_time_now();
  if (g->frame_t0) {
    f32 dt = (f32)os_ticks_to_ms(os_time_diff(now, g->frame_t0));
    if (g->frame_cpu_ms < dt * 0.75f) g->frame_period_ms += (dt - g->frame_period_ms) * 0.1f;
  }
  g->frame_t0 = now;
}

// job-phase length: bursts while the player's surroundings are missing, otherwise the frame's idle time
hz_internal f32 wc_stream_budget(WcGame *g) {
  WcPlayer *p = &g->player;
  f32 idle = m_clampf(g->frame_period_ms * 0.8f - g->main_ms, 1.5f, 12.0f);
  if (!g->started) return g->spawn_ready ? m_minf(idle, WC_READY_BUDGET_MS) : WC_LOAD_BUDGET_MS;
  if (!wc_world_is_ready(&g->world, p->pos.x, p->pos.z)) return WC_BURST_BUDGET_MS;
  if (wc_world_load_progress(&g->world, g->world.render_distance) < WC_BACKLOG_PROGRESS)
    return m_maxf(idle, WC_BACKLOG_BUDGET_MS);
  return idle;
}

hz_internal void wc_game_update(WcGame *g, AppMemory *memory) {
  u64 cpu0 = os_time_now();
  f32 dt = m_clampf(memory->dt, 0.0001f, 0.1f);
  g->elapsed += dt;
  input_update(&g->input, &memory->input_events, memory->total_time);
  g->fps_frames++;
  g->fps_time += dt;
  if (g->fps_time >= 0.5f) {
    g->fps = g->fps_frames / g->fps_time;
    g->fps_frames = 0;
    g->fps_time = 0;
  }
  wc_boot_poll(g);
  if (!g->world_created) return;

  WcPlayer *p = &g->player;
  wc_mesh_store_begin_frame(&g->meshes);
  wc_world_integrate(&g->world);
  // before the budget is chosen: the frame that finds the spawn area meshed must not run a full loading round
  if (!g->started) {
    f32 prog = wc_world_load_progress(&g->world, (i32)m_minf(4.0f, (f32)g->settings.render_distance));
    g->spawn_ready = wc_world_is_ready(&g->world, p->pos.x, p->pos.z) && prog >= 0.98f;
  }
  b32 near_first = g->started ? !wc_world_is_ready(&g->world, p->pos.x, p->pos.z) : !g->spawn_ready;
  wc_world_update(&g->world, p->pos.x, p->pos.z, wc_stream_budget(g), near_first);

  if (!g->started) {
    if (g->spawn_ready && wc_render_ready(&g->renderer) && ++g->ready_frames > 2) {
      g->started = true;
      g->boot = WC_BOOT_DONE;
      // make sure we are not stuck inside terrain
      i32 bx = wc_floor_i(p->pos.x), bz = wc_floor_i(p->pos.z);
      i32 y = wc_floor_i(p->pos.y);
      if (y < 1) y = 1;
      while (y < WC_HEIGHT - 2 &&
             (wc_solid[wc_world_block(&g->world, bx, y, bz)] || wc_solid[wc_world_block(&g->world, bx, y + 1, bz)]))
        y++;
      p->pos.y = y + 0.001;
      if (g->mode == WC_MODE_LOADING) wc_game_set_mode(g, g->test_play ? WC_MODE_PLAYING : WC_MODE_MENU);
    }
  }
  if (g->has_pending_above && wc_world_is_ready(&g->world, p->pos.x, p->pos.z)) {
    // test tooling: place the camera a given height above the surface
    i32 bx = wc_floor_i(p->pos.x), bz = wc_floor_i(p->pos.z);
    i32 y = WC_HEIGHT - 1;
    while (y > 0 && !wc_solid[wc_world_block(&g->world, bx, y, bz)] && wc_world_block(&g->world, bx, y, bz) != B_WATER)
      y--;
    p->pos.y = y + 1 + g->pending_above;
    g->has_pending_above = false;
    wc_render_invalidate_history(&g->renderer);
  }

  wc_handle_input(g, dt);
  if (g->mode == WC_MODE_PLAYING || g->mode == WC_MODE_MENU) wc_auto_tune(g, dt);
  if (g->started && (wc_world_is_ready(&g->world, p->pos.x, p->pos.z) || p->flying)) {
    WcPlayerInput pin = wc_player_input(g);
    wc_player_update(p, dt, &pin, &g->world, g->elapsed);
  }
  g->water_timer += dt;
  if (g->water_timer > 0.15f && g->water_count) {
    g->water_timer = 0;
    wc_tick_water(g);
  }
  wc_update_weather(g, dt);
  // footsteps and splashes
  if (g->started && p->on_ground && !p->flying) {
    g->step_dist += m_sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z) * dt;
    if (g->step_dist > (p->sprinting ? 2.1f : 1.7f)) {
      g->step_dist = 0;
      wc_audio_step(&g->audio, wc_world_block(&g->world, wc_floor_i(p->pos.x), wc_floor_i(p->pos.y - 0.1),
                                              wc_floor_i(p->pos.z)));
    }
  }
  if (p->in_water && !g->was_in_water && m_absf(p->vel.y) > 2) wc_audio_splash(&g->audio);
  g->was_in_water = p->in_water;
  // day cycle
  g->day_time = m_fmodf(g->day_time + dt * g->time_scale / (g->settings.day_length * 60.0f) + 1.0f, 1.0f);

  WcV3d eye = wc_player_eye(p);
  g->target = (g->mode == WC_MODE_PLAYING) ? wc_raycast(&g->world, eye, wc_player_forward(p), 5.5f) : (WcRayHit){0};
  // smoothed eye sky light drives exposure and fog
  u8 el = wc_world_light(&g->world, wc_floor_i(eye.x), wc_floor_i(eye.y), wc_floor_i(eye.z));
  g->eye_sky += ((f32)(el >> 4) / 15.0f - g->eye_sky) * (1 - m_expf(-2 * dt));

  wc_entities_update(&g->ents, dt, &g->world);
  b32 warm_night = g->day_time > 0.53f && g->day_time < 0.97f && g->rain < 0.2f && g->snow < 0.2f;
  wc_entities_update_fireflies(&g->ents, dt, &g->world, wc_player_camera(p, g->settings.bobbing), warm_night && g->started);

  // saves: debounced after edits, periodic for the player position
  if (g->save_debounce > 0) {
    g->save_debounce -= dt;
    if (g->save_debounce <= 0) wc_save_world(g);
  }
  g->meta_timer += dt;
  if (g->meta_timer > WC_META_INTERVAL) {
    g->meta_timer = 0;
    wc_save_world(g);
  }
  // quitting finishes this frame: persist the world while it still exists
  if (hz_quit_requested()) wc_save_world(g);
  if (g->toast_t < 10.0f) g->toast_t += dt;
  g->debug_timer -= dt;
  if (g->show_debug && g->debug_timer <= 0) {
    g->debug_timer = 0.25f;
    wc_update_debug_text(g);
  }
  {
    TempAllocator gt = tctx_temp_allocator_begin(NULL);
    f32 *groups = ALLOC_ARRAY(&gt.allocator, f32, WC_GPU_GROUP_COUNT);
    f32 gpu_frame = 0;
    if (wc_render_gpu_times(&g->renderer, groups, &gpu_frame) && gpu_frame > 0)
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

hz_internal void wc_game_render(WcGame *g, AppMemory *memory) {
  f32 dt = m_clampf(memory->dt, 0.0001f, 0.1f);
  WcPlayer *p = &g->player;
  WcV3d cam = wc_player_camera(p, g->settings.bobbing);
  WcEntityGeometry ents = {.hand_rot = m4_identity()};
  if (g->world_created)
    ents = wc_entities_build(&g->ents, &g->world, cam, p->yaw, p->pitch, g->hotbar[g->selected], p->bob,
                             g->settings.bobbing ? p->bob_amount : 0, !g->hide_hud && g->started);
  WcFrameInput in = {
      .cam = cam,
      .yaw = p->yaw,
      .pitch = p->pitch,
      .fov = g->settings.fov * (1 + p->fov_boost),
      .time = g->elapsed,
      .day_time = g->day_time,
      .dt = dt,
      .underwater = p->head_in_water,
      .eye_sky = g->eye_sky,
      .has_selection = g->target.hit && !g->hide_hud,
      .sel = {g->target.x, g->target.y, g->target.z},
      .cloud_coverage = g->settings.cloud_coverage,
      .rain = g->rain,
      .wetness = g->wetness,
      .snow = g->snow,
      .snow_cover = g->snow_cover,
      .flash = g->flash,
      .crosshair = g->mode == WC_MODE_PLAYING && !g->hide_hud,
      .dpr = memory->dpr,
  };
  // the swapchain is the logical canvas at the effective dpr
  wc_render_frame(&g->renderer, &g->world, &in, &ents, (u32)(memory->canvas_width * memory->dpr),
                  (u32)(memory->canvas_height * memory->dpr));
  if (g->world_created) wc_world_clear_mesh_changes(&g->world);
  wc_audio_update(&g->audio, dt, g->settings.volume, g->rain, g->eye_sky);

  ui_begin_frame(g->ui, &g->input, dt);
  wc_ui_frame(g, dt);
  wc_render_timer_begin(&g->renderer, WC_GPU_UI);
  ui_end_frame(g->ui);
  wc_render_timer_end(&g->renderer);
  os_set_cursor(g->mode == WC_MODE_PLAYING ? OS_CURSOR_ARROW : g->cursor);

  if (g->screenshot_requested) {
    g->screenshot_requested = false;
    Allocator fa = tctx_temp_allocator(NULL);
    String path = str_format(&fa, "webcraft-%.png", fmt_u64(os_time_unix_ms()));
    if (hz_screenshot_request(path.value)) wc_game_toast(g, "Screenshot saved");
  }
}

// ---- test commands (--commands) ----

hz_internal b32 wc_arg_f32(CString args, const char *key, f32 *out) {
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

// wc_view:x=.. y=.. z=.. yaw=.. pitch=.. time=.. fly=1 debug=n rain=.. snow=.. flash=.. above=.. scale=..
hz_internal void wc_cmd_view(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  WcPlayer *p = &g->player;
  // a scripted camera never overwrites the player's save
  g->no_save = true;
  f32 v;
  if (wc_arg_f32(ctx->args, "x", &v)) p->pos.x = v;
  if (wc_arg_f32(ctx->args, "y", &v)) p->pos.y = v;
  if (wc_arg_f32(ctx->args, "z", &v)) p->pos.z = v;
  if (wc_arg_f32(ctx->args, "yaw", &v)) p->yaw = v;
  if (wc_arg_f32(ctx->args, "pitch", &v)) p->pitch = v;
  if (wc_arg_f32(ctx->args, "time", &v)) g->day_time = v;
  if (wc_arg_f32(ctx->args, "fly", &v)) p->flying = v > 0.5f;
  if (wc_arg_f32(ctx->args, "scale", &v)) {
    g->settings.render_scale = m_clampf(v, 0.5f, 1.0f);
    g->auto_quality = false;
    wc_game_apply_settings(g, false);
  }
  if (wc_arg_f32(ctx->args, "debug", &v)) g->renderer.debug_view = (u32)v;
  if (wc_arg_f32(ctx->args, "above", &v)) {
    g->pending_above = v;
    g->has_pending_above = true;
  }
  if (wc_arg_f32(ctx->args, "snow", &v)) {
    g->snow = v;
    g->coldness = v;
    g->climate_timer = 1e9f;
  }
  if (wc_arg_f32(ctx->args, "flash", &v)) {
    g->flash = v;
    g->flash_pulses = 0;
  }
  if (wc_arg_f32(ctx->args, "rain", &v)) {
    g->flash_timer = 1e9f;
    g->rain = v;
    g->rain_target = v;
    g->wetness = v * (1 - g->snow);
    g->snow_cover = v * g->snow;
    g->settings.weather = v > 0.5f ? WC_WEATHER_RAIN : WC_WEATHER_CLEAR;
  }
  p->vel = (v3){0, 0, 0};
  wc_render_invalidate_history(&g->renderer);
}

// wc_seed:<n>: fresh world from a fixed seed; this session no longer saves
hz_internal void wc_cmd_seed(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  f64 v = 0;
  if (!cstr_to_f64(ctx->args, &v)) return;
  g->no_save = true;
  g->auto_quality = false;
  g->forced_seed = (u32)v;
  g->has_forced_seed = true;
  if (g->boot == WC_BOOT_READ_SAVES) return;
  wc_edits_clear(&g->edits);
  mem_cpy(g->hotbar, wc_default_hotbar, WC_HOTBAR_SLOTS);
  g->seed = g->forced_seed;
  wc_start_world(g, NULL);
}

// wc_hud:0|1
hz_internal void wc_cmd_hud(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  g->hide_hud = ctx->args && ctx->args[0] == '0';
}

// wc_play: enter play without grabbing the mouse (scripted runs)
hz_internal void wc_cmd_play(const HzCommandCtx *ctx, void *user) {
  UNUSED(ctx);
  WcGame *g = user;
  g->test_play = true;
  if (g->started) g->mode = WC_MODE_PLAYING;
}

// wc_mode:menu|settings|inventory|paused
hz_internal void wc_cmd_mode(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  String a = cstr_to_str(ctx->args);
  if (str_equal(a, STR_FROM_CSTR("menu"))) g->mode = WC_MODE_MENU;
  else if (str_equal(a, STR_FROM_CSTR("paused"))) g->mode = WC_MODE_PAUSED;
  else if (str_equal(a, STR_FROM_CSTR("inventory"))) g->mode = WC_MODE_INVENTORY;
  else if (str_equal(a, STR_FROM_CSTR("settings"))) {
    g->settings_return = WC_MODE_MENU;
    g->mode = WC_MODE_SETTINGS;
  }
}

// wc_preset:low|medium|high|ultra
hz_internal void wc_cmd_preset(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  String a = cstr_to_str(ctx->args);
  WcPreset p = str_equal(a, STR_FROM_CSTR("low"))      ? WC_PRESET_LOW
               : str_equal(a, STR_FROM_CSTR("medium")) ? WC_PRESET_MEDIUM
               : str_equal(a, STR_FROM_CSTR("ultra"))  ? WC_PRESET_ULTRA
                                                       : WC_PRESET_HIGH;
  wc_settings_apply_preset(&g->settings, p);
  g->auto_quality = false;
  wc_game_apply_settings(g, false);
}

// wc_setblock:x y z id
hz_internal void wc_cmd_setblock(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  f32 x, y, z, id;
  if (wc_arg_f32(ctx->args, "x", &x) && wc_arg_f32(ctx->args, "y", &y) && wc_arg_f32(ctx->args, "z", &z) &&
      wc_arg_f32(ctx->args, "id", &id) && id >= 0 && id < B_COUNT)
    wc_world_set_block(&g->world, (i32)x, (i32)y, (i32)z, (u8)id, true);
}

// css radial-gradient(ellipse at center, rgba(0,0,0,.25), rgba(0,0,0,.6)), stretched over the screen
hz_internal GpuTexture wc_make_scrim(void) {
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
  WcGame *g = memory->state;
  if (is_main_thread()) {
    wc_render_create_pipelines(&g->renderer);
    // compiles start now, not when init returns: the rest of init overlaps them
    gpu_flush();
    g->arena = arena_create(GB(1), MB(1));
    g->alloc = make_arena_allocator(g->arena);
    wc_textures_alloc(&g->tex, &g->alloc);
    Allocator icon_ta = tctx_temp_allocator(NULL);
    g->icon_pixels = ALLOC_ARRAY_NO_ZERO(&icon_ta, u8, B_COUNT * WC_ICON_SIZE * WC_ICON_SIZE * 4);
  }
  lane_sync();
  wc_textures_generate(&g->tex);
  lane_sync();
  wc_icons_raster(g->icon_pixels, &g->tex, WC_ICON_SIZE);
  lane_sync();
  if (!is_main_thread()) return;

  Allocator *a = &g->alloc;
  g->input = input_init();
  g->assets = asset_system_create(NULL);
  g->ui = ui_create(NULL, g->assets);
  g->rng = random_create(os_time_unix_ms());
  wc_settings_defaults(&g->settings);
  g->renderer.settings = wc_settings_render(&g->settings);
  g->renderer.gpu_timing = true;
  wc_render_init(&g->renderer, a, &g->tex, (u32)(memory->canvas_width * memory->dpr),
                 (u32)(memory->canvas_height * memory->dpr), memory->dpr);
  g->icons = ALLOC_ARRAY(a, GpuTexture, WC_BLOCK_IDS);
  wc_icons_upload(g->icons, g->icon_pixels, WC_ICON_SIZE);
  g->icon_pixels = NULL;
  g->scrim = wc_make_scrim();
  wc_entities_init(&g->ents, a, os_time_unix_ms());
  wc_audio_init(&g->audio, a);
  wc_edits_init(&g->edits);
  wc_mesh_store_init(&g->meshes, a);

  g->hotbar = ALLOC_ARRAY(a, u8, WC_HOTBAR_SLOTS);
  mem_cpy(g->hotbar, wc_default_hotbar, WC_HOTBAR_SLOTS);
  g->toast = ALLOC_ARRAY(a, char, WC_TOAST_MAX);
  g->debug_text = ALLOC_ARRAY(a, char, 1024);
  g->water_q = ALLOC_ARRAY(a, WcWaterCell, WC_WATER_QUEUE_MAX);
  g->perf_samples = ALLOC_ARRAY(a, f32, WC_PERF_SAMPLES);
  g->sliders = ALLOC_ARRAY(a, UIInteract_Slider, WC_SETTINGS_SLIDERS);
  g->dropdowns = ALLOC_ARRAY(a, UIInteract_Dropdown, WC_SETTINGS_DROPDOWNS);
  g->button_anims = ALLOC_ARRAY(a, UIInteractAnim, WC_UI_BUTTONS);
  wc_player_init(&g->player);
  g->player.sensitivity = 0.0022f * g->settings.sensitivity;
  g->day_time = 0.08f;
  g->frame_period_ms = 16.667f;
  g->time_scale = 1;
  g->eye_sky = 1;
  g->flash_timer = 20;
  g->weather_timer = 240 + random_f32(&g->rng) * 360;
  g->mode = WC_MODE_LOADING;
  g->boot = WC_BOOT_READ_SAVES;

  // saves live in the per-user app data dir; wasm keeps them in opfs under webcraft/
  char *dir = ALLOC_ARRAY(a, char, 1024);
  u32 n = os_app_data_dir("saves", dir, 1024);
  String base = n ? STR(dir, n) : STR_FROM_CSTR("webcraft");
  g->settings_path = str_format(a, "%/settings.hza", fmt_str(base));
  g->world_path = str_format(a, "%/world.hza", fmt_str(base));
  os_create_dir(base.value);
  g->settings_op = os_file_exists(g->settings_path.value) ? os_start_read_file(g->settings_path.value) : NULL;
  g->world_op = os_file_exists(g->world_path.value) ? os_start_read_file(g->world_path.value) : NULL;

  hz_command_register("wc_view", wc_cmd_view, g);
  hz_command_register("wc_play", wc_cmd_play, g);
  hz_command_register("wc_mode", wc_cmd_mode, g);
  hz_command_register("wc_preset", wc_cmd_preset, g);
  hz_command_register("wc_setblock", wc_cmd_setblock, g);
  hz_command_register("wc_seed", wc_cmd_seed, g);
  hz_command_register("wc_hud", wc_cmd_hud, g);
}

HZ_APP_API void app_update_and_render(AppMemory *memory) {
  WcGame *g = memory->state;
  // collective: every lane participates
  asset_system_update(g->assets);
  ui_font_update(g->ui);
  b32 main = is_main_thread();
  if (main) {
    wc_frame_begin(g);
    wc_game_update(g, memory);
  }
  lane_sync();
  if (main) {
    wc_game_render(g, memory);
    f32 ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), g->frame_t0));
    g->main_ms += (ms - g->main_ms) * 0.1f;
  }
  // world_created is published by the sync above, so every lane agrees on entering the job rounds
  if (g->world_created) wc_world_run_jobs(&g->world);
  if (main) g->frame_cpu_ms = (f32)os_ticks_to_ms(os_time_diff(os_time_now(), g->frame_t0));
}

HZ_APP_API size_t app_state_size(void) { return sizeof(WcGame); }

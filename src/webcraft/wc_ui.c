#include "webcraft/wc_game.h"
#include "input.h"

// screens (loading, menu, settings, inventory, error) and the hud, after index.html

#define WC_C_TEXT ((HzUIColor){242, 242, 242, 255})
#define WC_C_MUTED ((HzUIColor){169, 176, 189, 255})
#define WC_C_ACCENT ((HzUIColor){127, 211, 107, 255})
#define WC_C_PANEL ((HzUIColor){12, 14, 20, 184})
#define WC_C_PANEL_BORDER ((HzUIColor){255, 255, 255, 31})
#define WC_C_BTN ((HzUIColor){255, 255, 255, 20})
#define WC_C_BTN_HI ((HzUIColor){255, 255, 255, 41})
#define WC_C_BTN_BORDER ((HzUIColor){255, 255, 255, 36})
#define WC_C_INSET ((HzUIColor){255, 255, 255, 20})
#define WC_C_POPUP ((HzUIColor){27, 30, 37, 250})

#define WC_SETTING_LABEL_W 170.0f
#define WC_SETTING_VALUE_W 60.0f
#define WC_SLIDER_TRACK_W 250.0f
#define WC_SLIDER_THUMB_W 14.0f
#define WC_INV_COLS 10
#define WC_INV_CELL 54.0f
#define WC_INV_GAP 6.0f

typedef enum {
  WC_BTN_PLAY,
  WC_BTN_SETTINGS,
  WC_BTN_NEW_WORLD,
  WC_BTN_SETTINGS_DONE,
  WC_BTN_MODAL_CANCEL,
  WC_BTN_MODAL_CONFIRM,
  WC_BTN_INVENTORY_DONE,
} WcButtonSlot;

// a phone held in landscape is about 390 ui px tall: screens drop to their compact layout
#define WC_COMPACT_H 500.0f

hz_internal HzUIString wc_uis(const char *s) { return (HzUIString){.chars = s, .length = (i32)cstr_len(s)}; }

hz_internal HzUIString wc_uistr(String s) { return (HzUIString){.chars = s.value, .length = (i32)s.len}; }

// dynamic text lives on the non-scoped temp allocator until the ui is drawn
hz_internal Allocator wc_frame_alloc(void) { return tctx_temp_allocator(NULL); }

hz_internal b32 wc_released_on(WcGame *g) {
  return ui_hovered() && g->input.buttons[MOUSE_LEFT].released_this_frame;
}

hz_internal b32 wc_button(WcGame *g, HzUIElementId id, const char *label, WcButtonSlot slot, b32 primary, b32 grow) {
  UIInteractAnim *anim = &g->button_anims[slot];
  UIGradient grad = {0};
  HzUIColor bg = ui_color_lerp(WC_C_BTN, WC_C_BTN_HI, anim->hover);
  HzUIColor border = WC_C_BTN_BORDER;
  if (primary) {
    HzUIColor top = ui_color_lerp((HzUIColor){98, 176, 79, 255}, (HzUIColor){111, 195, 91, 255}, anim->hover);
    HzUIColor bot = ui_color_lerp((HzUIColor){63, 138, 51, 255}, (HzUIColor){71, 154, 58, 255}, anim->hover);
    grad = (UIGradient){.kind = UI_GRADIENT_LINEAR, .angle = 180, .stop_count = 2,
                        .stops = {{0, top}, {1, bot}}};
    border = WC_C_ACCENT;
  }
  b32 clicked = false;
  HzUITransform pressed = {.translate = {0, anim->press}};
  Optional(HzUITransform) transform = anim->press > 0.01f ? opt_some(HzUITransform, pressed) : opt_none(HzUITransform);
  ui_element({
      .id = id,
      .layout = {.sizing = {.width = grow ? ui_sizing_grow(0) : ui_sizing_fit(0), .height = ui_sizing_fixed(44)},
                 .padding = {.left = 16, .right = 16},
                 .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
      .background_color = bg,
      .gradient = grad,
      .corner_radius = ui_corner_radius(10),
      .border = {.color = border, .width = ui_border_outside(1)},
      .transform = transform,
  }) {
    *anim = ui_interact_anim(g->ui, id, &g->input);
    clicked = wc_released_on(g);
    if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
    ui_text(wc_uis(label), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = WC_C_TEXT}));
  }
  return clicked;
}

hz_internal void wc_title(const char *text, f32 size) {
  ui_text(wc_uis(text), ui_text_config({.fontSize = size, .weight = UI_FONT_BOLD, .letterSpacing = size > 40 ? 1 : 0,
                                        .textColor = (HzUIColor){236, 245, 232, 255}}));
}

hz_internal void wc_muted(const char *text, f32 size) {
  ui_text(wc_uis(text), ui_text_config({.fontSize = size, .textColor = WC_C_MUTED, .lineHeight = size * 1.4f,
                                        .wrapMode = UI_TEXT_WRAP_WORDS}));
}

// full-screen scrim (radial vignette) with a centred column for the panel
hz_internal HzUIElementDesc wc_screen_desc(const WcGame *g, HzUIElementId id) {
  return (HzUIElementDesc){
      .id = id,
      .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 10},
      .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                 .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
      .image = {.texture = g->scrim},
  };
}

hz_internal HzUIElementDesc wc_panel_desc(HzUIElementId id, f32 panel_w) {
  return (HzUIElementDesc){
      .id = id,
      .layout = {.sizing = {.width = panel_w > 0 ? ui_sizing_fixed(panel_w) : ui_sizing_fit(0),
                            .height = ui_sizing_fit(0)},
                 .layout_direction = UI_TOP_TO_BOTTOM,
                 .padding = {.left = 32, .right = 32, .top = 28, .bottom = 28},
                 .child_gap = 8},
      .background_color = WC_C_PANEL,
      .corner_radius = ui_corner_radius(16),
      .corner_smoothing = 0.6f,
      .border = {.color = WC_C_PANEL_BORDER, .width = ui_border_outside(1)},
      .shadow = {.color = {0, 0, 0, 128}, .offset_y = 20, .blur = 60},
  };
}

// ---- loading ----

hz_internal void wc_loading_screen(WcGame *g) {
  f32 prog = 0.05f;
  const char *text = "Starting...";
  if (g->boot != WC_BOOT_START) {
    f32 wp = g->world_created ? wc_world_load_progress(&g->world, (i32)m_minf(4.0f, (f32)g->settings.render_distance)) : 0;
    prog = 0.3f + wp * 0.7f;
    Allocator fa = wc_frame_alloc();
    text = str_format(&fa, "Generating terrain... %%", fmt_u32((u32)(wp * 100 + 0.5f)), fmt_char('%')).value;
  }
  HzUIElementDesc sd1 = wc_screen_desc(g, ui_id("LoadingScreen"));
  ui_element_of(&sd1) {
    HzUIElementDesc pd6 = wc_panel_desc(ui_id("LoadingPanel"), 0);
    ui_element_of(&pd6) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_alignment = {.x = UI_ALIGN_X_CENTER}, .child_gap = 2}}) {
        wc_title("WebCraft", 44);
        wc_muted("Shaders Edition", 14);
        ui_element({.id = ui_id("LoadingBar"),
                    .layout = {.sizing = {.width = ui_sizing_fixed(320), .height = ui_sizing_fixed(8)},
                               .margin = {.top = 14}},
                    .background_color = {255, 255, 255, 26},
                    .corner_radius = ui_corner_radius(4)}) {
          ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(320 * m_clampf(prog, 0, 1)),
                                            .height = ui_sizing_grow(0)}},
                      .corner_radius = ui_corner_radius(4),
                      .gradient = {.kind = UI_GRADIENT_LINEAR, .angle = 90, .stop_count = 2,
                                   .stops = {{0, {98, 176, 79, 255}}, {1, {155, 224, 127, 255}}}}}) {}
        }
        ui_element({.layout = {.margin = {.top = 8}}}) {
          ui_text(wc_uis(text), ui_text_config({.fontSize = 13, .textColor = WC_C_MUTED}));
        }
      }
    }
  }
}

// ---- menu ----

typedef struct {
  const char *key;
  const char *what;
} WcControlRow;

hz_internal const WcControlRow WC_CONTROLS[] = {
    {"W A S D", "Move \xc2\xb7 double-tap W or Ctrl to sprint"},
    {"Space", "Jump \xc2\xb7 double-tap to fly (or F)"},
    {"Shift", "Sneak \xc2\xb7 fly down"},
    {"Left / Right click", "Break / place block"},
    {"Middle click", "Pick block"},
    {"1\xe2\x80\x93" "9 / wheel", "Select hotbar slot"},
    {"E", "Block inventory"},
    {"T (hold)", "Fast-forward time \xc2\xb7 Shift+T rewind"},
    {"F1 / F2 / F3", "Hide HUD / screenshot / debug info"},
};

hz_internal void wc_new_world_modal(WcGame *g) {
  f32 t = ui_spring(g->ui, ui_id("NewWorldModalT"), g->new_world_modal.open ? 1.0f : 0.0f);
  if (t < 0.01f) return;
  ui_element({.id = ui_id("NewWorldScrim"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 30},
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
              .background_color = {0, 0, 0, 140.0f * t}}) {
    ui_interact_modal(&g->new_world_modal, &g->input);
    HzUIElementDesc pd7 = wc_panel_desc(ui_id("NewWorldPanel"), 380);
    ui_element_of(&pd7) {
      ui_interact_modal_content(&g->new_world_modal);
      wc_title("New World", 24);
      wc_muted("Start a brand new world? Your current world will be replaced.", 14);
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 10, .padding = {.top = 10}}}) {
        if (wc_button(g, ui_id("NewWorldCancelBtn"), "Cancel", WC_BTN_MODAL_CANCEL, false, true))
          ui_interact_modal_close(&g->new_world_modal);
        if (wc_button(g, ui_id("NewWorldConfirmBtn"), "New World", WC_BTN_MODAL_CONFIRM, true, true)) {
          ui_interact_modal_close(&g->new_world_modal);
          wc_game_new_world(g);
        }
      }
    }
  }
}

hz_internal const WcControlRow WC_TOUCH_CONTROLS[] = {
    {"Left thumb", "Move \xc2\xb7 push to the edge to sprint"},
    {"Drag", "Look around"},
    {"Tap / hold", "Place / break a block"},
    {"Jump", "Double-tap to fly \xc2\xb7 Sneak flies down"},
};

hz_internal void wc_menu_screen(WcGame *g) {
  b32 compact = (f32)g->ui->canvas_height < WC_COMPACT_H;
  const WcControlRow *controls = g->touch_mode ? WC_TOUCH_CONTROLS : WC_CONTROLS;
  u32 control_count = g->touch_mode ? ARRAY_SIZE(WC_TOUCH_CONTROLS) : ARRAY_SIZE(WC_CONTROLS);
  HzUIElementDesc sd2 = wc_screen_desc(g, ui_id("MenuScreen"));
  ui_element_of(&sd2) {
    HzUIElementDesc pd8 = wc_panel_desc(ui_id("MenuPanel"), 460);
    if (compact) pd8.layout.padding = (HzUIPadding){.left = 24, .right = 24, .top = 18, .bottom = 18};
    ui_element_of(&pd8) {
      wc_title("WebCraft", compact ? 30 : 44);
      ui_element({.layout = {.margin = {.bottom = compact ? 6 : 12}}}) {
        wc_muted("Shaders Edition \xe2\x80\x94 a voxel sandbox", 14);
      }
      const char *play = g->mode == WC_MODE_PAUSED ? "Resume" : g->touch_mode ? "Tap to Play" : "Click to Play";
      if (wc_button(g, ui_id("PlayBtn"), play, WC_BTN_PLAY, true, true))
        wc_game_lock(g);
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 10}}) {
        if (wc_button(g, ui_id("SettingsBtn"), "Settings", WC_BTN_SETTINGS, false, true)) {
          g->settings_return = g->mode;
          wc_game_set_mode(g, WC_MODE_SETTINGS);
        }
        if (wc_button(g, ui_id("NewWorldBtn"), "New World", WC_BTN_NEW_WORLD, false, true))
          ui_interact_modal_open(&g->new_world_modal);
      }
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_gap = 3, .padding = {.top = compact ? 8 : 14}}}) {
        for (u32 i = 0; i < control_count; i++) {
          ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 14}}) {
            ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(130)}}}) {
              ui_text(wc_uis(controls[i].key),
                      ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = WC_C_TEXT}));
            }
            ui_text(wc_uis(controls[i].what), ui_text_config({.fontSize = 13, .textColor = WC_C_MUTED}));
          }
        }
      }
      if (!compact) ui_element({.layout = {.padding = {.top = 14}}}) {
#ifdef WASM
        wc_muted("Renderer: WebGPU", 12);
#else
        wc_muted("Renderer: Direct3D 12", 12);
#endif
      }
    }
  }
  wc_new_world_modal(g);
}

// ---- settings ----

hz_internal void wc_setting_label(const char *label) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(WC_SETTING_LABEL_W)}}}) {
    ui_text(wc_uis(label), ui_text_config({.fontSize = 13, .textColor = WC_C_MUTED}));
  }
}

hz_internal void wc_setting_value(String text) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(WC_SETTING_VALUE_W)}}}) {
    ui_text(wc_uistr(text),
            ui_text_config({.fontSize = 13, .textColor = WC_C_TEXT, .textAlignment = UI_TEXT_ALIGN_RIGHT}));
  }
}

#define wc_setting_row(name, i)                                                                                \
  ui_element({.id = ui_idi(name, i),                                                                           \
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(30)},                \
                         .child_gap = 10,                                                                      \
                         .child_alignment = {.y = UI_ALIGN_Y_CENTER}}})

hz_internal String wc_slider_text(WcSliderId id, f32 v, f32 day_time) {
  Allocator fa = wc_frame_alloc();
  switch (id) {
  case WC_SLIDER_RENDER_DISTANCE: return str_format(&fa, "% ch", fmt_u32((u32)(v + 0.5f)));
  case WC_SLIDER_RENDER_SCALE:
  case WC_SLIDER_VOLUME:
  case WC_SLIDER_CLOUD_COVERAGE: return str_format(&fa, "%%", fmt_u32((u32)(v * 100 + 0.5f)), fmt_char('%'));
  case WC_SLIDER_SHADOW_DISTANCE: return str_format(&fa, "% m", fmt_u32((u32)(v + 0.5f)));
  case WC_SLIDER_FOV: return str_format(&fa, "%\xc2\xb0", fmt_u32((u32)(v + 0.5f)));
  case WC_SLIDER_SENSITIVITY: return str_format(&fa, "%\xc3\x97", fmt_f32_p(v, 2));
  case WC_SLIDER_DAY_LENGTH: return str_format(&fa, "% min", fmt_u32((u32)(v + 0.5f)));
  case WC_SLIDER_TIME_OF_DAY: return wc_clock_string(day_time, &fa);
  }
  return STR_FROM_CSTR("");
}

// returns true when the user moved it; *value snaps to step
hz_internal b32 wc_slider(WcGame *g, WcSliderId id, const char *label, f32 *value, f32 min, f32 max, f32 step) {
  UIInteract_Slider *s = &g->sliders[id];
  if (!s->drag.dragging) s->value = *value;
  // from the current value: ui_interact_slider runs inside the thumb, after the fill is laid out
  f32 thumb = max > min ? m_clampf((s->value - min) / (max - min), 0, 1) * (WC_SLIDER_TRACK_W - WC_SLIDER_THUMB_W) : 0;
  b32 changed = false;
  wc_setting_row("SliderRow", id) {
    wc_setting_label(label);
    ui_element({.id = ui_idi("SliderTrack", id),
                .layout = {.sizing = {.width = ui_sizing_fixed(WC_SLIDER_TRACK_W), .height = ui_sizing_fixed(4)}},
                .background_color = {255, 255, 255, 38},
                .corner_radius = ui_corner_radius(2)}) {
      // filled part of the track
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(thumb + WC_SLIDER_THUMB_W * 0.5f),
                                        .height = ui_sizing_grow(0)}},
                  .background_color = WC_C_ACCENT, .corner_radius = ui_corner_radius(2)}) {}
      ui_element({.id = ui_idi("SliderThumb", id),
                  .floating = {.attach_to = UI_ATTACH_TO_PARENT, .offset = {thumb, -5},
                               .clip_to = UI_CLIP_TO_ATTACHED_PARENT},
                  .layout = {.sizing = {.width = ui_sizing_fixed(WC_SLIDER_THUMB_W),
                                        .height = ui_sizing_fixed(WC_SLIDER_THUMB_W)}},
                  .background_color = s->drag.dragging ? (HzUIColor){170, 235, 150, 255} : WC_C_ACCENT,
                  .corner_radius = ui_corner_radius(WC_SLIDER_THUMB_W * 0.5f)}) {
        ui_interact_slider(s, min, max, WC_SLIDER_TRACK_W, WC_SLIDER_THUMB_W, &g->input);
        if (ui_hovered() || s->drag.dragging) g->cursor = OS_CURSOR_HAND;
      }
    }
    if (s->changed) {
      f32 v = min + m_floorf((s->value - min) / step + 0.5f) * step;
      v = m_clampf(v, min, max);
      if (v != *value) {
        *value = v;
        changed = true;
      }
    }
    ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}}}) {}
    wc_setting_value(wc_slider_text(id, *value, g->day_time));
  }
  return changed;
}

hz_internal b32 wc_checkbox(WcGame *g, u32 index, const char *label, b32 *value) {
  b32 changed = false;
  wc_setting_row("CheckRow", index) {
    wc_setting_label(label);
    ui_element({.id = ui_idi("Check", index),
                .layout = {.sizing = {.width = ui_sizing_fixed(18), .height = ui_sizing_fixed(18)},
                           .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                .background_color = *value ? WC_C_ACCENT : (HzUIColor){255, 255, 255, 20},
                .corner_radius = ui_corner_radius(4),
                .border = {.color = *value ? WC_C_ACCENT : (HzUIColor){255, 255, 255, 60},
                           .width = ui_border_outside(1)}}) {
      if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
      if (wc_released_on(g)) {
        *value = !*value;
        changed = true;
      }
      if (*value) {
        ui_text(ui_string("\xe2\x9c\x93"), ui_text_config({.fontSize = 13, .weight = UI_FONT_BOLD,
                                                            .textColor = {12, 14, 20, 255}}));
      }
    }
  }
  return changed;
}

// returns the chosen option, or -1
hz_internal i32 wc_dropdown(WcGame *g, WcDropdownId id, const char *label, const char *const *options, u32 count,
                            u32 selected) {
  UIInteract_Dropdown *dd = &g->dropdowns[id];
  i32 picked = -1;
  wc_setting_row("DropRow", id) {
    wc_setting_label(label);
    ui_element({.id = ui_idi("Drop", id),
                .layout = {.sizing = {.width = ui_sizing_fixed(150), .height = ui_sizing_fixed(28)},
                           .padding = {.left = 8, .right = 8},
                           .child_alignment = {.y = UI_ALIGN_Y_CENTER}},
                .background_color = WC_C_INSET,
                .corner_radius = ui_corner_radius(6),
                .border = {.color = {255, 255, 255, 38}, .width = ui_border_outside(1)}}) {
      ui_interact_dropdown(dd, &g->input);
      if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
      ui_text(wc_uis(options[selected]), ui_text_config({.fontSize = 13, .textColor = WC_C_TEXT}));
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}}}) {}
      ui_text(ui_string("\xe2\x96\xbe"), ui_text_config({.fontSize = 12, .textColor = WC_C_MUTED}));
      if (dd->open) {
        ui_element({.id = ui_idi("DropPopup", id),
                    .floating = {.attach_to = UI_ATTACH_TO_PARENT,
                                 .attach_points = {.parent = UI_ATTACH_POINT_LEFT_BOTTOM,
                                                   .element = UI_ATTACH_POINT_LEFT_TOP},
                                 .offset = {0, 4},
                                 .z_index = 40},
                    .layout = {.sizing = {.width = ui_sizing_fixed(150), .height = ui_sizing_fit(0)},
                               .layout_direction = UI_TOP_TO_BOTTOM,
                               .padding = ui_padding_all(4)},
                    .background_color = WC_C_POPUP,
                    .corner_radius = ui_corner_radius(6),
                    .border = {.color = {255, 255, 255, 38}, .width = ui_border_outside(1)},
                    .shadow = {.color = {0, 0, 0, 150}, .offset_y = 8, .blur = 24}}) {
          ui_interact_dropdown_content(dd);
          for (u32 i = 0; i < count; i++) {
            // a desc is built before its element opens: ui_hovered() there would test the popup
            HzUIElementId opt_id = ui_idi("DropOpt", id * 16 + i);
            ui_element({.id = opt_id,
                        .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(26)},
                                   .padding = {.left = 8, .right = 8},
                                   .child_alignment = {.y = UI_ALIGN_Y_CENTER}},
                        .background_color =
                            ui_pointer_over(opt_id) ? (HzUIColor){255, 255, 255, 26} : (HzUIColor){0, 0, 0, 0},
                        .corner_radius = ui_corner_radius(4)}) {
              if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
              if (ui_interact_dropdown_option(dd, &g->input, i == selected)) picked = (i32)i;
              ui_text(wc_uis(options[i]),
                      ui_text_config({.fontSize = 13, .textColor = i == selected ? WC_C_ACCENT : WC_C_TEXT}));
            }
          }
        }
      }
    }
  }
  return picked;
}

hz_internal void wc_section(const char *title) {
  ui_element({.layout = {.padding = {.top = 14, .bottom = 2}}}) {
    ui_text(wc_uis(title), ui_text_config({.fontSize = 12, .weight = UI_FONT_SEMIBOLD, .letterSpacing = 1.5f,
                                           .textColor = WC_C_ACCENT}));
  }
}

hz_internal const char *const WC_PRESET_NAMES[] = {"Low", "Medium", "High", "Ultra", "Custom"};
hz_internal const char *const WC_SHADOW_RES_NAMES[] = {"1024", "2048", "3072", "4096"};
hz_internal const char *const WC_CLOUD_NAMES[] = {"Off", "Fast", "Fancy"};
hz_internal const char *const WC_WEATHER_NAMES[] = {"Dynamic", "Always clear", "Rain"};

hz_internal void wc_settings_screen(WcGame *g, f32 viewport_h) {
  WcSettings *s = &g->settings;
  b32 changed = false, custom = false;
  HzUIElementDesc sd3 = wc_screen_desc(g, ui_id("SettingsScreen"));
  ui_element_of(&sd3) {
    ui_element({.id = ui_id("SettingsPanel"),
                .layout = {.sizing = {.width = ui_sizing_fixed(560),
                                      .height = ui_sizing_fit(0, m_maxf(240.0f, viewport_h * 0.9f))},
                           .layout_direction = UI_TOP_TO_BOTTOM,
                           .padding = {.left = 32, .right = 32, .top = 28, .bottom = 28},
                           .child_gap = 8},
                .background_color = WC_C_PANEL,
                .corner_radius = ui_corner_radius(16),
                .corner_smoothing = 0.6f,
                .border = {.color = WC_C_PANEL_BORDER, .width = ui_border_outside(1)},
                .shadow = {.color = {0, 0, 0, 128}, .offset_y = 20, .blur = 60}}) {
      wc_title("Settings", 30);
      // by id: a desc is built before its element opens, so ui_scroll_offset() would read the panel's
      HzUIElementId body_id = ui_id("SettingsBody");
      ui_element({.id = body_id,
                  .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                             .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_gap = 6,
                             .padding = {.right = 8}},
                  .clip = {.vertical = true, .child_offset = ui_scroll_offset_for(body_id)}}) {
        wc_section("GRAPHICS");
        i32 p = wc_dropdown(g, WC_DROPDOWN_PRESET, "Quality preset", WC_PRESET_NAMES, ARRAY_SIZE(WC_PRESET_NAMES),
                            s->preset);
        if (p >= 0) {
          wc_settings_apply_preset(s, (WcPreset)p);
          s->preset = (WcPreset)p;
          changed = true;
        }
        f32 rd = (f32)s->render_distance;
        if (wc_slider(g, WC_SLIDER_RENDER_DISTANCE, "Render distance", &rd, 4, 24, 1)) {
          s->render_distance = (u32)rd;
          changed = custom = true;
        }
        if (wc_slider(g, WC_SLIDER_RENDER_SCALE, "Render scale", &s->render_scale, 0.5f, 1.0f, 0.05f))
          changed = custom = true;
        if (wc_checkbox(g, 0, "Shadows", &s->shadows)) changed = custom = true;
        u32 res_idx = s->shadow_res <= 1024 ? 0 : s->shadow_res <= 2048 ? 1 : s->shadow_res <= 3072 ? 2 : 3;
        i32 r = wc_dropdown(g, WC_DROPDOWN_SHADOW_RES, "Shadow resolution", WC_SHADOW_RES_NAMES,
                            ARRAY_SIZE(WC_SHADOW_RES_NAMES), res_idx);
        if (r >= 0) {
          s->shadow_res = 1024 * (u32)(r + 1);
          changed = custom = true;
        }
        if (wc_slider(g, WC_SLIDER_SHADOW_DISTANCE, "Shadow distance", &s->shadow_distance, 48, 256, 16))
          changed = custom = true;
        if (wc_checkbox(g, 1, "Ambient occlusion (SSAO)", &s->ssao)) changed = custom = true;
        if (wc_checkbox(g, 2, "Volumetric light", &s->volumetrics)) changed = custom = true;
        i32 c = wc_dropdown(g, WC_DROPDOWN_CLOUDS, "Clouds", WC_CLOUD_NAMES, ARRAY_SIZE(WC_CLOUD_NAMES), s->clouds);
        if (c >= 0) {
          s->clouds = (u32)c;
          changed = custom = true;
        }
        if (wc_checkbox(g, 3, "Water reflections (SSR)", &s->ssr)) changed = custom = true;
        if (wc_checkbox(g, 4, "Temporal anti-aliasing", &s->taa)) changed = custom = true;
        if (wc_checkbox(g, 5, "Bloom", &s->bloom)) changed = custom = true;
        wc_section("GAME");
        if (wc_slider(g, WC_SLIDER_FOV, "Field of view", &s->fov, 50, 110, 1)) changed = custom = true;
        if (wc_slider(g, WC_SLIDER_SENSITIVITY, "Mouse sensitivity", &s->sensitivity, 0.2f, 3.0f, 0.05f))
          changed = custom = true;
        if (wc_checkbox(g, 6, "View bobbing", &s->bobbing)) changed = custom = true;
        if (wc_slider(g, WC_SLIDER_VOLUME, "Volume", &s->volume, 0, 1, 0.05f)) changed = custom = true;
        if (wc_slider(g, WC_SLIDER_DAY_LENGTH, "Day length", &s->day_length, 2, 60, 1)) changed = custom = true;
        if (wc_slider(g, WC_SLIDER_CLOUD_COVERAGE, "Cloud coverage", &s->cloud_coverage, 0, 1, 0.05f))
          changed = custom = true;
        i32 wmode = wc_dropdown(g, WC_DROPDOWN_WEATHER, "Weather", WC_WEATHER_NAMES, ARRAY_SIZE(WC_WEATHER_NAMES),
                                s->weather);
        if (wmode >= 0) {
          s->weather = (WcWeatherMode)wmode;
          changed = true;
        }
        wc_slider(g, WC_SLIDER_TIME_OF_DAY, "Time of day", &g->day_time, 0, 1, 0.005f);
      }
      if (wc_button(g, ui_id("SettingsDoneBtn"), "Done", WC_BTN_SETTINGS_DONE, true, true)) {
        wc_game_apply_settings(g);
        wc_game_set_mode(g, g->settings_return == WC_MODE_PLAYING ? WC_MODE_PAUSED : g->settings_return);
      }
    }
  }
  if (custom) s->preset = WC_PRESET_CUSTOM;
  if (changed) wc_game_apply_settings(g);
}

// ---- inventory ----

hz_internal void wc_inventory_screen(WcGame *g) {
  HzUIElementDesc sd4 = wc_screen_desc(g, ui_id("InventoryScreen"));
  ui_element_of(&sd4) {
    b32 compact = (f32)g->ui->canvas_height < WC_COMPACT_H;
    // compact: wider and shorter, so all 72 blocks fit a landscape phone without scrolling
    u32 cols = compact ? 16 : WC_INV_COLS;
    f32 cell = compact ? 40.0f : WC_INV_CELL, gap = compact ? 4.0f : WC_INV_GAP, icon = compact ? 30.0f : 42.0f;
    HzUIElementDesc pd9 = wc_panel_desc(ui_id("InventoryPanel"), 0);
    if (compact) pd9.layout.padding = (HzUIPadding){.left = 20, .right = 20, .top = 14, .bottom = 14};
    ui_element_of(&pd9) {
      wc_title("Blocks", compact ? 22 : 28);
      wc_muted(g->touch_mode ? "Tap a block to put it in the selected hotbar slot."
                             : "Click a block to put it in the selected hotbar slot. Press E or Esc to close.",
               14);
      f32 grid_w = cols * cell + (cols - 1) * gap;
      i32 hovered = -1;
      ui_element({.id = ui_id("InventoryGrid"),
                  .layout = {.sizing = {.width = ui_sizing_fixed(grid_w)},
                             .layout_direction = UI_LEFT_TO_RIGHT_WRAP,
                             .child_gap = (u16)gap,
                             .padding = {.top = compact ? 6 : 12}}}) {
        for (u32 i = 0; i < WC_INVENTORY_COUNT; i++) {
          u8 id = wc_inventory_order[i];
          HzUIElementId eid = ui_idi("InvItem", i);
          b32 hot = g->inv_hover == i + 1;
          ui_element({.id = eid,
                      .layout = {.sizing = {.width = ui_sizing_fixed(cell), .height = ui_sizing_fixed(cell)},
                                 .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER},
                                 .margin = {.bottom = (u16)gap}},
                      .background_color = hot ? (HzUIColor){255, 255, 255, 46} : (HzUIColor){255, 255, 255, 15},
                      .corner_radius = ui_corner_radius(8),
                      .border = {.color = hot ? (HzUIColor){255, 255, 255, 255} : (HzUIColor){255, 255, 255, 26},
                                 .width = ui_border_outside(1)}}) {
            UIInteract a = ui_interact(&g->input);
            if (a.hovered) {
              hovered = (i32)i;
              g->cursor = OS_CURSOR_HAND;
            }
            if (a.clicked) {
              g->hotbar[g->selected] = id;
              wc_game_toast(g, wc_block_label[id]);
            }
            ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(icon), .height = ui_sizing_fixed(icon)}},
                        .image = {.texture = g->icons[id]},
                        .aspect_ratio = {1.0f}}) {}
          }
        }
      }
      g->inv_hover = hovered >= 0 ? (u32)hovered + 1 : 0;
      // no keyboard to close it with: a button, and the label row gives way to it
      if (g->touch_mode) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .margin = {.top = 4}}}) {
          if (wc_button(g, ui_id("InventoryDoneBtn"), "Done", WC_BTN_INVENTORY_DONE, true, true)) wc_game_lock(g);
        }
      } else {
        ui_element({.layout = {.sizing = {.height = ui_sizing_fixed(20)}, .margin = {.top = 4}}}) {
          if (hovered >= 0)
            ui_text(wc_uis(wc_block_label[wc_inventory_order[hovered]]),
                    ui_text_config({.fontSize = 13, .textColor = WC_C_MUTED}));
        }
      }
    }
  }
}

// ---- error ----

hz_internal void wc_error_screen(WcGame *g) {
  HzUIElementDesc sd5 = wc_screen_desc(g, ui_id("ErrorScreen"));
  ui_element_of(&sd5) {
    HzUIElementDesc pd10 = wc_panel_desc(ui_id("ErrorPanel"), 720);
    ui_element_of(&pd10) {
      wc_title("Something went wrong", 28);
      ui_text(wc_uis(g->error_text ? g->error_text : ""),
              ui_text_config({.fontSize = 12, .textColor = {255, 180, 180, 255}, .lineHeight = 17,
                              .wrapMode = UI_TEXT_WRAP_WORDS}));
    }
  }
}

// ---- hud ----

#define WC_TOUCH_SIDE 44.0f // clears the notch of a phone held in landscape
#define WC_TOUCH_BOTTOM 22.0f

// a round on-screen button; `lit` while held or toggled on
hz_internal void wc_touch_button(HzUIElementId id, const char *label, f32 size, v2 offset, b32 lit) {
  ui_element({.id = id,
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_BOTTOM,
                                             .parent = UI_ATTACH_POINT_RIGHT_BOTTOM},
                           .offset = {offset.x, offset.y},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {.width = ui_sizing_fixed(size), .height = ui_sizing_fixed(size)},
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
              .background_color = lit ? (HzUIColor){255, 255, 255, 90} : (HzUIColor){10, 12, 16, 110},
              .corner_radius = ui_corner_radius(size * 0.5f),
              .border = {.color = {255, 255, 255, lit ? 220 : 90}, .width = ui_border_outside(2)}}) {
    ui_text(wc_uis(label), ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = WC_C_TEXT}));
  }
}

// stick, jump, sneak and menu, laid out for a phone in landscape; wc_touch_update hit-tests these ids
hz_internal void wc_touch_hud(WcGame *g) {
  const WcTouch *t = &g->touch;
  f32 h = (f32)g->ui->canvas_height;
  b32 stick = wc_touch_stick_active(t);
  // the stick sits where the thumb landed; idle, a faint ring shows where to put it
  v2 center = stick ? t->stick_center : (v2){WC_TOUCH_SIDE + 90, h - WC_TOUCH_BOTTOM - 90};
  v2 knob = stick ? t->stick_knob : center;
  const f32 base = 132, nub = 56;
  ui_element({.id = ui_id("TouchStickBase"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {center.x - base * 0.5f, center.y - base * 0.5f},
                           .z_index = 4, .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {.width = ui_sizing_fixed(base), .height = ui_sizing_fixed(base)}},
              .background_color = {10, 12, 16, stick ? 90 : 40},
              .corner_radius = ui_corner_radius(base * 0.5f),
              .border = {.color = {255, 255, 255, stick ? 110 : 50}, .width = ui_border_outside(2)}}) {}
  ui_element({.id = ui_id("TouchStickKnob"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {knob.x - nub * 0.5f, knob.y - nub * 0.5f},
                           .z_index = 5, .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {.width = ui_sizing_fixed(nub), .height = ui_sizing_fixed(nub)}},
              .background_color = {255, 255, 255, stick ? 150 : 60},
              .corner_radius = ui_corner_radius(nub * 0.5f)}) {}

  wc_touch_button(ui_id("TouchJump"), "Jump", 76, (v2){-WC_TOUCH_SIDE, -WC_TOUCH_BOTTOM}, t->jump_held);
  b32 flying = g->player.flying;
  wc_touch_button(ui_id("TouchSneak"), flying ? "Down" : "Sneak", 60, (v2){-WC_TOUCH_SIDE - 8, -WC_TOUCH_BOTTOM - 92},
                  flying ? t->sneak_held : t->sneak_on);

  ui_element({.id = ui_id("TouchPause"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-WC_TOUCH_SIDE, 12},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.padding = {.left = 16, .right = 16, .top = 10, .bottom = 10}},
              .background_color = {10, 12, 16, 140},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {255, 255, 255, 60}, .width = ui_border_outside(1)}}) {
    ui_text(ui_string("Menu"), ui_text_config({.fontSize = 14, .weight = UI_FONT_SEMIBOLD, .textColor = WC_C_TEXT}));
  }
}

// touch devices play in landscape: portrait gets this instead of the game's screens
hz_internal void wc_rotate_prompt(WcGame *g) {
  HzUIElementDesc sd = wc_screen_desc(g, ui_id("RotateScreen"));
  sd.floating.z_index = 40;
  ui_element_of(&sd) {
    HzUIElementDesc pd = wc_panel_desc(ui_id("RotatePanel"), 0);
    ui_element_of(&pd) {
      wc_title("Rotate your phone", 24);
      wc_muted("WebCraft plays in landscape.", 14);
    }
  }
}

hz_internal void wc_hud(WcGame *g) {
  // hotbar
  ui_element({.id = ui_id("Hotbar"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_BOTTOM,
                                             .parent = UI_ATTACH_POINT_CENTER_BOTTOM},
                           .offset = {0, -14},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.child_gap = 4, .padding = ui_padding_all(5)},
              .background_color = {10, 12, 16, 140},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {255, 255, 255, 26}, .width = ui_border_outside(1)}}) {
    for (u32 i = 0; i < WC_HOTBAR_SLOTS; i++) {
      b32 sel = i == g->selected;
      ui_element({.id = ui_idi("HotbarSlot", i),
                  .layout = {.sizing = {.width = ui_sizing_fixed(50), .height = ui_sizing_fixed(50)},
                             .layout_direction = UI_STACK,
                             .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                  .background_color = sel ? (HzUIColor){255, 255, 255, 41} : (HzUIColor){255, 255, 255, 15},
                  .corner_radius = ui_corner_radius(7),
                  .border = {.color = sel ? (HzUIColor){255, 255, 255, 255} : (HzUIColor){255, 255, 255, 20},
                             .width = ui_border_outside(2)},
                  .shadow = sel ? (HzUIShadow){.color = {255, 255, 255, 64}, .blur = 12} : (HzUIShadow){0}}) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(40), .height = ui_sizing_fixed(40)}},
                    .image = {.texture = g->icons[g->hotbar[i]]},
                    .aspect_ratio = {1.0f}}) {}
        ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT, .offset = {4, 1}}}) {
          Allocator fa = wc_frame_alloc();
          ui_text(wc_uistr(str_format(&fa, "%", fmt_u32(i + 1))),
                  ui_text_config({.fontSize = 10, .textColor = {255, 255, 255, 140}}));
        }
      }
    }
    // minecraft's "..." slot: the inventory, which a keyboard reaches with E
    if (g->touch_mode) {
      ui_element({.id = ui_id("TouchInventory"),
                  .layout = {.sizing = {.width = ui_sizing_fixed(50), .height = ui_sizing_fixed(50)},
                             .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                  .background_color = {255, 255, 255, 15},
                  .corner_radius = ui_corner_radius(7),
                  .border = {.color = {255, 255, 255, 20}, .width = ui_border_outside(2)}}) {
        ui_text(ui_string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"),
                ui_text_config({.fontSize = 18, .weight = UI_FONT_BOLD, .textColor = WC_C_TEXT}));
      }
    }
  }
  // toast above the hotbar
  b32 visible = g->toast[0] != 0 && g->toast_t < 1.6f;
  f32 t = ui_spring(g->ui, ui_id("ToastT"), visible ? 1.0f : 0.0f);
  if (t > 0.01f && g->toast[0]) {
    ui_element({.id = ui_id("Toast"),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                             .attach_points = {.element = UI_ATTACH_POINT_CENTER_BOTTOM,
                                               .parent = UI_ATTACH_POINT_CENTER_BOTTOM},
                             .offset = {0, -84},
                             .z_index = 6,
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .opacity = opt_some(f32, t)}) {
      ui_text(wc_uis(g->toast), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = WC_C_TEXT,
                                                 .outlineColor = {0, 0, 0, 170}, .outlineWidth = 1}));
    }
  }
  // debug overlay
  if (g->show_debug && g->debug_text) {
    ui_element({.id = ui_id("Debug"),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {10, 10}, .z_index = 6,
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .layout = {.padding = {.left = 10, .right = 10, .top = 8, .bottom = 8}},
                .background_color = {0, 0, 0, 115},
                .corner_radius = ui_corner_radius(6)}) {
      ui_text(wc_uis(g->debug_text), ui_text_config({.fontSize = 12, .lineHeight = 17, .textColor = {232, 232, 232, 255},
                                                      .wrapMode = UI_TEXT_WRAP_NEWLINES}));
    }
  }
  // the stick and buttons only mean something in play
  if (g->touch_mode) {
    if (g->mode == WC_MODE_PLAYING) wc_touch_hud(g);
    return;
  }
  // key hint
  ui_element({.id = ui_id("Hint"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-12, 10},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
    ui_text(ui_string("F3 debug \xc2\xb7 E inventory \xc2\xb7 Esc menu"),
            ui_text_config({.fontSize = 12, .textColor = {255, 255, 255, 178}, .outlineColor = {0, 0, 0, 200},
                            .outlineWidth = 1}));
  }
}

// big readable frame stats for recordings; F3 swaps it for the debug text
hz_internal void wc_stats_row(const char *label, String value) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 18}}) {
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(56)}}}) {
      ui_text(wc_uis(label), ui_text_config({.fontSize = 15, .textColor = {255, 255, 255, 150}}));
    }
    ui_text(wc_uistr(value), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = {255, 255, 255, 240}}));
  }
}

hz_internal void wc_stats_panel(WcGame *g) {
  Allocator ta = tctx_temp_allocator(NULL);
  f32 fps = g->fps;
  f32 frame_ms = fps > 0 ? 1000.0f / fps : 0;
  u64 tris = (u64)g->renderer.stats.quads * 2;
  String tri_s = tris >= 1000000 ? str_format(&ta, "%M", fmt_f32_p((f32)tris / 1e6f, 1))
                                 : str_format(&ta, "%K", fmt_u32((u32)(tris / 1000)));
#ifdef WASM
  const char *backend = "WebGPU";
#else
  const char *backend = "Direct3D 12";
#endif
  ui_element({.id = ui_id("Stats"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {12, 12}, .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.layout_direction = UI_TOP_TO_BOTTOM,
                         .padding = {.left = 16, .right = 16, .top = 12, .bottom = 12},
                         .child_gap = 3},
              .background_color = {0, 0, 0, 140},
              .corner_radius = ui_corner_radius(8)}) {
    ui_element({.layout = {.child_gap = 8, .child_alignment = {.y = UI_ALIGN_Y_BOTTOM}}}) {
      ui_text(wc_uistr(str_format(&ta, "%", fmt_u32((u32)(fps + 0.5f)))),
              ui_text_config({.fontSize = 38, .weight = UI_FONT_BOLD, .textColor = {120, 235, 140, 255}}));
      ui_element({.layout = {.padding = {.bottom = 6}}}) {
        ui_text(ui_string("FPS"), ui_text_config({.fontSize = 16, .weight = UI_FONT_SEMIBOLD,
                                                   .textColor = {120, 235, 140, 220}}));
      }
    }
    wc_stats_row("frame", str_format(&ta, "% ms", fmt_f32_p(frame_ms, 2)));
    if (g->gpu_ms > 0) wc_stats_row("GPU", str_format(&ta, "% ms", fmt_f32_p(g->gpu_ms, 2)));
    wc_stats_row("CPU", str_format(&ta, "% ms", fmt_f32_p(g->main_ms, 2)));
    ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(1)},
                           .padding = {.top = 4}},
                .background_color = {255, 255, 255, 50}}) {}
    ui_element({.layout = {.padding = {.top = 4}}}) {
      ui_text(wc_uistr(str_format(&ta, "% triangles \xc2\xb7 % draw calls", fmt_str(tri_s),
                                  fmt_u32(g->renderer.stats.draw_calls + g->renderer.stats.shadow_draw_calls))),
              ui_text_config({.fontSize = 13, .textColor = {255, 255, 255, 200}}));
    }
    ui_text(wc_uistr(str_format(&ta, "% \xc2\xb7 % threads", fmt_cstr(backend), fmt_u32(g->world.lane_count))),
            ui_text_config({.fontSize = 13, .textColor = {255, 255, 255, 200}}));
  }
}

void wc_ui_frame(WcGame *g, f32 dt) {
  UNUSED(dt);
  g->cursor = OS_CURSOR_ARROW;
  // clay lays out in logical px, which is what the ui canvas size is
  f32 view_h = (f32)g->ui->canvas_height;
  ui_element({.id = ui_id("WcRoot"),
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)}},
              .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}) {
    // the settings panel is most of the screen tall, and on a short screen every panel is: the hotbar would draw over it
    b32 compact = view_h < WC_COMPACT_H;
    if (g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR && g->mode != WC_MODE_SETTINGS && !g->hide_hud &&
        g->started && (!compact || g->mode == WC_MODE_PLAYING))
      wc_hud(g);
    if (g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR && g->started && !g->show_debug) wc_stats_panel(g);
    if (wc_touch_portrait(g) && g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR) wc_rotate_prompt(g);
    else switch (g->mode) {
    case WC_MODE_LOADING: wc_loading_screen(g); break;
    case WC_MODE_MENU:
    case WC_MODE_PAUSED: wc_menu_screen(g); break;
    case WC_MODE_SETTINGS: wc_settings_screen(g, view_h); break;
    case WC_MODE_INVENTORY: wc_inventory_screen(g); break;
    case WC_MODE_ERROR: wc_error_screen(g); break;
    case WC_MODE_PLAYING: break;
    }
  }
}

#undef wc_setting_row

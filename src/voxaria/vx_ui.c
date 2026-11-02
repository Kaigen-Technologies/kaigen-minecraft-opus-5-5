#include "voxaria/vx_game.h"
#include "input.h"

// screens (loading, menu, settings, inventory, error) and the hud, after index.html

#define VX_C_TEXT ((HzUIColor){242, 242, 242, 255})
#define VX_C_MUTED ((HzUIColor){169, 176, 189, 255})
#define VX_C_ACCENT ((HzUIColor){127, 211, 107, 255})
#define VX_C_PANEL ((HzUIColor){12, 14, 20, 184})
#define VX_C_PANEL_BORDER ((HzUIColor){255, 255, 255, 31})
#define VX_C_BTN ((HzUIColor){255, 255, 255, 20})
#define VX_C_BTN_HI ((HzUIColor){255, 255, 255, 41})
#define VX_C_BTN_BORDER ((HzUIColor){255, 255, 255, 36})
#define VX_C_INSET ((HzUIColor){255, 255, 255, 20})
#define VX_C_POPUP ((HzUIColor){27, 30, 37, 250})

#define VX_SETTING_LABEL_W 170.0f
#define VX_SETTING_VALUE_W 60.0f
#define VX_SLIDER_TRACK_W 250.0f
#define VX_SLIDER_THUMB_W 14.0f
#define VX_INV_COLS 10
#define VX_INV_CELL 52.0f
#define VX_INV_GAP 4.0f

typedef enum {
  VX_BTN_PLAY,
  VX_BTN_SETTINGS,
  VX_BTN_NEW_WORLD,
  VX_BTN_SETTINGS_DONE,
  VX_BTN_MODAL_CANCEL,
  VX_BTN_MODAL_CONFIRM,
  VX_BTN_INVENTORY_DONE,
  VX_BTN_LOOT_ALL,
  VX_BTN_DEPOSIT_ALL,
  VX_BTN_WARDROBE_CLEAR,
  VX_BTN_WARDROBE_DONE,
  VX_BTN_COUNT,
} VxButtonSlot;
static_assert(VX_BTN_COUNT <= VX_UI_BUTTONS, "every button needs an animation slot");

// a phone held in landscape is about 390 ui px tall: screens drop to their compact layout
#define VX_COMPACT_H 500.0f

hz_internal HzUIString vx_uis(const char *s) { return (HzUIString){.chars = s, .length = (i32)cstr_len(s)}; }

hz_internal HzUIString vx_uistr(String s) { return (HzUIString){.chars = s.value, .length = (i32)s.len}; }

// dynamic text lives on the non-scoped temp allocator until the ui is drawn
hz_internal Allocator vx_frame_alloc(void) { return tctx_temp_allocator(NULL); }

hz_internal b32 vx_released_on(VxGame *g) {
  return ui_hovered() && g->input.buttons[MOUSE_LEFT].released_this_frame;
}

hz_internal b32 vx_button(VxGame *g, HzUIElementId id, const char *label, VxButtonSlot slot, b32 primary, b32 grow) {
  UIInteractAnim *anim = &g->button_anims[slot];
  UIGradient grad = {0};
  HzUIColor bg = ui_color_lerp(VX_C_BTN, VX_C_BTN_HI, anim->hover);
  HzUIColor border = VX_C_BTN_BORDER;
  if (primary) {
    HzUIColor top = ui_color_lerp((HzUIColor){98, 176, 79, 255}, (HzUIColor){111, 195, 91, 255}, anim->hover);
    HzUIColor bot = ui_color_lerp((HzUIColor){63, 138, 51, 255}, (HzUIColor){71, 154, 58, 255}, anim->hover);
    grad = (UIGradient){.kind = UI_GRADIENT_LINEAR, .angle = 180, .stop_count = 2,
                        .stops = {{0, top}, {1, bot}}};
    border = VX_C_ACCENT;
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
    clicked = vx_released_on(g);
    if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
    ui_text(vx_uis(label), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
  }
  return clicked;
}

hz_internal void vx_title(const char *text, f32 size) {
  ui_text(vx_uis(text), ui_text_config({.fontSize = size, .weight = UI_FONT_BOLD, .letterSpacing = size > 40 ? 1 : 0,
                                        .textColor = (HzUIColor){236, 245, 232, 255}}));
}

hz_internal void vx_muted(const char *text, f32 size) {
  ui_text(vx_uis(text), ui_text_config({.fontSize = size, .textColor = VX_C_MUTED, .lineHeight = size * 1.4f,
                                        .wrapMode = UI_TEXT_WRAP_WORDS}));
}

// full-screen scrim (radial vignette) with a centred column for the panel
hz_internal HzUIElementDesc vx_screen_desc(const VxGame *g, HzUIElementId id) {
  return (HzUIElementDesc){
      .id = id,
      .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 10},
      .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                 .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
      .image = {.texture = g->scrim},
  };
}

hz_internal HzUIElementDesc vx_panel_desc(HzUIElementId id, f32 panel_w) {
  return (HzUIElementDesc){
      .id = id,
      .layout = {.sizing = {.width = panel_w > 0 ? ui_sizing_fixed(panel_w) : ui_sizing_fit(0),
                            .height = ui_sizing_fit(0)},
                 .layout_direction = UI_TOP_TO_BOTTOM,
                 .padding = {.left = 32, .right = 32, .top = 28, .bottom = 28},
                 .child_gap = 8},
      .background_color = VX_C_PANEL,
      .corner_radius = ui_corner_radius(16),
      .corner_smoothing = 0.6f,
      .border = {.color = VX_C_PANEL_BORDER, .width = ui_border_outside(1)},
      .shadow = {.color = {0, 0, 0, 128}, .offset_y = 20, .blur = 60},
  };
}

// ---- loading ----

hz_internal void vx_loading_screen(VxGame *g) {
  f32 prog = 0.05f;
  const char *text = "Starting...";
  if (g->boot != VX_BOOT_START) {
    f32 wp = g->world_created ? vx_world_load_progress(&g->world, (i32)m_minf(4.0f, (f32)g->settings.render_distance)) : 0;
    prog = 0.3f + wp * 0.7f;
    Allocator fa = vx_frame_alloc();
    text = str_format(&fa, "Generating terrain... %%", fmt_u32((u32)(wp * 100 + 0.5f)), fmt_char('%')).value;
  }
  HzUIElementDesc sd1 = vx_screen_desc(g, ui_id("LoadingScreen"));
  ui_element_of(&sd1) {
    HzUIElementDesc pd6 = vx_panel_desc(ui_id("LoadingPanel"), 0);
    ui_element_of(&pd6) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_alignment = {.x = UI_ALIGN_X_CENTER}, .child_gap = 2}}) {
        vx_title("Voxaria", 44);
        vx_muted("Dig, build and fight", 14);
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
          ui_text(vx_uis(text), ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
        }
      }
    }
  }
}

// ---- menu ----

typedef struct {
  const char *key;
  const char *what;
} VxControlRow;

hz_internal const VxControlRow VX_CONTROLS[] = {
    {"W A S D", "Move"},
    {"Space", "Jump \xc2\xb7 hold to fly with wings"},
    {"Shift", "Drop through platforms"},
    {"Left click", "Use the held item: swing, mine, build"},
    {"Right click", "Open doors and chests"},
    // ascii only: a dash in this bold column pulls in a whole extra glyph page
    {"1-0 / wheel", "Select hotbar slot"},
    {"E", "Inventory and crafting"},
    {"G / H", "Grappling hook / quick heal"},
    {"C / V", "Side view / first person"},
    {"M / Ctrl", "Map / smart cursor"},
    {"F1 / F2 / F3", "Hide HUD / screenshot / debug info"},
};

hz_internal void vx_new_world_modal(VxGame *g) {
  f32 t = ui_spring(g->ui, ui_id("NewWorldModalT"), g->new_world_modal.open ? 1.0f : 0.0f);
  if (t < 0.01f) return;
  ui_element({.id = ui_id("NewWorldScrim"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 30},
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
              .background_color = {0, 0, 0, 140.0f * t}}) {
    ui_interact_modal(&g->new_world_modal, &g->input);
    HzUIElementDesc pd7 = vx_panel_desc(ui_id("NewWorldPanel"), 380);
    ui_element_of(&pd7) {
      ui_interact_modal_content(&g->new_world_modal);
      vx_title("New World", 24);
      vx_muted("Start a brand new world? Your current world will be replaced.", 14);
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 10, .padding = {.top = 10}}}) {
        if (vx_button(g, ui_id("NewWorldCancelBtn"), "Cancel", VX_BTN_MODAL_CANCEL, false, true))
          ui_interact_modal_close(&g->new_world_modal);
        if (vx_button(g, ui_id("NewWorldConfirmBtn"), "New World", VX_BTN_MODAL_CONFIRM, true, true)) {
          ui_interact_modal_close(&g->new_world_modal);
          vx_game_new_world(g);
        }
      }
    }
  }
}

hz_internal const VxControlRow VX_TOUCH_CONTROLS[] = {
    {"Left thumb", "Move"},
    {"Drag", "Look around"},
    {"Tap / hold", "Use the held item or build \xc2\xb7 hold to mine"},
    {"Jump / Hook", "Jump \xc2\xb7 fire the grappling hook"},
    {"Side", "Switch to the side view"},
};

hz_internal void vx_menu_screen(VxGame *g) {
  b32 compact = (f32)g->ui->canvas_height < VX_COMPACT_H;
  const VxControlRow *controls = g->touch_mode ? VX_TOUCH_CONTROLS : VX_CONTROLS;
  u32 control_count = g->touch_mode ? ARRAY_SIZE(VX_TOUCH_CONTROLS) : ARRAY_SIZE(VX_CONTROLS);
  HzUIElementDesc sd2 = vx_screen_desc(g, ui_id("MenuScreen"));
  ui_element_of(&sd2) {
    HzUIElementDesc pd8 = vx_panel_desc(ui_id("MenuPanel"), 460);
    if (compact) pd8.layout.padding = (HzUIPadding){.left = 24, .right = 24, .top = 18, .bottom = 18};
    ui_element_of(&pd8) {
      vx_title("Voxaria", compact ? 30 : 44);
      ui_element({.layout = {.margin = {.bottom = compact ? 6 : 12}}}) {
        vx_muted("Dig, build and fight through a hand-made voxel world", 14);
      }
      const char *play = g->mode == VX_MODE_PAUSED ? "Resume" : g->touch_mode ? "Tap to Play" : "Click to Play";
      if (vx_button(g, ui_id("PlayBtn"), play, VX_BTN_PLAY, true, true))
        vx_game_lock(g);
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 10}}) {
        if (vx_button(g, ui_id("SettingsBtn"), "Settings", VX_BTN_SETTINGS, false, true)) {
          g->settings_return = g->mode;
          vx_game_set_mode(g, VX_MODE_SETTINGS);
        }
        if (vx_button(g, ui_id("NewWorldBtn"), "New World", VX_BTN_NEW_WORLD, false, true))
          ui_interact_modal_open(&g->new_world_modal);
      }
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_gap = 3, .padding = {.top = compact ? 8 : 14}}}) {
        for (u32 i = 0; i < control_count; i++) {
          ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 14}}) {
            ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(130)}}}) {
              ui_text(vx_uis(controls[i].key),
                      ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
            }
            ui_text(vx_uis(controls[i].what), ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
          }
        }
      }
      if (!compact) ui_element({.layout = {.padding = {.top = 14}}}) {
#ifdef WASM
        vx_muted("Renderer: WebGPU", 12);
#else
        vx_muted("Renderer: Direct3D 12", 12);
#endif
      }
    }
  }
  vx_new_world_modal(g);
}

// the title over the flyover: the logo across a dark band, as S20, and the menu below it
hz_internal void vx_title_screen(VxGame *g) {
  f32 h = (f32)g->ui->canvas_height, w = (f32)g->ui->canvas_width;
  f32 logo = m_clampf(m_minf(h * 0.14f, w * 0.13f), 48, 150);
  HzUIColor cream = {255, 241, 166, 255}, shade = {24, 16, 10, 170};
  ui_element({.id = ui_id("TitleScreen"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 10},
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                         .layout_direction = UI_TOP_TO_BOTTOM, .child_gap = logo * 0.35f,
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}}}) {
    ui_element({.id = ui_id("TitleBand"),
                .layout = {.sizing = {.width = ui_sizing_grow(0)}, .layout_direction = UI_TOP_TO_BOTTOM,
                           .padding = {.top = (u16)(logo * 0.2f), .bottom = (u16)(logo * 0.24f)}, .child_gap = (u16)(logo * 0.06f),
                           .child_alignment = {.x = UI_ALIGN_X_CENTER}},
                .background_color = {8, 10, 14, 120}}) {
      // the in-flow copy is the drop shadow, padded down and right; the cream logo floats over it at the corner
      HzUITextConfig face = {.fontSize = logo, .weight = UI_FONT_BOLD, .letterSpacing = logo * 0.06f, .outlineWidth = 3};
      ui_element({.id = ui_id("TitleLogo"), .layout = {.padding = {.left = (u16)(logo * 0.025f), .top = (u16)(logo * 0.04f)}}}) {
        HzUITextConfig back = face;
        back.textColor = back.outlineColor = shade;
        ui_text(vx_uis("VOXARIA"), back);
        ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT, .z_index = 11}}) {
          HzUITextConfig front = face;
          front.textColor = front.outlineColor = cream;
          ui_text(vx_uis("VOXARIA"), front);
        }
      }
      ui_text(vx_uis("Dig, build and fight through a hand-made voxel world"),
              ui_text_config({.fontSize = m_maxf(15, logo * 0.22f), .weight = UI_FONT_SEMIBOLD, .letterSpacing = 1,
                              .textColor = {240, 238, 228, 255}, .outlineColor = {0, 0, 0, 160}, .outlineWidth = 1}));
    }
  }
  // the menu is one row pinned to the bottom edge, so the band sits alone over the town as in S20
  ui_element({.id = ui_id("TitleMenu"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 12,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_BOTTOM, .parent = UI_ATTACH_POINT_CENTER_BOTTOM},
                           .offset = {0, -m_maxf(16, h * 0.04f)}},
              .layout = {.sizing = {.width = ui_sizing_fixed(m_minf(560, w - 32))}, .child_gap = 10}}) {
    if (vx_button(g, ui_id("PlayBtn"), g->touch_mode ? "Tap to Play" : "Play", VX_BTN_PLAY, true, true)) vx_game_lock(g);
    if (vx_button(g, ui_id("SettingsBtn"), "Settings", VX_BTN_SETTINGS, false, true)) {
      g->settings_return = g->mode;
      vx_game_set_mode(g, VX_MODE_SETTINGS);
    }
    if (vx_button(g, ui_id("NewWorldBtn"), "New World", VX_BTN_NEW_WORLD, false, true))
      ui_interact_modal_open(&g->new_world_modal);
  }
  vx_new_world_modal(g);
}

// ---- settings ----

hz_internal void vx_setting_label(const char *label) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(VX_SETTING_LABEL_W)}}}) {
    ui_text(vx_uis(label), ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
  }
}

hz_internal void vx_setting_value(String text) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(VX_SETTING_VALUE_W)}}}) {
    ui_text(vx_uistr(text),
            ui_text_config({.fontSize = 13, .textColor = VX_C_TEXT, .textAlignment = UI_TEXT_ALIGN_RIGHT}));
  }
}

#define vx_setting_row(name, i)                                                                                \
  ui_element({.id = ui_idi(name, i),                                                                           \
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(30)},                \
                         .child_gap = 10,                                                                      \
                         .child_alignment = {.y = UI_ALIGN_Y_CENTER}}})

hz_internal String vx_slider_text(VxSliderId id, f32 v, f32 day_time) {
  Allocator fa = vx_frame_alloc();
  switch (id) {
  case VX_SLIDER_RENDER_DISTANCE: return str_format(&fa, "% ch", fmt_u32((u32)(v + 0.5f)));
  case VX_SLIDER_RENDER_SCALE:
  case VX_SLIDER_VOLUME:
  case VX_SLIDER_CLOUD_COVERAGE: return str_format(&fa, "%%", fmt_u32((u32)(v * 100 + 0.5f)), fmt_char('%'));
  case VX_SLIDER_SHADOW_DISTANCE: return str_format(&fa, "% m", fmt_u32((u32)(v + 0.5f)));
  case VX_SLIDER_FOV: return str_format(&fa, "%\xc2\xb0", fmt_u32((u32)(v + 0.5f)));
  case VX_SLIDER_SENSITIVITY: return str_format(&fa, "%\xc3\x97", fmt_f32_p(v, 2));
  case VX_SLIDER_DAY_LENGTH: return str_format(&fa, "% min", fmt_u32((u32)(v + 0.5f)));
  case VX_SLIDER_TIME_OF_DAY: return vx_clock_string(day_time, &fa);
  }
  return STR_FROM_CSTR("");
}

// returns true when the user moved it; *value snaps to step
hz_internal b32 vx_slider(VxGame *g, VxSliderId id, const char *label, f32 *value, f32 min, f32 max, f32 step) {
  UIInteract_Slider *s = &g->sliders[id];
  if (!s->drag.dragging) s->value = *value;
  // from the current value: ui_interact_slider runs inside the thumb, after the fill is laid out
  f32 thumb = max > min ? m_clampf((s->value - min) / (max - min), 0, 1) * (VX_SLIDER_TRACK_W - VX_SLIDER_THUMB_W) : 0;
  b32 changed = false;
  vx_setting_row("SliderRow", id) {
    vx_setting_label(label);
    ui_element({.id = ui_idi("SliderTrack", id),
                .layout = {.sizing = {.width = ui_sizing_fixed(VX_SLIDER_TRACK_W), .height = ui_sizing_fixed(4)}},
                .background_color = {255, 255, 255, 38},
                .corner_radius = ui_corner_radius(2)}) {
      // filled part of the track
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(thumb + VX_SLIDER_THUMB_W * 0.5f),
                                        .height = ui_sizing_grow(0)}},
                  .background_color = VX_C_ACCENT, .corner_radius = ui_corner_radius(2)}) {}
      ui_element({.id = ui_idi("SliderThumb", id),
                  .floating = {.attach_to = UI_ATTACH_TO_PARENT, .offset = {thumb, -5},
                               .clip_to = UI_CLIP_TO_ATTACHED_PARENT},
                  .layout = {.sizing = {.width = ui_sizing_fixed(VX_SLIDER_THUMB_W),
                                        .height = ui_sizing_fixed(VX_SLIDER_THUMB_W)}},
                  .background_color = s->drag.dragging ? (HzUIColor){170, 235, 150, 255} : VX_C_ACCENT,
                  .corner_radius = ui_corner_radius(VX_SLIDER_THUMB_W * 0.5f)}) {
        ui_interact_slider(s, min, max, VX_SLIDER_TRACK_W, VX_SLIDER_THUMB_W, &g->input);
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
    vx_setting_value(vx_slider_text(id, *value, g->day_time));
  }
  return changed;
}

hz_internal b32 vx_checkbox(VxGame *g, u32 index, const char *label, b32 *value) {
  b32 changed = false;
  vx_setting_row("CheckRow", index) {
    vx_setting_label(label);
    ui_element({.id = ui_idi("Check", index),
                .layout = {.sizing = {.width = ui_sizing_fixed(18), .height = ui_sizing_fixed(18)},
                           .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                .background_color = *value ? VX_C_ACCENT : (HzUIColor){255, 255, 255, 20},
                .corner_radius = ui_corner_radius(4),
                .border = {.color = *value ? VX_C_ACCENT : (HzUIColor){255, 255, 255, 60},
                           .width = ui_border_outside(1)}}) {
      if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
      if (vx_released_on(g)) {
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
hz_internal i32 vx_dropdown(VxGame *g, VxDropdownId id, const char *label, const char *const *options, u32 count,
                            u32 selected) {
  UIInteract_Dropdown *dd = &g->dropdowns[id];
  i32 picked = -1;
  vx_setting_row("DropRow", id) {
    vx_setting_label(label);
    ui_element({.id = ui_idi("Drop", id),
                .layout = {.sizing = {.width = ui_sizing_fixed(150), .height = ui_sizing_fixed(28)},
                           .padding = {.left = 8, .right = 8},
                           .child_alignment = {.y = UI_ALIGN_Y_CENTER}},
                .background_color = VX_C_INSET,
                .corner_radius = ui_corner_radius(6),
                .border = {.color = {255, 255, 255, 38}, .width = ui_border_outside(1)}}) {
      ui_interact_dropdown(dd, &g->input);
      if (ui_hovered()) g->cursor = OS_CURSOR_HAND;
      ui_text(vx_uis(options[selected]), ui_text_config({.fontSize = 13, .textColor = VX_C_TEXT}));
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}}}) {}
      ui_text(ui_string("\xe2\x96\xbe"), ui_text_config({.fontSize = 12, .textColor = VX_C_MUTED}));
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
                    .background_color = VX_C_POPUP,
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
              ui_text(vx_uis(options[i]),
                      ui_text_config({.fontSize = 13, .textColor = i == selected ? VX_C_ACCENT : VX_C_TEXT}));
            }
          }
        }
      }
    }
  }
  return picked;
}

hz_internal void vx_section(const char *title) {
  ui_element({.layout = {.padding = {.top = 14, .bottom = 2}}}) {
    ui_text(vx_uis(title), ui_text_config({.fontSize = 12, .weight = UI_FONT_SEMIBOLD, .letterSpacing = 1.5f,
                                           .textColor = VX_C_ACCENT}));
  }
}

hz_internal const char *const VX_PRESET_NAMES[] = {"Low", "Medium", "High", "Ultra", "Custom"};
hz_internal const char *const VX_SHADOW_RES_NAMES[] = {"1024", "2048", "3072", "4096"};
hz_internal const char *const VX_CLOUD_NAMES[] = {"Off", "Fast", "Fancy"};
hz_internal const char *const VX_WEATHER_NAMES[] = {"Dynamic", "Always clear", "Rain"};
hz_internal const char *const VX_LIGHT_MODE_NAMES[] = {"Color", "White", "Retro", "Trippy"};
hz_internal const char *const VX_PP_LABELS[VX_PP_COUNT] = {"Skin",  "Hair colour", "Hair style", "Eyes",
                                                           "Shirt", "Undershirt",  "Trousers",   "Shoes"};

hz_internal u32 *vx_pp_field(VxSettings *s, VxPuppetPart part) {
  switch (part) {
  case VX_PP_SKIN: return &s->skin_tone;
  case VX_PP_HAIR: return &s->hair_color;
  case VX_PP_HAIR_STYLE: return &s->hair_style;
  case VX_PP_EYES: return &s->eye_color;
  case VX_PP_SHIRT: return &s->shirt_color;
  case VX_PP_UNDERSHIRT: return &s->undershirt_color;
  case VX_PP_PANTS: return &s->pants_color;
  default: return &s->shoe_color;
  }
}

hz_internal void vx_settings_screen(VxGame *g, f32 viewport_h) {
  VxSettings *s = &g->settings;
  b32 changed = false, custom = false;
  HzUIElementDesc sd3 = vx_screen_desc(g, ui_id("SettingsScreen"));
  ui_element_of(&sd3) {
    ui_element({.id = ui_id("SettingsPanel"),
                .layout = {.sizing = {.width = ui_sizing_fixed(560),
                                      .height = ui_sizing_fit(0, m_maxf(240.0f, viewport_h * 0.9f))},
                           .layout_direction = UI_TOP_TO_BOTTOM,
                           .padding = {.left = 32, .right = 32, .top = 28, .bottom = 28},
                           .child_gap = 8},
                .background_color = VX_C_PANEL,
                .corner_radius = ui_corner_radius(16),
                .corner_smoothing = 0.6f,
                .border = {.color = VX_C_PANEL_BORDER, .width = ui_border_outside(1)},
                .shadow = {.color = {0, 0, 0, 128}, .offset_y = 20, .blur = 60}}) {
      vx_title("Settings", 30);
      // by id: a desc is built before its element opens, so ui_scroll_offset() would read the panel's
      HzUIElementId body_id = ui_id("SettingsBody");
      ui_element({.id = body_id,
                  .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                             .layout_direction = UI_TOP_TO_BOTTOM,
                             .child_gap = 6,
                             .padding = {.right = 8}},
                  .clip = {.vertical = true, .child_offset = ui_scroll_offset_for(body_id)}}) {
        vx_section("GRAPHICS");
        i32 p = vx_dropdown(g, VX_DROPDOWN_PRESET, "Quality preset", VX_PRESET_NAMES, ARRAY_SIZE(VX_PRESET_NAMES),
                            s->preset);
        if (p >= 0) {
          vx_settings_apply_preset(s, (VxPreset)p);
          s->preset = (VxPreset)p;
          changed = true;
        }
        f32 rd = (f32)s->render_distance;
        if (vx_slider(g, VX_SLIDER_RENDER_DISTANCE, "Render distance", &rd, 4, 24, 1)) {
          s->render_distance = (u32)rd;
          changed = custom = true;
        }
        if (vx_slider(g, VX_SLIDER_RENDER_SCALE, "Render scale", &s->render_scale, 0.5f, 1.0f, 0.05f))
          changed = custom = true;
        if (vx_checkbox(g, 0, "Shadows", &s->shadows)) changed = custom = true;
        u32 res_idx = s->shadow_res <= 1024 ? 0 : s->shadow_res <= 2048 ? 1 : s->shadow_res <= 3072 ? 2 : 3;
        i32 r = vx_dropdown(g, VX_DROPDOWN_SHADOW_RES, "Shadow resolution", VX_SHADOW_RES_NAMES,
                            ARRAY_SIZE(VX_SHADOW_RES_NAMES), res_idx);
        if (r >= 0) {
          s->shadow_res = 1024 * (u32)(r + 1);
          changed = custom = true;
        }
        if (vx_slider(g, VX_SLIDER_SHADOW_DISTANCE, "Shadow distance", &s->shadow_distance, 48, 256, 16))
          changed = custom = true;
        if (vx_checkbox(g, 3, "Soft shadows", &s->soft_shadows)) changed = custom = true;
        if (vx_checkbox(g, 1, "Ambient occlusion (SSAO)", &s->ssao)) changed = custom = true;
        if (vx_checkbox(g, 2, "Volumetric light", &s->volumetrics)) changed = custom = true;
        i32 c = vx_dropdown(g, VX_DROPDOWN_CLOUDS, "Clouds", VX_CLOUD_NAMES, ARRAY_SIZE(VX_CLOUD_NAMES), s->clouds);
        if (c >= 0) {
          s->clouds = (u32)c;
          changed = custom = true;
        }
        if (vx_checkbox(g, 4, "Temporal anti-aliasing", &s->taa)) changed = custom = true;
        if (vx_checkbox(g, 5, "Bloom", &s->bloom)) changed = custom = true;
        // the lighting style is a look, not a quality level: presets leave it alone
        i32 lm = vx_dropdown(g, VX_DROPDOWN_LIGHTING, "Lighting", VX_LIGHT_MODE_NAMES, ARRAY_SIZE(VX_LIGHT_MODE_NAMES),
                             s->lighting);
        if (lm >= 0) {
          s->lighting = (VxLightMode)lm;
          changed = true;
        }
        vx_section("GAME");
        if (vx_slider(g, VX_SLIDER_FOV, "Field of view", &s->fov, 50, 110, 1)) changed = custom = true;
        if (vx_slider(g, VX_SLIDER_SENSITIVITY, "Mouse sensitivity", &s->sensitivity, 0.2f, 3.0f, 0.05f))
          changed = custom = true;
        if (vx_checkbox(g, 6, "View bobbing", &s->bobbing)) changed = custom = true;
        if (vx_checkbox(g, 7, "Screen shake", &s->screen_shake)) changed = custom = true;
        if (vx_slider(g, VX_SLIDER_VOLUME, "Volume", &s->volume, 0, 1, 0.05f)) changed = custom = true;
        if (vx_slider(g, VX_SLIDER_DAY_LENGTH, "Day length", &s->day_length, 2, 60, 1)) changed = custom = true;
        if (vx_slider(g, VX_SLIDER_CLOUD_COVERAGE, "Cloud coverage", &s->cloud_coverage, 0, 1, 0.05f))
          changed = custom = true;
        i32 wmode = vx_dropdown(g, VX_DROPDOWN_WEATHER, "Weather", VX_WEATHER_NAMES, ARRAY_SIZE(VX_WEATHER_NAMES),
                                s->weather);
        if (wmode >= 0) {
          s->weather = (VxWeatherMode)wmode;
          changed = true;
        }
        vx_slider(g, VX_SLIDER_TIME_OF_DAY, "Time of day", &g->day_time, 0, 1, 0.005f);
        vx_section("CHARACTER");
        for (u32 i = 0; i < VX_PP_COUNT; i++) {
          u32 n = vx_puppet_option_count((VxPuppetPart)i), *field = vx_pp_field(s, (VxPuppetPart)i);
          i32 pick = vx_dropdown(g, (VxDropdownId)(VX_DROPDOWN_CHARACTER + i), VX_PP_LABELS[i],
                                 vx_puppet_option_names((VxPuppetPart)i), n, *field % n);
          if (pick >= 0) *field = (u32)pick;
        }
      }
      if (vx_button(g, ui_id("SettingsDoneBtn"), "Done", VX_BTN_SETTINGS_DONE, true, true)) {
        vx_game_apply_settings(g);
        vx_game_set_mode(g, g->settings_return == VX_MODE_PLAYING ? VX_MODE_PAUSED : g->settings_return);
      }
    }
  }
  if (custom) s->preset = VX_PRESET_CUSTOM;
  if (changed) vx_game_apply_settings(g);
}

// ---- inventory ----

hz_internal HzUIColor vx_rarity_color(u8 rarity) {
  switch (rarity) {
  case VX_RARE_BLUE: return (HzUIColor){150, 150, 255, 255};
  case VX_RARE_GREEN: return (HzUIColor){150, 255, 150, 255};
  case VX_RARE_ORANGE: return (HzUIColor){255, 200, 150, 255};
  case VX_RARE_LIGHT_RED: return (HzUIColor){255, 150, 150, 255};
  default: return (HzUIColor){255, 255, 255, 255};
  }
}

// one slot: Terraria's blue tile with the icon and stack count; in the inventory screen it takes clicks
hz_internal void vx_slot_ui(VxGame *g, HzUIElementId id, VxStack *s, VxSlotKind kind, f32 cell, b32 sel, b32 live,
                            const char *hint) {
  HzUIColor bg = kind == VX_SLOT_INV ? (HzUIColor){52, 70, 150, 190}
                 : kind == VX_SLOT_TRASH ? (HzUIColor){60, 60, 70, 190}
                                         : (HzUIColor){70, 110, 170, 190};
  b32 hot = false;
  ui_element({.id = id,
              .layout = {.sizing = {.width = ui_sizing_fixed(cell), .height = ui_sizing_fixed(cell)},
                         .layout_direction = UI_STACK,
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
              .background_color = bg,
              .corner_radius = ui_corner_radius(cell * 0.14f),
              .border = {.color = sel ? (HzUIColor){255, 222, 90, 255} : (HzUIColor){12, 16, 40, 200},
                         .width = ui_border_outside(sel ? 3 : 2)}}) {
    if (live) {
      UIInteract a = ui_interact(&g->input);
      hot = a.hovered;
      if (a.hovered) {
        g->inv_hover_item = s->item;
        g->cursor = OS_CURSOR_HAND;
        // touch has no right click: a tap on armor or an accessory in the bag equips it
        u8 ik = vx_item(s->item)->kind;
        b32 equip = g->touch_mode && kind == VX_SLOT_INV && !g->inv.cursor.item && s->item &&
                    (ik == VX_IK_HEAD || ik == VX_IK_BODY || ik == VX_IK_LEGS || ik == VX_IK_ACCESSORY);
        if (a.clicked) vx_inventory_click(&g->inv, s, kind, equip);
        else if (g->input.buttons[MOUSE_RIGHT].pressed_this_frame) vx_inventory_click(&g->inv, s, kind, true);
      }
    }
    if (hot) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)}},
                  .background_color = {255, 255, 255, 40},
                  .corner_radius = ui_corner_radius(cell * 0.14f)}) {}
    }
    if (s->item) {
      f32 icon = cell * (vx_item_is_block(s->item) ? 0.74f : 0.8f);
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(icon), .height = ui_sizing_fixed(icon)}},
                  .image = {.texture = g->icons[s->item]},
                  .aspect_ratio = {1.0f}}) {}
      if (s->count > 1) {
        ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT,
                                 .attach_points = {.element = UI_ATTACH_POINT_LEFT_BOTTOM,
                                                   .parent = UI_ATTACH_POINT_LEFT_BOTTOM},
                                 .offset = {5, -2},
                                 .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
          Allocator fa = vx_frame_alloc();
          ui_text(vx_uistr(str_format(&fa, "%", fmt_u32(s->count))),
                  ui_text_config({.fontSize = (u16)(cell * 0.27f), .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
                                  .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
        }
      }
    } else if (hint) {
      ui_text(vx_uis(hint), ui_text_config({.fontSize = (u16)(cell * 0.22f), .textColor = {255, 255, 255, 80}}));
    }
  }
}

// name in its rarity colour, then what the item does; for a recipe, what it takes and where
hz_internal void vx_item_tooltip(VxGame *g, VxItemId item, const VxRecipe *r) {
  const VxItemInfo *it = vx_item(item);
  Allocator fa = vx_frame_alloc();
  v2 m = g->input.mouse_pos;
  ui_element({.id = ui_id("ItemTooltip"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {m.x + 18, m.y + 18}, .z_index = 31,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 2,
                         .padding = {.left = 10, .right = 10, .top = 7, .bottom = 7}},
              .background_color = {14, 18, 40, 225},
              .corner_radius = ui_corner_radius(6),
              .border = {.color = {120, 140, 220, 120}, .width = ui_border_outside(1)}}) {
    HzUITextConfig line = {.fontSize = 13, .textColor = VX_C_TEXT, .outlineColor = {0, 0, 0, 200}, .outlineWidth = 1};
    HzUITextConfig name = line;
    name.fontSize = 15;
    name.weight = UI_FONT_SEMIBOLD;
    name.textColor = vx_rarity_color(it->rarity);
    ui_text(vx_uis(it->name), name);
    if (it->damage && it->kind != VX_IK_PICKAXE && it->kind != VX_IK_AXE)
      ui_text(vx_uistr(str_format(&fa, "% damage", fmt_u32(it->damage))), line);
    switch (it->kind) {
    case VX_IK_PICKAXE: ui_text(vx_uistr(str_format(&fa, "% pickaxe power", fmt_u32(it->power))), line); break;
    case VX_IK_AXE: ui_text(vx_uistr(str_format(&fa, "% axe power", fmt_u32(it->power))), line); break;
    case VX_IK_HEAD:
    case VX_IK_BODY:
    case VX_IK_LEGS: ui_text(vx_uistr(str_format(&fa, "% defense", fmt_u32(it->power))), line); break;
    case VX_IK_ACCESSORY: ui_text(vx_uis("Equipable"), line); break;
    case VX_IK_CONSUMABLE: ui_text(vx_uis("Consumable"), line); break;
    case VX_IK_MATERIAL: ui_text(vx_uis("Material"), line); break;
    case VX_IK_AMMO: ui_text(vx_uis("Ammo"), line); break;
    default: break;
    }
    if (it->mana) ui_text(vx_uistr(str_format(&fa, "Uses % mana", fmt_u32(it->mana))), line);
    if (vx_item_is_block(item)) ui_text(vx_uis("Can be placed"), line);
    if (r) {
      HzUITextConfig need = line;
      need.textColor = (HzUIColor){255, 222, 120, 255};
      ui_text(vx_uistr(str_format(&fa, "Requires (%):", fmt_cstr(vx_station_name[r->station]))), need);
      for (u32 i = 0; i < VX_RECIPE_INPUTS; i++) {
        if (!r->in[i].item) continue;
        b32 have = vx_inventory_count(&g->inv, r->in[i].item) >= r->in[i].count;
        HzUITextConfig c = line;
        c.textColor = have ? VX_C_TEXT : (HzUIColor){255, 120, 120, 255};
        ui_text(vx_uistr(str_format(&fa, "  % %", fmt_u32(r->in[i].count), fmt_cstr(vx_item(r->in[i].item)->name))),
                c);
      }
    }
  }
}

// an open chest's 4x10 slots over the bag, with Terraria's Loot All and Deposit All
hz_internal void vx_chest_panel(VxGame *g, f32 cell, f32 gap) {
  VxChest *c = &g->chests[g->open_chest];
  ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap, .padding = {.bottom = 10}}}) {
    ui_element({.layout = {.child_gap = 8, .child_alignment = {.y = UI_ALIGN_Y_CENTER}}}) {
      ui_text(vx_uis(vx_world_block(&g->world, c->x, c->y, c->z) == B_GOLD_CHEST ? "Gold Chest" : "Chest"),
              ui_text_config({.fontSize = 18, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(12)}}}) {}
      if (vx_button(g, ui_id("LootAllBtn"), "Loot All", VX_BTN_LOOT_ALL, false, false)) vx_chest_loot_all(g);
      if (vx_button(g, ui_id("DepositAllBtn"), "Deposit All", VX_BTN_DEPOSIT_ALL, false, false)) {
        // the hotbar stays with the player
        for (u32 i = VX_HOTBAR_SLOTS; i < VX_INV_SLOTS; i++) {
          VxStack *s = &g->inv.slot[i];
          for (u32 k = 0; k < VX_CHEST_SLOTS && s->item; k++) {
            VxStack *d = &c->slot[k];
            u32 cap = vx_item(s->item)->stack;
            if (d->item && (d->item != s->item || d->count >= cap)) continue;
            u32 n = d->item ? MIN((u32)s->count, cap - d->count) : s->count;
            *d = (VxStack){s->item, (u16)(d->count + n)};
            s->count = (u16)(s->count - n);
            if (!s->count) s->item = 0;
          }
        }
      }
    }
    for (u32 row = 0; row < VX_CHEST_SLOTS / VX_INV_COLS; row++) {
      ui_element({.layout = {.child_gap = (u16)gap}}) {
        for (u32 col = 0; col < VX_INV_COLS; col++) {
          u32 i = row * VX_INV_COLS + col;
          vx_slot_ui(g, ui_idi("ChestSlot", i), &c->slot[i], VX_SLOT_INV, cell, false, true, NULL);
        }
      }
    }
  }
}

// what can be crafted here and now, as in Terraria: only recipes whose station is near and whose inputs are held
hz_internal void vx_crafting_panel(VxGame *g, f32 cell, f32 gap) {
  Allocator fa = vx_frame_alloc();
  const u32 cols = 4;
  f32 grid_w = cols * cell + (cols - 1) * gap;
  ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap,
                         .sizing = {.width = ui_sizing_fixed(grid_w)}}}) {
    ui_text(vx_uis("Crafting"), ui_text_config({.fontSize = 18, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
    StringBuilder nearby = sb_create(256, &fa);
    for (u32 s = VX_ST_WORKBENCH; s < VX_ST_COUNT; s++)
      if (g->stations & (1u << s))
        sb_append_format(&nearby, "%%", fmt_cstr(sb_length(&nearby) ? ", " : ""), fmt_cstr(vx_station_name[s]));
    String near_s = sb_length(&nearby) ? str_format(&fa, "Near: %", fmt_cstr(sb_get(&nearby))) : STR_FROM_CSTR("No station near");
    ui_text(vx_uistr(near_s), ui_text_config({.fontSize = 12, .textColor = VX_C_MUTED, .wrapMode = UI_TEXT_WRAP_WORDS}));
    u32 shown = 0;
    ui_element({.layout = {.layout_direction = UI_LEFT_TO_RIGHT_WRAP, .child_gap = (u16)gap,
                           .sizing = {.width = ui_sizing_fixed(grid_w)}}}) {
      for (u32 i = 0; i < vx_recipe_count; i++) {
        const VxRecipe *r = &vx_recipes[i];
        if (!vx_recipe_available(&g->inv, r, g->stations)) continue;
        shown++;
        VxStack out = r->out;
        ui_element({.id = ui_idi("Recipe", i),
                    .layout = {.sizing = {.width = ui_sizing_fixed(cell), .height = ui_sizing_fixed(cell)},
                               .layout_direction = UI_STACK,
                               .margin = {.bottom = (u16)gap},
                               .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                    .background_color = {90, 70, 150, 190},
                    .corner_radius = ui_corner_radius(cell * 0.14f),
                    .border = {.color = {20, 12, 50, 200}, .width = ui_border_outside(2)}}) {
          UIInteract a = ui_interact(&g->input);
          if (a.hovered) {
            g->inv_hover_recipe = (i32)i;
            g->cursor = OS_CURSOR_HAND;
            if (a.clicked) {
              b32 made = vx_craft(&g->inv, r, g->stations);
              if (made) {
                vx_audio_play(&g->audio, VX_SND_CRAFT);
                vx_event(g, out.count > 1 ? str_format(&fa, "Crafted % (%)", fmt_cstr(vx_item(out.item)->name),
                                                       fmt_u32(out.count))
                                          : str_format(&fa, "Crafted %", fmt_cstr(vx_item(out.item)->name)));
              }
              if (g->scripted)
                log_info("crafting: % x % at %%", fmt_u32(out.count), fmt_cstr(vx_item(out.item)->name),
                         fmt_cstr(vx_station_name[r->station]), fmt_cstr(made ? "" : " failed: the cursor holds another item"));
            }
          }
          f32 icon = cell * 0.74f;
          ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(icon), .height = ui_sizing_fixed(icon)}},
                      .image = {.texture = g->icons[out.item]},
                      .aspect_ratio = {1.0f}}) {}
          if (out.count > 1) {
            ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT,
                                     .attach_points = {.element = UI_ATTACH_POINT_LEFT_BOTTOM,
                                                       .parent = UI_ATTACH_POINT_LEFT_BOTTOM},
                                     .offset = {4, -1},
                                     .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
              ui_text(vx_uistr(str_format(&fa, "%", fmt_u32(out.count))),
                      ui_text_config({.fontSize = (u16)(cell * 0.27f), .weight = UI_FONT_SEMIBOLD,
                                      .textColor = VX_C_TEXT, .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
            }
          }
        }
      }
    }
    if (!shown) ui_text(vx_uis("Nothing to craft"), ui_text_config({.fontSize = 12, .textColor = VX_C_MUTED}));
  }
}

hz_internal void vx_inventory_screen(VxGame *g) {
  b32 compact = (f32)g->ui->canvas_height < VX_COMPACT_H;
  b32 chest = g->open_chest >= 0;
  f32 cell = compact || chest ? (compact ? (chest ? 26.0f : 38.0f) : 46.0f) : VX_INV_CELL;
  f32 gap = compact ? 3.0f : VX_INV_GAP;
  g->inv_hover_item = 0;
  g->inv_hover_recipe = -1;
  HzUIElementDesc sd4 = vx_screen_desc(g, ui_id("InventoryScreen"));
  ui_element_of(&sd4) {
    HzUIElementDesc pd9 = vx_panel_desc(ui_id("InventoryPanel"), 0);
    pd9.layout.padding = compact ? (HzUIPadding){.left = 16, .right = 16, .top = 10, .bottom = 10}
                                 : (HzUIPadding){.left = 24, .right = 24, .top = 18, .bottom = 18};
    ui_element_of(&pd9) {
      if (chest) vx_chest_panel(g, cell, gap);
      vx_title("Inventory", compact ? 20 : 26);
      ui_element({.layout = {.child_gap = (u16)(gap * 6), .padding = {.top = compact ? 6 : 10}}}) {
        vx_crafting_panel(g, compact ? cell : cell * 0.85f, gap);
        // 5 rows of 10: the top row is the hotbar
        ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
          for (u32 row = 0; row < VX_INV_SLOTS / VX_INV_COLS; row++) {
            ui_element({.layout = {.child_gap = (u16)gap}}) {
              for (u32 c = 0; c < VX_INV_COLS; c++) {
                u32 i = row * VX_INV_COLS + c;
                vx_slot_ui(g, ui_idi("InvSlot", i), &g->inv.slot[i], VX_SLOT_INV, cell, i == g->inv.selected, true,
                           NULL);
              }
            }
          }
        }
        // equipment, defense and the trash
        ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
          ui_element({.layout = {.child_gap = (u16)gap}}) {
            ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
              for (u32 i = 0; i < VX_ARMOR_SLOTS; i++)
                vx_slot_ui(g, ui_idi("ArmorSlot", i), &g->inv.armor[i], (VxSlotKind)(VX_SLOT_HEAD + i), cell, false,
                           true, i == 0 ? "Head" : i == 1 ? "Body" : "Legs");
            }
            ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
              for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
                vx_slot_ui(g, ui_idi("AccSlot", i), &g->inv.accessory[i], VX_SLOT_ACCESSORY, cell, false, true, "Acc");
            }
          }
          Allocator fa = vx_frame_alloc();
          ui_text(vx_uistr(str_format(&fa, "Defense %", fmt_u32(vx_inventory_defense(&g->inv)))),
                  ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
          ui_element({.layout = {.sizing = {.height = ui_sizing_grow(0)}}}) {}
          vx_slot_ui(g, ui_id("TrashSlot"), &g->inv.trash, VX_SLOT_TRASH, cell, false, true, "Trash");
        }
      }
      if (g->touch_mode) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .margin = {.top = 4}}}) {
          if (vx_button(g, ui_id("InventoryDoneBtn"), "Done", VX_BTN_INVENTORY_DONE, true, true)) vx_game_lock(g);
        }
      } else {
        ui_element({.layout = {.margin = {.top = 6}}}) {
          vx_muted("Click to pick up and place, right click to take one or equip. E or Esc closes.", 13);
        }
      }
    }
  }
  // the held stack follows the pointer; a tooltip shows what is under it otherwise
  if (g->inv.cursor.item) {
    v2 m = g->input.mouse_pos;
    ui_element({.id = ui_id("CursorStack"),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {m.x - cell * 0.25f, m.y - cell * 0.25f},
                             .z_index = 32, .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .layout = {.layout_direction = UI_STACK}}) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(cell * 0.8f), .height = ui_sizing_fixed(cell * 0.8f)}},
                  .image = {.texture = g->icons[g->inv.cursor.item]},
                  .aspect_ratio = {1.0f}}) {}
      if (g->inv.cursor.count > 1) {
        Allocator fa = vx_frame_alloc();
        ui_text(vx_uistr(str_format(&fa, "%", fmt_u32(g->inv.cursor.count))),
                ui_text_config({.fontSize = 12, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
                                .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
      }
    }
  } else if (g->inv_hover_recipe >= 0 && !g->touch_mode) {
    const VxRecipe *r = &vx_recipes[g->inv_hover_recipe];
    vx_item_tooltip(g, r->out.item, r);
  } else if (g->inv_hover_item && !g->touch_mode) {
    vx_item_tooltip(g, g->inv_hover_item, NULL);
  }
}

// ---- wardrobe ----

hz_internal const VxItemId vx_wardrobe_weapons[] = {I_COPPER_SHORTSWORD, I_COPPER_BROADSWORD, I_IRON_BROADSWORD,
                                                    I_SILVER_BROADSWORD, I_GOLD_BROADSWORD,   I_ICE_BLADE,
                                                    I_ENCHANTED_SWORD,   I_NIGHTS_EDGE,       I_WOODEN_BOW,
                                                    I_WAND_OF_SPARKING};
hz_internal const VxItemId vx_wardrobe_head[] = {I_MINING_HELMET, I_IRON_HELMET, I_GOLD_HELMET};
hz_internal const VxItemId vx_wardrobe_body[] = {I_IRON_CHAINMAIL, I_GOLD_CHAINMAIL};
hz_internal const VxItemId vx_wardrobe_legs[] = {I_IRON_GREAVES, I_GOLD_GREAVES};
hz_internal const VxItemId vx_wardrobe_acc[] = {I_ANGEL_WINGS, I_HERMES_BOOTS, I_CLOUD_BOTTLE, I_GRAPPLING_HOOK};

hz_internal b32 vx_wardrobe_worn(const VxGame *g, VxItemId item) {
  for (u32 i = 0; i < VX_ARMOR_SLOTS; i++)
    if (g->inv.armor[i].item == item) return true;
  for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
    if (g->inv.accessory[i].item == item) return true;
  return g->inv.slot[g->inv.selected].item == item;
}

// a Terraria slot that acts on a click instead of picking the stack up; returns the click
hz_internal b32 vx_wardrobe_slot(VxGame *g, HzUIElementId id, VxStack s, VxSlotKind kind, f32 cell, b32 lit,
                                 const char *hint) {
  b32 clicked = false;
  ui_element({.id = id, .layout = {.layout_direction = UI_STACK}}) {
    UIInteract a = ui_interact(&g->input);
    if (a.hovered) {
      g->inv_hover_item = s.item;
      g->cursor = OS_CURSOR_HAND;
      clicked = a.clicked;
    }
    vx_slot_ui(g, ui_idi("WardrobeTile", id.id), &s, kind, cell, lit, false, hint);
    if (a.hovered) {
      ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT, .z_index = 12,
                               .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                  .layout = {.sizing = {.width = ui_sizing_fixed(cell), .height = ui_sizing_fixed(cell)}},
                  .background_color = {255, 255, 255, 45},
                  .corner_radius = ui_corner_radius(cell * 0.14f)}) {}
    }
  }
  return clicked;
}

hz_internal void vx_wardrobe_label(const char *text, f32 size) {
  ui_text(vx_uis(text), ui_text_config({.fontSize = (u16)size, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
                                        .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
}

// a row of catalog items, optionally under a side label; a click wears, holds or takes off
hz_internal void vx_wardrobe_row(VxGame *g, const char *label, const VxItemId *items, u32 n, u32 base, f32 cell,
                                 f32 gap, f32 label_w) {
  ui_element({.layout = {.child_gap = (u16)gap, .child_alignment = {.y = UI_ALIGN_Y_CENTER}}}) {
    if (label_w > 0) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(label_w)}}}) {
        ui_text(vx_uis(label), ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
      }
    }
    for (u32 i = 0; i < n; i++) {
      u8 ik = vx_item(items[i])->kind;
      VxSlotKind kind = ik == VX_IK_ACCESSORY                   ? VX_SLOT_ACCESSORY
                        : ik >= VX_IK_HEAD && ik <= VX_IK_LEGS ? VX_SLOT_HEAD
                                                                : VX_SLOT_INV;
      if (vx_wardrobe_slot(g, ui_idi("WardrobeItem", base + i), (VxStack){items[i], 1}, kind, cell,
                           vx_wardrobe_worn(g, items[i]), NULL))
        vx_wardrobe_pick(g, items[i]);
    }
  }
}

// the character creator's way: arrows step through each choice
hz_internal void vx_wardrobe_stepper(VxGame *g, u32 part, const char *label) {
  VxSettings *s = &g->settings;
  u32 n = vx_puppet_option_count((VxPuppetPart)part), *field = vx_pp_field(s, (VxPuppetPart)part);
  u32 cur = *field % n;
  ui_element({.layout = {.child_gap = 6, .child_alignment = {.y = UI_ALIGN_Y_CENTER}}}) {
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(84)}}}) {
      ui_text(vx_uis(label), ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
    }
    for (u32 side = 0; side < 2; side++) {
      if (side == 1) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(110), .height = ui_sizing_fixed(28)},
                               .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                    .background_color = {52, 70, 150, 190},
                    .corner_radius = ui_corner_radius(5),
                    .border = {.color = {12, 16, 40, 200}, .width = ui_border_outside(2)}}) {
          ui_text(vx_uis(vx_puppet_option_names((VxPuppetPart)part)[cur]),
                  ui_text_config({.fontSize = 13, .textColor = VX_C_TEXT, .outlineColor = {0, 0, 0, 200},
                                  .outlineWidth = 1}));
        }
      }
      HzUIElementId aid = ui_idi("WardrobeArrow", part * 2 + side);
      ui_element({.id = aid,
                  .layout = {.sizing = {.width = ui_sizing_fixed(28), .height = ui_sizing_fixed(28)},
                             .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                  .background_color =
                      ui_pointer_over(aid) ? (HzUIColor){90, 120, 200, 230} : (HzUIColor){70, 110, 170, 190},
                  .corner_radius = ui_corner_radius(5),
                  .border = {.color = {12, 16, 40, 200}, .width = ui_border_outside(2)}}) {
        UIInteract a = ui_interact(&g->input);
        if (a.hovered) g->cursor = OS_CURSOR_HAND;
        if (a.clicked) *field = side ? (cur + 1) % n : (cur + n - 1) % n;
        ui_text(vx_uis(side ? ">" : "<"), ui_text_config({.fontSize = 16, .weight = UI_FONT_BOLD, .textColor = VX_C_TEXT}));
      }
    }
  }
}

hz_internal void vx_wardrobe_screen(VxGame *g) {
  b32 compact = (f32)g->ui->canvas_height < VX_COMPACT_H;
  f32 cell = compact ? 36.0f : 48.0f, gap = compact ? 3.0f : VX_INV_GAP, label_w = 44;
  g->inv_hover_item = 0;
  HzUIElementDesc sd = vx_screen_desc(g, ui_id("WardrobeScreen"));
  ui_element_of(&sd) {
    HzUIElementDesc pd = vx_panel_desc(ui_id("WardrobePanel"), 0);
    pd.layout.padding = compact ? (HzUIPadding){.left = 16, .right = 16, .top = 10, .bottom = 10}
                                : (HzUIPadding){.left = 24, .right = 24, .top = 18, .bottom = 18};
    ui_element_of(&pd) {
      vx_title("Wardrobe", compact ? 20 : 26);
      ui_element({.layout = {.child_gap = (u16)(gap * 8), .padding = {.top = compact ? 4 : 10}}}) {
        // the catalog: everything there is to wear or hold
        ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
          vx_wardrobe_label("Weapons", 16);
          vx_wardrobe_row(g, "", vx_wardrobe_weapons, 5, 0, cell, gap, 0);
          vx_wardrobe_row(g, "", vx_wardrobe_weapons + 5, 5, 5, cell, gap, 0);
          ui_element({.layout = {.sizing = {.height = ui_sizing_fixed(gap * 2)}}}) {}
          vx_wardrobe_label("Armor", 16);
          vx_wardrobe_row(g, "Head", vx_wardrobe_head, ARRAY_SIZE(vx_wardrobe_head), 10, cell, gap, label_w);
          vx_wardrobe_row(g, "Body", vx_wardrobe_body, ARRAY_SIZE(vx_wardrobe_body), 20, cell, gap, label_w);
          vx_wardrobe_row(g, "Legs", vx_wardrobe_legs, ARRAY_SIZE(vx_wardrobe_legs), 30, cell, gap, label_w);
          ui_element({.layout = {.sizing = {.height = ui_sizing_fixed(gap * 2)}}}) {}
          vx_wardrobe_label("Accessories", 16);
          vx_wardrobe_row(g, "", vx_wardrobe_acc, ARRAY_SIZE(vx_wardrobe_acc), 40, cell, gap, 0);
        }
        // what is on now: a click takes it off
        ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
          vx_wardrobe_label("Equipped", 16);
          ui_element({.layout = {.child_gap = (u16)gap}}) {
            ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
              for (u32 i = 0; i < VX_ARMOR_SLOTS; i++)
                if (vx_wardrobe_slot(g, ui_idi("WardrobeArmor", i), g->inv.armor[i], (VxSlotKind)(VX_SLOT_HEAD + i),
                                     cell, false, i == 0 ? "Head" : i == 1 ? "Body" : "Legs"))
                  g->inv.armor[i] = (VxStack){0};
            }
            ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap}}) {
              for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
                if (vx_wardrobe_slot(g, ui_idi("WardrobeAcc", i), g->inv.accessory[i], VX_SLOT_ACCESSORY, cell, false,
                                     "Acc") &&
                    g->inv.accessory[i].item)
                  vx_wardrobe_pick(g, g->inv.accessory[i].item);
            }
          }
          Allocator fa = vx_frame_alloc();
          ui_text(vx_uistr(str_format(&fa, "Defense %", fmt_u32(vx_inventory_defense(&g->inv)))),
                  ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
        }
        // the body under it all
        ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)(gap + 2)}}) {
          vx_wardrobe_label("Appearance", 16);
          for (u32 i = 0; i < VX_PP_COUNT; i++) vx_wardrobe_stepper(g, i, VX_PP_LABELS[i]);
        }
      }
      // the hotbar: a click picks the slot a weapon goes into
      ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = (u16)gap, .padding = {.top = 8}}}) {
        vx_wardrobe_label("Hotbar", 16);
        ui_element({.layout = {.child_gap = (u16)gap}}) {
          for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++)
            if (vx_wardrobe_slot(g, ui_idi("WardrobeHotbar", i), g->inv.slot[i], VX_SLOT_INV, cell,
                                 i == g->inv.selected, NULL))
              vx_game_select(g, i);
        }
      }
      ui_element({.layout = {.child_gap = 10, .padding = {.top = 8}, .child_alignment = {.y = UI_ALIGN_Y_CENTER}}}) {
        if (vx_button(g, ui_id("WardrobeClearBtn"), "Clear outfit", VX_BTN_WARDROBE_CLEAR, false, false))
          vx_wardrobe_clear(g);
        if (vx_button(g, ui_id("WardrobeDoneBtn"), "Done", VX_BTN_WARDROBE_DONE, true, false)) vx_game_lock(g);
        vx_muted("Click to wear or hold, click again to take off. Weapons go in the selected hotbar slot. B or Esc "
                 "closes.",
                 13);
      }
    }
  }
  if (g->inv_hover_item && !g->touch_mode) vx_item_tooltip(g, g->inv_hover_item, NULL);
}

// ---- error ----

hz_internal void vx_error_screen(VxGame *g) {
  HzUIElementDesc sd5 = vx_screen_desc(g, ui_id("ErrorScreen"));
  ui_element_of(&sd5) {
    HzUIElementDesc pd10 = vx_panel_desc(ui_id("ErrorPanel"), 720);
    ui_element_of(&pd10) {
      vx_title("Something went wrong", 28);
      ui_text(vx_uis(g->error_text ? g->error_text : ""),
              ui_text_config({.fontSize = 12, .textColor = {255, 180, 180, 255}, .lineHeight = 17,
                              .wrapMode = UI_TEXT_WRAP_WORDS}));
    }
  }
}

// ---- hud ----

// the web page's "Made with Kaigen" link owns the top-right corner above this
#define VX_BADGE_CLEAR 66.0f
#define VX_TOUCH_SIDE 44.0f // clears the notch of a phone held in landscape
#define VX_TOUCH_BOTTOM 22.0f

// a round on-screen button; `lit` while held or toggled on
hz_internal void vx_touch_button(HzUIElementId id, const char *label, f32 size, v2 offset, b32 lit) {
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
    ui_text(vx_uis(label), ui_text_config({.fontSize = 13, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
  }
}

// stick, jump, sneak and menu, laid out for a phone in landscape; vx_touch_update hit-tests these ids
hz_internal void vx_touch_hud(VxGame *g) {
  const VxTouch *t = &g->touch;
  f32 h = (f32)g->ui->canvas_height;
  b32 stick = vx_touch_stick_active(t);
  // the stick sits where the thumb landed; idle, a faint ring shows where to put it
  v2 center = stick ? t->stick_center : (v2){VX_TOUCH_SIDE + 90, h - VX_TOUCH_BOTTOM - 90};
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

  vx_touch_button(ui_id("TouchJump"), "Jump", 76, (v2){-VX_TOUCH_SIDE, -VX_TOUCH_BOTTOM}, t->jump_held);
  // the hook only shows once one is equipped; the potion button only with potions in the bag
  if (g->player.has_hook)
    vx_touch_button(ui_id("TouchHook"), "Hook", 60, (v2){-VX_TOUCH_SIDE - 96, -VX_TOUCH_BOTTOM - 8}, false);
  if (vx_inventory_count(&g->inv, I_HEALING_POTION))
    vx_touch_button(ui_id("TouchHeal"), "Heal", 52, (v2){-VX_TOUCH_SIDE - 92, -VX_TOUCH_BOTTOM - 96}, false);
  b32 flying = g->player.flying;
  vx_touch_button(ui_id("TouchSneak"), flying ? "Down" : "Sneak", 60, (v2){-VX_TOUCH_SIDE - 8, -VX_TOUCH_BOTTOM - 92},
                  flying ? t->sneak_held : t->sneak_on);

  ui_element({.id = ui_id("TouchPause"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-VX_TOUCH_SIDE, VX_BADGE_CLEAR},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.padding = {.left = 16, .right = 16, .top = 10, .bottom = 10}},
              .background_color = {10, 12, 16, 140},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {255, 255, 255, 60}, .width = ui_border_outside(1)}}) {
    ui_text(ui_string("Menu"), ui_text_config({.fontSize = 14, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
  }
  ui_element({.id = ui_id("TouchMap"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-VX_TOUCH_SIDE - 84, VX_BADGE_CLEAR},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.padding = {.left = 16, .right = 16, .top = 10, .bottom = 10}},
              .background_color = g->show_map ? (HzUIColor){255, 255, 255, 90} : (HzUIColor){10, 12, 16, 140},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {255, 255, 255, 60}, .width = ui_border_outside(1)}}) {
    ui_text(ui_string("Map"), ui_text_config({.fontSize = 14, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
  }
  // the Terraria side view, the touch counterpart of C
  ui_element({.id = ui_id("TouchView"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-VX_TOUCH_SIDE - 160, VX_BADGE_CLEAR},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.padding = {.left = 16, .right = 16, .top = 10, .bottom = 10}},
              .background_color = g->cut.on ? (HzUIColor){255, 255, 255, 90} : (HzUIColor){10, 12, 16, 140},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {255, 255, 255, 60}, .width = ui_border_outside(1)}}) {
    ui_text(ui_string("Side"), ui_text_config({.fontSize = 14, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT}));
  }
}

// touch devices play in landscape: portrait gets this instead of the game's screens
hz_internal void vx_rotate_prompt(VxGame *g) {
  HzUIElementDesc sd = vx_screen_desc(g, ui_id("RotateScreen"));
  sd.floating.z_index = 40;
  ui_element_of(&sd) {
    HzUIElementDesc pd = vx_panel_desc(ui_id("RotatePanel"), 0);
    ui_element_of(&pd) {
      vx_title("Rotate your phone", 24);
      vx_muted("Voxaria plays in landscape.", 14);
    }
  }
}

// the vitals' top edge: below the web badge, and below the touch Menu and Map buttons when they show
force_inline f32 vx_vitals_top(const VxGame *g) { return VX_BADGE_CLEAR + (g->touch_mode ? 56.0f : 24.0f); }

// one hud picture centred in a fixed cell; size and alpha show a partly filled heart or star
hz_internal void vx_hud_pic(GpuTexture tex, u32 cell, f32 size, f32 alpha) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(cell), .height = ui_sizing_fixed(cell)},
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}}}) {
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(size), .height = ui_sizing_fixed(size)}},
                .image = {.texture = tex},
                .opacity = opt_some(f32, alpha)}) {}
  }
}

hz_internal void vx_hud_label(String s) {
  ui_text(vx_uistr(s), ui_text_config({.fontSize = 14, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
                                        .outlineColor = {0, 0, 0, 200}, .outlineWidth = 1}));
}

// Terraria's corner: life as hearts of 20, a row of bubbles while under water, mana as a column of stars
hz_internal void vx_vitals_hud(VxGame *g) {
  const VxVitals *v = &g->vitals;
  Allocator fa = vx_frame_alloc();
  ui_element({.id = ui_id("Vitals"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-14, vx_vitals_top(g)},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.child_gap = 10}}) {
    ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 2,
                           .child_alignment = {.x = UI_ALIGN_X_RIGHT}}}) {
      vx_hud_label(str_format(&fa, "Life: %/%", fmt_u32((u32)m_ceilf(m_maxf(v->hp, 0))), fmt_u32((u32)v->hp_max)));
      u32 hearts = (u32)m_ceilf(v->hp_max / 20.0f);
      for (u32 row = 0; row * 10 < hearts; row++) {
        ui_element({.layout = {.child_gap = 0}}) {
          for (u32 i = row * 10; i < MIN(hearts, row * 10 + 10); i++) {
            f32 fill = m_clampf((v->hp - (f32)i * 20.0f) / 20.0f, 0, 1);
            if (fill <= 0) vx_hud_pic(g->hud_icons[VX_HUD_HEART_EMPTY], 26, 22, 0.8f);
            else vx_hud_pic(g->hud_icons[VX_HUD_HEART], 26, 22 * (0.55f + 0.45f * fill), 0.55f + 0.45f * fill);
          }
        }
      }
      if (v->breath < v->breath_max) {
        ui_element({.layout = {.child_gap = 0}}) {
          for (u32 i = 0; i < 10; i++) {
            f32 fill = m_clampf(v->breath / v->breath_max * 10.0f - (f32)i, 0, 1);
            if (fill > 0) vx_hud_pic(g->hud_icons[VX_HUD_BUBBLE], 22, 18 * (0.5f + 0.5f * fill), fill);
          }
        }
      }
    }
    ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 0,
                           .child_alignment = {.x = UI_ALIGN_X_CENTER}}}) {
      vx_hud_label(STR_FROM_CSTR("Mana"));
      u32 stars = (u32)m_ceilf(v->mana_max / 20.0f);
      for (u32 i = 0; i < stars; i++) {
        f32 fill = m_clampf((v->mana - (f32)i * 20.0f) / 20.0f, 0, 1);
        if (fill <= 0) vx_hud_pic(g->hud_icons[VX_HUD_STAR_EMPTY], 28, 24, 0.8f);
        else vx_hud_pic(g->hud_icons[VX_HUD_STAR], 28, 24 * (0.55f + 0.45f * fill), 0.55f + 0.45f * fill);
      }
    }
  }
  // in first person no body shows the hit, so the screen takes the red flash; otherwise the puppet does
  if (v->hurt_flash > 0.01f && !v->dead && g->cam.mode == VX_CAM_FIRST && !g->cut.on) {
    ui_element({.id = ui_id("HurtFlash"),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 4,
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)}},
                .background_color = {200, 20, 20, (u8)(70 * v->hurt_flash)}}) {}
  }
  if (!v->dead) return;
  u32 left = (u32)m_ceilf(m_maxf(0, VX_RESPAWN_TIME - v->death_t));
  f32 in = m_clampf(v->death_t / 0.6f, 0, 1);
  ui_element({.id = ui_id("DeathScreen"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT, .z_index = 30,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)},
                         .layout_direction = UI_TOP_TO_BOTTOM,
                         .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER},
                         .child_gap = 10},
              .background_color = {30, 0, 0, (u8)(120 * in)},
              .opacity = opt_some(f32, in)}) {
    ui_text(ui_string("You were slain..."),
            ui_text_config({.fontSize = 46, .weight = UI_FONT_BOLD, .textColor = {235, 70, 60, 255},
                            .outlineColor = {20, 0, 0, 230}, .outlineWidth = 2}));
    ui_text(vx_uis(v->death_msg ? v->death_msg : ""),
            ui_text_config({.fontSize = 18, .textColor = VX_C_TEXT, .outlineColor = {0, 0, 0, 200}, .outlineWidth = 1}));
    ui_text(vx_uistr(str_format(&fa, "Respawning in %", fmt_u32(left))),
            ui_text_config({.fontSize = 15, .textColor = {255, 255, 255, 190}, .outlineColor = {0, 0, 0, 200},
                            .outlineWidth = 1}));
  }
}

// pickup, damage and heal numbers floating where they happened, projected with this frame's camera
hz_internal void vx_world_texts(VxGame *g) {
  const VxRenderer *r = &g->renderer;
  f32 W = (f32)g->ui->canvas_width, H = (f32)g->ui->canvas_height;
  Allocator fa = vx_frame_alloc();
  for (u32 i = 0; i < g->text_count; i++) {
    const VxWorldText *t = &g->texts[i];
    v4 c = m4_mulv4(r->frame.view_proj_nj, (v4){(f32)(t->p.x - r->prev_cam.x), (f32)(t->p.y - r->prev_cam.y),
                                                (f32)(t->p.z - r->prev_cam.z), 1});
    if (c.w < 0.1f) continue;
    f32 sx = (c.x / c.w * 0.5f + 0.5f) * W, sy = (0.5f - c.y / c.w * 0.5f) * H;
    if (sx < -200 || sx > W + 200 || sy < -50 || sy > H + 50) continue;
    f32 life = t->t / VX_WORLD_TEXT_LIFE, alpha = life < 0.7f ? 1.0f : 1.0f - (life - 0.7f) / 0.3f;
    // a short pop as it appears
    f32 pop = 1.0f + 0.35f * m_maxf(0, 1 - t->t * 8);
    String s;
    HzUIColor col;
    if (t->kind == VX_WT_PICKUP) {
      const VxItemInfo *it = vx_item(t->item);
      s = t->amount > 1 ? str_format(&fa, "% (%)", fmt_cstr(it->name), fmt_u32(t->amount)) : str_format(&fa, "%", fmt_cstr(it->name));
      col = vx_rarity_color(it->rarity);
    } else {
      s = str_format(&fa, "%", fmt_u32(t->amount));
      col = t->kind == VX_WT_HEAL   ? (HzUIColor){100, 255, 100, 255}
            : t->kind == VX_WT_CRIT ? (HzUIColor){255, 150, 40, 255}
                                    : (HzUIColor){255, 90, 70, 255};
      if (t->kind == VX_WT_CRIT) pop *= 1.3f;
    }
    ui_element({.id = ui_idi("WorldText", i),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {sx, sy}, .z_index = 4,
                             .attach_points = {.element = UI_ATTACH_POINT_CENTER_CENTER},
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .opacity = opt_some(f32, alpha)}) {
      ui_text(vx_uistr(s), ui_text_config({.fontSize = (u16)(16 * pop), .weight = UI_FONT_BOLD, .textColor = col,
                                           .outlineColor = {0, 0, 0, 230}, .outlineWidth = 2}));
    }
  }
}

// a life bar under each creature hurt in the last few seconds, as Terraria's
hz_internal void vx_creature_bars(VxGame *g) {
  const VxRenderer *r = &g->renderer;
  f32 W = (f32)g->ui->canvas_width, H = (f32)g->ui->canvas_height;
  for (u32 i = 0; i < g->mobs.count; i++) {
    const VxCreature *m = &g->mobs.items[i];
    if (m->show_hp <= 0 || m->hp <= 0 || m->hp >= m->hp_max || vx_creature_info[m->kind].behaviour == VX_BEH_BOSS) continue;
    v4 c = m4_mulv4(r->frame.view_proj_nj, (v4){(f32)(m->pos.x - r->prev_cam.x), (f32)(m->pos.y - 0.25 - r->prev_cam.y),
                                                (f32)(m->pos.z - r->prev_cam.z), 1});
    if (c.w < 0.1f) continue;
    f32 sx = (c.x / c.w * 0.5f + 0.5f) * W, sy = (0.5f - c.y / c.w * 0.5f) * H;
    if (sx < -60 || sx > W + 60 || sy < -20 || sy > H + 20) continue;
    f32 k = m_saturatef(m->hp / m->hp_max), alpha = m_minf(1, m->show_hp * 2);
    // green at full, through yellow, to red
    HzUIColor fill = {(u8)(255 * m_minf(1, 2 - 2 * k)), (u8)(220 * m_minf(1, 2 * k)), 40, 255};
    f32 w = m_clampf(1400.0f / c.w, 34, 80);
    ui_element({.id = ui_idi("CreatureBar", i),
                .layout = {.sizing = {.width = ui_sizing_fixed(w), .height = ui_sizing_fixed(7)}, .padding = ui_padding_all(1)},
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {sx, sy}, .z_index = 3,
                             .attach_points = {.element = UI_ATTACH_POINT_CENTER_CENTER},
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .background_color = {10, 10, 14, 220},
                .corner_radius = ui_corner_radius(2),
                .opacity = opt_some(f32, alpha)}) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed((w - 2) * k), .height = ui_sizing_grow(0)}},
                  .background_color = fill,
                  .corner_radius = ui_corner_radius(1)}) {}
    }
  }
}

// townsfolk names float over their heads while the player is near, as Terraria's hover names
hz_internal void vx_townsfolk_names(VxGame *g) {
  const VxRenderer *r = &g->renderer;
  f32 W = (f32)g->ui->canvas_width, H = (f32)g->ui->canvas_height;
  for (u32 i = 0; i < g->mobs.count; i++) {
    const VxCreature *m = &g->mobs.items[i];
    if (!vx_creature_friendly(m->kind)) continue;
    f64 dx = m->pos.x - g->player.pos.x, dz = m->pos.z - g->player.pos.z;
    f32 d = (f32)m_sqrt(dx * dx + dz * dz);
    if (d > 14) continue;
    f32 half, h;
    vx_creature_size(m, &half, &h);
    v4 c = m4_mulv4(r->frame.view_proj_nj, (v4){(f32)(m->pos.x - r->prev_cam.x), (f32)(m->pos.y + h + 0.45 - r->prev_cam.y),
                                                (f32)(m->pos.z - r->prev_cam.z), 1});
    if (c.w < 0.1f) continue;
    f32 sx = (c.x / c.w * 0.5f + 0.5f) * W, sy = (0.5f - c.y / c.w * 0.5f) * H;
    if (sx < -100 || sx > W + 100 || sy < -30 || sy > H + 30) continue;
    ui_element({.id = ui_idi("TownName", i),
                .floating = {.attach_to = UI_ATTACH_TO_ROOT, .offset = {sx, sy}, .z_index = 3,
                             .attach_points = {.element = UI_ATTACH_POINT_CENTER_CENTER},
                             .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .opacity = opt_some(f32, m_saturatef((14 - d) / 3))}) {
      ui_text(vx_uis(vx_creature_name(m)), ui_text_config({.fontSize = 15, .weight = UI_FONT_BOLD, .textColor = {255, 240, 190, 255},
                                                            .outlineColor = {0, 0, 0, 230}, .outlineWidth = 2}));
    }
  }
}

// Terraria's chat box: the townsperson's words at the top of the screen, and how to go on
hz_internal void vx_talk_panel(VxGame *g) {
  const VxCreature *m = vx_talk_partner(g);
  f32 t = ui_spring(g->ui, ui_id("TalkT"), m ? 1.0f : 0.0f);
  if (!m || t < 0.01f) return;
  ui_element({.id = ui_id("Talk"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_TOP, .parent = UI_ATTACH_POINT_CENTER_TOP},
                           .offset = {0, 18 + 10 * (1 - t)},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .opacity = opt_some(f32, t),
              .layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 8,
                         .sizing = {.width = ui_sizing_fixed(m_minf(560, (f32)g->ui->canvas_width - 32))},
                         .padding = {.left = 18, .right = 18, .top = 12, .bottom = 12}},
              .background_color = {26, 36, 92, 215},
              .corner_radius = ui_corner_radius(10),
              .border = {.color = {120, 140, 220, 200}, .width = ui_border_outside(2)}}) {
    ui_text(vx_uis(vx_creature_name(m)), ui_text_config({.fontSize = 16, .weight = UI_FONT_BOLD, .textColor = {255, 214, 110, 255}}));
    ui_text(vx_uis(vx_talk_text(g)), ui_text_config({.fontSize = 17, .lineHeight = 23, .weight = UI_FONT_SEMIBOLD,
                                                     .textColor = VX_C_TEXT, .wrapMode = UI_TEXT_WRAP_WORDS}));
    ui_text(vx_uis(g->touch_mode ? "Tap the Guide for another hint \xc2\xb7 walk away to close"
                                    : "Right-click for another hint \xc2\xb7 walk away to close"),
            ui_text_config({.fontSize = 12, .textColor = {200, 210, 255, 190}}));
  }
}

// the explored map under a window centred on the player, north up, scale ui px per block
hz_internal void vx_map_view(VxGame *g, HzUIElementId id, f32 w, f32 h, f32 scale, v2 centre_world) {
  f32 full = VX_MAP_SIZE * scale;
  v2 c = {(centre_world.x + VX_WORLD_HALF) * scale, (centre_world.y + VX_WORLD_HALF) * scale};
  ui_element({.id = id,
              .layout = {.sizing = {.width = ui_sizing_fixed(w), .height = ui_sizing_fixed(h)}},
              .background_color = {8, 10, 18, 230},
              .clip = {.horizontal = true, .vertical = true, .child_offset = {w * 0.5f - c.x, h * 0.5f - c.y}},
              .corner_radius = ui_corner_radius(6)}) {
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(full), .height = ui_sizing_fixed(full)}},
                .image = {.texture = g->map.tex}}) {}
  }
}

// a marker over a map view: offset in ui px from the view's centre
hz_internal void vx_map_marker(HzUIElementId id, v2 offset, f32 size, HzUIColor fill) {
  ui_element({.id = id,
              .floating = {.attach_to = UI_ATTACH_TO_PARENT,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_CENTER,
                                             .parent = UI_ATTACH_POINT_CENTER_CENTER},
                           .offset = {offset.x, offset.y},
                           .z_index = 7,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {.width = ui_sizing_fixed(size), .height = ui_sizing_fixed(size)}},
              .background_color = fill,
              .corner_radius = ui_corner_radius(size * 0.5f),
              .border = {.color = {255, 255, 255, 255}, .width = ui_border_outside(2)}}) {}
}

// Terraria's corner minimap, under the vitals
hz_internal void vx_minimap(VxGame *g) {
  const f32 size = 168, scale = 1.5f;
  VxV3d p = g->player.pos;
  ui_element({.id = ui_id("Minimap"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-14, vx_vitals_top(g) + 94},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.layout_direction = UI_STACK},
              .border = {.color = {200, 170, 90, 220}, .width = ui_border_outside(2)},
              .corner_radius = ui_corner_radius(6)}) {
    vx_map_view(g, ui_id("MinimapView"), size, size, scale, (v2){(f32)p.x, (f32)p.z});
    v2 s = {(f32)(g->spawn_pos.x - p.x) * scale, (f32)(g->spawn_pos.z - p.z) * scale};
    if (m_absf(s.x) < size * 0.5f - 4 && m_absf(s.y) < size * 0.5f - 4)
      vx_map_marker(ui_id("MinimapSpawn"), s, 8, (HzUIColor){90, 200, 255, 255});
    vx_map_marker(ui_id("MinimapPlayer"), (v2){0, 0}, 9, (HzUIColor){235, 60, 50, 255});
  }
}

// the whole explored world, fitted to the screen
hz_internal void vx_full_map(VxGame *g) {
  f32 W = (f32)g->ui->canvas_width, H = (f32)g->ui->canvas_height;
  f32 side = m_minf(W, H) * 0.86f, scale = side / VX_MAP_SIZE;
  HzUIElementDesc sd = vx_screen_desc(g, ui_id("MapScreen"));
  sd.floating.z_index = 20;
  sd.floating.pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH;
  ui_element_of(&sd) {
    ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 6,
                           .child_alignment = {.x = UI_ALIGN_X_CENTER}}}) {
      ui_element({.layout = {.layout_direction = UI_STACK},
                  .border = {.color = {200, 170, 90, 220}, .width = ui_border_outside(2)}}) {
        vx_map_view(g, ui_id("FullMapView"), side, side, scale, (v2){0, 0});
        VxV3d p = g->player.pos;
        vx_map_marker(ui_id("MapSpawn"), (v2){(f32)g->spawn_pos.x * scale, (f32)g->spawn_pos.z * scale}, 10,
                      (HzUIColor){90, 200, 255, 255});
        vx_map_marker(ui_id("MapPlayer"), (v2){(f32)p.x * scale, (f32)p.z * scale}, 11, (HzUIColor){235, 60, 50, 255});
      }
      ui_text(vx_uis(g->touch_mode ? "Map: tap Map to close" : "Map: M to close  -  red: you, blue: spawn"),
              ui_text_config({.fontSize = 13, .textColor = VX_C_MUTED}));
    }
  }
}

// Terraria's chat log: recent events bottom left, fading out
hz_internal void vx_event_log(VxGame *g) {
  if (!g->event_count) return;
  ui_element({.id = ui_id("EventLog"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_LEFT_BOTTOM, .parent = UI_ATTACH_POINT_LEFT_BOTTOM},
                           .offset = {g->touch_mode ? 240.0f : 16.0f, g->touch_mode ? -24.0f : -84.0f},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 2}}) {
    for (u32 i = 0; i < g->event_count; i++) {
      f32 t = g->event_t[i];
      if (t >= VX_EVENT_LIFE) continue;
      f32 a = m_saturatef((VX_EVENT_LIFE - t) / 2.0f);
      const char *line = g->events + i * VX_EVENT_LEN;
      u32 c = g->event_rgb[i];
      HzUIColor col = c ? (HzUIColor){(u8)(c >> 16), (u8)(c >> 8), (u8)c, 255} : VX_C_TEXT;
      ui_element({.id = ui_idi("EventLine", i), .opacity = opt_some(f32, a)}) {
        ui_text(vx_uis(line), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = col,
                                              .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
      }
    }
  }
}

// the boss's life across the bottom of the screen, its eye (or, once changed, its maw) as the icon
hz_internal void vx_boss_bar(VxGame *g) {
  i32 bi = vx_boss_index(g);
  f32 t = ui_spring(g->ui, ui_id("BossBarT"), bi >= 0 ? 1.0f : 0.0f);
  if (bi < 0 || t < 0.01f) return;
  const VxCreature *m = &g->mobs.items[bi];
  f32 k = m_saturatef(m->hp / m->hp_max), w = 440;
  b32 emp = m->kind == VX_CR_EMPRESS, maw = !emp && m->boss_phase == 2;
  Allocator fa = vx_frame_alloc();
  ui_element({.id = ui_id("BossBar"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_BOTTOM, .parent = UI_ATTACH_POINT_CENTER_BOTTOM},
                           .offset = {0, g->touch_mode ? -150.0f : -112.0f},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.child_gap = 8, .child_alignment = {.y = UI_ALIGN_Y_CENTER}},
              .opacity = opt_some(f32, t)}) {
    // the icon: a white eyeball with its blue iris, or a red maw
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(30), .height = ui_sizing_fixed(30)},
                           .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                .background_color = {240, 234, 226, 255},
                .corner_radius = ui_corner_radius(15),
                .border = {.color = {150, 30, 40, 255}, .width = ui_border_outside(2)}}) {
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(16), .height = ui_sizing_fixed(16)},
                             .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                  .background_color = emp ? (HzUIColor){255, 140, 230, 255} : maw ? (HzUIColor){120, 14, 24, 255} : (HzUIColor){60, 120, 216, 255},
                  .corner_radius = ui_corner_radius(8)}) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(7), .height = ui_sizing_fixed(7)}},
                    .background_color = emp ? (HzUIColor){255, 246, 200, 255} : maw ? (HzUIColor){236, 226, 200, 255} : (HzUIColor){10, 10, 14, 255},
                    .corner_radius = ui_corner_radius(maw ? 1 : 4)}) {}
      }
    }
    ui_element({.layout = {.layout_direction = UI_TOP_TO_BOTTOM, .child_gap = 3}}) {
      ui_text(vx_uis(vx_creature_name(m)), ui_text_config({.fontSize = 14, .weight = UI_FONT_BOLD, .textColor = VX_C_TEXT,
                                                           .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
      ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(w), .height = ui_sizing_fixed(18)}, .padding = ui_padding_all(2),
                             .layout_direction = UI_STACK},
                  .background_color = {22, 10, 14, 225},
                  .corner_radius = ui_corner_radius(4),
                  .border = {.color = {196, 160, 86, 255}, .width = ui_border_outside(2)}}) {
        ui_element({.layout = {.sizing = {.width = ui_sizing_fixed((w - 4) * k), .height = ui_sizing_grow(0)},
                               .layout_direction = UI_TOP_TO_BOTTOM},
                    .background_color = {206, 34, 46, 255},
                    .corner_radius = ui_corner_radius(2)}) {
          // the lit top half of the fill
          ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(6)}},
                      .background_color = {255, 116, 110, 110},
                      .corner_radius = ui_corner_radius(2)}) {}
        }
        ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT,
                                 .attach_points = {.element = UI_ATTACH_POINT_CENTER_CENTER, .parent = UI_ATTACH_POINT_CENTER_CENTER},
                                 .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
          ui_text(vx_uistr(str_format(&fa, "% / %", fmt_f32_p(m_ceilf(m_maxf(m->hp, 0)), 0), fmt_f32_p(m->hp_max, 0))),
                  ui_text_config({.fontSize = 11, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
                                  .outlineColor = {0, 0, 0, 220}, .outlineWidth = 1}));
        }
      }
    }
  }
}

hz_internal void vx_hud(VxGame *g) {
  vx_creature_bars(g);
  vx_boss_bar(g);
  vx_townsfolk_names(g);
  vx_world_texts(g);
  vx_talk_panel(g);
  vx_vitals_hud(g);
  vx_event_log(g);
  if (g->show_map && g->mode == VX_MODE_PLAYING) vx_full_map(g);
  else vx_minimap(g);
  // hotbar: the bag's top row, the held item named above it; the inventory screen shows the whole bag instead
  if (g->mode == VX_MODE_INVENTORY) return;
  ui_element({.id = ui_id("Hotbar"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_CENTER_BOTTOM,
                                             .parent = UI_ATTACH_POINT_CENTER_BOTTOM},
                           .offset = {0, -14},
                           .z_index = 5,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.child_gap = 4, .padding = ui_padding_all(5)},
              .background_color = {10, 12, 16, 120},
              .corner_radius = ui_corner_radius(10)}) {
    for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++) {
      ui_element({.layout = {.layout_direction = UI_STACK}}) {
        vx_slot_ui(g, ui_idi("HotbarSlot", i), &g->inv.slot[i], VX_SLOT_INV, 48, i == g->inv.selected, false, NULL);
        ui_element({.floating = {.attach_to = UI_ATTACH_TO_PARENT, .offset = {5, 2},
                                 .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
          Allocator fa = vx_frame_alloc();
          ui_text(vx_uistr(str_format(&fa, "%", fmt_u32((i + 1) % 10))),
                  ui_text_config({.fontSize = 10, .textColor = {255, 255, 255, 170}}));
        }
      }
    }
    // the touch "..." slot opens the bag, which a keyboard reaches with E
    if (g->touch_mode) {
      ui_element({.id = ui_id("TouchInventory"),
                  .layout = {.sizing = {.width = ui_sizing_fixed(48), .height = ui_sizing_fixed(48)},
                             .child_alignment = {.x = UI_ALIGN_X_CENTER, .y = UI_ALIGN_Y_CENTER}},
                  .background_color = {52, 70, 150, 190},
                  .corner_radius = ui_corner_radius(7),
                  .border = {.color = {12, 16, 40, 200}, .width = ui_border_outside(2)}}) {
        ui_text(ui_string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"),
                ui_text_config({.fontSize = 18, .weight = UI_FONT_BOLD, .textColor = VX_C_TEXT}));
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
      ui_text(vx_uis(g->toast), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = VX_C_TEXT,
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
      ui_text(vx_uis(g->debug_text), ui_text_config({.fontSize = 12, .lineHeight = 17, .textColor = {232, 232, 232, 255},
                                                      .wrapMode = UI_TEXT_WRAP_NEWLINES}));
    }
  }
  // the stick and buttons only mean something in play
  if (g->touch_mode) {
    if (g->mode == VX_MODE_PLAYING) vx_touch_hud(g);
    return;
  }
  // key hint
  ui_element({.id = ui_id("Hint"),
              .floating = {.attach_to = UI_ATTACH_TO_ROOT,
                           .attach_points = {.element = UI_ATTACH_POINT_RIGHT_TOP, .parent = UI_ATTACH_POINT_RIGHT_TOP},
                           .offset = {-12, VX_BADGE_CLEAR},
                           .z_index = 6,
                           .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
    ui_text(ui_string("F3 debug \xc2\xb7 E inventory \xc2\xb7 B wardrobe \xc2\xb7 Esc menu"),
            ui_text_config({.fontSize = 12, .textColor = {255, 255, 255, 178}, .outlineColor = {0, 0, 0, 200},
                            .outlineWidth = 1}));
  }
}

// big readable frame stats for recordings; F3 swaps it for the debug text
hz_internal void vx_stats_row(const char *label, String value) {
  ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0)}, .child_gap = 18}}) {
    ui_element({.layout = {.sizing = {.width = ui_sizing_fixed(56)}}}) {
      ui_text(vx_uis(label), ui_text_config({.fontSize = 15, .textColor = {255, 255, 255, 150}}));
    }
    ui_text(vx_uistr(value), ui_text_config({.fontSize = 15, .weight = UI_FONT_SEMIBOLD, .textColor = {255, 255, 255, 240}}));
  }
}

hz_internal void vx_stats_panel(VxGame *g) {
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
      ui_text(vx_uistr(str_format(&ta, "%", fmt_u32((u32)(fps + 0.5f)))),
              ui_text_config({.fontSize = 38, .weight = UI_FONT_BOLD, .textColor = {120, 235, 140, 255}}));
      ui_element({.layout = {.padding = {.bottom = 6}}}) {
        ui_text(ui_string("FPS"), ui_text_config({.fontSize = 16, .weight = UI_FONT_SEMIBOLD,
                                                   .textColor = {120, 235, 140, 220}}));
      }
    }
    vx_stats_row("frame", str_format(&ta, "% ms", fmt_f32_p(frame_ms, 2)));
    if (g->gpu_ms > 0) vx_stats_row("GPU", str_format(&ta, "% ms", fmt_f32_p(g->gpu_ms, 2)));
    vx_stats_row("CPU", str_format(&ta, "% ms", fmt_f32_p(g->main_ms, 2)));
    ui_element({.layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_fixed(1)},
                           .padding = {.top = 4}},
                .background_color = {255, 255, 255, 50}}) {}
    ui_element({.layout = {.padding = {.top = 4}}}) {
      ui_text(vx_uistr(str_format(&ta, "% triangles \xc2\xb7 % draw calls", fmt_str(tri_s),
                                  fmt_u32(g->renderer.stats.draw_calls + g->renderer.stats.shadow_draw_calls))),
              ui_text_config({.fontSize = 13, .textColor = {255, 255, 255, 200}}));
    }
    ui_text(vx_uistr(str_format(&ta, "% \xc2\xb7 % threads", fmt_cstr(backend), fmt_u32(g->world.lane_count))),
            ui_text_config({.fontSize = 13, .textColor = {255, 255, 255, 200}}));
  }
}

void vx_ui_frame(VxGame *g, f32 dt) {
  UNUSED(dt);
  g->cursor = OS_CURSOR_ARROW;
  // clay lays out in logical px, which is what the ui canvas size is
  f32 view_h = (f32)g->ui->canvas_height;
  ui_element({.id = ui_id("VxRoot"),
              .layout = {.sizing = {.width = ui_sizing_grow(0), .height = ui_sizing_grow(0)}},
              .pointer_capture_mode = UI_POINTER_CAPTURE_MODE_PASSTHROUGH}) {
    // the settings panel is most of the screen tall, and on a short screen every panel is: the hotbar would draw over it
    b32 compact = view_h < VX_COMPACT_H;
    b32 title = vx_title_active(g);
    if (g->mode != VX_MODE_LOADING && g->mode != VX_MODE_ERROR && g->mode != VX_MODE_SETTINGS && !g->hide_hud &&
        g->started && !title && (!compact || g->mode == VX_MODE_PLAYING))
      vx_hud(g);
    if (g->mode != VX_MODE_LOADING && g->mode != VX_MODE_ERROR && g->started && !g->show_debug && !g->hide_hud && !title) vx_stats_panel(g);
    if (vx_touch_portrait(g) && g->mode != VX_MODE_LOADING && g->mode != VX_MODE_ERROR) vx_rotate_prompt(g);
    else switch (g->mode) {
    case VX_MODE_LOADING: vx_loading_screen(g); break;
    case VX_MODE_MENU:
      if (title) vx_title_screen(g);
      else vx_menu_screen(g);
      break;
    case VX_MODE_PAUSED: vx_menu_screen(g); break;
    case VX_MODE_SETTINGS: vx_settings_screen(g, view_h); break;
    case VX_MODE_INVENTORY: vx_inventory_screen(g); break;
    case VX_MODE_WARDROBE: vx_wardrobe_screen(g); break;
    case VX_MODE_ERROR: vx_error_screen(g); break;
    case VX_MODE_PLAYING: break;
    }
  }
}

#undef vx_setting_row

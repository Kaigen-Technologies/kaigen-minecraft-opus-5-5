#include "webcraft/wc_game.h"
#include "input.h"

// text-mode screens (loading, menu, settings, inventory, error) and the hud, drawn into g->tui

#define WC_C_TEXT 0xF2F2F2u
#define WC_C_MUTED 0xA9B0BDu
#define WC_C_DIM 0x6A7080u
#define WC_C_ACCENT 0x7FD36Bu
#define WC_C_HOVER 0xFFFF7Au // minecraft's highlighted button text
#define WC_C_ERROR 0xFFB4B4u
#define WC_C_PANEL 0x0C0E14u
#define WC_C_BUTTON 0x2A2F3Au
#define WC_C_BUTTON_HI 0x3D4556u
#define WC_A_PANEL 225
#define WC_A_SCRIM 120
#define WC_A_HUD 235 // hud panels: scene glyphs must not show through what sits on them

// the web page's "Made with Kaigen" link owns the top-right corner above this many logical px
#define WC_BADGE_CLEAR 66.0f
#define WC_LOGO_WORD "ASCII CRAFT"
#define WC_LOGO_W (WC_CELL_W * (i32)(sizeof(WC_LOGO_WORD) - 1))
#define WC_LOGO_H WC_CELL_H
// grid rows from which the big logo shows: below, it would push the menu's controls off the panel
#define WC_LOGO_MIN_ROWS 30

hz_internal Allocator wc_frame_alloc(void) { return tctx_temp_allocator(NULL); }

force_inline WcRect wc_rect(i32 x, i32 y, i32 w, i32 h) { return (WcRect){x, y, w, h}; }

// a panel w x h centred on the grid (clamped to it)
hz_internal WcRect wc_centred(const WcTui *t, i32 w, i32 h) {
  w = MIN(w, (i32)t->cols);
  h = MIN(h, (i32)t->rows);
  return wc_rect(((i32)t->cols - w) / 2, ((i32)t->rows - h) / 2, w, h);
}

hz_internal void wc_scrim(WcTui *t) { wc_tui_fill(t, wc_rect(0, 0, (i32)t->cols, (i32)t->rows), 0, WC_A_SCRIM); }

hz_internal void wc_panel(WcTui *t, WcRect r) {
  wc_tui_fill(t, r, WC_C_PANEL, WC_A_PANEL);
  wc_tui_box(t, r, WC_C_DIM);
}

// a button: boxed when h >= 3, else one row "[ label ]"; hover lights the text like minecraft's
hz_internal b32 wc_button(WcGame *g, u32 id, WcRect r, const char *label, b32 primary) {
  WcTui *t = &g->tui;
  WcTuiHit h = wc_tui_area(t, id, r);
  if (h.hovered) g->cursor = OS_CURSOR_HAND;
  wc_tui_fill(t, r, h.hovered || h.held ? WC_C_BUTTON_HI : WC_C_BUTTON, 255);
  u32 fg = h.hovered ? WC_C_HOVER : primary ? WC_C_ACCENT : WC_C_TEXT;
  String s = cstr_to_str(label);
  i32 ty = r.y + r.h / 2;
  if (r.h >= 3) {
    wc_tui_box(t, r, h.hovered ? WC_C_HOVER : primary ? WC_C_ACCENT : WC_C_DIM);
  } else {
    wc_tui_put(t, r.x, ty, '[', fg);
    wc_tui_put(t, r.x + r.w - 1, ty, ']', fg);
  }
  wc_tui_center(t, r, ty, s, fg);
  return h.clicked;
}

// the logo in Monocraft letters, one cell per font pixel: stone glyphs in greys over a darker extruded shadow
hz_internal void wc_logo(WcTui *t, i32 x, i32 y) {
  const char *word = WC_LOGO_WORD;
  for (u32 pass = 0; pass < 2; pass++) {
    for (u32 i = 0; word[i]; i++) {
      u32 g = (u32)word[i] - 32;
      for (u32 fy = 0; fy < WC_CELL_H; fy++) {
        for (u32 fx = 0; fx < WC_CELL_W; fx++) {
          if (!wc_font_bit(g, fx, fy)) continue;
          i32 cx = x + (i32)(i * WC_CELL_W + fx), cy = y + (i32)fy;
          if (pass == 0) {
            wc_tui_fill(t, wc_rect(cx + 1, cy + 1, 1, 1), 0x15161Au, 255);
            continue;
          }
          u32 h = wc_hash2i(cx, cy, 0x51u);
          // lighter toward the top, like the lit face of stone
          u32 v = 150 + (h & 31) - fy * 7;
          u32 grey = (v << 16) | (v << 8) | v, under = ((v / 3) << 16) | ((v / 3) << 8) | (v / 3 + 4);
          wc_tui_fill(t, wc_rect(cx, cy, 1, 1), under, 255);
          const char *stone = "#@%&8$";
          wc_tui_put(t, cx, cy, stone[(h >> 8) % 6], grey);
        }
      }
    }
  }
}

hz_internal b32 wc_logo_fits(const WcTui *t, i32 panel_w) {
  return panel_w >= WC_LOGO_W + 4 && (i32)t->rows >= WC_LOGO_MIN_ROWS;
}

// the title block's rows: the big logo when the grid has room, else spaced capitals
hz_internal i32 wc_title_rows(const WcTui *t, i32 panel_w) { return wc_logo_fits(t, panel_w) ? WC_LOGO_H + 2 : 2; }

// the title block: the big logo when the grid has room, else spaced capitals; returns rows used
hz_internal i32 wc_title_block(WcTui *t, WcRect r, i32 y, const char *subtitle) {
  if (wc_logo_fits(t, r.w)) {
    wc_logo(t, r.x + (r.w - WC_LOGO_W) / 2, y);
    wc_tui_center(t, r, y + WC_LOGO_H + 1, cstr_to_str(subtitle), WC_C_MUTED);
    return WC_LOGO_H + 2;
  }
  wc_tui_center(t, r, y, STR_FROM_CSTR("A S C I I   C R A F T"), WC_C_ACCENT);
  wc_tui_center(t, r, y + 1, cstr_to_str(subtitle), WC_C_MUTED);
  return 2;
}

// ---- loading ----

hz_internal void wc_progress_bar(WcTui *t, i32 x, i32 y, i32 w, f32 v, u32 fg) {
  i32 inner = MAX(1, w - 2);
  i32 n = (i32)(m_clampf(v, 0, 1) * (f32)inner + 0.5f);
  wc_tui_put(t, x, y, '[', WC_C_DIM);
  for (i32 i = 0; i < inner; i++) wc_tui_put(t, x + 1 + i, y, i < n ? '#' : '.', i < n ? fg : WC_C_DIM);
  wc_tui_put(t, x + 1 + inner, y, ']', WC_C_DIM);
}

hz_internal void wc_loading_screen(WcGame *g) {
  WcTui *t = &g->tui;
  f32 prog = 0.05f;
  Allocator fa = wc_frame_alloc();
  String text = STR_FROM_CSTR("Starting...");
  if (g->boot != WC_BOOT_START) {
    f32 wp = g->world_created ? wc_world_load_progress(&g->world, (i32)m_minf(4.0f, (f32)g->settings.render_distance)) : 0;
    prog = 0.3f + wp * 0.7f;
    text = str_format(&fa, "Generating terrain...", fmt_u32(0));
  }
  wc_scrim(t);
  i32 pw = MIN(MAX(60, WC_LOGO_W + 4), (i32)t->cols);
  WcRect p = wc_centred(t, pw, wc_logo_fits(t, pw) ? WC_LOGO_H + 8 : 7);
  wc_panel(t, p);
  i32 y = p.y + 1 + wc_title_block(t, p, p.y + 1, "a voxel sandbox");
  i32 bw = MIN(p.w - 10, 40);
  wc_progress_bar(t, p.x + (p.w - bw - 5) / 2, y, bw, prog, WC_C_ACCENT);
  String pct = str_format(&fa, "%%", fmt_u32((u32)(m_clampf(prog, 0, 1) * 100 + 0.5f)), fmt_char('%'));
  wc_tui_text(t, p.x + (p.w - bw - 5) / 2 + bw + 1, y, pct, WC_C_TEXT);
  wc_tui_center(t, p, y + 1, text, WC_C_MUTED);
}

// ---- menu ----

typedef struct {
  const char *key;
  const char *what;
} WcControlRow;

hz_internal const WcControlRow WC_CONTROLS[] = {
    {"W A S D", "Move - double-tap W or Ctrl to sprint"},
    {"Space", "Jump - double-tap to fly (or F)"},
    {"Shift", "Sneak - fly down"},
    {"Left / Right click", "Break / place block"},
    {"Middle click", "Pick block"},
    {"1-9 / wheel", "Select hotbar slot"},
    {"E", "Block inventory"},
    {"T (hold)", "Fast-forward time - Shift+T rewind"},
    {"F1 / F2 / F3", "Hide HUD / screenshot / debug info"},
};

hz_internal const WcControlRow WC_TOUCH_CONTROLS[] = {
    {"Left thumb", "Move - push to the edge to sprint"},
    {"Drag", "Look around"},
    {"Tap / hold", "Place / break a block"},
    {"Jump", "Double-tap to fly - Sneak flies down"},
};

hz_internal void wc_new_world_modal(WcGame *g) {
  if (!g->new_world_open) return;
  WcTui *t = &g->tui;
  wc_tui_fill(t, wc_rect(0, 0, (i32)t->cols, (i32)t->rows), 0, 140);
  String msg = STR_FROM_CSTR("Start a brand new world? Your current world will be replaced.");
  i32 w = MIN(46, (i32)t->cols);
  i32 rows = wc_tui_wrap_rows(msg, w - 4);
  WcRect p = wc_centred(t, w, rows + 8);
  wc_panel(t, p);
  // a click outside the box cancels, like the scrim of a modal
  WcTuiHit outside = wc_tui_area(t, wc_tui_id("NewWorldScrim", 0), wc_rect(0, 0, (i32)t->cols, (i32)t->rows));
  WcTuiHit inside = wc_tui_area(t, wc_tui_id("NewWorldPanel", 0), p);
  UNUSED(inside);
  wc_tui_cstr(t, p.x + 2, p.y + 1, "New World", WC_C_TEXT);
  wc_tui_wrap(t, wc_rect(p.x + 2, p.y + 3, p.w - 4, rows), msg, WC_C_MUTED);
  i32 bw = (p.w - 5) / 2, by = p.y + p.h - 4;
  if (wc_button(g, wc_tui_id("NewWorldCancelBtn", 0), wc_rect(p.x + 2, by, bw, 3), "Cancel", false)) {
    g->new_world_open = false;
  } else if (wc_button(g, wc_tui_id("NewWorldConfirmBtn", 0), wc_rect(p.x + 3 + bw, by, bw, 3), "New World", true)) {
    g->new_world_open = false;
    wc_game_new_world(g);
  } else if (outside.clicked) {
    g->new_world_open = false;
  }
}

hz_internal void wc_menu_screen(WcGame *g) {
  WcTui *t = &g->tui;
  b32 compact = t->rows < 30;
  const WcControlRow *controls = g->touch_mode ? WC_TOUCH_CONTROLS : WC_CONTROLS;
  i32 control_count = g->touch_mode ? (i32)ARRAY_SIZE(WC_TOUCH_CONTROLS) : (i32)ARRAY_SIZE(WC_CONTROLS);
  i32 bh = compact ? 1 : 3;
  i32 pw = MIN(MAX(62, WC_LOGO_W + 4), (i32)t->cols);
  i32 title_h = wc_title_rows(t, pw);
  i32 rows_left = (i32)t->rows - 2 - title_h - 2 * bh - 3;
  i32 shown = MAX(0, MIN(control_count, rows_left - (compact ? 0 : 2)));
  i32 h = 2 + title_h + 1 + 2 * bh + 1 + (shown ? shown + 1 : 0) + (compact ? 0 : 2);
  wc_scrim(t);
  WcRect p = wc_centred(t, pw, h);
  wc_panel(t, p);
  i32 y = p.y + 1 + wc_title_block(t, p, p.y + 1, "a voxel sandbox") + 1;
  const char *play = g->mode == WC_MODE_PAUSED ? "Resume" : g->touch_mode ? "Tap to Play" : "Click to Play";
  if (wc_button(g, wc_tui_id("PlayBtn", 0), wc_rect(p.x + 2, y, p.w - 4, bh), play, true)) wc_game_lock(g);
  y += bh;
  i32 bw = (p.w - 5) / 2;
  if (wc_button(g, wc_tui_id("SettingsBtn", 0), wc_rect(p.x + 2, y, bw, bh), "Settings", false)) {
    g->settings_return = g->mode;
    wc_game_set_mode(g, WC_MODE_SETTINGS);
  }
  if (wc_button(g, wc_tui_id("NewWorldBtn", 0), wc_rect(p.x + 3 + bw, y, bw, bh), "New World", false))
    g->new_world_open = true;
  y += bh + 1;
  i32 key_w = g->touch_mode ? 12 : 20;
  for (i32 i = 0; i < shown; i++) {
    wc_tui_cstr(t, p.x + 2, y, controls[i].key, WC_C_TEXT);
    String what = cstr_to_str(controls[i].what);
    what.len = MIN(what.len, (u32)MAX(0, p.w - 4 - key_w));
    wc_tui_text(t, p.x + 2 + key_w, y, what, WC_C_MUTED);
    y++;
  }
  if (!compact) {
#ifdef WASM
    wc_tui_cstr(t, p.x + 2, y + 1, "Renderer: WebGPU", WC_C_DIM);
#else
    wc_tui_cstr(t, p.x + 2, y + 1, "Renderer: Direct3D 12", WC_C_DIM);
#endif
  }
  wc_new_world_modal(g);
}

// ---- settings ----

typedef struct {
  WcGame *g;
  i32 x, w;      // row columns
  i32 y0;        // grid row of the first visible row
  i32 first, n;  // first visible row index, visible row count
  i32 row;       // index of the next row
  b32 changed, custom;
} WcRows;

// the grid row of the next settings row, or false when it is scrolled out
hz_internal b32 wc_row(WcRows *rs, i32 *y) {
  i32 r = rs->row++;
  if (r < rs->first || r >= rs->first + rs->n) return false;
  *y = rs->y0 + r - rs->first;
  return true;
}

#define WC_LABEL_W 22
#define WC_VALUE_W 9

hz_internal void wc_section(WcRows *rs, const char *title) {
  i32 y;
  if (!wc_row(rs, &y)) return;
  wc_tui_cstr(&rs->g->tui, rs->x, y, title, WC_C_ACCENT);
}

hz_internal void wc_row_label(WcRows *rs, i32 y, const char *label, b32 hot) {
  String s = cstr_to_str(label);
  s.len = MIN(s.len, (u32)(WC_LABEL_W - 1));
  wc_tui_text(&rs->g->tui, rs->x, y, s, hot ? WC_C_TEXT : WC_C_MUTED);
}

// a slider "[#####|......]  value": click or drag along it; *value snaps to step
hz_internal b32 wc_slider(WcRows *rs, u32 index, const char *label, f32 *value, f32 min, f32 max, f32 step,
                          String text) {
  i32 y;
  if (!wc_row(rs, &y)) return false;
  WcTui *t = &rs->g->tui;
  i32 tx = rs->x + WC_LABEL_W, tw = MAX(6, rs->w - WC_LABEL_W - WC_VALUE_W - 1);
  WcRect track = wc_rect(tx, y, tw, 1);
  WcTuiHit h = wc_tui_area(t, wc_tui_id("SliderTrack", index), track);
  if (h.hovered || h.held) rs->g->cursor = OS_CURSOR_HAND;
  b32 changed = false;
  if (h.held && t->px >= 0 && max > min) {
    f32 f = m_clampf((f32)(t->px - (tx + 1)) / (f32)MAX(1, tw - 3), 0, 1);
    f32 v = m_clampf(min + m_floorf(f * (max - min) / step + 0.5f) * step, min, max);
    if (v != *value) {
      *value = v;
      changed = true;
    }
  }
  wc_row_label(rs, y, label, h.hovered || h.held);
  i32 inner = tw - 2;
  i32 knob = max > min ? (i32)(m_clampf((*value - min) / (max - min), 0, 1) * (f32)(inner - 1) + 0.5f) : 0;
  u32 fg = h.hovered || h.held ? WC_C_HOVER : WC_C_ACCENT;
  wc_tui_put(t, tx, y, '[', WC_C_DIM);
  for (i32 i = 0; i < inner; i++)
    wc_tui_put(t, tx + 1 + i, y, i == knob ? '|' : i < knob ? '#' : '.', i <= knob ? fg : WC_C_DIM);
  wc_tui_put(t, tx + tw - 1, y, ']', WC_C_DIM);
  wc_tui_text(t, tx + tw + 1, y, text, WC_C_TEXT);
  return changed;
}

hz_internal b32 wc_toggle(WcRows *rs, u32 index, const char *label, b32 *value) {
  i32 y;
  if (!wc_row(rs, &y)) return false;
  WcTui *t = &rs->g->tui;
  WcTuiHit h = wc_tui_area(t, wc_tui_id("Check", index), wc_rect(rs->x, y, WC_LABEL_W + 3, 1));
  if (h.hovered) rs->g->cursor = OS_CURSOR_HAND;
  b32 changed = false;
  if (h.clicked) {
    *value = !*value;
    changed = true;
  }
  wc_row_label(rs, y, label, h.hovered);
  u32 fg = h.hovered ? WC_C_HOVER : WC_C_ACCENT;
  wc_tui_put(t, rs->x + WC_LABEL_W, y, '[', WC_C_DIM);
  wc_tui_put(t, rs->x + WC_LABEL_W + 1, y, *value ? 'x' : ' ', fg);
  wc_tui_put(t, rs->x + WC_LABEL_W + 2, y, ']', WC_C_DIM);
  return changed;
}

// a cycle button "< Fancy >": the arrows step back and forward, the name steps forward; returns the new option or -1
hz_internal i32 wc_cycle(WcRows *rs, u32 index, const char *label, const char *const *options, u32 count,
                         u32 selected) {
  i32 y;
  if (!wc_row(rs, &y)) return -1;
  WcTui *t = &rs->g->tui;
  i32 x = rs->x + WC_LABEL_W, w = 16;
  WcTuiHit back = wc_tui_area(t, wc_tui_id("CycleBack", index), wc_rect(x, y, 2, 1));
  WcTuiHit fwd = wc_tui_area(t, wc_tui_id("Cycle", index), wc_rect(x + 2, y, w - 2, 1));
  if (back.hovered || fwd.hovered) rs->g->cursor = OS_CURSOR_HAND;
  wc_row_label(rs, y, label, back.hovered || fwd.hovered);
  wc_tui_put(t, x, y, '<', back.hovered ? WC_C_HOVER : WC_C_DIM);
  wc_tui_put(t, x + w - 1, y, '>', fwd.hovered ? WC_C_HOVER : WC_C_DIM);
  wc_tui_center(t, wc_rect(x + 1, y, w - 2, 1), y, cstr_to_str(options[selected % count]),
                fwd.hovered ? WC_C_HOVER : WC_C_TEXT);
  if (back.clicked) return (i32)((selected + count - 1) % count);
  if (fwd.clicked) return (i32)((selected + 1) % count);
  return -1;
}

hz_internal const char *const WC_PRESET_NAMES[] = {"Low", "Medium", "High", "Ultra", "Custom"};
hz_internal const char *const WC_SHADOW_RES_NAMES[] = {"1024", "2048", "3072", "4096"};
hz_internal const char *const WC_CLOUD_NAMES[] = {"Off", "Fast", "Fancy"};
hz_internal const char *const WC_WEATHER_NAMES[] = {"Dynamic", "Always clear", "Rain"};
hz_internal const char *const WC_TEXT_SIZE_NAMES[] = {"Large", "Medium", "Small"};
hz_internal const char *const WC_TINT_NAMES[] = {"Black", "Faint", "Full"};
hz_internal const char *const WC_PALETTE_NAMES[] = {"Colour", "Phosphor", "Amber"};

#define WC_SETTINGS_ROWS 27

hz_internal void wc_settings_rows(WcRows *rs) {
  WcGame *g = rs->g;
  WcSettings *s = &g->settings;
  Allocator fa = wc_frame_alloc();
  wc_section(rs, "GRAPHICS");
  i32 p = wc_cycle(rs, 0, "Quality preset", WC_PRESET_NAMES, ARRAY_SIZE(WC_PRESET_NAMES), s->preset);
  // custom shows but is never stepped onto: any other change is what lands there
  if (p == WC_PRESET_CUSTOM) p = s->preset == WC_PRESET_ULTRA ? WC_PRESET_LOW : WC_PRESET_ULTRA;
  if (p >= 0) {
    wc_settings_apply_preset(s, (WcPreset)p);
    s->preset = (WcPreset)p;
    rs->changed = true;
  }
  f32 rd = (f32)s->render_distance;
  if (wc_slider(rs, 0, "Render distance", &rd, 4, 24, 1, str_format(&fa, "% ch", fmt_u32(s->render_distance)))) {
    s->render_distance = (u32)rd;
    rs->changed = rs->custom = true;
  }
  if (wc_slider(rs, 1, "Render scale", &s->render_scale, 0.5f, 1.0f, 0.05f,
                str_format(&fa, "%%", fmt_u32((u32)(s->render_scale * 100 + 0.5f)), fmt_char('%'))))
    rs->changed = rs->custom = true;
  if (wc_toggle(rs, 0, "Shadows", &s->shadows)) rs->changed = rs->custom = true;
  u32 res_idx = s->shadow_res <= 1024 ? 0 : s->shadow_res <= 2048 ? 1 : s->shadow_res <= 3072 ? 2 : 3;
  i32 r = wc_cycle(rs, 1, "Shadow resolution", WC_SHADOW_RES_NAMES, ARRAY_SIZE(WC_SHADOW_RES_NAMES), res_idx);
  if (r >= 0) {
    s->shadow_res = 1024 * (u32)(r + 1);
    rs->changed = rs->custom = true;
  }
  if (wc_slider(rs, 2, "Shadow distance", &s->shadow_distance, 48, 256, 16,
                str_format(&fa, "% m", fmt_u32((u32)(s->shadow_distance + 0.5f)))))
    rs->changed = rs->custom = true;
  if (wc_toggle(rs, 1, "Ambient occlusion", &s->ssao)) rs->changed = rs->custom = true;
  if (wc_toggle(rs, 2, "Volumetric light", &s->volumetrics)) rs->changed = rs->custom = true;
  i32 c = wc_cycle(rs, 2, "Clouds", WC_CLOUD_NAMES, ARRAY_SIZE(WC_CLOUD_NAMES), s->clouds);
  if (c >= 0) {
    s->clouds = (u32)c;
    rs->changed = rs->custom = true;
  }
  if (wc_toggle(rs, 3, "Water reflections", &s->ssr)) rs->changed = rs->custom = true;
  if (wc_toggle(rs, 4, "Temporal AA", &s->taa)) rs->changed = rs->custom = true;
  if (wc_toggle(rs, 5, "Bloom", &s->bloom)) rs->changed = rs->custom = true;
  wc_section(rs, "TEXT MODE");
  i32 ts = wc_cycle(rs, 3, "Text size", WC_TEXT_SIZE_NAMES, WC_TEXT_SIZES, s->text_size);
  if (ts >= 0) {
    s->text_size = (u32)ts;
    rs->changed = true;
  }
  i32 ti = wc_cycle(rs, 4, "Background tint", WC_TINT_NAMES, WC_TINTS, s->ascii_tint);
  if (ti >= 0) {
    s->ascii_tint = (u32)ti;
    rs->changed = true;
  }
  if (wc_toggle(rs, 6, "Glow", &s->ascii_glow)) rs->changed = true;
  i32 pa = wc_cycle(rs, 5, "Palette", WC_PALETTE_NAMES, WC_PALETTES, s->ascii_palette);
  if (pa >= 0) {
    s->ascii_palette = (u32)pa;
    rs->changed = true;
  }
  wc_section(rs, "GAME");
  if (wc_slider(rs, 3, "Field of view", &s->fov, 50, 110, 1, str_format(&fa, "% deg", fmt_u32((u32)(s->fov + 0.5f)))))
    rs->changed = rs->custom = true;
  if (wc_slider(rs, 4, "Mouse sensitivity", &s->sensitivity, 0.2f, 3.0f, 0.05f,
                str_format(&fa, "%x", fmt_f32_p(s->sensitivity, 2))))
    rs->changed = rs->custom = true;
  if (wc_toggle(rs, 7, "View bobbing", &s->bobbing)) rs->changed = rs->custom = true;
  if (wc_slider(rs, 5, "Volume", &s->volume, 0, 1, 0.05f,
                str_format(&fa, "%%", fmt_u32((u32)(s->volume * 100 + 0.5f)), fmt_char('%'))))
    rs->changed = rs->custom = true;
  if (wc_slider(rs, 6, "Day length", &s->day_length, 2, 60, 1,
                str_format(&fa, "% min", fmt_u32((u32)(s->day_length + 0.5f)))))
    rs->changed = rs->custom = true;
  if (wc_slider(rs, 7, "Cloud coverage", &s->cloud_coverage, 0, 1, 0.05f,
                str_format(&fa, "%%", fmt_u32((u32)(s->cloud_coverage * 100 + 0.5f)), fmt_char('%'))))
    rs->changed = rs->custom = true;
  i32 wm = wc_cycle(rs, 6, "Weather", WC_WEATHER_NAMES, ARRAY_SIZE(WC_WEATHER_NAMES), s->weather);
  if (wm >= 0) {
    s->weather = (WcWeatherMode)wm;
    rs->changed = true;
  }
  wc_slider(rs, 8, "Time of day", &g->day_time, 0, 1, 0.005f, wc_clock_string(g->day_time, &fa));
}

hz_internal void wc_settings_screen(WcGame *g) {
  WcTui *t = &g->tui;
  WcSettings *s = &g->settings;
  b32 compact = t->rows < 30;
  i32 bh = compact ? 1 : 3;
  wc_scrim(t);
  WcRect p = wc_centred(t, MIN(60, (i32)t->cols), MIN((i32)t->rows, WC_SETTINGS_ROWS + 6 + bh));
  wc_panel(t, p);
  wc_tui_cstr(t, p.x + 2, p.y + 1, "Settings", WC_C_TEXT);
  i32 list_y = p.y + 3, list_n = MAX(1, p.h - 5 - bh);
  i32 max_first = MAX(0, WC_SETTINGS_ROWS - list_n);
  // the wheel over the list scrolls it; the arrows do the same for touch
  WcRect list = wc_rect(p.x + 1, list_y, p.w - 2, list_n);
  WcTuiHit lh = wc_tui_area(t, wc_tui_id("SettingsBody", 0), list);
  if (lh.hovered || wc_rect_has(list, t->px, t->py)) g->settings_scroll += (i32)(t->scroll * 3.0f);
  g->settings_scroll = MAX(0, MIN(g->settings_scroll, max_first));
  WcRows rs = {.g = g, .x = p.x + 2, .w = p.w - 4, .y0 = list_y, .first = g->settings_scroll, .n = list_n};
  wc_settings_rows(&rs);
  if (rs.row != WC_SETTINGS_ROWS) log_warn("settings: % rows, WC_SETTINGS_ROWS is wrong", fmt_u32((u32)rs.row));
  if (g->settings_scroll > 0) {
    WcRect up = wc_rect(p.x + p.w - 8, p.y + 1, 6, 1);
    WcTuiHit h = wc_tui_area(t, wc_tui_id("SettingsUp", 0), up);
    wc_tui_cstr(t, up.x, up.y, "^ more", h.hovered ? WC_C_HOVER : WC_C_DIM);
    if (h.clicked) g->settings_scroll = MAX(0, g->settings_scroll - list_n / 2);
  }
  if (g->settings_scroll < max_first) {
    WcRect dn = wc_rect(p.x + p.w - 8, list_y + list_n, 6, 1);
    WcTuiHit h = wc_tui_area(t, wc_tui_id("SettingsDown", 0), dn);
    wc_tui_cstr(t, dn.x, dn.y, "v more", h.hovered ? WC_C_HOVER : WC_C_DIM);
    if (h.clicked) g->settings_scroll = MIN(max_first, g->settings_scroll + list_n / 2);
  }
  if (wc_button(g, wc_tui_id("SettingsDoneBtn", 0), wc_rect(p.x + 2, p.y + p.h - 1 - bh, p.w - 4, bh), "Done", true)) {
    wc_game_apply_settings(g);
    wc_game_set_mode(g, g->settings_return == WC_MODE_PLAYING ? WC_MODE_PAUSED : g->settings_return);
  }
  if (rs.custom) s->preset = WC_PRESET_CUSTOM;
  if (rs.changed) wc_game_apply_settings(g);
}

// ---- inventory ----

hz_internal void wc_icon(WcTui *t, const WcIcon *ic, i32 x, i32 y) {
  for (i32 r = 0; r < WC_ICON_H; r++)
    for (i32 c = 0; c < WC_ICON_W; c++) {
      u32 i = (u32)(r * WC_ICON_W + c);
      if (!ic->glyph[i]) continue;
      wc_tui_fill(t, wc_rect(x + c, y + r, 1, 1), ic->back[i], 255);
      wc_tui_put(t, x + c, y + r, (char)(ic->glyph[i] + 32), ic->colour[i]);
    }
}

hz_internal void wc_inventory_screen(WcGame *g) {
  WcTui *t = &g->tui;
  // an item is its icon plus one cell of gap on the right and below
  i32 cols = MIN(18, ((i32)t->cols - 6) / (WC_ICON_W + 1));
  i32 rows = (WC_INVENTORY_COUNT + cols - 1) / cols;
  i32 grid_w = cols * (WC_ICON_W + 1) - 1, grid_h = rows * (WC_ICON_H + 1) - 1;
  WcRect p = wc_centred(t, MAX(grid_w + 4, 30), grid_h + 9);
  wc_scrim(t);
  wc_panel(t, p);
  wc_tui_cstr(t, p.x + 2, p.y + 1, "Blocks", WC_C_TEXT);
  String hint = g->touch_mode ? STR_FROM_CSTR("Tap a block to put it in the selected hotbar slot.")
                              : STR_FROM_CSTR("Click a block to put it in the selected slot. E or Esc closes.");
  hint.len = MIN(hint.len, (u32)MAX(0, p.w - 4));
  wc_tui_text(t, p.x + 2, p.y + 2, hint, WC_C_MUTED);
  i32 gx = p.x + (p.w - grid_w) / 2, gy = p.y + 4;
  i32 hovered = -1;
  for (i32 i = 0; i < WC_INVENTORY_COUNT; i++) {
    u8 id = wc_inventory_order[i];
    WcRect cell = wc_rect(gx + (i % cols) * (WC_ICON_W + 1), gy + (i / cols) * (WC_ICON_H + 1), WC_ICON_W, WC_ICON_H);
    WcTuiHit h = wc_tui_area(t, wc_tui_id("InvItem", (u32)i), cell);
    if (h.hovered) {
      hovered = i;
      g->cursor = OS_CURSOR_HAND;
      // brackets in the gap columns: the icon's own background fills the cell
      for (i32 r = 0; r < WC_ICON_H; r++) {
        wc_tui_put(t, cell.x - 1, cell.y + r, '[', WC_C_HOVER);
        wc_tui_put(t, cell.x + WC_ICON_W, cell.y + r, ']', WC_C_HOVER);
      }
    }
    if (h.clicked) {
      g->hotbar[g->selected] = id;
      wc_game_toast(g, wc_block_label[id]);
    }
    wc_icon(t, &g->icons[id], cell.x, cell.y);
  }
  i32 by = p.y + p.h - 2;
  if (g->touch_mode) {
    if (wc_button(g, wc_tui_id("InventoryDoneBtn", 0), wc_rect(p.x + 2, by, p.w - 4, 1), "Done", true)) wc_game_lock(g);
  } else if (hovered >= 0) {
    wc_tui_cstr(t, p.x + 2, by, wc_block_label[wc_inventory_order[hovered]], WC_C_TEXT);
  }
}

// ---- error ----

hz_internal void wc_error_screen(WcGame *g) {
  WcTui *t = &g->tui;
  String msg = cstr_to_str(g->error_text ? g->error_text : "");
  i32 w = MIN(80, (i32)t->cols);
  i32 rows = MIN(wc_tui_wrap_rows(msg, w - 4), (i32)t->rows - 6);
  WcRect p = wc_centred(t, w, rows + 5);
  wc_scrim(t);
  wc_panel(t, p);
  wc_tui_cstr(t, p.x + 2, p.y + 1, "Something went wrong", WC_C_TEXT);
  wc_tui_wrap(t, wc_rect(p.x + 2, p.y + 3, p.w - 4, rows), msg, WC_C_ERROR);
}

// ---- hud ----

// touch devices play in landscape: portrait gets this instead of the game's screens
hz_internal void wc_rotate_prompt(WcGame *g) {
  WcTui *t = &g->tui;
  wc_tui_fill(t, wc_rect(0, 0, (i32)t->cols, (i32)t->rows), 0, 200);
  WcRect p = wc_centred(t, MIN(32, (i32)t->cols), 5);
  wc_panel(t, p);
  wc_tui_center(t, p, p.y + 1, STR_FROM_CSTR("Rotate your phone"), WC_C_TEXT);
  wc_tui_center(t, p, p.y + 3, STR_FROM_CSTR("ASCIICRAFT plays in landscape."), WC_C_MUTED);
}

// rows the web page's corner link covers at the top right
hz_internal i32 wc_badge_rows(const WcTui *t) { return (i32)m_ceilf(WC_BADGE_CLEAR / m_maxf(t->ch, 1.0f)); }

// a one-row touch button "( JUMP )"; lit while held or toggled on
hz_internal void wc_touch_button(WcTui *t, u32 id, i32 x, i32 y, const char *label, b32 lit) {
  i32 w = (i32)cstr_len(label) + 4;
  WcRect r = wc_rect(x - w, y, w, 1);
  wc_tui_area(t, id, r);
  wc_tui_fill(t, r, lit ? 0x8A8F9Au : 0x0A0C10u, lit ? 220 : 150);
  u32 fg = lit ? 0x0A0C10u : WC_C_TEXT;
  wc_tui_put(t, r.x, y, '(', fg);
  wc_tui_put(t, r.x + w - 1, y, ')', fg);
  wc_tui_cstr(t, r.x + 2, y, label, fg);
}

// stick, jump, sneak and menu for a phone in landscape; wc_touch_update hit-tests these ids
hz_internal void wc_touch_hud(WcGame *g) {
  WcTui *t = &g->tui;
  const WcTouch *tc = &g->touch;
  b32 stick = wc_touch_stick_active(tc);
  // idle, a faint ring shows where to put the thumb
  f32 rest_x = t->ox + t->cw * 7.5f, rest_y = t->oy + t->ch * ((f32)t->rows - 5.5f);
  v2 center = stick ? tc->stick_center : (v2){rest_x, rest_y};
  v2 knob = stick ? tc->stick_knob : center;
  i32 cx, cy, kx, ky;
  if (wc_tui_cell_at(t, center.x, center.y, &cx, &cy)) {
    // the ring's radius is the stick's travel in cells; cells are taller than wide
    f32 rx = 56.0f / t->cw, ry = 56.0f / t->ch;
    u32 ring = stick ? 0xD0D4DCu : 0x8A8F9Au;
    // dark backed so the ring reads over a bright scene
    for (i32 a = 0; a < 32; a++) {
      f32 ang = (f32)a * (2.0f * PI / 32.0f);
      i32 x = cx + (i32)m_roundf(m_cosf(ang) * rx), y = cy + (i32)m_roundf(m_sinf(ang) * ry);
      wc_tui_fill(t, wc_rect(x, y, 1, 1), 0x0A0C10u, 170);
      wc_tui_put(t, x, y, 'o', ring);
    }
    if (wc_tui_cell_at(t, knob.x, knob.y, &kx, &ky)) {
      wc_tui_fill(t, wc_rect(kx, ky, 1, 1), 0x0A0C10u, 200);
      wc_tui_put(t, kx, ky, 'O', stick ? WC_C_TEXT : 0xB4B8C0u);
    }
  }
  i32 right = (i32)t->cols - 2, bottom = (i32)t->rows - 2;
  wc_touch_button(t, wc_tui_id("TouchJump", 0), right, bottom, "JUMP", tc->jump_held);
  b32 flying = g->player.flying;
  wc_touch_button(t, wc_tui_id("TouchSneak", 0), right, bottom - 3, flying ? "DOWN" : "SNEAK",
                  flying ? tc->sneak_held : tc->sneak_on);
  i32 my = wc_badge_rows(t);
  WcRect menu = wc_rect(right - 8, my, 8, 1);
  wc_tui_area(t, wc_tui_id("TouchPause", 0), menu);
  wc_tui_fill(t, menu, 0x0A0C10u, WC_A_HUD);
  wc_tui_cstr(t, menu.x, menu.y, "[ Menu ]", WC_C_TEXT);
}

// slots share their borders: .---.---. over |icon|icon| rows
hz_internal void wc_hotbar(WcGame *g) {
  WcTui *t = &g->tui;
  u32 slots = WC_HOTBAR_SLOTS + (g->touch_mode ? 1 : 0);
  i32 sw = WC_ICON_W + 1, w = (i32)slots * sw + 1, h = WC_ICON_H + 2;
  i32 x0 = ((i32)t->cols - w) / 2, y0 = (i32)t->rows - h;
  wc_tui_fill(t, wc_rect(x0, y0, w, h), 0x0A0C10u, WC_A_HUD);
  for (u32 i = 0; i < slots; i++) {
    i32 x = x0 + (i32)i * sw;
    WcRect slot = wc_rect(x, y0, sw + 1, h);
    b32 inv = i == WC_HOTBAR_SLOTS;
    wc_tui_area(t, inv ? wc_tui_id("TouchInventory", 0) : wc_tui_id("HotbarSlot", i), slot);
    if (inv) {
      wc_tui_box(t, slot, WC_C_DIM);
      wc_tui_cstr(t, x + 1, y0 + 1, "...", WC_C_TEXT);
      continue;
    }
    wc_icon(t, &g->icons[g->hotbar[i]], x + 1, y0 + 1);
  }
  // the selected slot's frame last, so it wins the shared borders
  for (u32 i = 0; i < slots; i++)
    if (i != g->selected) wc_tui_box(t, wc_rect(x0 + (i32)i * sw, y0, sw + 1, h), WC_C_DIM);
  wc_tui_box(t, wc_rect(x0 + (i32)g->selected * sw, y0, sw + 1, h), 0xFFFFFFu);
}

hz_internal void wc_hud(WcGame *g) {
  WcTui *t = &g->tui;
  wc_hotbar(g);
  // toast above the hotbar
  if (g->toast[0] && g->toast_t < 1.6f) {
    String s = cstr_to_str(g->toast);
    i32 y = (i32)t->rows - (WC_ICON_H + 2) - 2;
    i32 x = ((i32)t->cols - (i32)s.len) / 2;
    wc_tui_fill(t, wc_rect(x - 1, y, (i32)s.len + 2, 1), 0x0A0C10u, WC_A_HUD);
    wc_tui_text(t, x, y, s, WC_C_TEXT);
  }
  if (g->show_debug && g->debug_text) {
    String s = cstr_to_str(g->debug_text);
    i32 w = MIN(64, (i32)t->cols - 2);
    i32 rows = wc_tui_wrap_rows(s, w);
    wc_tui_fill(t, wc_rect(0, 0, w + 2, rows + 2), 0, WC_A_HUD);
    wc_tui_wrap(t, wc_rect(1, 1, w, rows), s, 0xE8E8E8u);
  }
  // the stick and buttons only mean something in play
  if (g->touch_mode) {
    if (g->mode == WC_MODE_PLAYING) wc_touch_hud(g);
    return;
  }
  const char *hint = "F3 debug - E inventory - Esc menu";
  wc_tui_cstr(t, (i32)t->cols - 1 - (i32)cstr_len(hint), wc_badge_rows(t), hint, 0xB4B4B4u);
}

// frame stats for recordings, top left; F3 swaps it for the debug text
hz_internal void wc_stats_panel(WcGame *g) {
  WcTui *t = &g->tui;
  Allocator fa = wc_frame_alloc();
  f32 fps = g->fps;
  f32 frame_ms = fps > 0 ? 1000.0f / fps : 0;
  u64 tris = (u64)g->renderer.stats.quads * 2;
  String tri_s = tris >= 1000000 ? str_format(&fa, "%M", fmt_f32_p((f32)tris / 1e6f, 1))
                                 : str_format(&fa, "%K", fmt_u32((u32)(tris / 1000)));
#ifdef WASM
  const char *backend = "WebGPU";
#else
  const char *backend = "Direct3D 12";
#endif
  String lines[6];
  u32 n = 0;
  lines[n++] = str_format(&fa, "frame % ms", fmt_f32_p(frame_ms, 2));
  if (g->gpu_ms > 0) lines[n++] = str_format(&fa, "GPU   % ms", fmt_f32_p(g->gpu_ms, 2));
  lines[n++] = str_format(&fa, "CPU   % ms", fmt_f32_p(g->main_ms, 2));
  lines[n++] = str_format(&fa, "% triangles - % draw calls", fmt_str(tri_s),
                          fmt_u32(g->renderer.stats.draw_calls + g->renderer.stats.shadow_draw_calls));
  lines[n++] = str_format(&fa, "% - % threads", fmt_cstr(backend), fmt_u32(g->world.lane_count));
  i32 w = 0;
  for (u32 i = 0; i < n; i++) w = MAX(w, (i32)lines[i].len);
  wc_tui_fill(t, wc_rect(0, 0, w + 4, (i32)n + 4), 0, WC_A_HUD);
  String f = str_format(&fa, "% FPS", fmt_u32((u32)(fps + 0.5f)));
  wc_tui_text(t, 2, 1, f, 0x78EB8Cu);
  for (u32 i = 0; i < n; i++) wc_tui_text(t, 2, 3 + (i32)i, lines[i], i < 3 ? WC_C_TEXT : 0xC8C8C8u);
}

void wc_ui_frame(WcGame *g) {
  g->cursor = OS_CURSOR_ARROW;
  b32 compact = g->tui.rows < 30;
  // a short grid gives every screen the whole height: the hotbar would draw over it
  if (g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR && g->mode != WC_MODE_SETTINGS && !g->hide_hud &&
      g->started && (!compact || g->mode == WC_MODE_PLAYING))
    wc_hud(g);
  if (g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR && g->started && !g->show_debug && !g->hide_hud)
    wc_stats_panel(g);
  if (wc_touch_portrait(g) && g->mode != WC_MODE_LOADING && g->mode != WC_MODE_ERROR) {
    wc_rotate_prompt(g);
    return;
  }
  switch (g->mode) {
  case WC_MODE_LOADING: wc_loading_screen(g); break;
  case WC_MODE_MENU:
  case WC_MODE_PAUSED: wc_menu_screen(g); break;
  case WC_MODE_SETTINGS: wc_settings_screen(g); break;
  case WC_MODE_INVENTORY: wc_inventory_screen(g); break;
  case WC_MODE_ERROR: wc_error_screen(g); break;
  case WC_MODE_PLAYING: break;
  }
}

// ---- scripted runs ----

// wc_ui_move_to:<name>[#<index>]: the pointer onto a widget, then touch_start/touch_end or key_down:Mouse Left click it
hz_internal void wc_cmd_ui_move_to(const HzCommandCtx *ctx, void *user) {
  WcGame *g = user;
  String a = cstr_to_str(ctx->args);
  u32 index = 0, name_len = a.len;
  for (u32 i = 0; i < a.len; i++) {
    if (a.value[i] != '#') continue;
    name_len = i;
    f64 v = 0;
    if (cstr_to_f64(a.value + i + 1, &v)) index = (u32)v;
    break;
  }
  Allocator fa = wc_frame_alloc();
  String head = {.value = a.value, .len = name_len};
  String name = str_format(&fa, "%", fmt_str(head));
  WcRect r;
  if (!wc_tui_find(&g->tui, wc_tui_id(name.value, index), &r)) {
    log_error("wc_ui_move_to: no widget '%'", fmt_cstr(ctx->args));
    return;
  }
  v2 c = wc_tui_rect_center(&g->tui, r);
  AppInputEvents *ev = ctx->events;
  ev->mouse_dx += c.x - ev->mouse_x;
  ev->mouse_dy += c.y - ev->mouse_y;
  ev->mouse_x = c.x;
  ev->mouse_y = c.y;
  // a touch event would switch the game to touch mode, so it moves the finger only when already there
  if (!g->touch_mode || ev->len >= GAME_INPUT_EVENTS_MAX_COUNT) return;
  AppInputEvent *e = &ev->events[ev->len++];
  e->type = INPUT_EVENT_TOUCH_MOVE;
  e->touch.id = 1;
  e->touch.x = c.x;
  e->touch.y = c.y;
}

void wc_ui_register_commands(WcGame *g) { hz_command_register("wc_ui_move_to", wc_cmd_ui_move_to, g); }

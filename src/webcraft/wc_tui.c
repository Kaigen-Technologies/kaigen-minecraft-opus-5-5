#include "webcraft/wc_tui.h"
#include "lib/hash.h"

void wc_tui_init(WcTui *t, Allocator *alloc) {
  memzero_struct(t);
  t->alloc = *alloc;
  t->rects = ALLOC_ARRAY(alloc, WcTuiRect, WC_TUI_MAX_RECTS);
  t->prev = ALLOC_ARRAY(alloc, WcTuiRect, WC_TUI_MAX_RECTS);
  t->px = t->py = -1;
}

void wc_tui_begin(WcTui *t, u32 cols, u32 rows, f32 ox, f32 oy, f32 cw, f32 ch, const InputSystem *in, b32 pointer) {
  if (cols * rows > t->cap) {
    // grows only: the grid is bounded by the output size and the arena never frees
    t->cap = cols * rows;
    t->cells = ALLOC_ARRAY_NO_ZERO(&t->alloc, u8, t->cap * WC_TUI_CELL_BYTES);
  }
  t->cols = cols;
  t->rows = rows;
  t->ox = ox;
  t->oy = oy;
  t->cw = cw;
  t->ch = ch;
  mem_zero(t->cells, cols * rows * WC_TUI_CELL_BYTES);
  // last frame's layout is what the pointer saw
  WcTuiRect *swap = t->prev;
  t->prev = t->rects;
  t->rects = swap;
  t->prev_count = t->rect_count;
  t->rect_count = 0;
  t->hot = 0;
  t->px = t->py = -1;
  if (pointer) wc_tui_cell_at(t, in->mouse_pos.x, in->mouse_pos.y, &t->px, &t->py);
  if (t->px >= 0) {
    for (u32 i = t->prev_count; i-- > 0;)
      if (wc_rect_has(t->prev[i].r, t->px, t->py)) {
        t->hot = t->prev[i].id;
        break;
      }
  }
  const AppButton *b = &in->buttons[MOUSE_LEFT];
  t->down = pointer && b->is_pressed;
  t->pressed = pointer && b->pressed_this_frame;
  t->released = pointer && b->released_this_frame;
  t->scroll = pointer ? -in->scroll_delta.y : 0;
  if (t->pressed) t->active = t->hot;
}

u32 wc_tui_id(const char *name, u32 index) { return (fnv1a_hash(name) ^ (index * 0x9e3779b1u)) | 1u; }

force_inline u8 *wc_tui_cell(WcTui *t, i32 x, i32 y) {
  if (x < 0 || y < 0 || x >= (i32)t->cols || y >= (i32)t->rows) return NULL;
  return t->cells + ((u32)y * t->cols + (u32)x) * WC_TUI_CELL_BYTES;
}

void wc_tui_fill(WcTui *t, WcRect r, u32 bg, u8 alpha) {
  for (i32 y = r.y; y < r.y + r.h; y++) {
    for (i32 x = r.x; x < r.x + r.w; x++) {
      u8 *c = wc_tui_cell(t, x, y);
      if (!c) continue;
      c[3] = 0;
      c[4] = (u8)(bg >> 16);
      c[5] = (u8)(bg >> 8);
      c[6] = (u8)bg;
      c[7] = alpha;
    }
  }
}

void wc_tui_put(WcTui *t, i32 x, i32 y, char ch, u32 fg) {
  u8 *c = wc_tui_cell(t, x, y);
  if (!c) return;
  u32 g = (u8)ch >= 32 && (u8)ch < 127 ? (u32)(u8)ch - 32 : (u32)('?' - 32);
  c[0] = (u8)(fg >> 16);
  c[1] = (u8)(fg >> 8);
  c[2] = (u8)fg;
  c[3] = (u8)g;
}

i32 wc_tui_text(WcTui *t, i32 x, i32 y, String s, u32 fg) {
  for (u32 i = 0; i < s.len; i++) wc_tui_put(t, x + (i32)i, y, s.value[i], fg);
  return (i32)s.len;
}

i32 wc_tui_cstr(WcTui *t, i32 x, i32 y, const char *s, u32 fg) { return wc_tui_text(t, x, y, cstr_to_str(s), fg); }

void wc_tui_center(WcTui *t, WcRect r, i32 y, String s, u32 fg) {
  wc_tui_text(t, r.x + (r.w - (i32)s.len) / 2, y, s, fg);
}

void wc_tui_box(WcTui *t, WcRect r, u32 fg) {
  if (r.w < 2 || r.h < 2) return;
  for (i32 x = r.x + 1; x < r.x + r.w - 1; x++) {
    wc_tui_put(t, x, r.y, '-', fg);
    wc_tui_put(t, x, r.y + r.h - 1, '-', fg);
  }
  for (i32 y = r.y + 1; y < r.y + r.h - 1; y++) {
    wc_tui_put(t, r.x, y, '|', fg);
    wc_tui_put(t, r.x + r.w - 1, y, '|', fg);
  }
  wc_tui_put(t, r.x, r.y, '.', fg);
  wc_tui_put(t, r.x + r.w - 1, r.y, '.', fg);
  wc_tui_put(t, r.x, r.y + r.h - 1, '\'', fg);
  wc_tui_put(t, r.x + r.w - 1, r.y + r.h - 1, '\'', fg);
}

// the next line of s from *pos that fits w columns, breaking at spaces (or mid-word when one word is too long)
hz_internal String wc_tui_next_line(String s, u32 *pos, i32 w) {
  u32 i = *pos;
  while (i < s.len && s.value[i] == ' ') i++;
  u32 start = i, end = i, last_space = 0;
  b32 has_space = false;
  while (i < s.len && s.value[i] != '\n' && (i32)(i - start) < w) {
    if (s.value[i] == ' ') {
      last_space = i;
      has_space = true;
    }
    i++;
  }
  end = i;
  if (i < s.len && s.value[i] != '\n' && s.value[i] != ' ' && has_space) end = last_space;
  *pos = end < s.len && s.value[end] == '\n' ? end + 1 : end;
  return (String){.value = s.value + start, .len = end - start};
}

i32 wc_tui_wrap_rows(String s, i32 w) {
  if (w <= 0) return 0;
  u32 pos = 0;
  i32 n = 0;
  while (pos < s.len) {
    wc_tui_next_line(s, &pos, w);
    n++;
  }
  return n;
}

i32 wc_tui_wrap(WcTui *t, WcRect r, String s, u32 fg) {
  if (r.w <= 0) return 0;
  u32 pos = 0;
  i32 y = 0;
  while (pos < s.len && y < r.h) {
    String line = wc_tui_next_line(s, &pos, r.w);
    wc_tui_text(t, r.x, r.y + y, line, fg);
    y++;
  }
  return y;
}

WcTuiHit wc_tui_area(WcTui *t, u32 id, WcRect r) {
  if (t->rect_count < WC_TUI_MAX_RECTS) t->rects[t->rect_count++] = (WcTuiRect){.id = id, .r = r};
  WcTuiHit h = {0};
  h.hovered = t->hot == id && wc_rect_has(r, t->px, t->py);
  h.held = t->down && t->active == id;
  h.clicked = t->released && t->active == id && h.hovered;
  return h;
}

b32 wc_tui_find(const WcTui *t, u32 id, WcRect *out) {
  for (u32 i = 0; i < t->prev_count; i++) {
    if (t->prev[i].id == id) {
      *out = t->prev[i].r;
      return true;
    }
  }
  return false;
}

b32 wc_tui_cell_at(const WcTui *t, f32 x, f32 y, i32 *cx, i32 *cy) {
  if (t->cw <= 0 || t->ch <= 0) return false;
  f32 fx = m_floorf((x - t->ox) / t->cw), fy = m_floorf((y - t->oy) / t->ch);
  if (fx < 0 || fy < 0 || fx >= (f32)t->cols || fy >= (f32)t->rows) return false;
  *cx = (i32)fx;
  *cy = (i32)fy;
  return true;
}

v2 wc_tui_rect_center(const WcTui *t, WcRect r) {
  return (v2){t->ox + ((f32)r.x + (f32)r.w * 0.5f) * t->cw, t->oy + ((f32)r.y + (f32)r.h * 0.5f) * t->ch};
}

#ifndef WC_TUI_H
#define WC_TUI_H

#include "lib/typedefs.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "input.h"

// text-mode ui: immediate-mode widgets drawn into a grid of character cells, hit-tested in cells

// a cell is 8 bytes: fg r, g, b, glyph (0..94, code - 32), then bg r, g, b, bg opacity
#define WC_TUI_CELL_BYTES 8
#define WC_TUI_MAX_RECTS 512

typedef struct {
  i32 x, y, w, h;
} WcRect;

typedef struct {
  u32 id;
  WcRect r;
} WcTuiRect;

typedef struct {
  b32 hovered; // the pointer is over it
  b32 held;    // the press began on it and the button is still down
  b32 clicked; // pressed and released on it
} WcTuiHit;

typedef struct {
  u32 cols, rows, cap;
  u8 *cells;
  WcTuiRect *rects, *prev; // widgets laid out this frame and last frame
  u32 rect_count, prev_count;
  f32 ox, oy, cw, ch; // logical px of cell (0, 0) and of one cell
  i32 px, py;         // pointer cell, -1 when there is no pointer
  b32 down, pressed, released;
  f32 scroll;    // wheel notches this frame, down positive
  u32 active;    // the widget the current press began on
  u32 hot;       // the widget under the pointer last frame
  Allocator alloc;
} WcTui;

void wc_tui_init(WcTui *t, Allocator *alloc);
// starts a frame on a cols x rows grid placed at (ox, oy) logical px with cells cw x ch; no pointer when !pointer
void wc_tui_begin(WcTui *t, u32 cols, u32 rows, f32 ox, f32 oy, f32 cw, f32 ch, const InputSystem *in, b32 pointer);
u32 wc_tui_id(const char *name, u32 index);

// drawing; everything clips to the grid
void wc_tui_fill(WcTui *t, WcRect r, u32 bg, u8 alpha);
void wc_tui_put(WcTui *t, i32 x, i32 y, char c, u32 fg);
i32 wc_tui_text(WcTui *t, i32 x, i32 y, String s, u32 fg);
i32 wc_tui_cstr(WcTui *t, i32 x, i32 y, const char *s, u32 fg);
void wc_tui_center(WcTui *t, WcRect r, i32 y, String s, u32 fg);
// .---. corners, | and - sides
void wc_tui_box(WcTui *t, WcRect r, u32 fg);
// words wrapped into r; returns the rows used
i32 wc_tui_wrap(WcTui *t, WcRect r, String s, u32 fg);
i32 wc_tui_wrap_rows(String s, i32 w);

// interaction: registers r under id for this frame
WcTuiHit wc_tui_area(WcTui *t, u32 id, WcRect r);
// last frame's rect of a widget
b32 wc_tui_find(const WcTui *t, u32 id, WcRect *out);
// the cell under a logical px point
b32 wc_tui_cell_at(const WcTui *t, f32 x, f32 y, i32 *cx, i32 *cy);
// logical px centre of a cell rect
v2 wc_tui_rect_center(const WcTui *t, WcRect r);

force_inline b32 wc_rect_has(WcRect r, i32 x, i32 y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

#endif

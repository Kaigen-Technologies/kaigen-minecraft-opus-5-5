#ifndef WC_ASCII_H
#define WC_ASCII_H

#include "webcraft/wc.h"
#include "webcraft/wc_font.h"

// ---- character grid ----

#define WC_CELL_W WC_FONT_W
#define WC_CELL_H WC_FONT_H_PX
#define WC_ASCII_MIN_COLS 96 // phones stay at least this wide
#define WC_UI_CAP_CSS 12.0f  // ui capitals are at least this tall in css px

// the scene grid and the ui grid laid over it; every font pixel is k (scene) or k * m (ui) device px
typedef struct {
  u32 k;
  u32 cols, rows;
  u32 ox, oy; // device px of the grid's top-left, centred in the output
  u32 m;
  u32 ucols, urows;
} WcAsciiGrid;

// text sizes the settings offer, in css px per font pixel
#define WC_TEXT_SIZES 3
f32 wc_ascii_text_css(u32 size);
// background tints the settings offer, as a fraction of the cell colour
#define WC_TINTS 3
f32 wc_ascii_tint(u32 tint);
#define WC_PALETTES 3
WcAsciiGrid wc_ascii_grid(u32 out_w, u32 out_h, f32 dpr, f32 css_per_px);

// ---- shape matching (Harri's sampling circles, integer coverage weights) ----

#define WC_ASCII_INNER 6
#define WC_ASCII_OUTER 10
#define WC_ASCII_SITES (WC_ASCII_INNER + WC_ASCII_OUTER)
#define WC_ASCII_MAX_ENTRIES 512
#define WC_ASCII_MAX_GLYPHS 96
#define WC_ASCII_LAYERS 96
#define WC_RAMP_STEPS WC_FONT_GLYPHS
#define WC_ASCII_SUPERSAMPLE 8 // a texel's weight is its subsamples inside the circle (0..64)

// mirrors _CellTables in shaders/wc_ascii_cells.glsl
typedef struct {
  uv4 site_range[WC_ASCII_SITES / 4];    // site s: first entry | count << 16
  uv4 entries[WC_ASCII_MAX_ENTRIES / 4]; // dx + 16 | (dy + 16) << 6 | weight << 12, from the cell's top-left texel
  uv4 glyph_a[WC_ASCII_MAX_GLYPHS];      // shape vector of vocabulary slot g, sites 0..3 (0..65535)
  uv4 glyph_b[WC_ASCII_MAX_GLYPHS];      // sites 4, 5, glyph index, ink coverage (lit px of 54)
  uv4 vocab;                             // x edge glyph count (slots from 0), y first cloud slot, z cloud count
  uv4 layer_info[WC_ASCII_LAYERS];       // wc_ascii_layer_info
  uv4 ramp[24];                          // fill ramp position p (4i + component) -> glyph index, lightest first
  uv4 rank[24];                          // glyph index g -> its ramp position
  uv4 ramp_lut[64];                      // mean display luma (0..255) -> ramp position
} WcAsciiTables;
static_assert(T_COUNT <= WC_ASCII_LAYERS, "layer_info holds every texture layer");

// glyphs the edge matcher may pick: outline strokes only; earlier wins ties
#define WC_ASCII_VOCAB " _-|/\\.,'`"
// glyphs the cloud matcher may pick, roughly by ink
#define WC_ASCII_CLOUD " .,'`-:;~=+*o%#@"
#define WC_ASCII_ALL                                                                                                   \
  " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

// circle weights, then raw-coverage shape vectors of vocab (slots from 0) and vocab2 (after it)
void wc_ascii_build_tables(WcAsciiTables *t, const char *vocab, const char *vocab2);
// fill ramp of vocab by ink coverage; luma l maps to position round(clamp((l - lo) / (hi - lo))^curve * (n - 1))
void wc_ascii_build_ramp(WcAsciiTables *t, const char *vocab, u32 lo, u32 hi, f32 curve);
#define WC_RAMP_NONE 255u // rank of a glyph outside the fill ramp
#define WC_ASCII_FILL " .:-=+*#%@" // a short undirected density ramp: a lit face stays one glyph, shading steps it
#define WC_ASCII_CLASSIC_RAMP /* Paul Bourke's 70 steps, densest first; breaks coverage ties */ "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. "
// font pixel (x, y) of glyph g's 6x9 cell
force_inline b32 wc_font_bit(u32 g, u32 x, u32 y) { return (wc_font_rows[g * WC_FONT_H_PX + y] >> x) & 1u; }

// per texture layer: z flags (x, y unused)
#define WC_LAYER_PLANT 1u
#define WC_LAYER_EMISSIVE 2u
#define WC_LAYER_FOLIAGE 4u
void wc_ascii_layer_info(uv4 *info);

// R8 atlas: glyph g occupies columns [6g, 6g + 6), 9 rows; 255 where ink
void wc_ascii_atlas_pixels(u8 *out);
#define WC_ATLAS_W (WC_FONT_GLYPHS * WC_FONT_W)

#endif

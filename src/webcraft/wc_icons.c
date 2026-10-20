#include "webcraft/wc_game.h"

// text-mode block icons: the top face's row of cells over the side face's, each cell the fill-ramp glyph of its patch

#define WC_ICON_INK_GAIN 2.5f // matches the scene's WC_INK_GAIN
#define WC_ICON_BACK 0.38f    // matches the default (Full) background tint

hz_internal v3 wc_icon_tint(u8 id) {
  switch (wc_tint[id]) {
  case WC_TINT_GRASS: return (v3){0.52f, 0.76f, 0.36f};
  case WC_TINT_FOLIAGE: return (v3){0.4f, 0.64f, 0.22f};
  case WC_TINT_WATER: return (v3){0.25f, 0.45f, 0.9f};
  case WC_TINT_BIRCH: return (v3){0.5f, 0.65f, 0.33f};
  case WC_TINT_SPRUCE: return (v3){0.38f, 0.6f, 0.38f};
  default: return (v3){1, 1, 1};
  }
}

// one cell over texels [x0, x1) x [y0, y1) of a layer: mean colour of the opaque texels, glyph by its luma
hz_internal void wc_icon_cell(const WcTextureData *tex, const WcAsciiTables *t, u32 layer, u32 x0, u32 x1, u32 y0,
                              u32 y1, b32 tinted, v3 tint, f32 shade, u8 *glyph, u32 *colour, u32 *back) {
  const u8 *px = tex->pixels + layer * WC_TEX_SIZE * WC_TEX_SIZE * 4;
  b32 cut = tex->cutout[layer];
  f32 r = 0, g = 0, b = 0;
  u32 n = 0, total = 0;
  for (u32 y = y0; y < y1; y++) {
    for (u32 x = x0; x < x1; x++) {
      const u8 *c = px + (y * WC_TEX_SIZE + x) * 4;
      total++;
      if (cut && c[3] < 128) continue;
      // opaque textures carry the tint mask in alpha
      f32 m = tinted ? (cut ? 1.0f : (f32)c[3] / 255.0f) : 0.0f;
      r += c[0] * (1 - m + m * tint.x);
      g += c[1] * (1 - m + m * tint.y);
      b += c[2] * (1 - m + m * tint.z);
      n++;
    }
  }
  // a mostly see-through patch (a flower's edge) stays empty
  if (n * 3 < total) {
    *glyph = 0;
    *colour = 0;
    *back = 0;
    return;
  }
  r = r * shade / (f32)n;
  g = g * shade / (f32)n;
  b = b * shade / (f32)n;
  u32 l = ((u32)(54.0f * r + 183.0f * g + 19.0f * b) + 128u) >> 8;
  if (l > 255) l = 255;
  const u32 *ramp = (const u32 *)t->ramp, *lut = (const u32 *)t->ramp_lut;
  // icons always show ink: a dark block takes the lightest visible glyph rather than a space
  u32 p = lut[l] < 1 ? 1 : lut[l];
  *glyph = (u8)ramp[p];
  // as scene cells: ink is the colour x WC_ICON_INK_GAIN short of clipping, the background WC_ICON_BACK of it
  f32 mx = m_maxf(m_maxf(r, g), m_maxf(b, 1.0f));
  f32 k = m_minf(WC_ICON_INK_GAIN, 255.0f / mx);
  *colour = ((u32)(r * k) << 16) | ((u32)(g * k) << 8) | (u32)(b * k);
  *back = ((u32)(r * WC_ICON_BACK) << 16) | ((u32)(g * WC_ICON_BACK) << 8) | (u32)(b * WC_ICON_BACK);
}

void wc_icons_build(WcIcon *out, const WcTextureData *tex, const WcAsciiTables *t) {
  for (u32 id = 1; id < B_COUNT; id++) {
    WcIcon *ic = &out[id];
    v3 tint = wc_icon_tint((u8)id);
    b32 has_tint = wc_tint[id] != WC_TINT_NONE;
    b32 flat = wc_shape[id] == WC_SHAPE_CROSS || wc_shape[id] == WC_SHAPE_TORCH;
    u8 tt = wc_tint[id];
    b32 top_tint = id == B_GRASS || tt == WC_TINT_FOLIAGE || tt == WC_TINT_WATER || tt == WC_TINT_BIRCH ||
                   tt == WC_TINT_SPRUCE;
    for (u32 row = 0; row < WC_ICON_H; row++) {
      for (u32 col = 0; col < WC_ICON_W; col++) {
        u32 x0 = col * WC_TEX_SIZE / WC_ICON_W, x1 = (col + 1) * WC_TEX_SIZE / WC_ICON_W;
        u32 i = row * WC_ICON_W + col;
        if (flat) {
          // a plant is one sprite: the top row is its upper half, the bottom row its lower half
          u32 y0 = row * WC_TEX_SIZE / 2, y1 = (row + 1) * WC_TEX_SIZE / 2;
          wc_icon_cell(tex, t, wc_face_tex[id][0], x0, x1, y0, y1, has_tint, tint, 1.0f, &ic->glyph[i],
                       &ic->colour[i], &ic->back[i]);
        } else if (row == 0) {
          wc_icon_cell(tex, t, wc_face_tex[id][WC_FACE_PY], x0, x1, 0, WC_TEX_SIZE, top_tint, tint, 1.0f,
                       &ic->glyph[i], &ic->colour[i], &ic->back[i]);
        } else {
          wc_icon_cell(tex, t, wc_face_tex[id][WC_FACE_PZ], x0, x1, 0, WC_TEX_SIZE, has_tint, tint, 0.72f,
                       &ic->glyph[i], &ic->colour[i], &ic->back[i]);
        }
      }
    }
  }
}

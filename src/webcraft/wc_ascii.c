#include "webcraft/wc_ascii.h"
#include "webcraft/wc_render.h"

// ---- grid ----

f32 wc_ascii_text_css(u32 size) { return size == 0 ? 2.5f : size == 1 ? 2.0f : 1.5f; }

f32 wc_ascii_tint(u32 tint) { return tint == 0 ? 0.0f : tint == 1 ? 0.25f : 0.38f; }

WcAsciiGrid wc_ascii_grid(u32 out_w, u32 out_h, f32 dpr, f32 css_per_px) {
  WcAsciiGrid g = {0};
  u32 k = (u32)m_maxf(1.0f, m_roundf(css_per_px * dpr));
  u32 kmax = out_w / (WC_CELL_W * WC_ASCII_MIN_COLS);
  if (kmax < 1) kmax = 1;
  if (k > kmax) k = kmax;
  g.k = k;
  g.cols = out_w / (WC_CELL_W * k);
  g.rows = out_h / (WC_CELL_H * k);
  if (g.cols < 1) g.cols = 1;
  if (g.rows < 1) g.rows = 1;
  u32 gw = g.cols * WC_CELL_W * k, gh = g.rows * WC_CELL_H * k;
  g.ox = out_w > gw ? (out_w - gw) / 2 : 0;
  g.oy = out_h > gh ? (out_h - gh) / 2 : 0;
  // capitals are 7 font px tall
  f32 cap_css = 7.0f * (f32)k / m_maxf(dpr, 0.01f);
  u32 m = (u32)m_ceilf(WC_UI_CAP_CSS / m_maxf(cap_css, 0.01f));
  g.m = m < 1 ? 1 : m;
  g.ucols = g.cols / g.m;
  g.urows = g.rows / g.m;
  return g;
}

// ---- sampling circles (Harri's default config), in exact integer geometry so tools/ascii/reference.py agrees ----

// site centre in hundredths of the cell's width and height
hz_internal iv2 wc_ascii_site(u32 s) {
  switch (s) {
  case 0: return (iv2){30, 23};
  case 1: return (iv2){70, 18};
  case 2: return (iv2){30, 50};
  case 3: return (iv2){70, 50};
  case 4: return (iv2){30, 82};
  case 5: return (iv2){70, 77};
  case 6: return (iv2){7, -21};
  case 7: return (iv2){93, -21};
  case 8: return (iv2){-25, 7};
  case 9: return (iv2){125, 7};
  case 10: return (iv2){-25, 50};
  case 11: return (iv2){125, 50};
  case 12: return (iv2){-25, 93};
  case 13: return (iv2){125, 93};
  case 14: return (iv2){7, 121};
  default: return (iv2){93, 121};
  }
}

// lengths in 1/3200 font px: Harri's radius (13.5 of a 64 px cell height) is 6075 exactly
#define WC_ASCII_UNIT 3200
#define WC_ASCII_RADIUS_U 6075

// shape vectors of vocab into slots from first (inner-circle ink coverage); returns the glyph count
hz_internal u32 wc_ascii_add_vocab(WcAsciiTables *t, u32 first, const char *vocab, const u32 *cell_w, const u32 *total_w) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 count = 0;
  u32 *raw = ALLOC_ARRAY(&tmp.allocator, u32, WC_ASCII_MAX_GLYPHS * WC_ASCII_INNER);
  for (const char *p = vocab; *p; p++) {
    u32 g = (u32)(u8)*p - 32;
    assert_msg(g < WC_FONT_GLYPHS && first + count < WC_ASCII_MAX_GLYPHS, "ascii vocabulary holds printable ascii only");
    u32 lit = 0;
    for (u32 y = 0; y < WC_CELL_H; y++)
      for (u32 x = 0; x < WC_CELL_W; x++) lit += wc_font_bit(g, x, y);
    for (u32 s = 0; s < WC_ASCII_INNER; s++) {
      u32 sum = 0;
      for (u32 y = 0; y < WC_CELL_H; y++)
        for (u32 x = 0; x < WC_CELL_W; x++)
          if (wc_font_bit(g, x, y)) sum += cell_w[(s * WC_CELL_H + y) * WC_CELL_W + x];
      u32 v = (u32)(((u64)sum * 65535u) / total_w[s]);
      raw[count * WC_ASCII_INNER + s] = v;
    }
    t->glyph_b[first + count].z = g;
    t->glyph_b[first + count].w = lit;
    count++;
  }
  for (u32 i = 0; i < count; i++) {
    const u32 *v = raw + i * WC_ASCII_INNER;
    t->glyph_a[first + i] = (uv4){v[0], v[1], v[2], v[3]};
    t->glyph_b[first + i].x = v[4];
    t->glyph_b[first + i].y = v[5];
  }
  tctx_temp_allocator_end(tmp);
  return count;
}

void wc_ascii_build_tables(WcAsciiTables *t, const char *vocab, const char *vocab2) {
  mem_zero(t, sizeof(*t));
  u32 *entries = (u32 *)t->entries;
  u32 *ranges = (u32 *)t->site_range;
  u32 n = 0;
  const i64 ss = WC_ASCII_SUPERSAMPLE, U = WC_ASCII_UNIT, R = WC_ASCII_RADIUS_U;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  // per inner site: weight of every cell texel, and the site's total weight
  u32 *cell_w = ALLOC_ARRAY(&tmp.allocator, u32, WC_ASCII_INNER * WC_CELL_W * WC_CELL_H);
  u32 *total_w = ALLOC_ARRAY(&tmp.allocator, u32, WC_ASCII_INNER);
  for (u32 s = 0; s < WC_ASCII_SITES; s++) {
    iv2 c = wc_ascii_site(s);
    i64 cx = (i64)c.x * WC_CELL_W * U / 100, cy = (i64)c.y * WC_CELL_H * U / 100;
    i32 x0 = (i32)((cx - R) / U) - 2, x1 = (i32)((cx + R) / U) + 2;
    i32 y0 = (i32)((cy - R) / U) - 2, y1 = (i32)((cy + R) / U) + 2;
    u32 first = n;
    for (i32 y = y0; y <= y1; y++) {
      for (i32 x = x0; x <= x1; x++) {
        u32 w = 0;
        for (i64 j = 0; j < ss; j++) {
          for (i64 i = 0; i < ss; i++) {
            i64 dx = (x * ss * 2 + i * 2 + 1) * U / (ss * 2) - cx, dy = (y * ss * 2 + j * 2 + 1) * U / (ss * 2) - cy;
            if (dx * dx + dy * dy <= R * R) w++;
          }
        }
        if (!w) continue;
        assert_msg(n < WC_ASCII_MAX_ENTRIES, "ascii sampling table overflow");
        assert_msg(x >= -16 && x < 48 && y >= -16 && y < 48, "ascii sampling offset out of range");
        entries[n++] = (u32)(x + 16) | ((u32)(y + 16) << 6) | (w << 12);
        if (s < WC_ASCII_INNER) {
          total_w[s] += w;
          if (x >= 0 && x < WC_CELL_W && y >= 0 && y < WC_CELL_H) cell_w[(s * WC_CELL_H + y) * WC_CELL_W + x] = w;
        }
      }
    }
    ranges[s] = first | ((n - first) << 16);
  }
  t->vocab.x = wc_ascii_add_vocab(t, 0, vocab, cell_w, total_w);
  t->vocab.y = t->vocab.x;
  t->vocab.z = wc_ascii_add_vocab(t, t->vocab.y, vocab2, cell_w, total_w);
  tctx_temp_allocator_end(tmp);
}

// sort key: ink coverage, then lighter on the classic ramp first, then code; unique per glyph
hz_internal u32 wc_ramp_key(u32 g) {
  u32 lit = 0;
  for (u32 y = 0; y < WC_CELL_H; y++)
    for (u32 x = 0; x < WC_CELL_W; x++) lit += wc_font_bit(g, x, y);
  const char *classic = WC_ASCII_CLASSIC_RAMP;
  u32 n = (u32)sizeof(WC_ASCII_CLASSIC_RAMP) - 1, light = 0;
  for (u32 i = 0; i < n; i++)
    if ((u32)(u8)classic[i] - 32 == g) light = n - i;
  return (lit << 16) | ((128 - light) << 8) | g;
}

void wc_ascii_build_ramp(WcAsciiTables *t, const char *vocab, u32 lo, u32 hi, f32 curve) {
  u32 *ramp = (u32 *)t->ramp, *rank = (u32 *)t->rank, *lut = (u32 *)t->ramp_lut;
  u32 count = 0;
  for (const char *p = vocab; *p; p++) count++;
  assert_msg(count >= 2 && count <= WC_RAMP_STEPS, "fill ramp holds 2..95 glyphs");
  for (u32 g = 0; g < WC_RAMP_STEPS; g++) rank[g] = WC_RAMP_NONE;
  for (const char *p = vocab; *p; p++) {
    u32 g = (u32)(u8)*p - 32, pos = 0, kg = wc_ramp_key(g);
    for (const char *o = vocab; *o; o++) pos += wc_ramp_key((u32)(u8)*o - 32) < kg ? 1u : 0u;
    ramp[pos] = g;
    rank[g] = pos;
  }
  for (u32 l = 0; l < 256; l++) {
    f32 ink = m_powf(m_clampf(((f32)l - (f32)lo) / (f32)(hi - lo), 0.0f, 1.0f), curve);
    lut[l] = (u32)(ink * (f32)(count - 1) + 0.5f);
  }
}

// ---- texture layers ----

void wc_ascii_layer_info(uv4 *info) {
  for (u32 layer = 0; layer < T_COUNT; layer++) {
    uv4 v = {0};
    switch (layer) {
    case T_TALL_GRASS: case T_FERN: case T_DANDELION: case T_POPPY: case T_CORNFLOWER: case T_DAISY: case T_DEAD_BUSH:
    case T_SUGAR_CANE: case T_RED_MUSHROOM: case T_BROWN_MUSHROOM: v.z |= WC_LAYER_PLANT; break;
    case T_OAK_LEAVES: case T_BIRCH_LEAVES: case T_SPRUCE_LEAVES: v.z |= WC_LAYER_FOLIAGE; break;
    case T_GLOWSTONE: case T_SEA_LANTERN: case T_TORCH: case T_LAVA: case T_JACK_O_LANTERN: v.z |= WC_LAYER_EMISSIVE; break;
    default: break;
    }
    info[layer] = v;
  }
}

void wc_ascii_atlas_pixels(u8 *out) {
  for (u32 g = 0; g < WC_FONT_GLYPHS; g++)
    for (u32 y = 0; y < WC_FONT_H_PX; y++)
      for (u32 x = 0; x < WC_FONT_W; x++) out[y * WC_ATLAS_W + g * WC_FONT_W + x] = wc_font_bit(g, x, y) ? 255 : 0;
}

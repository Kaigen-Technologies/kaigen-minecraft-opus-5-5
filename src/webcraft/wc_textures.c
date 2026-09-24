#include "webcraft/wc_render.h"

// procedural 16x16 block textures: albedo, normal+height, specular (smooth, metal, sss, emit)

#define WC_TN 16
#define WC_TNN 256

typedef struct {
  f64 r, g, b;
} WcRgb;

typedef struct {
  f64 *r, *g, *b, *a, *h, *smooth, *metal, *sss, *emit;
  f64 bump;
  b32 cutout;
  u32 seed;
  Allocator *ta;
} WcTex;

typedef struct {
  f64 *d1, *d2;
  i32 *id;
} WcVor;

typedef struct {
  WcRgb c;
  f64 p;
} WcSpeck;

force_inline WcRgb wc_rgb(f64 r, f64 g, f64 b) { return (WcRgb){r, g, b}; }
force_inline WcRgb wc_rgb_scale(WcRgb c, f64 f) { return (WcRgb){c.r * f, c.g * f, c.b * f}; }
force_inline WcRgb wc_rgb_mix(WcRgb a, WcRgb b, f64 t) {
  return (WcRgb){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
force_inline f64 wc_clamp01(f64 v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
force_inline i32 wc_twrap(i32 v) { return ((v % WC_TN) + WC_TN) % WC_TN; }
force_inline i32 wc_tidx(i32 x, i32 y) { return wc_twrap(y) * WC_TN + wc_twrap(x); }
// Math.round for the non-negative values the generator produces
force_inline i32 wc_tround(f64 v) { return (i32)m_floor(v + 0.5); }
force_inline i32 wc_imod(i32 a, i32 m) { return ((a % m) + m) % m; }

hz_internal void wc_tex_init(WcTex *t, const char *name, Allocator *ta) {
  t->r = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->g = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->b = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->a = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->h = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->smooth = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->metal = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->sss = ALLOC_ARRAY(ta, f64, WC_TNN);
  t->emit = ALLOC_ARRAY(ta, f64, WC_TNN);
  for (u32 i = 0; i < WC_TNN; i++) {
    t->a[i] = 255;
    t->h[i] = 0.5;
    t->smooth[i] = 0.12;
  }
  t->bump = 1;
  t->cutout = false;
  t->seed = wc_hash_string(name);
  t->ta = ta;
}

hz_internal void wc_tset(WcTex *t, i32 x, i32 y, WcRgb c, f64 a) {
  i32 i = wc_tidx(x, y);
  t->r[i] = c.r;
  t->g[i] = c.g;
  t->b[i] = c.b;
  t->a[i] = a;
}
hz_internal void wc_tmul(WcTex *t, i32 x, i32 y, f64 f) {
  i32 i = wc_tidx(x, y);
  t->r[i] *= f;
  t->g[i] *= f;
  t->b[i] *= f;
}

// tileable value noise in [0,1); freq = lattice cells across the tile
hz_internal f64 wc_tvn(const WcTex *t, i32 px, i32 py, i32 freq, i32 s) {
  f64 x = ((px + 0.5) / WC_TN) * freq;
  f64 y = ((py + 0.5) / WC_TN) * freq;
  i32 x0 = wc_floor_i(x), y0 = wc_floor_i(y);
  f64 fx = x - x0, fy = y - y0;
  f64 sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  u32 seed = t->seed + (u32)s * 7919u;
  f64 l00 = wc_hash2(wc_imod(x0, freq), wc_imod(y0, freq), seed);
  f64 l10 = wc_hash2(wc_imod(x0 + 1, freq), wc_imod(y0, freq), seed);
  f64 l01 = wc_hash2(wc_imod(x0, freq), wc_imod(y0 + 1, freq), seed);
  f64 l11 = wc_hash2(wc_imod(x0 + 1, freq), wc_imod(y0 + 1, freq), seed);
  f64 a = l00 + (l10 - l00) * sx;
  f64 b = l01 + (l11 - l01) * sx;
  return a + (b - a) * sy;
}

// anisotropic tileable value noise (separate lattice frequencies per axis)
hz_internal f64 wc_tvna(const WcTex *t, i32 px, i32 py, i32 fx, i32 fy, i32 s) {
  f64 x = ((px + 0.5) / WC_TN) * fx;
  f64 y = ((py + 0.5) / WC_TN) * fy;
  i32 x0 = wc_floor_i(x), y0 = wc_floor_i(y);
  f64 tx = x - x0, ty = y - y0;
  f64 sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
  u32 seed = t->seed + (u32)s * 7919u;
  f64 l00 = wc_hash2(wc_imod(x0, fx), wc_imod(y0, fy), seed);
  f64 l10 = wc_hash2(wc_imod(x0 + 1, fx), wc_imod(y0, fy), seed);
  f64 l01 = wc_hash2(wc_imod(x0, fx), wc_imod(y0 + 1, fy), seed);
  f64 l11 = wc_hash2(wc_imod(x0 + 1, fx), wc_imod(y0 + 1, fy), seed);
  f64 a = l00 + (l10 - l00) * sx;
  f64 b = l01 + (l11 - l01) * sx;
  return a + (b - a) * sy;
}

hz_internal f64 wc_tfbm(const WcTex *t, i32 px, i32 py, i32 freq, i32 oct, i32 s) {
  f64 sum = 0, amp = 1, norm = 0;
  for (i32 o = 0; o < oct; o++) {
    sum += wc_tvn(t, px, py, freq, s + o * 31) * amp;
    norm += amp;
    amp *= 0.5;
    freq *= 2;
    if (freq > WC_TN) freq = WC_TN;
  }
  return sum / norm;
}

// per-pixel white noise in [0,1), stable per pixel
hz_internal f64 wc_tpx(const WcTex *t, i32 x, i32 y, i32 s) {
  return wc_hash2(wc_twrap(x), wc_twrap(y), t->seed + 101u + (u32)s * 131u);
}

// shades albedo from the height gradient (baked light from the top-left)
hz_internal void wc_temboss(WcTex *t, f64 k) {
  f64 *sh = ALLOC_ARRAY(t->ta, f64, WC_TNN);
  for (i32 y = 0; y < WC_TN; y++)
    for (i32 x = 0; x < WC_TN; x++)
      sh[y * WC_TN + x] = (t->h[wc_tidx(x - 1, y - 1)] - t->h[wc_tidx(x + 1, y + 1)]) * k;
  for (i32 i = 0; i < WC_TNN; i++) {
    f64 f = 1 + sh[i];
    t->r[i] *= f;
    t->g[i] *= f;
    t->b[i] *= f;
  }
}

hz_internal void wc_tmaterial(WcTex *t, f64 smooth, f64 metal, f64 sss, f64 emit) {
  for (i32 i = 0; i < WC_TNN; i++) {
    t->smooth[i] = smooth;
    t->metal[i] = metal;
    t->sss[i] = sss;
    t->emit[i] = emit;
  }
}

hz_internal void wc_tfill(f64 *ch, f64 v) {
  for (i32 i = 0; i < WC_TNN; i++) ch[i] = v;
}

// tileable voronoi: nearest and second-nearest feature distance per pixel
hz_internal WcVor wc_voronoi(const WcTex *t, i32 count, u32 s, f64 jitter) {
  WcRng rnd = wc_rng(t->seed + 977u + s);
  i32 g = (i32)m_ceil(m_sqrt((f64)count));
  f64 cell = (f64)WC_TN / g;
  f64 *pts = ALLOC_ARRAY(t->ta, f64, g * g * 2);
  i32 n = 0;
  for (i32 gy = 0; gy < g; gy++) {
    for (i32 gx = 0; gx < g; gx++) {
      if (n >= count) break;
      pts[n * 2] = (gx + 0.5 + (wc_rng_next(&rnd) - 0.5) * jitter) * cell;
      pts[n * 2 + 1] = (gy + 0.5 + (wc_rng_next(&rnd) - 0.5) * jitter) * cell;
      n++;
    }
  }
  WcVor v = {ALLOC_ARRAY(t->ta, f64, WC_TNN), ALLOC_ARRAY(t->ta, f64, WC_TNN),
             ALLOC_ARRAY(t->ta, i32, WC_TNN)};
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 b1 = 1e9, b2 = 1e9;
      i32 bi = 0;
      for (i32 p = 0; p < n; p++) {
        f64 dx = m_abs(x + 0.5 - pts[p * 2]);
        f64 dy = m_abs(y + 0.5 - pts[p * 2 + 1]);
        if (dx > WC_TN / 2) dx = WC_TN - dx;
        if (dy > WC_TN / 2) dy = WC_TN - dy;
        f64 d = m_sqrt(dx * dx + dy * dy);
        if (d < b1) {
          b2 = b1;
          b1 = d;
          bi = p;
        } else if (d < b2) {
          b2 = d;
        }
      }
      i32 i = y * WC_TN + x;
      v.d1[i] = b1;
      v.d2[i] = b2;
      v.id[i] = bi;
    }
  }
  return v;
}

// ---- base material generators ----

hz_internal void wc_stone_base(WcTex *t, WcRgb base, f64 contrast) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 n = wc_tfbm(t, x, y, 4, 3, 1) - 0.5;
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      f64 f = 1 + (n * 0.3 + p * 0.2) * contrast;
      wc_tset(t, x, y, wc_rgb_scale(base, f), 255);
      t->h[y * WC_TN + x] = 0.5 + n * 0.6 + p * 0.15;
    }
  }
  // chiselled streaks
  WcRng rnd = wc_rng(t->seed + 5u);
  for (i32 k = 0; k < 5; k++) {
    i32 sx = (i32)m_floor(wc_rng_next(&rnd) * WC_TN);
    i32 sy = (i32)m_floor(wc_rng_next(&rnd) * WC_TN);
    i32 len = 2 + (i32)m_floor(wc_rng_next(&rnd) * 3);
    for (i32 j = 0; j < len; j++) {
      wc_tmul(t, sx + j, sy, 0.82);
      t->h[wc_tidx(sx + j, sy)] -= 0.25;
      wc_tmul(t, sx + j, sy - 1, 1.08);
    }
  }
  wc_tmaterial(t, 0.16, 0, 0, 0);
  t->bump = 1.2;
}

hz_internal void wc_dirt_base(WcTex *t, WcRgb base) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 n = wc_tfbm(t, x, y, 4, 2, 3) - 0.5;
      f64 p = wc_tpx(t, x, y, 0);
      f64 f = 1 + n * 0.2 + (p - 0.5) * 0.34;
      if (p > 0.9) f *= 1.16;
      else if (p < 0.12) f *= 0.7;
      wc_tset(t, x, y, wc_rgb_scale(base, f), 255);
      t->h[y * WC_TN + x] = 0.5 + n * 0.4 + (p - 0.5) * 0.4;
    }
  }
  wc_tmaterial(t, 0.06, 0, 0, 0);
  t->bump = 0.9;
}

hz_internal void wc_grass_blades(WcTex *t, i32 x, i32 y, i32 i) {
  f64 n = wc_tfbm(t, x, y, 4, 2, 9) - 0.5;
  f64 p = wc_tpx(t, x, y, 3);
  f64 lum = 158 + n * 40 + (p - 0.5) * 44;
  if (p > 0.9) lum += 18;
  if (p < 0.08) lum -= 26;
  wc_tset(t, x, y, wc_rgb(lum, lum, lum), 255);
  t->h[i] = 0.5 + (p - 0.5) * 0.7 + n * 0.3;
}

hz_internal void wc_cobble_base(WcTex *t, WcRgb dark) {
  WcVor v = wc_voronoi(t, 9, 1, 0.9);
  f64 *shade = ALLOC_ARRAY(t->ta, f64, WC_TN);
  for (i32 k = 0; k < WC_TN; k++) shade[k] = 0.78 + wc_tpx(t, k, 7, 77) * 0.42;
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      i32 i = y * WC_TN + x;
      f64 edge = v.d2[i] - v.d1[i];
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      if (edge < 1.05) {
        wc_tset(t, x, y, wc_rgb_scale(dark, 1 + p * 0.3), 255);
        t->h[i] = 0.08;
      } else {
        f64 c = 118 * shade[v.id[i] % 16] * (1 + p * 0.16 + (wc_tfbm(t, x, y, 8, 2, 4) - 0.5) * 0.2);
        wc_tset(t, x, y, wc_rgb(c, c, c), 255);
        t->h[i] = m_min(1, 0.35 + edge * 0.18) + p * 0.08;
      }
    }
  }
  wc_temboss(t, 0.5);
  wc_tmaterial(t, 0.14, 0, 0, 0);
  t->bump = 1.6;
}

hz_internal void wc_planks_base(WcTex *t, WcRgb base, i32 s0, i32 s1, i32 s2, i32 s3) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      i32 board = y / 4, row = y % 4;
      f64 grain = wc_tvna(t, x, y, 2, 16, board + 2) - 0.5;
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      f64 tone = 1 + wc_tpx(t, board, 3, 9) * 0.1 - 0.05;
      f64 f = tone * (1 + grain * 0.22 + p * 0.06);
      f64 h = 0.62 + grain * 0.2;
      if (row == 3) {
        f *= 0.66;
        h = 0.1;
      } else if (row == 0) {
        f *= 1.07;
      }
      i32 seam = (board % 4) == 0 ? s0 : (board % 4) == 1 ? s1 : (board % 4) == 2 ? s2 : s3;
      if (x == seam && row != 3) {
        f *= 0.7;
        h = 0.18;
      }
      wc_tset(t, x, y, wc_rgb_scale(base, f), 255);
      t->h[y * WC_TN + x] = h;
    }
  }
  wc_tmaterial(t, 0.22, 0, 0, 0);
  t->bump = 1.1;
}

hz_internal void wc_bark_base(WcTex *t, WcRgb base, f64 groove_dark, f64 stripes) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 col = wc_tvna(t, x, y, 16, 2, 3);
      f64 ridge = m_sin(((f64)x / WC_TN) * M_PI * 2 * stripes + wc_tvn(t, x, y, 4, 5) * 5);
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      f64 f = 1 + ridge * 0.14 + (col - 0.5) * 0.25 + p * 0.08;
      if (ridge < -0.55) f *= groove_dark + 0.2;
      wc_tset(t, x, y, wc_rgb_scale(base, f), 255);
      t->h[y * WC_TN + x] = 0.5 + ridge * 0.35 + p * 0.1;
    }
  }
  wc_tmaterial(t, 0.08, 0, 0, 0);
  t->bump = 1.5;
}

hz_internal void wc_log_top(WcTex *t, WcRgb inner, WcRgb bark) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      i32 i = y * WC_TN + x;
      f64 dx = x - 7.5, dy = y - 7.5;
      f64 d = m_max(m_abs(dx), m_abs(dy)) * 0.55 + m_sqrt(dx * dx + dy * dy) * 0.45;
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      if (d > 6.9) {
        wc_tset(t, x, y, wc_rgb_scale(bark, 1 + p * 0.2), 255);
        t->h[i] = 0.7;
      } else {
        f64 ring = m_sin(d * 2.1 + wc_tvn(t, x, y, 4, 2) * 1.5);
        f64 f = 1 + ring * 0.09 + p * 0.06 - (d > 6 ? 0.1 : 0);
        wc_tset(t, x, y, wc_rgb_scale(inner, f), 255);
        t->h[i] = 0.5 + ring * 0.12;
      }
    }
  }
  wc_tmaterial(t, 0.18, 0, 0, 0);
}

hz_internal void wc_leaves_base(WcTex *t, f64 holes, i32 s) {
  t->cutout = true;
  // start transparent, then stamp leaf clusters with lit centres and dark rims
  for (i32 i = 0; i < WC_TNN; i++) {
    wc_tset(t, i % WC_TN, i / WC_TN, wc_rgb(70, 70, 70), 0);
    t->h[i] = 0.2;
  }
  WcRng rnd = wc_rng(t->seed + 71u + (u32)s);
  i32 clusters = wc_tround(22 * (1 - holes));
  for (i32 c = 0; c < clusters; c++) {
    f64 cx = wc_rng_next(&rnd) * 16;
    f64 cy = wc_rng_next(&rnd) * 16;
    f64 r = 1.3 + wc_rng_next(&rnd) * 1.4;
    f64 tone = 0.85 + wc_rng_next(&rnd) * 0.3;
    for (i32 dy = -3; dy <= 3; dy++) {
      for (i32 dx = -3; dx <= 3; dx++) {
        i32 px = (i32)m_floor(cx + dx), py = (i32)m_floor(cy + dy);
        f64 ox = px + 0.5 - cx, oy = py + 0.5 - cy;
        f64 d = m_sqrt(ox * ox + oy * oy);
        if (d > r) continue;
        i32 i = wc_tidx(px, py);
        f64 edge = d / r;
        // light from the top-left: brighter on that side of each cluster
        f64 light_side = (ox + oy) / r;
        f64 lum = (160 - edge * 60 - light_side * 22) * tone + (wc_tpx(t, px, py, 9 + s) - 0.5) * 28;
        if (t->a[i] > 0 && t->h[i] > 1 - edge) continue;
        wc_tset(t, px, py, wc_rgb(lum, lum, lum), 255);
        t->h[i] = 1 - edge * 0.8;
      }
    }
  }
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      i32 i = y * WC_TN + x;
      if (t->a[i] > 0 && wc_tpx(t, x, y, 13 + s) < holes * 0.25) t->a[i] = 0;
    }
  }
  wc_tmaterial(t, 0.34, 0, 0.9, 0);
  t->bump = 1.4;
}

hz_internal void wc_wool_base(WcTex *t, WcRgb base) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 wave = m_sin((x + y) * 1.3 + wc_tvn(t, x, y, 4, 1) * 4) * 0.5 + m_sin((x - y) * 1.1) * 0.3;
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      f64 f = 1 + wave * 0.06 + p * 0.07 + (wc_tfbm(t, x, y, 4, 2, 2) - 0.5) * 0.1;
      wc_tset(t, x, y, wc_rgb_scale(base, f), 255);
      t->h[y * WC_TN + x] = 0.5 + wave * 0.25 + p * 0.15;
    }
  }
  wc_tmaterial(t, 0.02, 0, 0.12, 0);
  t->bump = 0.9;
}

hz_internal void wc_ore_overlay(WcTex *t, WcRgb ore, WcRgb light, i32 blobs, f64 smooth, f64 metal, f64 emit) {
  WcRng rnd = wc_rng(t->seed + 333u);
  for (i32 k = 0; k < blobs; k++) {
    i32 x = 1 + (i32)m_floor(wc_rng_next(&rnd) * 14);
    i32 y = 1 + (i32)m_floor(wc_rng_next(&rnd) * 14);
    i32 size = 3 + (i32)m_floor(wc_rng_next(&rnd) * 3);
    for (i32 s = 0; s < size; s++) {
      i32 i = wc_tidx(x, y);
      b32 is_light = wc_rng_next(&rnd) < 0.35;
      WcRgb c = is_light ? light : ore;
      wc_tset(t, x, y, wc_rgb_scale(c, 0.9 + wc_rng_next(&rnd) * 0.2), 255);
      t->h[i] = 0.85 + wc_rng_next(&rnd) * 0.15;
      t->smooth[i] = smooth;
      t->metal[i] = metal;
      t->emit[i] = emit;
      i32 dir = (i32)m_floor(wc_rng_next(&rnd) * 4);
      x += dir == 0 ? 1 : dir == 1 ? -1 : 0;
      y += dir == 2 ? 1 : dir == 3 ? -1 : 0;
    }
  }
}

hz_internal void wc_speckled(WcTex *t, WcRgb base, const WcSpeck *specks, u32 count, f64 contrast) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 n = wc_tfbm(t, x, y, 4, 2, 1) - 0.5;
      f64 p = wc_tpx(t, x, y, 0);
      WcRgb c = wc_rgb_scale(base, 1 + n * contrast * 2 + (wc_tpx(t, x, y, 4) - 0.5) * 0.08);
      f64 acc = 0;
      for (u32 s = 0; s < count; s++) {
        acc += specks[s].p;
        if (p < acc) {
          c = wc_rgb_scale(specks[s].c, 1 + (wc_tpx(t, x, y, 8) - 0.5) * 0.15);
          break;
        }
      }
      wc_tset(t, x, y, c, 255);
      t->h[y * WC_TN + x] = 0.5 + n * 0.5 + (p - 0.5) * 0.3;
    }
  }
}

hz_internal void wc_metal_block(WcTex *t, WcRgb base, WcRgb hi, WcRgb lo, f64 metal, f64 smooth) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 n = wc_tfbm(t, x, y, 2, 2, 1) - 0.5;
      f64 band = m_sin(((f64)y / WC_TN) * M_PI * 4 + n * 2) * 0.04;
      WcRgb c = wc_rgb_scale(base, 1 + band + n * 0.08);
      f64 h = 0.8;
      if (x == 0 || y == 0) {
        c = hi;
        h = 1;
      } else if (x == WC_TN - 1 || y == WC_TN - 1) {
        c = lo;
        h = 0.55;
      } else if (x == 1 || y == 1) {
        c = wc_rgb_mix(c, hi, 0.4);
      } else if (x == WC_TN - 2 || y == WC_TN - 2) {
        c = wc_rgb_mix(c, lo, 0.4);
      }
      wc_tset(t, x, y, c, 255);
      t->h[y * WC_TN + x] = h;
    }
  }
  wc_tmaterial(t, smooth, metal, 0, 0);
  t->bump = 0.6;
}

hz_internal void wc_clear_sprite(WcTex *t) {
  t->cutout = true;
  for (i32 i = 0; i < WC_TNN; i++) {
    wc_tset(t, i % WC_TN, i / WC_TN, wc_rgb(60, 90, 40), 0);
    t->h[i] = 0.5;
  }
  wc_tmaterial(t, 0.2, 0, 0.8, 0);
}

hz_internal void wc_stem(WcTex *t, i32 x, i32 y0, i32 y1, WcRgb c) {
  for (i32 y = y0; y <= y1; y++) wc_tset(t, x, y, wc_rgb_scale(c, 0.9 + wc_tpx(t, x, y, 0) * 0.2), 255);
}

typedef enum { WC_PETAL_ROUND, WC_PETAL_CUP, WC_PETAL_STAR, WC_PETAL_SHAPES } WcPetalShape;
#define WC_PETAL_COUNT 12
#define WC_XY 2

hz_internal const i8 WC_PETALS[WC_PETAL_SHAPES][WC_PETAL_COUNT][WC_XY] = {
    {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {0, -2}, {-2, 0}, {2, 0}, {0, 2}},
    {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {-2, -1}, {2, -1}, {-2, -2}, {2, -2}, {0, 2}},
    {{0, -2}, {0, -1}, {-2, 0}, {-1, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {-1, -1}, {1, 1}, {1, -1}, {-1, 1}},
};

hz_internal void wc_flower(WcTex *t, WcRgb petal, WcRgb center, WcPetalShape shape) {
  wc_clear_sprite(t);
  WcRgb green = wc_rgb(58, 110, 36);
  wc_stem(t, 7, 8, 15, green);
  wc_tset(t, 6, 12, green, 255);
  wc_tset(t, 5, 11, wc_rgb_scale(green, 1.1), 255);
  wc_tset(t, 8, 13, green, 255);
  wc_tset(t, 9, 12, wc_rgb_scale(green, 1.1), 255);
  i32 cx = 7, cy = 5;
  for (u32 k = 0; k < WC_PETAL_COUNT; k++) {
    i32 dx = WC_PETALS[shape][k][0], dy = WC_PETALS[shape][k][1];
    f64 f = 0.85 + wc_tpx(t, cx + dx, cy + dy, 2) * 0.3;
    wc_tset(t, cx + dx, cy + dy, wc_rgb_scale(petal, f), 255);
  }
  wc_tset(t, cx, cy, center, 255);
  for (i32 i = 0; i < WC_TNN; i++)
    if (t->a[i] > 0) t->sss[i] = 0.8;
}

hz_internal void wc_dead_bush_branch(WcTex *t, WcRng *rnd, WcRgb c, i32 x, i32 y, i32 dx, i32 len) {
  for (i32 k = 0; k < len; k++) {
    wc_tset(t, x, y, wc_rgb_scale(c, 0.85 + wc_rng_next(rnd) * 0.3), 255);
    y--;
    if (wc_rng_next(rnd) < 0.5) x += dx;
    if (x < 0 || x >= WC_TN || y < 0) return;
  }
}

hz_internal void wc_pumpkin_side(WcTex *t) {
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      f64 p = wc_tpx(t, x, y, 0) - 0.5;
      WcRgb c = wc_rgb_scale(wc_rgb(228, 136, 28), 1 + p * 0.08 + (wc_tvn(t, x, y, 4, 1) - 0.5) * 0.1);
      f64 h = 0.7;
      if (x % 5 == 0) {
        c = wc_rgb_scale(c, 0.78);
        h = 0.3;
      }
      wc_tset(t, x, y, c, 255);
      t->h[y * WC_TN + x] = h;
    }
  }
  wc_tmaterial(t, 0.35, 0, 0, 0);
}

hz_internal const WcSpeck WC_SPECK_SAND[] = {{{196, 180, 136}, 0.1}, {{232, 224, 190}, 0.06}};
hz_internal const WcSpeck WC_SPECK_RED_SAND[] = {{{165, 85, 25}, 0.1}, {{210, 125, 50}, 0.06}};
hz_internal const WcSpeck WC_SPECK_SANDSTONE_TOP[] = {{{210, 198, 150}, 0.05}};
hz_internal const WcSpeck WC_SPECK_SANDSTONE_BOTTOM[] = {{{195, 180, 132}, 0.12}};
hz_internal const WcSpeck WC_SPECK_CLAY[] = {{{175, 181, 194}, 0.05}, {{148, 153, 165}, 0.05}};
hz_internal const WcSpeck WC_SPECK_TERRACOTTA[] = {{{140, 86, 60}, 0.06}};
hz_internal const WcSpeck WC_SPECK_GRANITE[] = {{{184, 134, 114}, 0.14}, {{116, 74, 60}, 0.1}, {{200, 160, 150}, 0.03}};
hz_internal const WcSpeck WC_SPECK_DIORITE[] = {{{120, 120, 122}, 0.1}, {{232, 232, 234}, 0.12}};
hz_internal const WcSpeck WC_SPECK_ANDESITE[] = {{{110, 110, 112}, 0.12}, {{160, 160, 162}, 0.1}};
hz_internal const WcRgb WC_GRAVEL_COLS[] = {{132, 126, 124}, {98, 94, 93},   {150, 140, 132},
                                            {116, 106, 100}, {165, 160, 158}, {80, 76, 76}};
hz_internal const WcRgb WC_BOOK_COLS[] = {{150, 40, 30},  {40, 70, 140},  {50, 110, 50}, {130, 90, 40},
                                          {100, 50, 110}, {170, 150, 60}, {60, 60, 60}};
hz_internal const i32 WC_CANE_COLS[] = {3, 8, 12};

typedef struct {
  i32 x, y;
  WcRgb c;
  f64 e;
} WcFlame;
hz_internal const WcFlame WC_TORCH_FLAME[] = {
    {7, 6, {255, 236, 150}, 1}, {8, 6, {255, 210, 110}, 1}, {7, 5, {255, 250, 210}, 1},
    {8, 5, {255, 230, 140}, 1}, {7, 7, {255, 170, 60}, 0.8}, {8, 7, {230, 130, 40}, 0.7},
};

hz_internal void wc_generate_texture(WcTex *t, u32 layer) {
  switch (layer) {
  case T_STONE: wc_stone_base(t, wc_rgb(127, 127, 127), 1); break;
  case T_DIRT: wc_dirt_base(t, wc_rgb(134, 96, 67)); break;
  case T_COARSE_DIRT: {
    wc_dirt_base(t, wc_rgb(119, 85, 59));
    WcVor v = wc_voronoi(t, 12, 3, 1);
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        if (v.d2[i] - v.d1[i] > 1.6 && wc_tpx(t, x, y, 9) > 0.55) {
          f64 g = 95 + wc_tpx(t, x, y, 3) * 40;
          wc_tset(t, x, y, wc_rgb(g, g * 0.95, g * 0.9), 255);
          t->h[i] = 0.8;
        }
      }
    }
  } break;
  case T_GRASS_TOP:
    for (i32 i = 0; i < WC_TNN; i++) wc_grass_blades(t, i % WC_TN, i / WC_TN, i);
    wc_tmaterial(t, 0.1, 0, 0.15, 0);
    break;
  case T_GRASS_SIDE:
    wc_dirt_base(t, wc_rgb(134, 96, 67));
    // alpha is the tint mask: dirt is not tinted
    for (i32 i = 0; i < WC_TNN; i++) t->a[i] = 0;
    for (i32 x = 0; x < WC_TN; x++) {
      i32 depth = 3 + (i32)m_floor(wc_tpx(t, x, 0, 7) * 2.2) + (wc_tpx(t, x, 1, 7) > 0.8 ? 1 : 0);
      for (i32 y = 0; y < depth; y++) {
        i32 i = wc_tidx(x, y);
        wc_grass_blades(t, x, y, i);
        t->a[i] = 255;
        if (y == depth - 1) wc_tmul(t, x, y, 0.86);
      }
    }
    break;
  case T_GRASS_SIDE_SNOWED:
    wc_dirt_base(t, wc_rgb(134, 96, 67));
    for (i32 x = 0; x < WC_TN; x++) {
      i32 depth = 3 + (i32)m_floor(wc_tpx(t, x, 0, 7) * 2.2);
      for (i32 y = 0; y < depth; y++) {
        f64 v = 238 + (wc_tpx(t, x, y, 0) - 0.5) * 14;
        wc_tset(t, x, y, wc_rgb(v, v + 3, v + 8), 255);
        i32 i = wc_tidx(x, y);
        t->h[i] = 0.8;
        t->sss[i] = 0.5;
        t->smooth[i] = 0.35;
      }
    }
    break;
  case T_SNOW:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 2, 1) - 0.5;
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        f64 v = 240 + n * 16 + p * 8;
        wc_tset(t, x, y, wc_rgb(v - 2, v + 2, v + 7), 255);
        t->h[y * WC_TN + x] = 0.5 + n * 0.5 + p * 0.2;
      }
    }
    wc_tmaterial(t, 0.38, 0, 0.55, 0);
    t->bump = 0.5;
    break;
  case T_ICE:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1) - 0.5;
        b32 crack = m_abs(wc_tvn(t, x, y, 8, 2) - 0.5) < 0.04;
        f64 v = crack ? 1.12 : 1 + n * 0.08;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(150, 190, 250), v), 190);
        t->h[y * WC_TN + x] = crack ? 0.3 : 0.6;
      }
    }
    wc_tmaterial(t, 0.92, 0, 0, 0);
    t->bump = 0.4;
    break;
  case T_PACKED_ICE:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1) - 0.5;
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(160, 190, 240), 1 + n * 0.12 + p * 0.05), 255);
        t->h[y * WC_TN + x] = 0.5 + n * 0.4;
      }
    }
    wc_tmaterial(t, 0.8, 0, 0.3, 0);
    t->bump = 0.4;
    break;
  case T_SAND: {
    wc_speckled(t, wc_rgb(219, 207, 163), WC_SPECK_SAND, ARRAY_SIZE(WC_SPECK_SAND), 0.06);
    wc_tmaterial(t, 0.08, 0, 0, 0);
    t->bump = 0.7;
  } break;
  case T_RED_SAND: {
    wc_speckled(t, wc_rgb(190, 102, 33), WC_SPECK_RED_SAND, ARRAY_SIZE(WC_SPECK_RED_SAND), 0.06);
    wc_tmaterial(t, 0.08, 0, 0, 0);
    t->bump = 0.7;
  } break;
  case T_SANDSTONE_SIDE:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        f64 band = wc_tvna(t, x, y, 2, 16, 3) - 0.5;
        WcRgb c = wc_rgb_scale(wc_rgb(216, 203, 155), 1 + band * 0.08 + p * 0.05);
        f64 h = 0.6 + band * 0.2;
        if (y <= 2) c = wc_rgb_scale(wc_rgb(226, 214, 168), 1 + p * 0.05);
        if (y == 3 || y == 12) {
          c = wc_rgb_scale(c, 0.82);
          h = 0.2;
        }
        if (y >= 13) c = wc_rgb_scale(wc_rgb(204, 190, 142), 1 + p * 0.06);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = h;
      }
    }
    wc_tmaterial(t, 0.1, 0, 0, 0);
    break;
  case T_SANDSTONE_TOP: {
    wc_speckled(t, wc_rgb(224, 214, 170), WC_SPECK_SANDSTONE_TOP, ARRAY_SIZE(WC_SPECK_SANDSTONE_TOP), 0.04);
    wc_tmaterial(t, 0.1, 0, 0, 0);
    t->bump = 0.5;
  } break;
  case T_SANDSTONE_BOTTOM: {
    wc_speckled(t, wc_rgb(214, 200, 152), WC_SPECK_SANDSTONE_BOTTOM, ARRAY_SIZE(WC_SPECK_SANDSTONE_BOTTOM), 0.06);
    wc_tmaterial(t, 0.1, 0, 0, 0);
  } break;
  case T_GRAVEL: {
    WcVor v = wc_voronoi(t, 22, 2, 1);
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        f64 edge = v.d2[i] - v.d1[i];
        WcRgb c = WC_GRAVEL_COLS[v.id[i] % ARRAY_SIZE(WC_GRAVEL_COLS)];
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        if (edge < 0.7) {
          wc_tset(t, x, y, wc_rgb_scale(c, 0.62), 255);
          t->h[i] = 0.1;
        } else {
          wc_tset(t, x, y, wc_rgb_scale(c, 1 + p * 0.1), 255);
          t->h[i] = m_min(1, 0.4 + edge * 0.25);
        }
      }
    }
    wc_temboss(t, 0.45);
    wc_tmaterial(t, 0.14, 0, 0, 0);
    t->bump = 1.6;
  } break;
  case T_BEDROCK:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1);
        f64 p = wc_tpx(t, x, y, 0);
        f64 v = 30 + n * 90 + (p - 0.5) * 40;
        wc_tset(t, x, y, wc_rgb(v, v, v), 255);
        t->h[y * WC_TN + x] = n;
      }
    }
    wc_tmaterial(t, 0.1, 0, 0, 0);
    t->bump = 2;
    break;
  case T_CLAY: {
    wc_speckled(t, wc_rgb(160, 166, 179), WC_SPECK_CLAY, ARRAY_SIZE(WC_SPECK_CLAY), 0.05);
    wc_tmaterial(t, 0.3, 0, 0, 0);
    t->bump = 0.5;
  } break;
  case T_TERRACOTTA: {
    wc_speckled(t, wc_rgb(152, 94, 67), WC_SPECK_TERRACOTTA, ARRAY_SIZE(WC_SPECK_TERRACOTTA), 0.05);
    wc_tmaterial(t, 0.2, 0, 0, 0);
    t->bump = 0.5;
  } break;
  case T_PODZOL_TOP:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 p = wc_tpx(t, x, y, 0);
        f64 n = wc_tfbm(t, x, y, 4, 2, 2) - 0.5;
        WcRgb c = wc_rgb_scale(wc_rgb(106, 72, 32), 1 + n * 0.3 + (p - 0.5) * 0.2);
        if (p > 0.85) c = wc_rgb(140, 100, 50);
        if (p < 0.12) c = wc_rgb(70, 46, 20);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = 0.5 + n * 0.4 + (p - 0.5) * 0.4;
      }
    }
    wc_tmaterial(t, 0.08, 0, 0, 0);
    break;
  case T_PODZOL_SIDE:
    wc_dirt_base(t, wc_rgb(134, 96, 67));
    for (i32 x = 0; x < WC_TN; x++) {
      i32 depth = 2 + (i32)m_floor(wc_tpx(t, x, 0, 7) * 2.5);
      for (i32 y = 0; y < depth; y++) {
        f64 p = wc_tpx(t, x, y, 2);
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(106, 72, 32), 0.85 + p * 0.3), 255);
      }
    }
    break;
  case T_MOSS_BLOCK:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1) - 0.5;
        f64 p = wc_tpx(t, x, y, 0);
        WcRgb c = wc_rgb_scale(wc_rgb(89, 109, 45), 1 + n * 0.35 + (p - 0.5) * 0.12);
        if (p > 0.9) c = wc_rgb(120, 145, 60);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = 0.5 + n * 0.6 + (p - 0.5) * 0.3;
      }
    }
    wc_tmaterial(t, 0.08, 0, 0.25, 0);
    t->bump = 1.2;
    break;
  case T_COBBLESTONE: wc_cobble_base(t, wc_rgb(70, 70, 70)); break;
  case T_MOSSY_COBBLESTONE:
    wc_cobble_base(t, wc_rgb(70, 70, 70));
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        f64 m = wc_tfbm(t, x, y, 4, 2, 11);
        if (m > 0.52 || (t->h[i] < 0.2 && m > 0.4)) {
          f64 p = wc_tpx(t, x, y, 3);
          wc_tset(t, x, y, wc_rgb_scale(wc_rgb(82, 112, 45), 0.8 + p * 0.4), 255);
          t->sss[i] = 0.2;
        }
      }
    }
    break;
  case T_STONE_BRICKS:
    wc_stone_base(t, wc_rgb(122, 122, 122), 0.7);
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        i32 row = y / 8, ry = y % 8;
        i32 off = row == 0 ? 0 : 4;
        i32 bx = wc_twrap(x - off) % 8;
        f64 h = 0.75 + (t->h[i] - 0.5) * 0.3;
        if (ry == 7 || bx == 7) {
          wc_tset(t, x, y, wc_rgb_scale(wc_rgb(72, 72, 72), 1 + (wc_tpx(t, x, y, 0) - 0.5) * 0.2), 255);
          h = 0.05;
        } else if (ry == 0 || bx == 0) {
          wc_tmul(t, x, y, 1.12);
          h = 0.85;
        } else if (ry == 6 || bx == 6) {
          wc_tmul(t, x, y, 0.86);
          h = 0.6;
        }
        t->h[i] = h;
      }
    }
    wc_tmaterial(t, 0.18, 0, 0, 0);
    t->bump = 1.4;
    break;
  case T_BRICKS: {
    f64 *tones = ALLOC_ARRAY(t->ta, f64, WC_TN);
    for (i32 k = 0; k < WC_TN; k++) tones[k] = 0.85 + wc_tpx(t, k, 3, 55) * 0.3;
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        i32 row = y / 4, ry = y % 4;
        i32 off = row % 2 == 0 ? 0 : 4;
        i32 bx = wc_twrap(x - off) % 8;
        i32 brick = row * 2 + wc_twrap(x - off) / 8;
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        if (ry == 3 || bx == 7) {
          f64 m = 168 + p * 30;
          wc_tset(t, x, y, wc_rgb(m, m * 0.96, m * 0.9), 255);
          t->h[i] = 0.1;
        } else {
          f64 f = tones[brick % 16] * (1 + p * 0.14 + (wc_tfbm(t, x, y, 8, 2, 3) - 0.5) * 0.2);
          wc_tset(t, x, y, wc_rgb_scale(wc_rgb(150, 72, 56), f), 255);
          t->h[i] = 0.75 + p * 0.2;
        }
      }
    }
    wc_temboss(t, 0.4);
    wc_tmaterial(t, 0.14, 0, 0, 0);
    t->bump = 1.4;
  } break;
  case T_OAK_LOG: wc_bark_base(t, wc_rgb(104, 82, 50), 0.62, 5); break;
  case T_OAK_LOG_TOP: wc_log_top(t, wc_rgb(176, 142, 88), wc_rgb(96, 75, 45)); break;
  case T_BIRCH_LOG: {
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 2, 1) - 0.5;
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(218, 216, 208), 1 + n * 0.1 + p * 0.05), 255);
        t->h[y * WC_TN + x] = 0.6 + n * 0.2;
      }
    }
    WcRng rnd = wc_rng(t->seed + 9u);
    for (i32 k = 0; k < 7; k++) {
      i32 x0 = (i32)m_floor(wc_rng_next(&rnd) * WC_TN);
      i32 y0 = (i32)m_floor(wc_rng_next(&rnd) * WC_TN);
      i32 len = 2 + (i32)m_floor(wc_rng_next(&rnd) * 4);
      for (i32 j = 0; j < len; j++) {
        wc_tset(t, x0 + j, y0, wc_rgb(48, 46, 42), 255);
        t->h[wc_tidx(x0 + j, y0)] = 0.2;
        if (wc_rng_next(&rnd) < 0.3) wc_tset(t, x0 + j, y0 + 1, wc_rgb(80, 76, 70), 255);
      }
    }
    wc_tmaterial(t, 0.12, 0, 0, 0);
    t->bump = 1.2;
  } break;
  case T_BIRCH_LOG_TOP: wc_log_top(t, wc_rgb(206, 186, 132), wc_rgb(214, 212, 204)); break;
  case T_SPRUCE_LOG: wc_bark_base(t, wc_rgb(62, 42, 22), 0.55, 4); break;
  case T_SPRUCE_LOG_TOP: wc_log_top(t, wc_rgb(128, 96, 58), wc_rgb(58, 38, 20)); break;
  case T_OAK_PLANKS: wc_planks_base(t, wc_rgb(164, 132, 80), 3, 11, 6, 14); break;
  case T_BIRCH_PLANKS: wc_planks_base(t, wc_rgb(198, 180, 124), 9, 2, 13, 5); break;
  case T_SPRUCE_PLANKS: wc_planks_base(t, wc_rgb(116, 86, 50), 5, 12, 1, 9); break;
  case T_OAK_LEAVES: wc_leaves_base(t, 0.3, 0); break;
  case T_BIRCH_LEAVES: wc_leaves_base(t, 0.27, 3); break;
  case T_SPRUCE_LEAVES:
    wc_leaves_base(t, 0.18, 7);
    // needle strokes
    for (i32 y = 0; y < WC_TN; y++)
      for (i32 x = 0; x < WC_TN; x++)
        if (t->a[y * WC_TN + x] > 0 && (x + y * 2) % 5 == 0) wc_tmul(t, x, y, 0.82);
    break;
  case T_GLASS: {
    t->cutout = true;
    for (i32 i = 0; i < WC_TNN; i++) {
      wc_tset(t, i % WC_TN, i / WC_TN, wc_rgb(230, 240, 245), 0);
      t->h[i] = 0.5;
    }
    WcRgb edge = wc_rgb(214, 228, 234);
    for (i32 k = 0; k < WC_TN; k++) {
      wc_tset(t, k, 0, edge, 255);
      wc_tset(t, 0, k, edge, 255);
      wc_tset(t, k, WC_TN - 1, wc_rgb_scale(edge, 0.8), 255);
      wc_tset(t, WC_TN - 1, k, wc_rgb_scale(edge, 0.8), 255);
    }
    WcRgb streak = wc_rgb(245, 250, 255);
    for (i32 k = 0; k < 4; k++) wc_tset(t, 3 + k, 5 - k, streak, 255);
    for (i32 k = 0; k < 2; k++) wc_tset(t, 4 + k, 7 - k, streak, 255);
    wc_tset(t, 11, 12, streak, 255);
    wc_tset(t, 12, 11, streak, 255);
    wc_tmaterial(t, 0.95, 0, 0, 0);
    t->bump = 0.2;
  } break;
  case T_WATER:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1);
        f64 v = 175 + n * 60;
        wc_tset(t, x, y, wc_rgb(v, v, v), 200);
        t->h[y * WC_TN + x] = n;
      }
    }
    wc_tmaterial(t, 0.97, 0, 0, 0);
    break;
  case T_LAVA:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        f64 n = wc_tfbm(t, x, y, 4, 3, 1);
        f64 m = wc_tvn(t, x, y, 8, 2);
        f64 hot = wc_clamp01(n * 1.4 - 0.2 + m * 0.3);
        wc_tset(t, x, y, wc_rgb_mix(wc_rgb(180, 50, 10), wc_rgb(255, 200, 60), hot), 255);
        t->h[i] = 1 - hot;
        t->emit[i] = 0.6 + hot * 0.4;
      }
    }
    wc_tfill(t->smooth, 0.4);
    break;
  case T_COAL_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(36, 36, 36), wc_rgb(70, 70, 70), 5, 0.5, 0, 0);
    break;
  case T_IRON_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(196, 150, 120), wc_rgb(230, 190, 160), 4, 0.65, 1, 0);
    break;
  case T_GOLD_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(240, 205, 50), wc_rgb(255, 245, 140), 4, 0.75, 1, 0);
    break;
  case T_DIAMOND_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(70, 215, 225), wc_rgb(180, 255, 250), 4, 0.9, 0, 0.12);
    break;
  case T_LAPIS_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(28, 60, 170), wc_rgb(70, 110, 220), 5, 0.7, 0, 0);
    break;
  case T_REDSTONE_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(190, 10, 10), wc_rgb(255, 70, 60), 5, 0.6, 0, 0.35);
    break;
  case T_EMERALD_ORE:
    wc_stone_base(t, wc_rgb(127, 127, 127), 1);
    wc_ore_overlay(t, wc_rgb(20, 180, 80), wc_rgb(110, 245, 150), 3, 0.9, 0, 0.1);
    break;
  case T_CACTUS_SIDE:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        WcRgb c = wc_rgb_scale(wc_rgb(86, 130, 44), 1 + p * 0.1 + (wc_tvn(t, x, y, 4, 2) - 0.5) * 0.15);
        f64 h = 0.6;
        if (x % 4 == 1) {
          c = wc_rgb_scale(c, 0.72);
          h = 0.3;
        }
        if (x == 0 || x == WC_TN - 1) c = wc_rgb_scale(c, 0.8);
        if (x % 4 == 3 && y % 4 == 2) {
          c = wc_rgb(220, 220, 170);
          h = 1;
        }
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = h;
      }
    }
    wc_tmaterial(t, 0.35, 0, 0.3, 0);
    break;
  case T_CACTUS_TOP:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 d = m_max(m_abs(x - 7.5), m_abs(y - 7.5));
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        f64 ring = ((i32)m_floor(d)) % 3 == 0 ? 0.8 : 1;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(96, 142, 52), ring * (1 + p * 0.1)), 255);
        t->h[y * WC_TN + x] = ring > 0.9 ? 0.6 : 0.4;
      }
    }
    wc_tmaterial(t, 0.35, 0, 0.3, 0);
    break;
  case T_CACTUS_BOTTOM:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(150, 170, 90), 1 + p * 0.1), 255);
        t->h[y * WC_TN + x] = 0.5;
      }
    }
    wc_tmaterial(t, 0.3, 0, 0, 0);
    break;
  case T_TALL_GRASS: {
    wc_clear_sprite(t);
    WcRng rnd = wc_rng(t->seed + 1u);
    for (i32 b = 0; b < 10; b++) {
      i32 x = 1 + (i32)m_floor(wc_rng_next(&rnd) * 14);
      i32 hgt = 6 + (i32)m_floor(wc_rng_next(&rnd) * 9);
      i32 lean = wc_rng_next(&rnd) < 0.5 ? -1 : 1;
      for (i32 k = 0; k < hgt; k++) {
        i32 y = WC_TN - 1 - k;
        f64 lum = 92 + ((f64)k / hgt) * 70 + wc_rng_next(&rnd) * 18;
        wc_tset(t, x, y, wc_rgb(lum, lum, lum), 255);
        if (k > hgt * 0.5 && wc_rng_next(&rnd) < 0.25) x += lean;
        if (x < 0 || x >= WC_TN) break;
      }
    }
    wc_tmaterial(t, 0.2, 0, 0.85, 0);
  } break;
  case T_FERN: {
    wc_clear_sprite(t);
    WcRng rnd = wc_rng(t->seed + 1u);
    for (i32 f = 0; f < 3; f++) {
      i32 x0 = 3 + f * 4 + (i32)m_floor(wc_rng_next(&rnd) * 2);
      for (i32 y = 15; y > 2 + f; y--) {
        f64 lum = 110 + (15 - y) * 6 + wc_rng_next(&rnd) * 20;
        wc_tset(t, x0, y, wc_rgb(lum, lum, lum), 255);
        if (y % 2 == 0 && y < 14) {
          wc_tset(t, x0 - 1, y - 1, wc_rgb(lum * 0.9, lum * 0.9, lum * 0.9), 255);
          wc_tset(t, x0 + 1, y - 1, wc_rgb(lum * 0.9, lum * 0.9, lum * 0.9), 255);
          if (y < 11) {
            wc_tset(t, x0 - 2, y - 2, wc_rgb(lum * 0.85, lum * 0.85, lum * 0.85), 255);
            wc_tset(t, x0 + 2, y - 2, wc_rgb(lum * 0.85, lum * 0.85, lum * 0.85), 255);
          }
        }
      }
    }
    wc_tmaterial(t, 0.2, 0, 0.85, 0);
  } break;
  case T_DANDELION: wc_flower(t, wc_rgb(250, 212, 28), wc_rgb(230, 160, 20), WC_PETAL_ROUND); break;
  case T_POPPY: wc_flower(t, wc_rgb(220, 38, 36), wc_rgb(40, 20, 20), WC_PETAL_CUP); break;
  case T_CORNFLOWER: wc_flower(t, wc_rgb(80, 110, 235), wc_rgb(40, 60, 160), WC_PETAL_STAR); break;
  case T_DAISY: wc_flower(t, wc_rgb(245, 245, 240), wc_rgb(240, 200, 40), WC_PETAL_STAR); break;
  case T_DEAD_BUSH: {
    wc_clear_sprite(t);
    WcRgb c = wc_rgb(120, 82, 36);
    WcRng rnd = wc_rng(t->seed + 1u);
    wc_dead_bush_branch(t, &rnd, c, 7, 15, 0, 5);
    wc_dead_bush_branch(t, &rnd, c, 7, 11, -1, 7);
    wc_dead_bush_branch(t, &rnd, c, 8, 11, 1, 7);
    wc_dead_bush_branch(t, &rnd, c, 7, 13, 1, 5);
    wc_dead_bush_branch(t, &rnd, c, 6, 9, -1, 4);
    wc_tfill(t->sss, 0.3);
  } break;
  case T_SUGAR_CANE: {
    wc_clear_sprite(t);
    for (u32 k = 0; k < ARRAY_SIZE(WC_CANE_COLS); k++) {
      i32 x0 = WC_CANE_COLS[k];
      for (i32 y = 0; y < WC_TN; y++) {
        b32 joint = (y + x0) % 5 == 0;
        WcRgb c = joint ? wc_rgb(120, 160, 70) : wc_rgb(150, 196, 96);
        wc_tset(t, x0, y, c, 255);
        wc_tset(t, x0 + 1, y, wc_rgb_scale(c, 0.85), 255);
      }
    }
    WcRgb leaf = wc_rgb(110, 160, 60);
    wc_tset(t, 5, 6, leaf, 255);
    wc_tset(t, 6, 5, leaf, 255);
    wc_tset(t, 10, 10, leaf, 255);
    wc_tset(t, 11, 9, leaf, 255);
    wc_tmaterial(t, 0.3, 0, 0.6, 0);
  } break;
  case T_RED_MUSHROOM: {
    wc_clear_sprite(t);
    for (i32 y = 10; y < 16; y++) {
      wc_tset(t, 7, y, wc_rgb(222, 212, 196), 255);
      wc_tset(t, 8, y, wc_rgb(200, 190, 175), 255);
    }
    for (i32 y = 5; y < 10; y++) {
      i32 w = y == 5 ? 2 : y == 6 ? 4 : 5;
      for (i32 x = 8 - w; x < 8 + w; x++) wc_tset(t, x, y, wc_rgb_scale(wc_rgb(214, 38, 30), y == 9 ? 0.8 : 1), 255);
    }
    WcRgb dot = wc_rgb(250, 245, 240);
    wc_tset(t, 6, 6, dot, 255);
    wc_tset(t, 9, 7, dot, 255);
    wc_tset(t, 5, 8, dot, 255);
    wc_tset(t, 11, 8, dot, 255);
    wc_tmaterial(t, 0.45, 0, 0.3, 0);
  } break;
  case T_BROWN_MUSHROOM:
    wc_clear_sprite(t);
    for (i32 y = 11; y < 16; y++) {
      wc_tset(t, 7, y, wc_rgb(210, 196, 176), 255);
      wc_tset(t, 8, y, wc_rgb(190, 176, 156), 255);
    }
    for (i32 y = 8; y < 11; y++) {
      i32 w = y == 8 ? 3 : 5;
      for (i32 x = 8 - w; x < 8 + w; x++)
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(150, 110, 80), y == 10 ? 0.82 : 1 + (wc_tpx(t, x, y, 0) - 0.5) * 0.2), 255);
    }
    wc_tmaterial(t, 0.25, 0, 0.3, 0);
    break;
  case T_TORCH: {
    wc_clear_sprite(t);
    for (i32 y = 6; y < 16; y++) {
      wc_tset(t, 7, y, wc_rgb(128, 94, 52), 255);
      wc_tset(t, 8, y, wc_rgb(96, 70, 38), 255);
    }
    for (u32 k = 0; k < ARRAY_SIZE(WC_TORCH_FLAME); k++) {
      const WcFlame *f = &WC_TORCH_FLAME[k];
      wc_tset(t, f->x, f->y, f->c, 255);
      t->emit[wc_tidx(f->x, f->y)] = f->e;
    }
    wc_tfill(t->sss, 0);
  } break;
  case T_OBSIDIAN:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 4, 3, 1);
        f64 p = wc_tpx(t, x, y, 0);
        WcRgb c = wc_rgb_scale(wc_rgb(22, 18, 34), 0.8 + n * 0.5);
        if (p > 0.9) c = wc_rgb(62, 44, 96);
        else if (p > 0.82) c = wc_rgb(42, 30, 66);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = 0.4 + n * 0.3;
      }
    }
    wc_tmaterial(t, 0.86, 0, 0, 0);
    t->bump = 0.8;
    break;
  case T_BOOKSHELF: {
    wc_planks_base(t, wc_rgb(164, 132, 80), 3, 11, 6, 14);
    WcRng rnd = wc_rng(t->seed + 1u);
    for (u32 sh = 0; sh < 2; sh++) {
      i32 y0 = sh == 0 ? 2 : 9, y1 = sh == 0 ? 6 : 13;
      i32 x = 1;
      while (x < 15) {
        i32 w = wc_rng_next(&rnd) < 0.3 ? 2 : 1;
        WcRgb c = WC_BOOK_COLS[(i32)m_floor(wc_rng_next(&rnd) * ARRAY_SIZE(WC_BOOK_COLS))];
        i32 top = y0 + (wc_rng_next(&rnd) < 0.4 ? 1 : 0);
        for (i32 xx = x; xx < (x + w < 15 ? x + w : 15); xx++) {
          for (i32 y = top; y <= y1; y++) {
            f64 f = y == top ? 1.2 : y == y1 ? 0.7 : 1;
            wc_tset(t, xx, y, wc_rgb_scale(c, f * (0.9 + wc_tpx(t, xx, y, 0) * 0.2)), 255);
            t->h[wc_tidx(xx, y)] = 0.5;
          }
        }
        x += w;
      }
      for (i32 x2 = 0; x2 < WC_TN; x2++) {
        wc_tset(t, x2, y0 - 1, wc_rgb(100, 76, 44), 255);
        wc_tset(t, x2, y1 + 1, wc_rgb(140, 110, 66), 255);
      }
    }
  } break;
  case T_GLOWSTONE: {
    WcVor v = wc_voronoi(t, 10, 2, 1);
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        f64 edge = v.d2[i] - v.d1[i];
        f64 center = wc_clamp01(1 - v.d1[i] / 4);
        f64 p = wc_tpx(t, x, y, 0);
        f64 bright = wc_clamp01(center * 0.7 + p * 0.3);
        WcRgb c = wc_rgb_mix(wc_rgb(160, 110, 50), wc_rgb(255, 236, 170), bright);
        if (edge < 0.8) c = wc_rgb(120, 80, 40);
        wc_tset(t, x, y, c, 255);
        t->h[i] = edge < 0.8 ? 0.2 : 0.6 + bright * 0.3;
        t->emit[i] = edge < 0.8 ? 0.35 : 0.55 + bright * 0.45;
      }
    }
    wc_tfill(t->smooth, 0.4);
  } break;
  case T_SEA_LANTERN:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        i32 i = y * WC_TN + x;
        i32 cx = x % 8, cy = y % 8;
        b32 border = cx == 0 || cy == 0 || cx == 7 || cy == 7;
        f64 d = m_abs(cx - 3.5) + m_abs(cy - 3.5);
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        WcRgb c = wc_rgb_mix(wc_rgb(235, 250, 245), wc_rgb(160, 200, 190), wc_clamp01(d / 7));
        if (border) c = wc_rgb(150, 180, 175);
        wc_tset(t, x, y, wc_rgb_scale(c, 1 + p * 0.06), 255);
        t->h[i] = border ? 0.3 : 0.7;
        t->emit[i] = border ? 0.4 : 0.95 - d * 0.05;
      }
    }
    wc_tfill(t->smooth, 0.7);
    break;
  case T_GOLD_BLOCK: wc_metal_block(t, wc_rgb(245, 205, 60), wc_rgb(255, 245, 160), wc_rgb(190, 130, 20), 0.82, 0.68); break;
  case T_IRON_BLOCK: wc_metal_block(t, wc_rgb(216, 216, 216), wc_rgb(250, 250, 250), wc_rgb(160, 160, 160), 0.75, 0.6); break;
  case T_DIAMOND_BLOCK: wc_metal_block(t, wc_rgb(98, 222, 214), wc_rgb(190, 255, 250), wc_rgb(40, 150, 150), 0, 0.93); break;
  case T_EMERALD_BLOCK: wc_metal_block(t, wc_rgb(42, 200, 90), wc_rgb(150, 255, 180), wc_rgb(10, 120, 50), 0, 0.9); break;
  case T_QUARTZ_BLOCK_SIDE:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = wc_tfbm(t, x, y, 2, 2, 1) - 0.5;
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        wc_tset(t, x, y, wc_rgb_scale(wc_rgb(236, 230, 222), 1 + n * 0.04 + p * 0.02), 255);
        t->h[y * WC_TN + x] = 0.6;
      }
    }
    wc_tmaterial(t, 0.62, 0, 0, 0);
    t->bump = 0.3;
    break;
  case T_QUARTZ_BLOCK_TOP:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        b32 border = x == 0 || y == 0 || x == 15 || y == 15;
        WcRgb c = border ? wc_rgb(220, 214, 205) : wc_rgb_scale(wc_rgb(238, 232, 225), 1 + p * 0.02);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = border ? 0.4 : 0.6;
      }
    }
    wc_tmaterial(t, 0.62, 0, 0, 0);
    t->bump = 0.4;
    break;
  case T_GRANITE: {
    wc_speckled(t, wc_rgb(152, 106, 88), WC_SPECK_GRANITE, ARRAY_SIZE(WC_SPECK_GRANITE), 0.1);
    wc_tmaterial(t, 0.3, 0, 0, 0);
  } break;
  case T_DIORITE: {
    wc_speckled(t, wc_rgb(192, 192, 194), WC_SPECK_DIORITE, ARRAY_SIZE(WC_SPECK_DIORITE), 0.08);
    wc_tmaterial(t, 0.3, 0, 0, 0);
  } break;
  case T_ANDESITE: {
    wc_speckled(t, wc_rgb(136, 136, 138), WC_SPECK_ANDESITE, ARRAY_SIZE(WC_SPECK_ANDESITE), 0.1);
    wc_tmaterial(t, 0.25, 0, 0, 0);
  } break;
  case T_WHITE_WOOL: wc_wool_base(t, wc_rgb(234, 236, 237)); break;
  case T_RED_WOOL: wc_wool_base(t, wc_rgb(161, 39, 34)); break;
  case T_ORANGE_WOOL: wc_wool_base(t, wc_rgb(240, 118, 19)); break;
  case T_YELLOW_WOOL: wc_wool_base(t, wc_rgb(248, 197, 39)); break;
  case T_LIME_WOOL: wc_wool_base(t, wc_rgb(112, 185, 25)); break;
  case T_BLUE_WOOL: wc_wool_base(t, wc_rgb(53, 57, 157)); break;
  case T_BLACK_WOOL: wc_wool_base(t, wc_rgb(26, 27, 31)); break;
  case T_PUMPKIN_SIDE: wc_pumpkin_side(t); break;
  case T_PUMPKIN_TOP:
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 d = m_max(m_abs(x - 7.5), m_abs(y - 7.5));
        f64 p = wc_tpx(t, x, y, 0) - 0.5;
        WcRgb c = wc_rgb_scale(wc_rgb(220, 128, 24), (((i32)m_floor(d)) % 3 == 0 ? 0.85 : 1) * (1 + p * 0.08));
        if (d < 1.6) c = wc_rgb(90, 70, 30);
        wc_tset(t, x, y, c, 255);
        t->h[y * WC_TN + x] = d < 1.6 ? 0.9 : 0.6;
      }
    }
    wc_tmaterial(t, 0.35, 0, 0, 0);
    break;
  case T_JACK_O_LANTERN: {
    wc_pumpkin_side(t);
    WcRgb glow = wc_rgb(255, 214, 90);
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        b32 eye = (x >= 3 && x <= 5 && y >= 4 && y <= 6) || (x >= 10 && x <= 12 && y >= 4 && y <= 6);
        b32 mouth = (y == 10 && x >= 3 && x <= 12) || (y == 11 && x >= 4 && x <= 11) ||
                    (y == 9 && (x == 3 || x == 12)) || (y == 12 && (x == 6 || x == 7 || x == 9 || x == 10));
        if (!eye && !mouth) continue;
        wc_tset(t, x, y, wc_rgb_scale(glow, 0.9 + wc_tpx(t, x, y, 0) * 0.2), 255);
        t->emit[y * WC_TN + x] = 1;
        t->h[y * WC_TN + x] = 0.2;
      }
    }
  } break;
  case T_DESTROY:
    t->cutout = true;
    for (i32 y = 0; y < WC_TN; y++) {
      for (i32 x = 0; x < WC_TN; x++) {
        f64 n = m_abs(wc_tvn(t, x, y, 4, 1) - 0.5);
        wc_tset(t, x, y, wc_rgb(30, 30, 30), n < 0.08 ? 200 : 0);
        t->h[y * WC_TN + x] = 0.5;
      }
    }
    break;
  default:
    for (i32 i = 0; i < WC_TNN; i++) {
      b32 odd = ((i % WC_TN) + (i / WC_TN)) % 2;
      wc_tset(t, i % WC_TN, i / WC_TN, odd ? wc_rgb(255, 0, 255) : wc_rgb(0, 0, 0), 255);
    }
    break;
  }
}

// ---- assembly: linear-space mips with coverage-preserving alpha ----

force_inline f64 wc_srgb_to_linear(f64 c) {
  c /= 255;
  return c <= 0.04045 ? c / 12.92 : m_pow((c + 0.055) / 1.055, 2.4);
}
force_inline u8 wc_linear_to_srgb8(f64 c) {
  f64 v = c <= 0.0031308 ? c * 12.92 : 1.055 * m_pow(c, 1 / 2.4) - 0.055;
  return (u8)wc_tround(wc_clamp01(v) * 255);
}
force_inline u8 wc_unorm8(f64 v) { return (u8)wc_tround(wc_clamp01(v) * 255); }
// Uint8ClampedArray conversion: clamp, round half to even
force_inline u8 wc_clamped8(f64 v) {
  if (!(v > 0)) return 0;
  if (v >= 255) return 255;
  f64 f = m_floor(v);
  f64 d = v - f;
  if (d > 0.5) return (u8)(f + 1);
  if (d < 0.5) return (u8)f;
  return ((i32)f % 2 == 0) ? (u8)f : (u8)(f + 1);
}

hz_internal f64 wc_coverage(const f64 *lin, u32 cnt, f64 thr, f64 k) {
  u32 c = 0;
  for (u32 i = 0; i < cnt; i++)
    if (lin[i * 4 + 3] * k > thr) c++;
  return (f64)c / cnt;
}

u32 wc_texture_mip_offset(u32 mip) {
  u32 off = 0;
  for (u32 m = 0; m < mip; m++) off += T_COUNT * (WC_TN >> m) * (WC_TN >> m) * 4;
  return off;
}

hz_internal void wc_build_layer(WcTextureData *out, u32 layer) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;
  WcTex tex;
  WcTex *t = &tex;
  wc_tex_init(t, wc_texture_name[layer], ta);
  wc_generate_texture(t, layer);
  out->cutout[layer] = (u8)t->cutout;

  f64 *lin = ALLOC_ARRAY(ta, f64, WC_TNN * 4);
  f64 *nrm = ALLOC_ARRAY(ta, f64, WC_TNN * 4);
  f64 *spc = ALLOC_ARRAY(ta, f64, WC_TNN * 4);
  u8 *px = out->pixels + layer * WC_TNN * 4;
  for (i32 i = 0; i < WC_TNN; i++) {
    lin[i * 4] = wc_srgb_to_linear(m_clamp(t->r[i], 0, 255));
    lin[i * 4 + 1] = wc_srgb_to_linear(m_clamp(t->g[i], 0, 255));
    lin[i * 4 + 2] = wc_srgb_to_linear(m_clamp(t->b[i], 0, 255));
    lin[i * 4 + 3] = t->a[i] / 255;
    px[i * 4] = wc_clamped8(t->r[i]);
    px[i * 4 + 1] = wc_clamped8(t->g[i]);
    px[i * 4 + 2] = wc_clamped8(t->b[i]);
    px[i * 4 + 3] = wc_clamped8(t->a[i]);
  }
  // normals from height
  for (i32 y = 0; y < WC_TN; y++) {
    for (i32 x = 0; x < WC_TN; x++) {
      i32 i = y * WC_TN + x;
      f64 hx = t->h[wc_tidx(x - 1, y)] - t->h[wc_tidx(x + 1, y)];
      f64 hy = t->h[wc_tidx(x, y - 1)] - t->h[wc_tidx(x, y + 1)];
      f64 nx = hx * t->bump * 1.2, ny = hy * t->bump * 1.2, nz = 1;
      f64 len = m_sqrt(nx * nx + ny * ny + nz * nz);
      nrm[i * 4] = nx / len;
      nrm[i * 4 + 1] = ny / len;
      nrm[i * 4 + 2] = nz / len;
      nrm[i * 4 + 3] = wc_clamp01(t->h[i]);
      spc[i * 4] = wc_clamp01(t->smooth[i]);
      spc[i * 4 + 1] = wc_clamp01(t->metal[i]);
      spc[i * 4 + 2] = wc_clamp01(t->sss[i]);
      spc[i * 4 + 3] = wc_clamp01(t->emit[i]);
    }
  }
  // cutouts bleed opaque colour into transparent texels so mips don't go dark
  if (t->cutout) {
    f64 *src = ALLOC_ARRAY(ta, f64, WC_TNN * 4);
    for (i32 pass = 0; pass < 4; pass++) {
      mem_cpy(src, lin, sizeof(f64) * WC_TNN * 4);
      for (i32 y = 0; y < WC_TN; y++) {
        for (i32 x = 0; x < WC_TN; x++) {
          i32 i = y * WC_TN + x;
          if (src[i * 4 + 3] > 0.5) continue;
          f64 r = 0, g = 0, b = 0, w = 0;
          for (i32 k = 0; k < 4; k++) {
            i32 dx = k == 0 ? 1 : k == 1 ? -1 : 0;
            i32 dy = k == 2 ? 1 : k == 3 ? -1 : 0;
            i32 j = wc_twrap(y + dy) * WC_TN + wc_twrap(x + dx);
            f64 a = src[j * 4 + 3];
            if (a > 0.5 || (pass > 0 && (src[j * 4] + src[j * 4 + 1] + src[j * 4 + 2]) > 0)) {
              r += src[j * 4];
              g += src[j * 4 + 1];
              b += src[j * 4 + 2];
              w++;
            }
          }
          if (w > 0) {
            lin[i * 4] = r / w;
            lin[i * 4 + 1] = g / w;
            lin[i * 4 + 2] = b / w;
          }
        }
      }
    }
  }
  f64 coverage0 = t->cutout ? wc_coverage(lin, WC_TNN, 0.5, 1) : 0;

  i32 size = WC_TN;
  for (u32 l = 0; l < WC_TEX_MIPS; l++) {
    u32 cnt = (u32)(size * size);
    if (l > 0) {
      i32 ns = size, ps = size * 2;
      f64 *nl = ALLOC_ARRAY(ta, f64, cnt * 4);
      f64 *nn = ALLOC_ARRAY(ta, f64, cnt * 4);
      f64 *n4 = ALLOC_ARRAY(ta, f64, cnt * 4);
      for (i32 y = 0; y < ns; y++) {
        for (i32 x = 0; x < ns; x++) {
          i32 o = (y * ns + x) * 4;
          for (i32 dy = 0; dy < 2; dy++) {
            for (i32 dx = 0; dx < 2; dx++) {
              i32 s = ((y * 2 + dy) * ps + (x * 2 + dx)) * 4;
              for (i32 c = 0; c < 4; c++) {
                nl[o + c] += lin[s + c] * 0.25;
                nn[o + c] += nrm[s + c] * 0.25;
                n4[o + c] += spc[s + c] * 0.25;
              }
            }
          }
          f64 len = m_sqrt(nn[o] * nn[o] + nn[o + 1] * nn[o + 1] + nn[o + 2] * nn[o + 2]);
          if (len == 0) len = 1;
          nn[o] /= len;
          nn[o + 1] /= len;
          nn[o + 2] /= len;
        }
      }
      lin = nl;
      nrm = nn;
      spc = n4;
      if (t->cutout && coverage0 > 0) {
        // preserve alpha-test coverage across mips
        f64 lo = 0.5, hi = 4;
        for (i32 it = 0; it < 12; it++) {
          f64 mid = (lo + hi) / 2;
          if (wc_coverage(lin, cnt, 0.5, mid) < coverage0) lo = mid;
          else hi = mid;
        }
        f64 k = (lo + hi) / 2;
        for (u32 i = 0; i < cnt; i++) lin[i * 4 + 3] = wc_clamp01(lin[i * 4 + 3] * k);
      }
    }
    u32 base = wc_texture_mip_offset(l) + layer * cnt * 4;
    u8 *A = out->albedo + base;
    u8 *Nm = out->normal + base;
    u8 *S = out->spec + base;
    for (u32 i = 0; i < cnt; i++) {
      A[i * 4] = wc_linear_to_srgb8(lin[i * 4]);
      A[i * 4 + 1] = wc_linear_to_srgb8(lin[i * 4 + 1]);
      A[i * 4 + 2] = wc_linear_to_srgb8(lin[i * 4 + 2]);
      A[i * 4 + 3] = wc_unorm8(lin[i * 4 + 3]);
      Nm[i * 4] = (u8)wc_tround((nrm[i * 4] * 0.5 + 0.5) * 255);
      Nm[i * 4 + 1] = (u8)wc_tround((nrm[i * 4 + 1] * 0.5 + 0.5) * 255);
      Nm[i * 4 + 2] = (u8)wc_tround((nrm[i * 4 + 2] * 0.5 + 0.5) * 255);
      Nm[i * 4 + 3] = wc_unorm8(nrm[i * 4 + 3]);
      S[i * 4] = wc_unorm8(spc[i * 4]);
      S[i * 4 + 1] = wc_unorm8(spc[i * 4 + 1]);
      S[i * 4 + 2] = wc_unorm8(spc[i * 4 + 2]);
      S[i * 4 + 3] = wc_unorm8(spc[i * 4 + 3]);
    }
    size >>= 1;
  }
  tctx_temp_allocator_end(tmp);
}

void wc_textures_alloc(WcTextureData *out, Allocator *alloc) {
  out->data_size = wc_texture_mip_offset(WC_TEX_MIPS);
  out->albedo = ALLOC_ARRAY(alloc, u8, out->data_size);
  out->normal = ALLOC_ARRAY(alloc, u8, out->data_size);
  out->spec = ALLOC_ARRAY(alloc, u8, out->data_size);
  out->pixels = ALLOC_ARRAY(alloc, u8, T_COUNT * WC_TNN * 4);
  out->cutout = ALLOC_ARRAY(alloc, u8, T_COUNT);
}

void wc_textures_generate(WcTextureData *out) {
  Range(u32) r = lane_range(T_COUNT);
  for (u32 layer = r.min; layer < r.max; layer++) wc_build_layer(out, layer);
}

#undef WC_TN
#undef WC_TNN

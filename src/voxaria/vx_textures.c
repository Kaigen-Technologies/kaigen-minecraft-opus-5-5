#include "voxaria/vx_render.h"
#include "procgen/pg_dump.h"

// procedural painted tiles, 2x2 blocks per layer: faces sample them in world space so neighbours continue one painting

#define VX_TN VX_TEX_SIZE
#define VX_TNN (VX_TN * VX_TN)
#define VX_TB (VX_TN / 2)
#define VX_PX (1.0f / (f32)VX_TB)

typedef struct {
  f32 r, g, b;
} VxCol;

// shade ramp, darkest first: 0 line work, 1 shadow, 2 base, 3 light, 4 highlight
typedef struct {
  VxCol s[5];
} VxRamp;

typedef struct {
  VxCol *c; // srgb 0..1
  f32 *a;
  f32 *emit;
  f32 *glint;
  u32 seed;
  u32 var;
  b32 cutout;
} VxTile;

force_inline VxCol vx_col(f32 r, f32 g, f32 b) { return (VxCol){r, g, b}; }
force_inline VxCol vx_hex(u32 h) {
  return (VxCol){(f32)((h >> 16) & 255) / 255.0f, (f32)((h >> 8) & 255) / 255.0f, (f32)(h & 255) / 255.0f};
}
force_inline VxCol vx_mixc(VxCol a, VxCol b, f32 t) {
  return (VxCol){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
force_inline VxCol vx_scalec(VxCol a, f32 k) { return (VxCol){a.r * k, a.g * k, a.b * k}; }

hz_internal void vx_rgb_to_hsv(VxCol c, f32 *h, f32 *s, f32 *v) {
  f32 mx = m_maxf(c.r, m_maxf(c.g, c.b)), mn = m_minf(c.r, m_minf(c.g, c.b));
  f32 d = mx - mn;
  *v = mx;
  *s = mx > 0 ? d / mx : 0;
  if (d <= 1e-6f) {
    *h = 0;
    return;
  }
  f32 hh;
  if (mx == c.r) hh = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
  else if (mx == c.g) hh = (c.b - c.r) / d + 2.0f;
  else hh = (c.r - c.g) / d + 4.0f;
  *h = hh * 60.0f;
}

hz_internal VxCol vx_hsv_to_rgb(f32 h, f32 s, f32 v) {
  h = m_fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  s = m_saturatef(s);
  v = m_saturatef(v);
  f32 c = v * s, x = c * (1.0f - m_absf(m_fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
  VxCol o;
  if (h < 60) o = vx_col(c, x, 0);
  else if (h < 120) o = vx_col(x, c, 0);
  else if (h < 180) o = vx_col(0, c, x);
  else if (h < 240) o = vx_col(0, x, c);
  else if (h < 300) o = vx_col(x, 0, c);
  else o = vx_col(c, 0, x);
  return vx_col(o.r + m, o.g + m, o.b + m);
}

// shifts hue up to `deg` degrees toward `target` and scales saturation and value
hz_internal VxCol vx_shift(VxCol c, f32 target, f32 deg, f32 sat_k, f32 val_k) {
  f32 h, s, v;
  vx_rgb_to_hsv(c, &h, &s, &v);
  f32 d = m_fmodf(target - h + 540.0f, 360.0f) - 180.0f;
  h += m_clampf(d, -deg, deg);
  return vx_hsv_to_rgb(h, s * sat_k, v * val_k);
}

// darker shades lean cool (toward violet-blue), lighter ones warm (toward yellow)
hz_internal VxRamp vx_ramp(u32 light, u32 dark) {
  VxCol l = vx_hex(light), d = vx_hex(dark);
  VxRamp r;
  r.s[0] = vx_shift(d, 245.0f, 16.0f, 1.12f, 0.6f);
  r.s[1] = vx_shift(d, 240.0f, 4.0f, 1.0f, 1.0f);
  r.s[2] = vx_mixc(d, l, 0.5f);
  r.s[3] = vx_shift(l, 55.0f, 4.0f, 1.0f, 1.0f);
  r.s[4] = vx_shift(l, 55.0f, 12.0f, 0.82f, 1.14f);
  return r;
}

// continuous position f (0..1) along the ramp
hz_internal VxCol vx_rampf(const VxRamp *r, f32 f) {
  f = m_saturatef(f) * 4.0f;
  i32 i = (i32)m_minf(m_floorf(f), 3.0f);
  return vx_mixc(r->s[i], r->s[i + 1], f - (f32)i);
}

// ---- canvas ----

// texel x's centre in blocks across the tile (0..2)
force_inline f32 vx_bc(i32 x) { return ((f32)x + 0.5f) * VX_PX; }
force_inline u32 vx_ti(i32 x, i32 y) { return (u32)(y * VX_TN + x); }

force_inline void vx_set(VxTile *t, i32 x, i32 y, VxCol c) {
  u32 i = vx_ti(x, y);
  t->c[i] = c;
  t->a[i] = 1.0f;
}
force_inline void vx_glow(VxTile *t, i32 x, i32 y, f32 e, f32 g) {
  u32 i = vx_ti(x, y);
  t->emit[i] = m_maxf(t->emit[i], e);
  t->glint[i] = m_maxf(t->glint[i], g);
}
force_inline void vx_clear(VxTile *t) {
  t->cutout = true;
  for (u32 i = 0; i < VX_TNN; i++) t->a[i] = 0;
}

// stable hash in [0, 1)
force_inline f32 vx_h(const VxTile *t, i32 x, i32 y, u32 s) {
  return (f32)(vx_hash3i(x, y, (i32)s, t->seed + t->var * 7919u) >> 8) / 16777216.0f;
}

// antialiased coverage of a line of half-width w at distance d (both in blocks)
force_inline f32 vx_line(f32 d, f32 w) { return 1.0f - m_smoothstepf(w - VX_PX * 0.7f, w + VX_PX * 0.7f, d); }
// antialiased inside test of a signed distance (negative inside, blocks)
force_inline f32 vx_inside(f32 sd) { return 1.0f - m_smoothstepf(-VX_PX * 0.7f, VX_PX * 0.7f, sd); }

// tileable value noise: nx by ny lattice cells across the tile
hz_internal f32 vx_vn(const VxTile *t, f32 bx, f32 by, i32 nx, i32 ny, u32 s) {
  f32 fx = bx * 0.5f * (f32)nx, fy = by * 0.5f * (f32)ny;
  i32 x0 = (i32)m_floorf(fx), y0 = (i32)m_floorf(fy);
  f32 tx = m_smoothf(fx - (f32)x0), ty = m_smoothf(fy - (f32)y0);
  i32 xa = ((x0 % nx) + nx) % nx, ya = ((y0 % ny) + ny) % ny, xb = (xa + 1) % nx, yb = (ya + 1) % ny;
  f32 a = vx_h(t, xa, ya, s), b = vx_h(t, xb, ya, s), c = vx_h(t, xa, yb, s), d = vx_h(t, xb, yb, s);
  return m_lerpf(m_lerpf(a, b, tx), m_lerpf(c, d, tx), ty);
}

// octaves of tileable noise from n cells across the tile, normalised to 0..1
hz_internal f32 vx_fbm(const VxTile *t, f32 bx, f32 by, i32 n, i32 oct, u32 s) {
  f32 sum = 0, amp = 0.5f, norm = 0;
  for (i32 k = 0; k < oct; k++) {
    sum += vx_vn(t, bx, by, n << k, n << k, s + (u32)k) * amp;
    norm += amp;
    amp *= 0.5f;
  }
  return sum / norm;
}

typedef struct {
  f32 edge;   // distance to the nearest cell border, blocks
  f32 ox, oy; // offset from the seed in cell radii
  u32 id;     // the cell's hash
} VxCell;

// tileable jittered voronoi with nx by ny cells across the tile
hz_internal VxCell vx_cells(const VxTile *t, f32 bx, f32 by, i32 nx, i32 ny, f32 jit, u32 s) {
  f32 cw = 2.0f / (f32)nx, ch = 2.0f / (f32)ny;
  i32 ix = (i32)m_floorf(bx / cw), iy = (i32)m_floorf(by / ch);
  f32 best = 1e30f, sx = 0, sy = 0;
  i32 bi = ix, bj = iy;
  for (i32 j = -1; j <= 1; j++)
    for (i32 i = -1; i <= 1; i++) {
      i32 cx = ix + i, cy = iy + j;
      i32 wx = ((cx % nx) + nx) % nx, wy = ((cy % ny) + ny) % ny;
      f32 px = ((f32)cx + 0.5f + (vx_h(t, wx, wy, s) - 0.5f) * jit) * cw;
      f32 py = ((f32)cy + 0.5f + (vx_h(t, wx, wy, s + 1) - 0.5f) * jit) * ch;
      f32 d = (bx - px) * (bx - px) + (by - py) * (by - py);
      if (d < best) best = d, sx = px, sy = py, bi = cx, bj = cy;
    }
  f32 edge = 1e30f;
  for (i32 j = -2; j <= 2; j++)
    for (i32 i = -2; i <= 2; i++) {
      i32 cx = bi + i, cy = bj + j;
      if (i == 0 && j == 0) continue;
      i32 wx = ((cx % nx) + nx) % nx, wy = ((cy % ny) + ny) % ny;
      f32 px = ((f32)cx + 0.5f + (vx_h(t, wx, wy, s) - 0.5f) * jit) * cw;
      f32 py = ((f32)cy + 0.5f + (vx_h(t, wx, wy, s + 1) - 0.5f) * jit) * ch;
      f32 dx = px - sx, dy = py - sy, len = m_sqrtf(dx * dx + dy * dy);
      if (len < 1e-6f) continue;
      f32 e = -((bx - (sx + px) * 0.5f) * dx + (by - (sy + py) * 0.5f) * dy) / len;
      edge = m_minf(edge, e);
    }
  i32 wx = ((bi % nx) + nx) % nx, wy = ((bj % ny) + ny) % ny;
  return (VxCell){.edge = edge, .ox = (bx - sx) / (cw * 0.5f), .oy = (by - sy) / (ch * 0.5f),
                  .id = vx_hash3i(wx, wy, (i32)s, t->seed)};
}

force_inline f32 vx_hid(u32 id, u32 k) { return (f32)(vx_hash3i((i32)id, (i32)k, 7, 0x51ed27u) >> 8) / 16777216.0f; }

// light on a raised shape from the top left, -1..1, from the offset to its centre in radii
force_inline f32 vx_dome(f32 ox, f32 oy) { return m_clampf(-(ox * 0.6f + oy * 0.8f), -1.0f, 1.0f); }

// a painted feature: tone on the ramp lifted by its light, with dark line work where edge < w
hz_internal VxCol vx_feature(const VxRamp *r, f32 tone, f32 light, f32 edge, f32 w, VxCol line) {
  VxCol c = vx_rampf(r, tone + light * 0.2f);
  return vx_mixc(c, line, vx_line(edge, w));
}

// a box feature's light from its edge distances (left, right, top, bottom): lit top-left rims, shaded bottom-right
force_inline f32 vx_bevel(f32 dl, f32 dr, f32 dt, f32 db, f32 bw) {
  f32 hi = 1.0f - m_smoothstepf(0.0f, bw, m_minf(dt, dl));
  f32 lo = 1.0f - m_smoothstepf(0.0f, bw, m_minf(db, dr));
  return hi - lo;
}

// distance from p to the segment ab
hz_internal f32 vx_seg(f32 px, f32 py, f32 ax, f32 ay, f32 bx, f32 by) {
  f32 vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  f32 k = m_saturatef((wx * vx + wy * vy) / m_maxf(vx * vx + vy * vy, 1e-9f));
  f32 dx = wx - vx * k, dy = wy - vy * k;
  return m_sqrtf(dx * dx + dy * dy);
}

// ---- material palettes ----

hz_internal const u32 VX_DIRT[2] = {0xA0704C, 0x5A3826};
hz_internal const u32 VX_STONE[2] = {0xACA49E, 0x5A534E};
hz_internal const u32 VX_GRASS[2] = {0x5ABA5C, 0x1C6236};
hz_internal const u32 VX_MUD[2] = {0x6E4C46, 0x34242A};

// ---- terrain painters ----

hz_internal void vx_paint_dirt(VxTile *t, const VxRamp *r) {
  VxCol line = r->s[0];
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 tone = 0.42f + (vx_fbm(t, bx, by, 4, 3, 10) - 0.5f) * 0.45f;
      VxCol c = vx_rampf(r, tone);
      // pebbles: round stones set in the soil, a darker rim and their own light
      VxCell p = vx_cells(t, bx, by, 12, 12, 0.8f, 11);
      f32 pr = 0.3f + 0.2f * vx_hid(p.id, 1), pd = m_sqrtf(p.ox * p.ox + p.oy * p.oy);
      if (vx_hid(p.id, 0) < 0.26f) {
        f32 cov = vx_inside((pd - pr) * (1.0f / 12.0f));
        VxCol pc = vx_rampf(r, 0.3f + vx_hid(p.id, 2) * 0.35f + vx_dome(p.ox / pr, p.oy / pr) * 0.18f);
        pc = vx_mixc(pc, line, vx_line((pr - pd) * (1.0f / 12.0f), 0.012f) * 0.8f);
        c = vx_mixc(c, pc, cov);
      }
      VxCell s = vx_cells(t, bx, by, 28, 28, 0.9f, 12);
      if (vx_hid(s.id, 0) < 0.2f) c = vx_mixc(c, r->s[1], vx_inside((m_sqrtf(s.ox * s.ox + s.oy * s.oy) - 0.3f) / 28.0f) * 0.7f);
      vx_set(t, x, y, c);
    }
}

hz_internal void vx_paint_grass_top(VxTile *t, const VxRamp *g) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 tone = 0.54f + (vx_fbm(t, bx, by, 4, 3, 20) - 0.5f) * 0.4f;
      VxCol c = vx_rampf(g, tone);
      // short flecks of lighter and darker blades lying in the turf
      VxCell f = vx_cells(t, bx, by, 26, 26, 0.9f, 21);
      f32 ang = vx_hid(f.id, 3) * 3.14159265f, ca = m_cosf(ang), sa = m_sinf(ang);
      f32 u = f.ox * ca + f.oy * sa, v = -f.ox * sa + f.oy * ca;
      f32 e = (u * u) / (0.5f * 0.5f) + (v * v) / (0.13f * 0.13f);
      f32 k = vx_hid(f.id, 0);
      if (k < 0.7f) c = vx_mixc(c, vx_rampf(g, tone + (k < 0.42f ? 0.3f : -0.34f)), vx_inside((e - 1.0f) * 0.02f));
      vx_set(t, x, y, c);
    }
}

// the grass cap's blades hang over the side of every block (the fringe) above the soil's own painting
hz_internal void vx_paint_grass_side(VxTile *t, const VxRamp *g, const VxRamp *soil, u32 soil_tex_seed) {
  u32 seed = t->seed;
  t->seed = soil_tex_seed;
  vx_paint_dirt(t, soil);
  t->seed = seed;
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y), ly = m_fracf(by);
      // the grass hangs most of the way down, so one-block terraces read as darker green bands, as in the reference
      f32 depth = 0.58f + 0.12f * vx_vn(t, bx, 0.0f, 20, 1, 23);
      i32 dc = (i32)m_floorf(bx * 6.0f);
      f32 dh = vx_h(t, dc % 12, 0, 24), du = m_fracf(bx * 6.0f) - 0.5f;
      if (dh < 0.45f) depth = m_maxf(depth, 0.66f + dh * 0.4f - du * du * 0.5f);
      u32 i = vx_ti(x, y);
      if (ly < depth + 0.06f && ly >= depth) t->c[i] = vx_mixc(t->c[i], soil->s[0], (1.0f - (ly - depth) / 0.06f) * 0.45f);
      f32 cov = vx_inside(ly - depth);
      if (cov <= 0) continue;
      f32 tone = 0.56f + (vx_fbm(t, bx, by, 8, 2, 25) - 0.5f) * 0.2f - ly * 0.4f;
      VxCol c = vx_rampf(g, tone + (ly < 0.035f ? 0.2f : 0.0f));
      c = vx_mixc(c, g->s[0], vx_line(depth - ly, 0.016f));
      t->c[i] = vx_mixc(t->c[i], c, cov);
    }
}

// rounded stones with dark joints, the cobble look of Terraria's stone at block scale
hz_internal void vx_paint_stones(VxTile *t, const VxRamp *r, i32 n, f32 joint, u32 s) {
  VxCol line = r->s[0];
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, n, n, 0.85f, s);
      f32 tone = 0.36f + vx_hid(c.id, 0) * 0.3f + (vx_fbm(t, bx, by, 8, 2, s + 5) - 0.5f) * 0.14f;
      f32 light = vx_dome(c.ox, c.oy) * m_smoothstepf(0.0f, 0.06f, c.edge) - 0.6f * (1.0f - m_smoothstepf(0.0f, 0.05f, c.edge));
      vx_set(t, x, y, vx_feature(r, tone, light, c.edge, joint, line));
    }
}

hz_internal void vx_paint_sand(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 tone = 0.6f + (vx_fbm(t, bx, by, 4, 4, 40) - 0.5f) * 0.22f;
      VxCell g = vx_cells(t, bx, by, 36, 36, 0.9f, 41);
      f32 k = vx_hid(g.id, 0), d = m_sqrtf(g.ox * g.ox + g.oy * g.oy);
      if (k < 0.3f) tone += (k < 0.12f ? 0.25f : -0.18f) * vx_inside((d - 0.35f) / 36.0f);
      vx_set(t, x, y, vx_rampf(r, tone));
    }
}

// layered sandstone: four wavy courses per block, each its own tone, split by fine dark seams
hz_internal void vx_paint_sandstone(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 w = by * 4.0f + (vx_vn(t, bx, by, 6, 2, 50) - 0.5f) * 0.5f;
      i32 row = (i32)m_floorf(w);
      f32 f = w - (f32)row;
      f32 edge = m_minf(f, 1.0f - f) * 0.25f;
      f32 tone = 0.42f + vx_h(t, ((row % 8) + 8) % 8, 0, 51) * 0.3f + (vx_fbm(t, bx, by, 8, 2, 52) - 0.5f) * 0.12f;
      f32 light = (1.0f - m_smoothstepf(0.0f, 0.12f, f)) - (1.0f - m_smoothstepf(0.0f, 0.12f, 1.0f - f)) * 0.6f;
      vx_set(t, x, y, vx_feature(r, tone, light, edge, 0.012f, r->s[0]));
    }
}

// the desert buttes' caps: wind-worn tan sandstone, a few rust flecks; variants break the repeat
hz_internal void vx_paint_mesa_top(VxTile *t, const VxRamp *tan, const VxRamp *red) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 tone = 0.62f + (vx_fbm(t, bx, by, 4, 3, 400) - 0.5f) * 0.22f;
      VxCell g = vx_cells(t, bx, by, 24, 24, 0.9f, 401);
      f32 k = vx_hid(g.id, 0), d = m_sqrtf(g.ox * g.ox + g.oy * g.oy);
      VxCol c = vx_rampf(tan, tone);
      if (k < 0.08f && d < 0.5f) c = vx_rampf(red, 0.45f + k * 3.0f);
      vx_set(t, x, y, c);
    }
}

// a butte tier's rim: rust-red sandstone in two rough courses
hz_internal void vx_paint_mesa_side(VxTile *t, const VxRamp *cream) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 w = by * 2.0f + (vx_vn(t, bx, by, 5, 2, 410) - 0.5f) * 0.5f;
      i32 row = (i32)m_floorf(w);
      f32 f = w - (f32)row;
      f32 edge = m_minf(f, 1.0f - f) * 0.33f;
      const VxRamp *r = cream;
      f32 tone = 0.55f + vx_h(t, ((row % 6) + 6) % 6, 0, 411) * 0.2f + (vx_fbm(t, bx, by, 6, 3, 412) - 0.5f) * 0.3f;
      f32 light = (1.0f - m_smoothstepf(0.0f, 0.12f, f)) - (1.0f - m_smoothstepf(0.0f, 0.12f, 1.0f - f)) * 0.6f;
      vx_set(t, x, y, vx_feature(r, tone, light, edge, 0.012f, r->s[0]));
    }
}

// the buttes' walls: tan sandstone courses pitted with rows of dark wind-cut hollows
hz_internal void vx_paint_butte(VxTile *t, const VxRamp *r) {
  VxCol hole = vx_hex(0x5A2E20), lip = vx_hex(0x9A4A30), rim = vx_hex(0xE2CC9C);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 w = by + (vx_vn(t, bx, by, 5, 2, 420) - 0.5f) * 0.15f;
      i32 row = (i32)m_floorf(w);
      f32 f = w - (f32)row;
      f32 edge = m_minf(f, 1.0f - f) * 0.5f;
      f32 tone = 0.5f + vx_h(t, ((row % 2) + 2) % 2, 0, 421) * 0.18f + (vx_fbm(t, bx, by, 6, 3, 422) - 0.5f) * 0.22f;
      f32 light = (1.0f - m_smoothstepf(0.0f, 0.06f, f)) * 0.4f - (1.0f - m_smoothstepf(0.0f, 0.06f, 1.0f - f)) * 0.3f;
      VxCol c = vx_feature(r, tone, light, edge, 0.01f, r->s[1]);
      // a hollow: a short dark slot in the middle of a course, its lower lip catching the light
      f32 u = bx + vx_h(t, ((row % 2) + 2) % 2, 1, 423) * 2.0f;
      i32 slot = (i32)m_floorf(u);
      f32 su = u - (f32)slot;
      u32 sh = vx_hash3i(((slot % 2) + 2) % 2, ((row % 2) + 2) % 2, 424, t->seed);
      f32 len = 0.45f + (f32)((sh >> 4) & 15u) / 15.0f * 0.35f;
      f32 hf = f - (f32)((sh >> 8) & 3u) * 0.07f + 0.1f;
      if ((sh & 7u) < 3u && su > 0.5f - len * 0.5f && su < 0.5f + len * 0.5f) {
        if (hf > 0.3f && hf < 0.62f) c = (su < 0.5f - len * 0.4f || hf > 0.56f) ? lip : hole;
        else if (hf >= 0.62f && hf < 0.7f) c = rim;
      }
      vx_set(t, x, y, c);
    }
}

// the sand's step faces: deeper ochre than the tops, a dark lip under each block's top edge, so terraces read as contour lines
hz_internal void vx_paint_sand_side(VxTile *t, const VxRamp *r) {
  VxCol lip = vx_hex(0x7A5230);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 fy = m_fracf(by);
      f32 tone = 0.55f + (vx_fbm(t, bx, by, 4, 4, 430) - 0.5f) * 0.2f - fy * 0.1f;
      VxCol c = vx_rampf(r, tone);
      if (fy < 0.05f) c = vx_mixc(c, lip, 0.85f);
      else if (fy < 0.075f) c = vx_mixc(c, lip, 0.3f);
      vx_set(t, x, y, c);
    }
}

hz_internal void vx_paint_snow(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 n = vx_fbm(t, bx, by, 4, 3, 60);
      // soft drifts: lit crests, blue hollows
      f32 tone = 0.72f + (n - 0.5f) * 0.45f;
      vx_set(t, x, y, vx_rampf(r, tone));
      if (vx_h(t, x, y, 61) < 0.004f) vx_glow(t, x, y, 0.0f, 1.0f);
    }
}

hz_internal void vx_paint_ice(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 4, 4, 0.9f, 70);
      f32 tone = 0.45f + vx_hid(c.id, 0) * 0.25f + (vx_fbm(t, bx, by, 4, 2, 71) - 0.5f) * 0.15f;
      // pale fracture lines between the plates and diagonal glints across them
      VxCol col = vx_rampf(r, tone);
      col = vx_mixc(col, r->s[4], vx_line(c.edge, 0.01f) * 0.8f);
      f32 diag = m_fracf((bx + by) * 1.5f + vx_hid(c.id, 1));
      f32 streak = vx_line(m_absf(diag - 0.5f) * 0.66f, 0.012f) * m_smoothstepf(0.0f, 0.1f, c.edge);
      col = vx_mixc(col, r->s[4], streak * 0.7f);
      vx_set(t, x, y, col);
      if (streak > 0.5f) vx_glow(t, x, y, 0.0f, 0.5f);
    }
}

// aetherium: pale lilac stone cracked into big rounded cells with dark violet seams
hz_internal void vx_paint_aetherium(VxTile *t, const VxRamp *r) {
  VxCol seam = vx_hex(0x4A2E6E);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 5, 5, 0.85f, 90);
      f32 tone = 0.45f + vx_hid(c.id, 0) * 0.25f + (vx_fbm(t, bx, by, 6, 2, 91) - 0.5f) * 0.12f;
      VxCol col = vx_rampf(r, tone + vx_dome(c.ox, c.oy) * 0.35f);
      col = vx_mixc(col, seam, vx_line(c.edge, 0.018f));
      vx_set(t, x, y, col);
    }
}

hz_internal void vx_paint_clay(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 6, 8, 0.7f, 80);
      f32 tone = 0.4f + vx_hid(c.id, 0) * 0.22f + (vx_fbm(t, bx, by, 4, 3, 81) - 0.5f) * 0.2f;
      f32 light = vx_dome(c.ox, c.oy) * 0.6f;
      vx_set(t, x, y, vx_feature(r, tone, light, c.edge, 0.01f, vx_mixc(r->s[0], r->s[1], 0.5f)));
    }
}

hz_internal void vx_paint_mud(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 n = vx_fbm(t, bx, by, 4, 3, 90);
      VxCell c = vx_cells(t, bx, by, 8, 8, 0.9f, 91);
      f32 tone = 0.38f + (n - 0.5f) * 0.4f + vx_dome(c.ox, c.oy) * 0.08f;
      VxCol col = vx_mixc(vx_rampf(r, tone), r->s[0], vx_line(c.edge, 0.008f) * 0.5f);
      vx_set(t, x, y, col);
      VxCell g = vx_cells(t, bx, by, 20, 20, 0.9f, 92);
      if (vx_hid(g.id, 0) < 0.12f && g.ox * g.ox + g.oy * g.oy < 0.06f) {
        vx_set(t, x, y, r->s[3]);
        vx_glow(t, x, y, 0.0f, 0.6f);
      }
    }
}

// dark rock cells, the joints of some glowing as cracks of ore
hz_internal void vx_paint_hellstone(VxTile *t, const VxRamp *rock, const VxRamp *glow) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 5, 5, 0.9f, 100);
      f32 tone = 0.3f + vx_hid(c.id, 0) * 0.3f + vx_dome(c.ox, c.oy) * 0.1f;
      VxCol col = vx_rampf(rock, tone);
      // clusters of fine glowing cracks, a web of small cells inside a blot
      VxCell f = vx_cells(t, bx, by, 16, 16, 0.9f, 103);
      f32 vein = vx_smoothstep(0.76f, 0.84f, vx_vn(t, bx, by, 3, 3, 104)) * vx_smoothstep(0.35f, 0.5f, vx_vn(t, bx, by, 10, 10, 105));
      f32 hot = vx_line(f.edge, 0.016f + 0.008f * vx_vn(t, bx, by, 16, 16, 101)) * vein;
      f32 core = vx_line(f.edge, 0.006f) * vein;
      col = vx_mixc(col, glow->s[2], hot);
      col = vx_mixc(col, glow->s[4], core);
      vx_set(t, x, y, col);
      vx_glow(t, x, y, hot * 0.7f + core * 0.3f, 0.0f);
    }
}

// dungeon brick: square tiles in stack bond, two a block each way, in dark joints; the last variant is one panel a block with a raised diamond
hz_internal void vx_paint_dungeon(VxTile *t, const VxRamp *r) {
  VxCol joint = vx_hex(0x061210);
  b32 panel = t->var == 3;
  f32 n = panel ? 1.0f : 2.0f, s = 1.0f / n;
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      i32 cx = (i32)m_floorf(bx * n), cy = (i32)m_floorf(by * n);
      f32 fx = bx - (f32)cx * s, fy = by - (f32)cy * s;
      f32 dl = fx, dr = s - fx, dt = fy, db = s - fy;
      f32 edge = m_minf(m_minf(dl, dr), m_minf(dt, db));
      u32 id = vx_hash3i(cx, cy, 151, t->seed + t->var * 7919u);
      f32 tone = 0.42f + vx_hid(id, 0) * 0.22f + (vx_fbm(t, bx, by, 8, 2, 152) - 0.5f) * 0.1f;
      f32 light = vx_bevel(dl, dr, dt, db, 0.04f);
      if (panel) {
        f32 dm = m_absf(fx - 0.5f) + m_absf(fy - 0.5f);
        light += vx_line(m_absf(dm - 0.26f), 0.02f) * (fx + fy < 1.0f ? 0.8f : -0.8f);
        if (dm < 0.26f) tone += 0.06f;
      }
      vx_set(t, x, y, vx_feature(r, tone, light, edge, 0.022f, joint));
    }
}

// a cobweb: spokes from its hub crossed by polygonal rings, pale and thin, fading at its reach
hz_internal void vx_paint_cobweb(VxTile *t) {
  vx_clear(t);
  VxCol silk = vx_hex(0xE4E2DA);
  f32 seg = (f32)M_PI * 2.0f / 9.0f;
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)) - 0.5f, ly = m_fracf(vx_bc(y)) - 0.45f;
      f32 rr = m_sqrtf(lx * lx + ly * ly);
      if (rr > 0.5f) continue;
      f32 w = m_atan2f(ly, lx) / seg, k = w - m_floorf(w) - 0.5f;
      f32 spoke = vx_line(m_absf(k) * seg * rr, VX_PX * 0.8f);
      f32 ring_r = rr * m_cosf(k * seg);
      f32 ring = ring_r > 0.05f ? vx_line(m_absf(m_fracf(ring_r / 0.09f + 0.5f) - 0.5f) * 0.09f, VX_PX * 0.7f) : 0.0f;
      f32 cov = m_maxf(spoke, ring) * (1.0f - m_smoothstepf(0.4f, 0.5f, rr));
      if (cov <= 0.02f) continue;
      u32 i = vx_ti(x, y);
      t->c[i] = silk;
      t->a[i] = cov;
    }
}

// glassy black facets with pale rims
hz_internal void vx_paint_obsidian(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 5, 5, 1.0f, 110);
      f32 tone = 0.2f + vx_hid(c.id, 0) * 0.35f + (c.ox * vx_hid(c.id, 1) - c.oy * 0.5f) * 0.12f;
      VxCol col = vx_rampf(r, tone);
      col = vx_mixc(col, r->s[3], vx_line(c.edge, 0.008f) * 0.7f);
      vx_set(t, x, y, col);
      if (vx_hid(c.id, 2) < 0.4f && c.ox * c.ox + c.oy * c.oy < 0.01f) vx_glow(t, x, y, 0.0f, 1.0f);
    }
}

// running-bond masonry: rows per block, bricks len blocks long, half-offset every other row
hz_internal void vx_paint_bricks(VxTile *t, const VxRamp *r, const VxRamp *mortar, i32 rows, f32 len, f32 joint,
                                 f32 *glow_k) {
  VxCol line = mortar->s[1];
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 fy = by * (f32)rows;
      i32 row = (i32)m_floorf(fy);
      f32 u = bx + ((row & 1) ? len * 0.5f : 0.0f);
      i32 col = (i32)m_floorf(u / len);
      f32 lx = (u / len - (f32)col) * len, ly = (fy - (f32)row) / (f32)rows;
      f32 h = 1.0f / (f32)rows;
      f32 dl = lx, dr = len - lx, dt = ly, db = h - ly;
      f32 edge = m_minf(m_minf(dl, dr), m_minf(dt, db));
      i32 per = (i32)m_roundf(2.0f / len);
      u32 id = vx_hash3i(((col % per) + per) % per, ((row % (rows * 2)) + rows * 2) % (rows * 2), 141, t->seed);
      f32 tone = 0.4f + vx_hid(id, 0) * 0.24f + (vx_fbm(t, bx, by, 8, 2, 142) - 0.5f) * 0.14f;
      f32 light = vx_bevel(dl, dr, dt, db, 0.035f);
      vx_set(t, x, y, vx_feature(r, tone, light, edge, joint, line));
      if (glow_k) {
        f32 g = vx_line(edge, joint);
        if (g > 0) {
          t->c[vx_ti(x, y)] = vx_mixc(t->c[vx_ti(x, y)], mortar->s[4], g);
          vx_glow(t, x, y, g * *glow_k, 0.0f);
        }
      }
    }
}

// honeycomb: hexagonal wax cells, each holding a glossy drop of honey
hz_internal void vx_paint_hive(VxTile *t, const VxRamp *wax, const VxRamp *honey) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      // hex lattice by voronoi over a regular staggered grid
      VxCell c = vx_cells(t, bx + (((i32)m_floorf(by * 3.0f)) & 1 ? 1.0f / 6.0f : 0.0f), by, 6, 6, 0.0f, 150);
      f32 tone = 0.45f + vx_hid(c.id, 0) * 0.25f + vx_dome(c.ox, c.oy) * 0.3f;
      VxCol col = vx_rampf(honey, tone);
      col = vx_mixc(col, wax->s[1], vx_line(c.edge, 0.03f));
      col = vx_mixc(col, wax->s[0], vx_line(c.edge, 0.012f));
      vx_set(t, x, y, col);
      if (vx_dome(c.ox, c.oy) > 0.75f && c.edge > 0.05f) vx_glow(t, x, y, 0.0f, 0.4f);
    }
}

// bark: deep vertical grooves between rounded ridges, with knots
hz_internal void vx_paint_log_side(VxTile *t, const VxRamp *r, b32 segmented) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 n = vx_vn(t, bx, by, 14, 3, 120) * 0.7f + vx_vn(t, bx, by, 28, 6, 121) * 0.3f;
      f32 ridge = m_fracf(bx * 5.0f + (n - 0.5f) * 0.8f);
      f32 groove = m_minf(ridge, 1.0f - ridge) / 5.0f;
      f32 tone = 0.35f + n * 0.35f + (ridge - 0.5f) * -0.2f;
      VxCol col = vx_feature(r, tone, 0.0f, groove, 0.014f, r->s[0]);
      if (segmented) {
        f32 ring = m_fracf(by * 2.0f + 0.1f);
        f32 d = m_minf(ring, 1.0f - ring) * 0.5f;
        col = vx_mixc(col, r->s[0], vx_line(d, 0.012f));
        col = vx_mixc(col, r->s[4], vx_line(m_absf(ring - 0.06f) * 0.5f, 0.008f) * 0.6f);
      }
      VxCell k = vx_cells(t, bx, by, 3, 4, 0.8f, 122);
      f32 kd = m_sqrtf(k.ox * k.ox * 2.2f + k.oy * k.oy);
      if (vx_hid(k.id, 0) < 0.35f && kd < 0.35f) {
        f32 ring = m_fracf(kd * 7.0f);
        col = vx_mixc(r->s[1], r->s[0], ring < 0.4f ? 0.7f : 0.0f);
      }
      vx_set(t, x, y, col);
    }
}

// a cut trunk seen from its end: growth rings inside a bark rim, per block
hz_internal void vx_paint_log_top(VxTile *t, const VxRamp *bark, const VxRamp *wood) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 dx = m_fracf(bx) - 0.5f, dy = m_fracf(by) - 0.5f;
      f32 sq = m_maxf(m_absf(dx), m_absf(dy));
      f32 d = m_sqrtf(dx * dx + dy * dy) * 0.6f + sq * 0.4f + (vx_vn(t, bx, by, 12, 12, 125) - 0.5f) * 0.03f;
      f32 ring = m_fracf(d * 14.0f);
      VxCol col = vx_rampf(wood, 0.55f - d * 0.5f + (ring < 0.25f ? -0.18f : 0.0f));
      f32 rim = 0.5f - sq;
      col = vx_mixc(col, wood->s[0], vx_line(m_absf(rim - 0.085f), 0.01f));
      if (rim < 0.085f) col = vx_rampf(bark, 0.35f + vx_vn(t, bx, by, 24, 24, 126) * 0.3f);
      vx_set(t, x, y, col);
    }
}

// horizontal boards, four per block, joints staggered across the 2-block span, nails beside each joint
hz_internal void vx_paint_planks(VxTile *t, const VxRamp *r) {
  VxCol line = r->s[0];
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      i32 row = (i32)m_floorf(by * 4.0f);
      f32 ly = by * 4.0f - (f32)row;
      // two joints per row inside the 2-block period
      f32 j0 = vx_h(t, row, 0, 130) * 2.0f, j1 = m_fmodf(j0 + 0.75f + vx_h(t, row, 1, 130) * 0.5f, 2.0f);
      f32 d0 = m_fmodf(bx - j0 + 4.0f, 2.0f), d1 = m_fmodf(bx - j1 + 4.0f, 2.0f);
      f32 dl = m_minf(d0, d1), dr = m_minf(2.0f - d0, 2.0f - d1);
      u32 seg = d0 < d1 ? 0u : 1u;
      f32 dt = ly * 0.25f, db = (1.0f - ly) * 0.25f;
      f32 edge = m_minf(m_minf(dl, dr), m_minf(dt, db));
      f32 grain = vx_vn(t, bx, by, 3, 64, 131);
      f32 tone = 0.45f + vx_h(t, row, (i32)seg + 2, 132) * 0.18f + (grain - 0.5f) * 0.18f;
      f32 light = vx_bevel(dl, dr, dt, db, 0.04f);
      VxCol col = vx_feature(r, tone, light, edge, 0.012f, line);
      // a nail near each end of a board
      f32 nd = m_minf(m_absf(dl - 0.07f), m_absf(dr - 0.07f));
      f32 nail = m_sqrtf(nd * nd + (ly - 0.5f) * (ly - 0.5f) * 0.0625f);
      col = vx_mixc(col, line, vx_inside(nail - 0.016f) * 0.85f);
      vx_set(t, x, y, col);
    }
}

// leafy canopy: soft mottled green made of small overlapping leaves, low contrast, no line work (the reference's canopies)
hz_internal void vx_paint_leaves(VxTile *t, const VxRamp *r, b32 frond) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 tone = 0.42f + (vx_fbm(t, bx, by, 4, 3, 150) - 0.5f) * 0.45f;
      // two layers of small leaves: each a lit upper edge and a shaded lower one
      for (i32 k = 0; k < 2; k++) {
        VxCell l = vx_cells(t, bx, by, 18 + k * 8, 18 + k * 8, 0.95f, 151 + (u32)k);
        f32 ang = vx_hid(l.id, 2) * 3.14159265f, ca = m_cosf(ang), sa = m_sinf(ang);
        f32 u = l.ox * ca + l.oy * sa, v = -l.ox * sa + l.oy * ca;
        f32 e = u * u / 0.42f + v * v / 0.16f;
        f32 cov = vx_inside((e - 1.0f) * 0.02f) * (vx_hid(l.id, 0) < 0.7f ? 1.0f : 0.0f);
        tone += cov * (vx_dome(l.ox, l.oy) * 0.24f + (vx_hid(l.id, 1) - 0.5f) * 0.2f);
      }
      if (frond) tone += m_fracf((bx - by) * 4.0f) < 0.15f ? -0.15f : 0.0f;
      vx_set(t, x, y, vx_rampf(r, tone));
    }
}

hz_internal void vx_paint_cactus(VxTile *t, const VxRamp *r, b32 top) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y), lx = m_fracf(bx), ly = m_fracf(by);
      VxCol col;
      if (top) {
        f32 dx = lx - 0.5f, dy = ly - 0.5f, d = m_sqrtf(dx * dx + dy * dy);
        f32 rib = m_fracf(m_atan2f(dy, dx) / 6.2831853f * 8.0f);
        col = vx_rampf(r, 0.6f - d * 0.5f + m_sinf(rib * 3.14159265f) * 0.1f);
        col = vx_mixc(col, r->s[0], vx_line(m_minf(rib, 1.0f - rib) * d * 0.8f, 0.006f) * m_smoothstepf(0.08f, 0.2f, d));
      } else {
        f32 rib = m_fracf(lx * 4.0f);
        f32 tone = 0.35f + m_sinf(rib * 3.14159265f) * 0.3f + (vx_vn(t, bx, by, 6, 6, 160) - 0.5f) * 0.1f;
        col = vx_feature(r, tone, 0.0f, m_minf(rib, 1.0f - rib) * 0.25f, 0.008f, r->s[0]);
        // spines along the grooves
        f32 sy = m_fracf(ly * 5.0f + (f32)((i32)(lx * 4.0f) & 1) * 0.5f);
        f32 sd = m_sqrtf(m_minf(rib, 1.0f - rib) * m_minf(rib, 1.0f - rib) * 0.0625f + (sy - 0.5f) * (sy - 0.5f) * 0.04f);
        col = vx_mixc(col, vx_hex(0xF4ECC4), vx_inside(sd - 0.014f));
      }
      vx_set(t, x, y, col);
    }
}

// ore nuggets set into stone: a dark rim, the ore ramp lit from the top left and a glinting highlight
hz_internal void vx_paint_ore(VxTile *t, const VxRamp *rock, const VxRamp *ore, f32 emit) {
  vx_paint_stones(t, rock, 6, 0.016f, 30);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 9, 9, 0.8f, 170);
      if (vx_hid(c.id, 0) > 0.42f) continue;
      f32 rr = 0.42f + vx_hid(c.id, 1) * 0.25f + (vx_vn(t, bx, by, 24, 24, 171) - 0.5f) * 0.18f;
      f32 d = m_sqrtf(c.ox * c.ox + c.oy * c.oy);
      f32 cov = vx_inside((d - rr) / 9.0f);
      if (cov <= 0) continue;
      f32 light = vx_dome(c.ox / rr, c.oy / rr);
      VxCol col = vx_rampf(ore, 0.5f + light * 0.4f);
      col = vx_mixc(col, ore->s[0], vx_line((rr - d) / 9.0f, 0.01f));
      u32 i = vx_ti(x, y);
      t->c[i] = vx_mixc(t->c[i], col, cov);
      f32 g = light > 0.7f ? 1.0f : 0.0f;
      vx_glow(t, x, y, emit * cov, g * cov);
    }
}

// a faceted gem in every block, set into stone
hz_internal void vx_paint_gem(VxTile *t, const VxRamp *rock, const VxRamp *gem) {
  vx_paint_stones(t, rock, 6, 0.016f, 30);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      i32 bi = (i32)m_floorf(bx), bj = (i32)m_floorf(by);
      for (i32 k = 0; k < 2; k++) {
        f32 cx = (f32)bi + 0.25f + vx_h(t, bi, bj * 2 + k, 181) * 0.5f, cy = (f32)bj + 0.25f + vx_h(t, bi + 7, bj * 2 + k, 181) * 0.5f;
        f32 sz = k == 0 ? 0.15f : 0.08f;
        f32 dx = bx - cx, dy = by - cy;
        f32 sd = (m_absf(dx) + m_absf(dy) * 0.8f) - sz;
        f32 cov = vx_inside(sd);
        if (cov <= 0) continue;
        // four facets: lit top-left, bright top-right, mid bottom-left, dark bottom-right
        f32 tone = dy < 0 ? (dx < 0 ? 0.75f : 0.95f) : (dx < 0 ? 0.5f : 0.25f);
        VxCol col = vx_mixc(vx_rampf(gem, tone), gem->s[0], vx_line(-sd, 0.01f));
        u32 i = vx_ti(x, y);
        t->c[i] = vx_mixc(t->c[i], col, cov);
        vx_glow(t, x, y, 0.35f * cov, dy < 0 && dx > 0 ? cov : 0.0f);
      }
    }
}

hz_internal void vx_paint_cloud(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 5, 5, 0.9f, 190);
      f32 tone = 0.72f + vx_dome(c.ox, c.oy) * 0.18f + (vx_fbm(t, bx, by, 4, 3, 191) - 0.5f) * 0.15f;
      tone -= (1.0f - m_smoothstepf(0.0f, 0.08f, c.edge)) * 0.1f;
      vx_set(t, x, y, vx_rampf(r, tone));
      // cloud scatters light into its shaded side, so it never goes grey
      vx_glow(t, x, y, 0.3f, 0.0f);
    }
}

// gold plates: a bevelled border and a raised boss in every block
hz_internal void vx_paint_sunplate(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      f32 dl = lx, dr = 1.0f - lx, dt = ly, db = 1.0f - ly;
      f32 edge = m_minf(m_minf(dl, dr), m_minf(dt, db));
      f32 ix = m_absf(lx - 0.5f), iy = m_absf(ly - 0.5f), inner = m_maxf(ix, iy);
      f32 tone = 0.5f + vx_bevel(dl, dr, dt, db, 0.08f) * 0.6f;
      tone += (inner < 0.2f ? vx_dome((lx - 0.5f) / 0.2f, (ly - 0.5f) / 0.2f) * 0.5f : 0.0f);
      VxCol col = vx_feature(r, tone, 0.0f, edge, 0.015f, r->s[0]);
      col = vx_mixc(col, r->s[0], vx_line(m_absf(inner - 0.2f), 0.008f));
      vx_set(t, x, y, col);
      if (inner < 0.06f) vx_glow(t, x, y, 0.0f, 1.0f);
    }
}

// a window pane per block: a light frame, clear glass and two white glare streaks
hz_internal void vx_paint_glass(VxTile *t, const VxRamp *r) {
  vx_clear(t);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      f32 edge = m_minf(m_minf(lx, 1.0f - lx), m_minf(ly, 1.0f - ly));
      f32 frame = vx_inside(edge - 0.06f);
      f32 s1 = vx_line(m_absf(lx + ly - 0.95f) * 0.7071f, 0.02f), s2 = vx_line(m_absf(lx + ly - 1.2f) * 0.7071f, 0.012f);
      f32 streak = m_maxf(s1, s2) * m_smoothstepf(0.1f, 0.14f, edge);
      f32 a = m_maxf(frame, streak);
      if (a <= 0.02f) continue;
      VxCol col = frame > 0 ? vx_rampf(r, lx < 0.06f || ly < 0.06f ? 0.75f : 0.35f) : r->s[4];
      col = vx_mixc(col, r->s[0], vx_line(m_absf(edge - 0.06f), 0.006f));
      u32 i = vx_ti(x, y);
      t->c[i] = col;
      t->a[i] = a;
      if (frame <= 0) vx_glow(t, x, y, 0.0f, 0.6f);
    }
}

// ---- models: block-local 16ths, matching the boxes in vx_blocks.c ----

// torch box: its sides map columns 7..9 and rows 6..16, its top rows 7..9; the flame fills rows 0..9
hz_internal void vx_paint_torch(VxTile *t, VxCol flame_hi, VxCol flame_mid, VxCol flame_lo) {
  vx_clear(t);
  VxRamp wood = vx_ramp(0xB08050, 0x5A3A22);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 gx = m_fracf(vx_bc(x)) * 16.0f, gy = m_fracf(vx_bc(y)) * 16.0f;
      if (gx < 6.5f || gx > 9.5f) continue;
      VxCol c;
      f32 e = 0;
      if (gy < 9.0f) {
        f32 k = gy / 9.0f;
        c = k < 0.3f ? vx_mixc(flame_lo, flame_mid, k / 0.3f) : vx_mixc(flame_mid, flame_hi, (k - 0.3f) / 0.7f);
        e = 1.0f;
        if (gy > 8.0f) c = vx_mixc(c, wood.s[1], 0.5f), e = 0.4f;
      } else {
        c = vx_rampf(&wood, (gx < 8.0f ? 0.7f : 0.35f) - (gy < 10.0f ? 0.3f : 0.0f));
      }
      u32 i = vx_ti(x, y);
      t->c[i] = c;
      t->a[i] = 1.0f;
      t->emit[i] = e;
    }
}

// clay pot boxes: rim rows 2-4, neck rows 4-6, body rows 6-16 with a painted band
hz_internal void vx_paint_pot(VxTile *t, const VxRamp *r, const VxRamp *band) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 gx = m_fracf(vx_bc(x)) * 16.0f, gy = m_fracf(vx_bc(y)) * 16.0f;
      // rounded body: light on the left, shade on the right
      f32 tone = 0.62f - (gx - 8.0f) / 16.0f * 0.5f;
      if (gy < 4.0f) tone = 0.8f - (gy - 2.0f) * 0.12f;
      else if (gy < 6.0f) tone = 0.22f;
      else if (gy > 14.0f) tone -= 0.2f;
      VxCol c = vx_rampf(r, tone);
      if (gy > 9.0f && gy < 11.5f) {
        f32 zig = m_absf(m_fracf(gx / 3.0f) - 0.5f) * 2.0f;
        c = vx_rampf(band, (gy - 9.0f) / 2.5f < zig ? 0.75f : 0.3f);
      }
      c = vx_mixc(c, r->s[0], vx_line(m_minf(m_absf(gy - 4.0f), m_absf(gy - 6.0f)) / 16.0f, 0.008f));
      vx_set(t, x, y, c);
    }
}

// pot from above: a dark opening inside the rim
hz_internal void vx_paint_pot_top(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)) - 0.5f, ly = m_fracf(vx_bc(y)) - 0.5f;
      f32 d = m_sqrtf(lx * lx + ly * ly);
      VxCol c = vx_rampf(r, 0.7f - (lx + ly) * 0.4f);
      c = vx_mixc(c, vx_mixc(r->s[0], vx_col(0.04f, 0.03f, 0.03f), 0.6f), vx_inside(d - 0.2f));
      vx_set(t, x, y, c);
    }
}

// chest boxes span rows 4-16: boards with metal corner bands, the lid seam at row 8 and a lock on the front
hz_internal void vx_paint_chest(VxTile *t, const VxRamp *wood, const VxRamp *metal, b32 front, b32 top) {
  vx_paint_planks(t, wood);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 gx = m_fracf(vx_bc(x)) * 16.0f, gy = m_fracf(vx_bc(y)) * 16.0f;
      u32 i = vx_ti(x, y);
      f32 band_d = m_minf(gx - 0.5f, 15.5f - gx);
      b32 band = band_d < 2.5f || (top ? (gy < 3.5f || gy > 12.5f) : (gy > 4.0f && gy < 5.2f) || gy > 15.0f);
      if (band) {
        VxCol c = vx_rampf(metal, 0.55f + (gx < 8.0f ? 0.15f : -0.1f));
        f32 rivet = m_minf(m_sqrtf((band_d - 1.2f) * (band_d - 1.2f) + (gy - 6.5f) * (gy - 6.5f)),
                           m_sqrtf((band_d - 1.2f) * (band_d - 1.2f) + (gy - 13.0f) * (gy - 13.0f)));
        c = vx_mixc(c, metal->s[4], vx_inside((rivet - 0.6f) / 16.0f));
        t->c[i] = c;
      }
      if (!top) t->c[i] = vx_mixc(t->c[i], wood->s[0], vx_line(m_absf(gy - 8.0f) / 16.0f, 0.01f));
      if (front && gx > 6.0f && gx < 10.0f && gy > 6.0f && gy < 11.0f) {
        f32 e = m_minf(m_minf(gx - 6.0f, 10.0f - gx), m_minf(gy - 6.0f, 11.0f - gy)) / 16.0f;
        t->c[i] = vx_feature(metal, 0.6f, vx_bevel(gx - 6.0f, 10.0f - gx, gy - 6.0f, 11.0f - gy, 1.0f), e, 0.008f, metal->s[0]);
      }
    }
}

// a door's repeating panel: vertical boards inside a dark frame, a crossbar at the top of every cell
hz_internal void vx_paint_door(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y), gx = m_fracf(bx) * 16.0f, gy = m_fracf(by) * 16.0f;
      f32 board = m_fracf(gx / 4.0f);
      f32 tone = 0.45f + vx_h(t, (i32)(gx / 4.0f), (i32)m_floorf(by), 150) * 0.15f + (vx_vn(t, bx, by, 4, 48, 151) - 0.5f) * 0.12f;
      f32 edge = m_minf(board, 1.0f - board) * 0.25f;
      VxCol c = vx_feature(r, tone, (board < 0.2f ? 0.5f : board > 0.8f ? -0.5f : 0.0f), edge, 0.01f, r->s[0]);
      if (gx < 2.0f || gx > 14.0f) c = vx_rampf(r, gx < 2.0f ? 0.3f : 0.15f);
      if (gy < 2.0f) c = vx_feature(r, 0.55f, gy < 0.6f ? 0.6f : 0.0f, m_minf(gy, 2.0f - gy) / 16.0f, 0.008f, r->s[0]);
      vx_set(t, x, y, c);
    }
}

// the furnace mouth: an arch in the stone with embers and flame at its foot
hz_internal void vx_paint_furnace_front(VxTile *t, const VxRamp *stone, const VxRamp *mortar) {
  vx_paint_bricks(t, stone, mortar, 4, 0.5f, 0.016f, NULL);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 gx = m_fracf(vx_bc(x)) * 16.0f, gy = m_fracf(vx_bc(y)) * 16.0f;
      f32 u = gx - 8.0f, v = gy - 10.0f;
      f32 sd = gy < 10.0f ? m_sqrtf(u * u + v * v * 1.6f) - 4.6f : m_absf(u) - 4.6f;
      if (gy > 16.0f || sd > 0.8f) continue;
      u32 i = vx_ti(x, y);
      if (sd > -0.4f) {
        t->c[i] = mortar->s[0];
        continue;
      }
      f32 heat = m_saturatef((gy - 8.0f) / 7.0f) * (0.8f + 0.2f * vx_vn(t, vx_bc(x), vx_bc(y), 32, 32, 152));
      VxCol c = heat > 0.7f ? vx_mixc(vx_hex(0xFF9A2E), vx_hex(0xFFE27A), (heat - 0.7f) / 0.3f)
                : heat > 0.25f ? vx_mixc(vx_hex(0xC2401A), vx_hex(0xFF9A2E), (heat - 0.25f) / 0.45f)
                               : vx_mixc(vx_hex(0x1A0C0A), vx_hex(0xC2401A), heat / 0.25f);
      t->c[i] = c;
      t->emit[i] = heat > 0.15f ? heat : 0.0f;
    }
}

// dark forged iron, lit along the top of every band
hz_internal void vx_paint_anvil(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y), gy = m_fracf(by) * 4.0f, f = m_fracf(gy);
      f32 tone = 0.35f + (1.0f - m_smoothstepf(0.0f, 0.25f, f)) * 0.35f - m_smoothstepf(0.75f, 1.0f, f) * 0.2f;
      tone += (vx_vn(t, bx, by, 16, 16, 152) - 0.5f) * 0.12f;
      vx_set(t, x, y, vx_rampf(r, tone));
    }
}

// ---- sprites: drawn once per block on clear texels (cross plants, particles) ----

// tapered grass blades rising from the block's foot, darker at the root, lit at the tip
hz_internal void vx_paint_tuft(VxTile *t, const VxRamp *r, f32 min_h, f32 max_h, i32 blades) {
  vx_clear(t);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y)), up = 1.0f - ly;
      f32 best = 0, tip = 0, edge = 1;
      for (i32 b = 0; b < blades; b++) {
        f32 h = min_h + vx_h(t, b, 1, 200) * (max_h - min_h);
        f32 s = up / h;
        if (s > 1.0f) continue;
        f32 x0 = 0.18f + 0.64f * ((f32)b + vx_h(t, b, 0, 200)) / (f32)blades;
        f32 lean = (vx_h(t, b, 2, 200) - 0.5f) * 0.5f;
        f32 cx = x0 + lean * s * s;
        f32 w = 0.05f * (1.0f - s) + 0.006f;
        f32 d = m_absf(lx - cx);
        f32 cov = vx_inside(d - w);
        if (cov > best) best = cov, tip = s, edge = (w - d) / w;
      }
      if (best <= 0.02f) continue;
      VxCol c = vx_rampf(r, 0.25f + tip * 0.6f + (edge > 0.6f ? 0.05f : -0.1f));
      u32 i = vx_ti(x, y);
      t->c[i] = c;
      t->a[i] = best;
    }
}

hz_internal void vx_paint_flowers(VxTile *t, const VxRamp *stem) {
  vx_paint_tuft(t, stem, 0.2f, 0.4f, 4);
  hz_internal const u32 petals[3] = {0xFF5A6E, 0xFFD23F, 0x8FA8FF};
  u32 pc = petals[t->var % 3];
  VxRamp p = vx_ramp(pc, (pc >> 1) & 0x7F7F7F);
  for (i32 k = 0; k < 2; k++) {
    f32 fx = 0.28f + k * 0.42f + vx_h(t, k, 0, 210) * 0.08f, fh = 0.45f + vx_h(t, k, 1, 210) * 0.2f;
    for (i32 y = 0; y < VX_TN; y++)
      for (i32 x = 0; x < VX_TN; x++) {
        f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y)), fy = 1.0f - fh;
        u32 i = vx_ti(x, y);
        f32 sd = vx_seg(lx, ly, fx, 1.0f, fx, fy) - 0.012f;
        if (vx_inside(sd) > 0.1f) {
          t->c[i] = stem->s[2];
          t->a[i] = m_maxf(t->a[i], vx_inside(sd));
        }
        // five round petals round a yellow eye
        f32 dx = lx - fx, dy = ly - fy, d = m_sqrtf(dx * dx + dy * dy);
        f32 ang = m_atan2f(dy, dx), lobe = m_cosf(ang * 5.0f) * 0.5f + 0.5f;
        f32 psd = d - (0.05f + 0.035f * lobe);
        if (vx_inside(psd) > 0.02f) {
          VxCol c = d < 0.03f ? vx_hex(0xFFE88A) : vx_rampf(&p, 0.45f + vx_dome(dx / 0.08f, dy / 0.08f) * 0.35f);
          c = vx_mixc(c, p.s[0], vx_line(-psd, 0.006f));
          t->c[i] = c;
          t->a[i] = m_maxf(t->a[i], vx_inside(psd));
        }
      }
  }
}

hz_internal void vx_paint_mushroom(VxTile *t, const VxRamp *cap, b32 glow) {
  vx_clear(t);
  VxRamp stem = vx_ramp(0xF2E8D4, 0xA89E8C);
  f32 cx = 0.4f + vx_h(t, 0, 0, 220) * 0.2f, sh = 0.18f + vx_h(t, 1, 0, 220) * 0.12f;
  f32 cw = 0.2f + vx_h(t, 2, 0, 220) * 0.06f, chh = 0.14f + vx_h(t, 3, 0, 220) * 0.06f;
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      u32 i = vx_ti(x, y);
      f32 top = 1.0f - sh;
      f32 ssd = m_maxf(m_absf(lx - cx) - 0.05f, m_maxf(top - 0.02f - ly, ly - 1.0f));
      if (vx_inside(ssd) > 0.02f) {
        t->c[i] = vx_rampf(&stem, lx < cx ? 0.75f : 0.45f);
        t->a[i] = vx_inside(ssd);
        if (glow) t->emit[i] = 0.25f;
      }
      // a domed cap: half an ellipse above the stem's top
      f32 dx = (lx - cx) / cw, dy = (ly - top) / chh;
      f32 csd = (m_sqrtf(dx * dx + dy * dy) - 1.0f) * chh;
      if (ly > top + 0.012f) csd = m_maxf(csd, ly - top - 0.012f);
      f32 cov = vx_inside(csd);
      if (cov <= 0.02f) continue;
      VxCol c = vx_rampf(cap, 0.5f + vx_dome(dx, dy) * 0.4f);
      if (!glow) {
        VxCell sp = vx_cells(t, vx_bc(x), vx_bc(y), 16, 16, 0.6f, 221);
        if (vx_hid(sp.id, 0) < 0.45f && sp.ox * sp.ox + sp.oy * sp.oy < 0.16f) c = vx_hex(0xFFF6EA);
      }
      c = vx_mixc(c, cap->s[0], vx_line(-csd, 0.008f));
      t->c[i] = c;
      t->a[i] = m_maxf(t->a[i], cov);
      if (glow) t->emit[i] = 0.9f;
    }
}

// the giant glowing mushroom's cap: blue gills in panels with glowing spots
hz_internal void vx_paint_glow_cap(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell c = vx_cells(t, bx, by, 4, 4, 0.7f, 230);
      f32 tone = 0.4f + vx_hid(c.id, 0) * 0.2f + vx_dome(c.ox, c.oy) * 0.15f;
      VxCol col = vx_feature(r, tone, 0.0f, c.edge, 0.012f, r->s[0]);
      vx_set(t, x, y, col);
      vx_glow(t, x, y, 0.3f, 0.0f);
      VxCell s = vx_cells(t, bx, by, 12, 12, 0.7f, 231);
      if (vx_hid(s.id, 0) < 0.3f && m_maxf(m_absf(s.ox), m_absf(s.oy)) < 0.32f) {
        vx_set(t, x, y, vx_hex(0xC8F4FF));
        vx_glow(t, x, y, 1.0f, 0.4f);
      }
    }
}

// the avenue's light box: a dark steel rim round a grey panel, its top a glowing pale cyan pane
hz_internal void vx_paint_light_box(VxTile *t, b32 top) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      i32 rim = MIN(MIN(x, y), MIN(VX_TN - 1 - x, VX_TN - 1 - y));
      f32 n = vx_h(t, x, y, 71) * 0.06f;
      if (rim < 2) {
        f32 k = 0.3f + n + (rim == 0 ? 0.0f : 0.08f);
        vx_set(t, x, y, vx_col(k * 0.95f, k, k * 1.08f));
      } else if (top) {
        f32 c = (f32)MIN(rim - 2, 4) / 4.0f;
        vx_set(t, x, y, vx_col(0.78f + 0.2f * c, 0.94f + 0.06f * c, 1.0f));
        vx_glow(t, x, y, 0.75f + 0.25f * c, 0.0f);
      } else {
        // a lit seam under the top edge; the rest a brushed grey panel
        b32 seam = y >= 2 && y < 4;
        f32 k = 0.52f + n - (f32)y / (f32)VX_TN * 0.12f + ((x / 2) % 2 ? 0.02f : 0.0f);
        vx_set(t, x, y, seam ? vx_hex(0xBFF2FF) : vx_col(k * 0.96f, k, k * 1.06f));
        if (seam) vx_glow(t, x, y, 0.8f, 0.0f);
      }
    }
}

// the giant mushroom's pale stalk, with glowing streaks running up it
hz_internal void vx_paint_glow_stem(VxTile *t, const VxRamp *r) {
  vx_paint_log_side(t, r, false);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      VxCell s = vx_cells(t, bx, by, 10, 5, 0.6f, 232);
      if (vx_hid(s.id, 0) < 0.35f && m_absf(s.ox) < 0.16f && m_absf(s.oy) < 0.7f) {
        vx_set(t, x, y, vx_hex(0x8FE3FF));
        vx_glow(t, x, y, 1.0f, 0.0f);
      }
    }
}

hz_internal void vx_paint_vine(VxTile *t, const VxRamp *r) {
  vx_clear(t);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      f32 best = 0, tone = 0;
      for (i32 s = 0; s < 2; s++) {
        f32 sx = 0.3f + s * 0.4f + (vx_h(t, s, 0, 240) - 0.5f) * 0.1f + m_sinf(ly * 6.2831853f + (f32)s * 2.0f) * 0.04f;
        f32 cov = vx_inside(m_absf(lx - sx) - 0.014f);
        if (cov > best) best = cov, tone = 0.3f;
        // leaves alternate down the strand
        for (i32 k = 0; k < 3; k++) {
          f32 ly0 = ((f32)k + 0.4f + (f32)s * 0.5f) / 3.0f, side = (k + s) & 1 ? 1.0f : -1.0f;
          f32 dx = (lx - sx - side * 0.06f) / 0.07f, dy = (ly - ly0) / 0.04f;
          f32 lc = vx_inside((m_sqrtf(dx * dx + dy * dy) - 1.0f) * 0.04f);
          if (lc > best) best = lc, tone = 0.5f + vx_dome(dx, dy) * 0.3f;
        }
      }
      if (best <= 0.02f) continue;
      u32 i = vx_ti(x, y);
      t->c[i] = vx_rampf(r, tone);
      t->a[i] = best;
    }
}

// red heart crystals on a rock foot, glowing from inside
hz_internal void vx_paint_life_crystal(VxTile *t) {
  vx_clear(t);
  VxRamp red = vx_ramp(0xFF7A9A, 0x9A1438);
  VxRamp rock = vx_ramp(VX_STONE[0], VX_STONE[1]);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      u32 i = vx_ti(x, y);
      f32 bsd = m_maxf(m_absf(lx - 0.5f) - 0.3f + (1.0f - ly) * 0.3f, 0.82f - ly);
      if (vx_inside(bsd) > 0.02f) {
        t->c[i] = vx_rampf(&rock, 0.4f - (lx - 0.5f) * 0.6f);
        t->a[i] = vx_inside(bsd);
      }
      for (i32 k = 0; k < 3; k++) {
        f32 cx = 0.32f + k * 0.18f, hgt = k == 1 ? 0.55f : 0.38f, w = k == 1 ? 0.11f : 0.08f;
        f32 dx = lx - cx, top = 0.84f - hgt;
        f32 sd = m_absf(dx) / w + m_absf(ly - (top + hgt * 0.45f)) / (hgt * 0.55f) - 1.0f;
        f32 cov = vx_inside(sd * w);
        if (cov <= 0.02f) continue;
        VxCol c = vx_rampf(&red, dx < 0 ? 0.85f : 0.45f);
        c = vx_mixc(c, red.s[0], vx_line(-sd * w, 0.006f));
        t->c[i] = c;
        t->a[i] = m_maxf(t->a[i], cov);
        t->emit[i] = dx < 0 ? 0.9f : 0.55f;
        t->glint[i] = dx < 0 && ly < top + hgt * 0.4f ? 1.0f : 0.0f;
      }
    }
}

hz_internal void vx_paint_liquid(VxTile *t, const VxRamp *r, f32 emit) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 n = vx_fbm(t, bx, by, 4, 3, 250);
      vx_set(t, x, y, vx_rampf(r, 0.4f + (n - 0.5f) * 0.6f));
      vx_glow(t, x, y, emit, 0.0f);
    }
}

// crack overlay per block; variants are the three break stages, each adding strokes
hz_internal void vx_paint_crack(VxTile *t) {
  vx_clear(t);
  i32 strokes = 2 + (i32)t->var * 2, steps = 4 + (i32)t->var * 2;
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 lx = m_fracf(vx_bc(x)), ly = m_fracf(vx_bc(y));
      f32 best = 1e9f;
      for (i32 s = 0; s < strokes; s++) {
        f32 px = 0.5f, py = 0.5f, ang = vx_h(t, s, 0, 260) * 6.2831853f;
        for (i32 k = 0; k < steps; k++) {
          ang += (vx_h(t, s, k + 1, 261) - 0.5f) * 1.4f;
          f32 nx = px + m_cosf(ang) * 0.08f, ny = py + m_sinf(ang) * 0.08f;
          best = m_minf(best, vx_seg(lx, ly, px, py, nx, ny));
          px = nx, py = ny;
        }
      }
      f32 cov = vx_line(best, 0.012f);
      if (cov <= 0.02f) continue;
      u32 i = vx_ti(x, y);
      t->c[i] = vx_col(0.05f, 0.04f, 0.05f);
      t->a[i] = cov;
    }
}

// particle sprites: light shapes on clear texels, coloured per particle
hz_internal void vx_paint_sprite(VxTile *t, u32 tex) {
  vx_clear(t);
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 dx = (m_fracf(vx_bc(x)) - 0.5f) * 8.0f, dy = (m_fracf(vx_bc(y)) - 0.5f) * 8.0f;
      f32 ax = m_absf(dx), ay = m_absf(dy), r = m_sqrtf(dx * dx + dy * dy);
      f32 a = 0, v = 1;
      switch (tex) {
      case T_PART_MOTE: a = 1.0f - m_smoothstepf(1.0f, 2.6f, r); break;
      case T_PART_PUFF:
        a = 1.0f - m_smoothstepf(2.4f, 3.6f, r);
        v = 0.84f + 0.16f * m_saturatef(-(dx + dy) / 6.0f + 0.5f);
        break;
      case T_PART_SPARK: a = m_maxf(1.0f - m_smoothstepf(0.6f, 1.4f, ax + ay), (1.0f - m_smoothstepf(0.25f, 0.6f, m_minf(ax, ay))) * (1.0f - m_smoothstepf(2.0f, 3.2f, ax + ay)) * 0.7f); break;
      case T_PART_FLAKE:
        a = (1.0f - m_smoothstepf(3.0f, 3.6f, r)) * (1.0f - m_smoothstepf(0.25f, 0.55f, m_minf(m_minf(ax, ay), m_absf(ax - ay))));
        a = m_maxf(a, 1.0f - m_smoothstepf(0.7f, 1.1f, r));
        break;
      case T_PART_LEAF: {
        f32 u = (dx + dy) * 0.7071f, w = (dx - dy) * 0.7071f;
        a = 1.0f - m_smoothstepf(0.85f, 1.05f, u * u / 9.0f + w * w / 2.4f);
        v = m_absf(w) < 0.35f ? 0.72f : 0.9f + 0.1f * m_clampf(-u / 3.0f, -1, 1);
      } break;
      default: break;
      }
      if (a <= 0.01f) continue;
      u32 i = vx_ti(x, y);
      t->c[i] = vx_col(v, v, v);
      t->a[i] = a;
    }
}

// near-white cloth and skin grain; the puppet's colours tint it
hz_internal void vx_paint_puppet(VxTile *t) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 v = 0.92f + 0.08f * vx_vn(t, vx_bc(x), vx_bc(y), 32, 32, 410);
      vx_set(t, x, y, vx_col(v, v, v));
    }
}

// two monster banners side by side on the grey brick wall, one per block column, each two blocks tall: a rod, a hemmed cloth and a mirrored pictogram
hz_internal void vx_paint_banner(VxTile *t) {
  VxRamp r = vx_ramp(0xB4B6BE, 0x6A6C78), r2 = vx_ramp(0x5A5C68, 0x2E3038);
  u32 var = t->var;
  // the same brick painting as the plain wall, so the stones run on past the banners
  t->seed = vx_hash_string(vx_texture_name[T_GRAY_BRICK]);
  vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.014f, NULL);
  hz_internal const u32 cloth_hex[] = {0x8A2424, 0x24388A, 0x2A6A36, 0x6A2878, 0x7A5A24, 0x22686A, 0x44444E, 0xA04A18,
                                       0x382858, 0x7E2A4C, 0x56682A, 0x284A6E, 0xB08A2A, 0x1E1E24, 0x5A2A1E, 0x3A7A8A};
  hz_internal const u32 figure_hex[] = {0xF2E6C8, 0xFFD04A, 0x9CE86A, 0xF08AD0, 0x8AD8FF, 0xFF8A5A, 0xE8E8F0, 0xC8A0FF};
  VxCol ink = vx_hex(0x14100E), rod = vx_hex(0x4A2E1C), rod_hi = vx_hex(0x7A5034);
  for (u32 col = 0; col < 2; col++) {
    u32 d = var * 2 + col, h = vx_hash2i((i32)d, 17, 0x5bd1e995u);
    VxCol cloth = vx_hex(cloth_hex[h % ARRAY_SIZE(cloth_hex)]);
    VxCol fig = vx_hex(figure_hex[(h >> 5) % ARRAY_SIZE(figure_hex)]);
    // a 7 x 8 pictogram mirrored about its middle column: a body that bulges toward the middle rows, then the eyes
    u32 cells = 0;
    for (u32 row = 0; row < 8; row++)
      for (u32 c = 0; c < 4; c++) {
        u32 bit = vx_hash2i((i32)row, (i32)c, h);
        f32 keep = c == 3 ? 0.92f : 0.75f - 0.2f * (f32)(3 - c) * (row == 0 || row == 7 ? 1.4f : 0.7f);
        if (row >= 1 && row <= 6 && c == 3) keep = 1;
        if ((f32)(bit & 1023u) / 1023.0f < keep) cells |= 1u << (row * 4 + c);
      }
    u32 eye_row = 2 + (h >> 9) % 2, eye_c = 1 + (h >> 11) % 2;
    i32 x0 = (i32)col * 64;
    for (i32 y = 4; y < VX_TN - 2; y++)
      for (i32 x = x0 + 4; x < x0 + 60; x++) {
        i32 lx = x - x0;
        if (y < 10) {
          // the hanging rod with knobbed ends, a lit top edge
          b32 knob = lx < 8 || lx >= 56;
          if (!knob && (y < 5 || y > 8)) continue;
          vx_set(t, x, y, y == 4 || (knob && y == 5) ? ink : y == 5 ? rod_hi : rod);
          continue;
        }
        // the cloth: 44 px wide, a shallow notch cut up into its foot
        i32 cx = lx - 32, ax = cx < 0 ? -cx - 1 : cx;
        i32 foot = 122 - (ax < 10 ? 10 - ax : 0) / 2 * 1;
        if (ax >= 22 || y >= foot) continue;
        b32 edge = ax == 21 || y == 10 || y == foot - 1;
        b32 hem = ax >= 18 || y < 14 || y >= foot - 4;
        f32 fold = 0.92f + 0.08f * m_sinf((f32)lx * 0.55f) + (hem ? -0.2f : 0.0f);
        VxCol c = edge ? ink : vx_scalec(cloth, fold);
        // the pictogram: 5 px cells from row 34
        i32 gx = cx + 17, gy = y - 34;
        if (!edge && gx >= -1 && gx < 36 && gy >= -1 && gy < 41) {
          b32 on = false, beside = false;
          for (i32 oy = -1; oy <= 1; oy++)
            for (i32 ox = -1; ox <= 1; ox++) {
              i32 sx = gx + ox, sy = gy + oy;
              if (sx < 0 || sx >= 35 || sy < 0 || sy >= 40) continue;
              i32 cc = sx / 5, rr = sy / 5, mc = cc > 3 ? 6 - cc : cc;
              b32 cell = (cells >> (rr * 4 + mc)) & 1u;
              if (ox == 0 && oy == 0) on = cell;
              else if (cell) beside = true;
            }
          if (on) {
            i32 cc = gx / 5, rr = gy / 5, mc = cc > 3 ? 6 - cc : cc;
            b32 eye = (u32)rr == eye_row && (u32)mc == eye_c;
            c = eye ? ((gx % 5) >= 2 && (gy % 5) >= 1 ? ink : vx_hex(0xFFFFFF)) : vx_scalec(fig, 0.94f + 0.06f * (f32)((gx / 5 + gy / 5) & 1));
          } else if (beside) {
            c = ink;
          }
        }
        vx_set(t, x, y, c);
      }
  }
}

// furniture materials as wood light, wood dark, cloth: wood, ebon, pearl, mahogany, boreal, palm, frozen, living, crystal, obsidian, pumpkin, flesh, skyware, dynasty, golden, bone
hz_internal const u32 VX_FURN_MATS[16][3] = {
    {0xE0A866, 0x8A5A30, 0xC8443C}, {0x9A7AC8, 0x4A3478, 0xE06AE0}, {0xF4E6EE, 0xB89AB0, 0x7AD8F0}, {0xC8604C, 0x6A2622, 0xF0D060},
    {0xC0B098, 0x6A5C4C, 0x4A8AD8}, {0xF0C860, 0xA07020, 0x3AA05A}, {0xC8ECFF, 0x6AA0D0, 0xF4F8FF}, {0x7AC85A, 0x3A6A2A, 0xF0E060},
    {0xE0F4FF, 0x90B8D8, 0xF07AB0}, {0x5A4A6A, 0x1E1828, 0xC83A3A}, {0xF09040, 0xA04A18, 0x3A3A3A}, {0xF0A0B8, 0xA85068, 0xFFE0A0},
    {0x7AA8F0, 0x3A5AA8, 0xFFE070}, {0xD04030, 0x702018, 0xF0C040}, {0xF8D870, 0xB08020, 0xB03030}, {0xE8E0CC, 0xA09880, 0x6A6A7A},
};

// upholstery: a soft weave with quilted seams
hz_internal void vx_paint_cloth(VxTile *t, VxCol base) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 v = 0.9f + 0.08f * vx_vn(t, bx, by, 24, 24, 77) + 0.04f * (f32)(((x >> 1) + (y >> 1)) & 1);
      f32 sx = m_fracf(bx * 2.0f), sy = m_fracf(by * 2.0f);
      if (sx < 0.04f || sy < 0.04f) v *= 0.78f;
      vx_set(t, x, y, vx_scalec(base, v));
    }
}

// shelved book spines: random widths and colours, a dark gap between, a gilt band on some
hz_internal void vx_paint_books(VxTile *t) {
  hz_internal const u32 spine[] = {0x8A2424, 0x24448A, 0x2A6A36, 0x7A5A24, 0x5A2A6A, 0xB08A3A, 0x2A5A6A, 0x6A3A24};
  VxCol gap = vx_hex(0x1A120E), gilt = vx_hex(0xF0C860);
  for (i32 x = 0, n = 0; x < VX_TN; n++) {
    u32 h = vx_hash2i(n, 3, t->seed);
    i32 w = 5 + (i32)(h % 5);
    VxCol c = vx_hex(spine[(h >> 4) % ARRAY_SIZE(spine)]);
    i32 top = (i32)((h >> 8) % 10);
    for (i32 xx = x; xx < MIN(x + w, VX_TN); xx++)
      for (i32 y = 0; y < VX_TN; y++) {
        i32 ly = y % 64;
        b32 dark = xx == x || ly < top;
        b32 band = ((h >> 12) & 1) && (ly == 18 || ly == 19 || ly == 46);
        vx_set(t, xx, y, dark ? gap : band ? gilt : vx_scalec(c, 0.85f + 0.15f * (f32)(xx - x) / (f32)w));
      }
    x += w;
  }
}

// polished marble paving: a square slab per block with faint grey veins and a bevelled seam
hz_internal void vx_paint_marble(VxTile *t, const VxRamp *r) {
  for (i32 y = 0; y < VX_TN; y++)
    for (i32 x = 0; x < VX_TN; x++) {
      f32 bx = vx_bc(x), by = vx_bc(y);
      f32 lx = m_fracf(bx), ly = m_fracf(by);
      f32 dl = lx, dr = 1.0f - lx, dt = ly, db = 1.0f - ly;
      f32 edge = m_minf(m_minf(dl, dr), m_minf(dt, db));
      u32 slab = vx_hash3i((i32)bx, (i32)by, 161, t->seed);
      f32 n = vx_fbm(t, bx, by, 4, 3, 160);
      f32 vein = m_absf(m_fracf(n * 3.0f + bx * 0.3f + by * 0.2f) - 0.5f);
      f32 tone = 0.78f + (f32)(slab & 255u) / 255.0f * 0.08f + (n - 0.5f) * 0.12f;
      VxCol col = vx_feature(r, tone, vx_bevel(dl, dr, dt, db, 0.05f), edge, 0.012f, r->s[1]);
      col = vx_mixc(col, r->s[2], vx_line(vein * 0.06f, 0.0035f) * 0.45f);
      vx_set(t, x, y, col);
    }
}

// 3x5 capitals A-Z, one octal digit per row from the top, the high bit the left column
hz_internal const u16 VX_GLYPHS[26] = {025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227,
                                       011152, 055655, 044447, 057755, 065555, 025552, 065644, 025563, 065655,
                                       034216, 072222, 055557, 055552, 055775, 055255, 055222, 071247};
// the words by VxSignWord, '|' breaking the line
hz_internal const char *const VX_SIGN_WORDS[VX_SIGN_COUNT] = {
    "SKY|ISLAND", "THE|CORRUPTION", "THE|HALLOW", "MARKET",  "DESERT",  "JUNGLE", "SNOW",           "OCEAN",
    "TOWN",       "MUSEUM",         "DUNGEON",    "ARMOURY", "BANNERS", "PYLONS", "MUSHROOM|CAVES", "UNDER|WORLD"};

// one texel of a block quarter's painting, repeated in all four quarters
force_inline void vx_sign_px(VxTile *t, i32 x, i32 y, VxCol c) {
  for (i32 q = 0; q < 4; q++) vx_set(t, x + (q & 1) * 64, y + (q >> 1) * 64, c);
}

// a wooden signboard lettered in pixel capitals; the board's face shows texels 4..60 x 4..40 of a block quarter
hz_internal void vx_paint_sign(VxTile *t) {
  VxRamp wood = vx_ramp(0xF0C888, 0x9A6436);
  VxCol ink = vx_hex(0x3A2212), frame = vx_hex(0x6A4022), lit = vx_hex(0xFFE8B8);
  for (i32 y = 0; y < 64; y++)
    for (i32 x = 0; x < 64; x++) {
      i32 row = y / 9;
      f32 tone = 0.55f + vx_h(t, row, 0, 170) * 0.15f + (vx_vn(t, vx_bc(x), vx_bc(y), 3, 48, 171) - 0.5f) * 0.16f;
      b32 seam = y % 9 == 0, rim = x < 6 || x >= 58 || y < 6 || y >= 38;
      VxCol c = vx_rampf(&wood, seam ? tone - 0.25f : tone);
      if (rim) c = (x == 6 - 1 || y == 6 - 1) ? vx_hex(0x4A2A14) : vx_scalec(frame, 0.9f + 0.2f * vx_h(t, x / 4, y / 4, 172));
      vx_sign_px(t, x, y, c);
    }
  const char *word = VX_SIGN_WORDS[t->var % VX_SIGN_COUNT];
  i32 brk = 0, total = 0;
  for (; word[total]; total++)
    if (word[total] == '|') brk = total;
  i32 len0 = brk ? brk : total, len1 = brk ? total - brk - 1 : 0;
  // two texels a glyph pixel where the line fits the board, else one
  i32 s0 = (len0 * 4 - 1) * 2 <= 48 ? 2 : 1, s1 = (len1 * 4 - 1) * 2 <= 48 ? 2 : 1;
  i32 y0 = 22 - (5 * s0 + (len1 ? 3 + 5 * s1 : 0)) / 2;
  for (i32 l = 0; l < (len1 ? 2 : 1); l++) {
    i32 n = l ? len1 : len0, s = l ? s1 : s0, x0 = 32 - (n * 4 - 1) * s / 2;
    for (i32 k = 0; k < n; k++) {
      char ch = word[(l ? brk + 1 : 0) + k];
      if (ch < 'A' || ch > 'Z') continue;
      u32 g = VX_GLYPHS[ch - 'A'];
      for (i32 gy = 0; gy < 5; gy++)
        for (i32 gx = 0; gx < 3; gx++) {
          if (!((g >> ((4 - gy) * 3 + (2 - gx))) & 1u)) continue;
          for (i32 sy = 0; sy < s; sy++)
            for (i32 sx = 0; sx < s; sx++) {
              i32 px = x0 + (k * 4 + gx) * s + sx, py = y0 + gy * s + sy;
              vx_sign_px(t, px, py, ink);
              // a carved letter catches the light along its lower edge
              if (sy == s - 1 && !((gy < 4) && ((g >> ((3 - gy) * 3 + (2 - gx))) & 1u))) vx_sign_px(t, px, py + 1, lit);
            }
        }
    }
    y0 += 5 * s + 3;
  }
}

hz_internal void vx_paint(VxTile *t, u32 tex) {
  VxRamp dirt = vx_ramp(VX_DIRT[0], VX_DIRT[1]);
  VxRamp stone = vx_ramp(VX_STONE[0], VX_STONE[1]);
  VxRamp grass = vx_ramp(VX_GRASS[0], VX_GRASS[1]);
  VxRamp mud = vx_ramp(VX_MUD[0], VX_MUD[1]);
  u32 dirt_seed = vx_hash_string(vx_texture_name[T_DIRT]), mud_seed = vx_hash_string(vx_texture_name[T_MUD]);
  f32 glow_k;
  VxRamp r, r2;
  switch (tex) {
  case T_DIRT: vx_paint_dirt(t, &dirt); break;
  case T_GRASS_TOP: vx_paint_grass_top(t, &grass); break;
  case T_GRASS_SIDE: vx_paint_grass_side(t, &grass, &dirt, dirt_seed); break;
  case T_STONE: vx_paint_stones(t, &stone, 6, 0.016f, 30); break;
  case T_CLAY: r = vx_ramp(0xC87A5C, 0x7E4236), vx_paint_clay(t, &r); break;
  case T_MUD: vx_paint_mud(t, &mud); break;
  case T_JUNGLE_GRASS_TOP: r = vx_ramp(0x9CE43C, 0x3E8A1E), vx_paint_grass_top(t, &r); break;
  case T_JUNGLE_GRASS_SIDE: {
    r = vx_ramp(0x9CE43C, 0x3E8A1E);
    u32 seed = t->seed;
    t->seed = mud_seed;
    vx_paint_mud(t, &mud);
    t->seed = seed;
    vx_paint_grass_side(t, &r, &mud, mud_seed);
  } break;
  case T_MUSHROOM_GRASS_TOP:
  case T_MUSHROOM_GRASS_SIDE: {
    b32 topf = tex == T_MUSHROOM_GRASS_TOP;
    r = vx_ramp(0x34406A, 0x0A0E1E);
    if (topf) vx_paint_grass_top(t, &r);
    else vx_paint_grass_side(t, &r, &mud, mud_seed);
    // dark mycelium strewn with glowing spores, as in the reference's starry cavern floor
    for (i32 y = 0; y < VX_TN; y++)
      for (i32 x = 0; x < VX_TN; x++) {
        if (!topf && m_fracf(vx_bc(y)) > 0.22f) continue;
        vx_glow(t, x, y, 0.05f, 0.0f);
        VxCell s = vx_cells(t, vx_bc(x), vx_bc(y), 14, 14, 0.8f, 300);
        if (vx_hid(s.id, 0) < 0.22f && s.ox * s.ox + s.oy * s.oy < 0.08f) {
          vx_set(t, x, y, vx_hex(0x9FEAFF));
          vx_glow(t, x, y, 1.0f, 0.3f);
        }
      }
  } break;
  case T_CORRUPT_GRASS_TOP: r = vx_ramp(0x9A7BC8, 0x46306E), vx_paint_grass_top(t, &r); break;
  case T_CORRUPT_GRASS_SIDE: r = vx_ramp(0x9A7BC8, 0x46306E), vx_paint_grass_side(t, &r, &dirt, dirt_seed); break;
  case T_EBONSTONE: r = vx_ramp(0x7A68A4, 0x382A50), vx_paint_stones(t, &r, 6, 0.016f, 31); break;
  case T_SAND: r = vx_ramp(0xFFF2C4, 0xDCB274), vx_paint_sand(t, &r); break;
  case T_SAND_SIDE: r = vx_ramp(0xF8E0AC, 0xC09860), vx_paint_sand_side(t, &r); break;
  case T_SANDSTONE: r = vx_ramp(0xE6BC7C, 0x9E6E3A), vx_paint_sandstone(t, &r); break;
  case T_SNOW: r = vx_ramp(0xFAFCFF, 0xA8C0E6), vx_paint_snow(t, &r); break;
  case T_ICE: r = vx_ramp(0x9ED2F6, 0x3A72BE), vx_paint_ice(t, &r); break;
  case T_PINK_ICE: r = vx_ramp(0xF6D6F2, 0x8A5C9E), vx_paint_ice(t, &r); break;
  case T_SHIMMER: r = vx_ramp(0xE4D0FF, 0x5A44B0), vx_paint_ice(t, &r); break;
  case T_AETHERIUM: r = vx_ramp(0xFBF4FF, 0xA88ACC), vx_paint_aetherium(t, &r); break;
  case T_DUNGEON_BRICK: r = vx_ramp(0x2C6E5A, 0x0A2420), vx_paint_dungeon(t, &r); break;
  case T_COBWEB: vx_paint_cobweb(t); break;
  case T_ASH: r = vx_ramp(0xA88672, 0x4A3432), vx_paint_stones(t, &r, 8, 0.012f, 105); break;
  case T_HELLSTONE:
    r = vx_ramp(0x7A5A86, 0x2A1A34), r2 = vx_ramp(0xFFC85A, 0xFF3D1F);
    vx_paint_hellstone(t, &r, &r2);
    break;
  case T_OBSIDIAN: r = vx_ramp(0x5A4878, 0x120C1E), vx_paint_obsidian(t, &r); break;
  case T_OBSIDIAN_BRICK:
    r = vx_ramp(0x6A5498, 0x241A40), r2 = vx_ramp(0x1A1428, 0x06040C);
    vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.016f, NULL);
    break;
  case T_PART_MOTE:
  case T_PART_PUFF:
  case T_PART_SPARK:
  case T_PART_FLAKE:
  case T_PART_LEAF: vx_paint_sprite(t, tex); break;
  case T_PUPPET: vx_paint_puppet(t); break;
  case T_HIVE: r = vx_ramp(0xC88A30, 0x6A4212), r2 = vx_ramp(0xFFE07A, 0xE09A22), vx_paint_hive(t, &r, &r2); break;
  case T_MESA_TOP: r = vx_ramp(0xF6DCA6, 0xC09458), r2 = vx_ramp(0xD8603A, 0x7E2A1A), vx_paint_mesa_top(t, &r, &r2); break;
  case T_MESA_SIDE: r = vx_ramp(0xD8805A, 0x7A3422), vx_paint_mesa_side(t, &r); break;
  case T_BUTTE: r = vx_ramp(0xC8B488, 0x7A6646), vx_paint_butte(t, &r); break;
  case T_HALLOW_GRASS_TOP: r = vx_ramp(0x8CF2E2, 0x248E94), vx_paint_grass_top(t, &r); break;
  case T_HALLOW_GRASS_SIDE: r = vx_ramp(0x8CF2E2, 0x248E94), vx_paint_grass_side(t, &r, &dirt, dirt_seed); break;
  case T_PEARL_LOG_SIDE: r = vx_ramp(0x7A7AA4, 0x22223C), vx_paint_log_side(t, &r, false); break;
  case T_PEARL_LOG_TOP: r = vx_ramp(0x7A7AA4, 0x22223C), r2 = vx_ramp(0xD8D4EC, 0x8E8AAE), vx_paint_log_top(t, &r, &r2); break;
  case T_HALLOW_LEAVES: r = vx_ramp(0xE6A2FF, 0x6A2AA0), vx_paint_leaves(t, &r, false); break;
  case T_GOLD_LEAVES: r = vx_ramp(0xFFE872, 0xA27A16), vx_paint_leaves(t, &r, false); break;
  case T_HALLOW_PLANT: r = vx_ramp(0xA6FAEC, 0x2A9C96), vx_paint_tuft(t, &r, 0.35f, 0.75f, 4); break;
  case T_COBBLE: r = vx_ramp(0xC4C8D2, 0x5A5E6C), vx_paint_stones(t, &r, 7, 0.03f, 520); break;
  case T_COPPER: r = vx_ramp(0xF4A866, 0x7E3E1C), vx_paint_sunplate(t, &r); break;
  case T_LIMESTONE_BRICK:
    r = vx_ramp(0xF6F0E0, 0xBCB096), r2 = vx_ramp(0xA89E88, 0x6E6656);
    vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.014f, NULL);
    break;
  case T_SLATE: r = vx_ramp(0x7C86A8, 0x262A3E), vx_paint_stones(t, &r, 5, 0.02f, 610); break;
  case T_PEARLSAND: r = vx_ramp(0xFCF2F8, 0xC6B2CC), vx_paint_sand(t, &r); break;
  case T_HELLSTONE_BRICK:
    // red-hot bricks in dark joints, glowing whole
    r = vx_ramp(0xFF6A2A, 0xB01E08), r2 = vx_ramp(0x5A1408, 0x200602);
    vx_paint_bricks(t, &r, &r2, 3, 0.5f, 0.03f, NULL);
    for (i32 y = 0; y < VX_TN; y++)
      for (i32 x = 0; x < VX_TN; x++) vx_glow(t, x, y, 0.85f, 0.0f);
    break;
  case T_LOG_SIDE: r = vx_ramp(0x8E6440, 0x3E2618), vx_paint_log_side(t, &r, false); break;
  case T_LOG_TOP: r = vx_ramp(0x8E6440, 0x3E2618), r2 = vx_ramp(0xD8AC74, 0x9C6E42), vx_paint_log_top(t, &r, &r2); break;
  case T_BOREAL_LOG_SIDE: r = vx_ramp(0x6E5A50, 0x2E2420), vx_paint_log_side(t, &r, false); break;
  case T_BOREAL_LOG_TOP:
    r = vx_ramp(0x6E5A50, 0x2E2420), r2 = vx_ramp(0xD6C2A4, 0x948064);
    vx_paint_log_top(t, &r, &r2);
    break;
  case T_MAHOGANY_LOG_SIDE: r = vx_ramp(0x6E5444, 0x241A18), vx_paint_log_side(t, &r, false); break;
  case T_MAHOGANY_LOG_TOP:
    r = vx_ramp(0xA4483C, 0x4A1C1A), r2 = vx_ramp(0xD07A5E, 0x8C3E30);
    vx_paint_log_top(t, &r, &r2);
    break;
  case T_PALM_LOG_SIDE: r = vx_ramp(0xC89E6A, 0x6A4A2C), vx_paint_log_side(t, &r, true); break;
  case T_PALM_LOG_TOP:
    r = vx_ramp(0xC89E6A, 0x6A4A2C), r2 = vx_ramp(0xEED2A2, 0xB0905E);
    vx_paint_log_top(t, &r, &r2);
    break;
  case T_PLANKS: r = vx_ramp(0xE0A866, 0x8A5A30), vx_paint_planks(t, &r); break;
  case T_BOREAL_PLANKS: r = vx_ramp(0xC0B098, 0x6A5C4C), vx_paint_planks(t, &r); break;
  case T_MAHOGANY_PLANKS: r = vx_ramp(0xC8604C, 0x6A2622), vx_paint_planks(t, &r); break;
  case T_LEAVES: r = vx_ramp(0x3E7A38, 0x0E301C), vx_paint_leaves(t, &r, false); break;
  case T_BOREAL_LEAVES: r = vx_ramp(0x264C40, 0x061614), vx_paint_leaves(t, &r, false); break;
  case T_JUNGLE_LEAVES: r = vx_ramp(0x5A9632, 0x164812), vx_paint_leaves(t, &r, false); break;
  case T_PALM_LEAVES: r = vx_ramp(0x98D04A, 0x3A7A1E), vx_paint_leaves(t, &r, true); break;
  case T_CORRUPT_LEAVES: r = vx_ramp(0x8A6AC8, 0x2E1E5A), vx_paint_leaves(t, &r, false); break;
  case T_EBON_LOG_SIDE: r = vx_ramp(0x7A6A8C, 0x2C2438), vx_paint_log_side(t, &r, false); break;
  case T_EBON_LOG_TOP: r = vx_ramp(0x7A6A8C, 0x2C2438), r2 = vx_ramp(0xB8A4CC, 0x6E5C84), vx_paint_log_top(t, &r, &r2); break;
  case T_CACTUS_SIDE: r = vx_ramp(0x2C6028, 0x0A2412), vx_paint_cactus(t, &r, false); break;
  case T_CACTUS_TOP: r = vx_ramp(0x2C6028, 0x0A2412), vx_paint_cactus(t, &r, true); break;
  case T_COPPER_ORE: r = vx_ramp(0xF0985A, 0x9A4A22), vx_paint_ore(t, &stone, &r, 0.0f); break;
  case T_TIN_ORE: r = vx_ramp(0xE2DAB8, 0x8C8466), vx_paint_ore(t, &stone, &r, 0.0f); break;
  case T_IRON_ORE: r = vx_ramp(0xD8B4A0, 0x7C5A4C), vx_paint_ore(t, &stone, &r, 0.0f); break;
  case T_SILVER_ORE: r = vx_ramp(0xF2F6FA, 0x8C9AAE), vx_paint_ore(t, &stone, &r, 0.1f); break;
  case T_GOLD_ORE: r = vx_ramp(0xFFE07A, 0xB07E1E), vx_paint_ore(t, &stone, &r, 0.15f); break;
  case T_DEMONITE_ORE: {
    r = vx_ramp(0x7A68A4, 0x382A50), r2 = vx_ramp(0xB58CFF, 0x4A2A8A);
    u32 seed = t->seed;
    vx_paint_ore(t, &r, &r2, 0.6f);
    t->seed = seed;
  } break;
  case T_AMETHYST: r = vx_ramp(0xD49CFF, 0x6A2AAA), vx_paint_gem(t, &stone, &r); break;
  case T_SAPPHIRE: r = vx_ramp(0x7AB8FF, 0x1A3A9A), vx_paint_gem(t, &stone, &r); break;
  case T_RUBY: r = vx_ramp(0xFF7A8A, 0x9A1428), vx_paint_gem(t, &stone, &r); break;
  case T_EMERALD: r = vx_ramp(0x7AF0A4, 0x147A4A), vx_paint_gem(t, &stone, &r); break;
  case T_BANNER: vx_paint_banner(t); break;
  case T_FURN_WOOD: r = vx_ramp(VX_FURN_MATS[t->var][0], VX_FURN_MATS[t->var][1]), vx_paint_planks(t, &r); break;
  case T_FURN_CLOTH: vx_paint_cloth(t, vx_hex(VX_FURN_MATS[t->var][2])); break;
  case T_BOOKS: vx_paint_books(t); break;
  case T_MARBLE: r = vx_ramp(0xFFFDF8, 0x9C98A4), vx_paint_marble(t, &r); break;
  case T_PYLON:
    r = vx_ramp(0xC4C8D6, 0x50546A), r2 = vx_ramp(0xD8FAFF, 0x4A9AD8), glow_k = 0.7f;
    vx_paint_bricks(t, &r, &r2, 2, 0.5f, 0.02f, &glow_k);
    break;
  case T_SIGN: vx_paint_sign(t); break;
  case T_LIGHT_BOX_SIDE: vx_paint_light_box(t, false); break;
  case T_LIGHT_BOX_TOP: vx_paint_light_box(t, true); break;
  case T_GRAY_BRICK:
    r = vx_ramp(0xB4B6BE, 0x6A6C78), r2 = vx_ramp(0x5A5C68, 0x2E3038);
    vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.014f, NULL);
    break;
  case T_RED_BRICK:
    r = vx_ramp(0xD06850, 0x7A2C24), r2 = vx_ramp(0x8A7A74, 0x3E3434);
    vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.014f, NULL);
    break;
  case T_CLOUD: r = vx_ramp(0xFFFFFF, 0xB6C8EA), vx_paint_cloud(t, &r); break;
  case T_SUNPLATE: r = vx_ramp(0xFFDE6A, 0xA86E1A), vx_paint_sunplate(t, &r); break;
  case T_GLASS: r = vx_ramp(0xEAF6FF, 0x7E9AB8), vx_paint_glass(t, &r); break;
  case T_TORCH: vx_paint_torch(t, vx_hex(0xFFF6C0), vx_hex(0xFFC23A), vx_hex(0xFF6A1A)); break;
  case T_ICE_TORCH: vx_paint_torch(t, vx_hex(0xF0FFFF), vx_hex(0x8AE6FF), vx_hex(0x3A8AFF)); break;
  case T_GRASS_PLANT: vx_paint_tuft(t, &grass, 0.35f, 0.75f, 4); break;
  case T_FLOWERS: vx_paint_flowers(t, &grass); break;
  case T_JUNGLE_PLANT: r = vx_ramp(0x9CE43C, 0x3E8A1A), vx_paint_tuft(t, &r, 0.5f, 0.95f, 5); break;
  case T_CORRUPT_PLANT: r = vx_ramp(0xB08CE0, 0x4A2E7A), vx_paint_tuft(t, &r, 0.3f, 0.7f, 4); break;
  case T_MUSHROOM: r = vx_ramp(0xFF6A5A, 0xA02A2A), vx_paint_mushroom(t, &r, false); break;
  case T_GLOW_MUSHROOM: r = vx_ramp(0x9AEAFF, 0x2A6ADA), vx_paint_mushroom(t, &r, true); break;
  case T_GLOW_CAP: r = vx_ramp(0x5AB4FF, 0x16409A), vx_paint_glow_cap(t, &r); break;
  case T_GLOW_STEM: r = vx_ramp(0xDCE6F0, 0x8A98B0), vx_paint_glow_stem(t, &r); break;
  case T_VINE: r = vx_ramp(0x7ACC3A, 0x2E6A1E), vx_paint_vine(t, &r); break;
  case T_LIFE_CRYSTAL: vx_paint_life_crystal(t); break;
  case T_POT: r = vx_ramp(0xC8774E, 0x6E3424), r2 = vx_ramp(0xF0D08A, 0x8A5A2A), vx_paint_pot(t, &r, &r2); break;
  case T_POT_TOP: r = vx_ramp(0xC8774E, 0x6E3424), vx_paint_pot_top(t, &r); break;
  case T_CHEST_SIDE:
  case T_CHEST_TOP:
  case T_CHEST_FRONT:
    r = vx_ramp(0xC88A4E, 0x6A4224), r2 = vx_ramp(0xC0C4D0, 0x4E5260);
    vx_paint_chest(t, &r, &r2, tex == T_CHEST_FRONT, tex == T_CHEST_TOP);
    break;
  case T_GOLD_CHEST_SIDE:
  case T_GOLD_CHEST_TOP:
  case T_GOLD_CHEST_FRONT:
    r = vx_ramp(0xFFD86A, 0xA8701E), r2 = vx_ramp(0x8A6A4A, 0x3A2A1E);
    vx_paint_chest(t, &r, &r2, tex == T_GOLD_CHEST_FRONT, tex == T_GOLD_CHEST_TOP);
    break;
  case T_DOOR: r = vx_ramp(0xC89058, 0x5E3A20), vx_paint_door(t, &r); break;
  case T_FURNACE:
    r = vx_ramp(0xA4A6B0, 0x4E5058), r2 = vx_ramp(0x4A4C54, 0x24262C);
    vx_paint_bricks(t, &r, &r2, 4, 0.5f, 0.016f, NULL);
    break;
  case T_FURNACE_FRONT:
    r = vx_ramp(0xA4A6B0, 0x4E5058), r2 = vx_ramp(0x4A4C54, 0x24262C), vx_paint_furnace_front(t, &r, &r2);
    break;
  case T_ANVIL: r = vx_ramp(0x8A90A0, 0x2A2E38), vx_paint_anvil(t, &r); break;
  case T_WATER: r = vx_ramp(0x4FD0EA, 0x1A4E8C), vx_paint_liquid(t, &r, 0.0f); break;
  case T_LAVA: r = vx_ramp(0xFFD04A, 0xE0400E), vx_paint_liquid(t, &r, 1.0f); break;
  case T_HONEY: r = vx_ramp(0xFFC44A, 0xB06A0E), vx_paint_liquid(t, &r, 0.0f); break;
  case T_CRACK: vx_paint_crack(t); break;
  default: break;
  }
}

// ---- assembly: linear-space mips with coverage-preserving alpha ----

force_inline f32 vx_srgb_to_linear(f32 c) {
  return c <= 0.04045f ? c / 12.92f : m_powf((c + 0.055f) / 1.055f, 2.4f);
}
force_inline u8 vx_linear_to_srgb8(f32 c) {
  c = m_saturatef(c);
  f32 v = c <= 0.0031308f ? c * 12.92f : 1.055f * m_powf(c, 1.0f / 2.4f) - 0.055f;
  return (u8)(m_saturatef(v) * 255.0f + 0.5f);
}
force_inline u8 vx_unorm8(f32 v) { return (u8)(m_saturatef(v) * 255.0f + 0.5f); }

hz_internal f32 vx_coverage(const f32 *lin, u32 cnt, f32 k) {
  u32 c = 0;
  for (u32 i = 0; i < cnt; i++)
    if (lin[i * 4 + 3] * k > 0.5f) c++;
  return (f32)c / (f32)cnt;
}

force_inline u32 vx_mip_offset(u32 mip, u32 bpp) {
  u32 off = 0;
  for (u32 m = 0; m < mip; m++) off += VX_TEX_LAYERS * (VX_TN >> m) * (VX_TN >> m) * bpp;
  return off;
}

// clear texels take the colour of the nearest opaque ones (push-pull), so filtering and mips keep cut edges bright
hz_internal void vx_fill_clear(f32 *lin, u32 size, Allocator *ta) {
  u32 levels = 0;
  while ((size >> levels) > 1) levels++;
  f32 **pyr = ALLOC_ARRAY(ta, f32 *, levels + 1);
  pyr[0] = ALLOC_ARRAY(ta, f32, size * size * 4);
  for (u32 i = 0; i < size * size; i++) {
    f32 w = lin[i * 4 + 3] > 0.5f ? 1.0f : 0.0f;
    for (u32 c = 0; c < 3; c++) pyr[0][i * 4 + c] = lin[i * 4 + c] * w;
    pyr[0][i * 4 + 3] = w;
  }
  for (u32 l = 1; l <= levels; l++) {
    u32 s = size >> l, ps = s * 2;
    pyr[l] = ALLOC_ARRAY(ta, f32, s * s * 4);
    for (u32 y = 0; y < s; y++)
      for (u32 x = 0; x < s; x++)
        for (u32 k = 0; k < 4; k++) {
          u32 src = (y * 2 + (k >> 1)) * ps + x * 2 + (k & 1);
          for (u32 c = 0; c < 4; c++) pyr[l][(y * s + x) * 4 + c] += pyr[l - 1][src * 4 + c];
        }
  }
  for (u32 y = 0; y < size; y++)
    for (u32 x = 0; x < size; x++) {
      u32 i = y * size + x;
      if (lin[i * 4 + 3] > 0.5f) continue;
      for (u32 l = 1; l <= levels; l++) {
        u32 s = size >> l, j = (y >> l) * s + (x >> l);
        f32 w = pyr[l][j * 4 + 3];
        if (w <= 0) continue;
        for (u32 c = 0; c < 3; c++) lin[i * 4 + c] = pyr[l][j * 4 + c] / w;
        break;
      }
    }
}

// rescales a mip's alpha so its alpha-tested coverage matches the top level's
hz_internal void vx_fit_coverage(f32 *lin, u32 cnt, f32 coverage0) {
  f32 lo = 0.5f, hi = 4;
  for (i32 it = 0; it < 12; it++) {
    f32 mid = (lo + hi) * 0.5f;
    if (vx_coverage(lin, cnt, mid) < coverage0) lo = mid;
    else hi = mid;
  }
  f32 k = (lo + hi) * 0.5f;
  for (u32 i = 0; i < cnt; i++) lin[i * 4 + 3] = m_saturatef(lin[i * 4 + 3] * k);
}

hz_internal void vx_build_layer(VxTextureData *out, u32 layer) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;
  u32 tex = 0;
  while (tex + 1 < T_COUNT && vx_tex_layer[tex + 1] <= layer) tex++;
  VxTile tile = {
      .c = ALLOC_ARRAY(ta, VxCol, VX_TNN),
      .a = ALLOC_ARRAY(ta, f32, VX_TNN),
      .emit = ALLOC_ARRAY(ta, f32, VX_TNN),
      .glint = ALLOC_ARRAY(ta, f32, VX_TNN),
      .seed = vx_hash_string(vx_texture_name[tex]),
      .var = layer - vx_tex_layer[tex],
  };
  VxTile *t = &tile;
  for (u32 i = 0; i < VX_TNN; i++) {
    t->c[i] = vx_col(1, 0, 1);
    t->a[i] = 1;
  }
  vx_paint(t, tex);
  if (t->var == 0) out->cutout[tex] = (u8)t->cutout;

  f32 *lin = ALLOC_ARRAY(ta, f32, VX_TNN * 4);
  f32 *em = ALLOC_ARRAY(ta, f32, VX_TNN * 2);
  for (u32 i = 0; i < VX_TNN; i++) {
    lin[i * 4] = vx_srgb_to_linear(m_saturatef(t->c[i].r));
    lin[i * 4 + 1] = vx_srgb_to_linear(m_saturatef(t->c[i].g));
    lin[i * 4 + 2] = vx_srgb_to_linear(m_saturatef(t->c[i].b));
    lin[i * 4 + 3] = t->cutout ? t->a[i] : 1.0f;
    em[i * 2] = t->emit[i];
    em[i * 2 + 1] = t->glint[i];
  }
  if (t->cutout) vx_fill_clear(lin, VX_TN, ta);
  f32 coverage0 = t->cutout ? vx_coverage(lin, VX_TNN, 1) : 0;

  // the cpu copy: one block (the tile's top-left quarter) box-filtered to 16x16
  u8 *px = out->pixels + layer * VX_TEX_PIXELS * VX_TEX_PIXELS * 4;
  u32 f = VX_TB / VX_TEX_PIXELS;
  for (u32 y = 0; y < VX_TEX_PIXELS; y++)
    for (u32 x = 0; x < VX_TEX_PIXELS; x++) {
      f32 ar = 0, ag = 0, ab = 0, aa = 0, k = 1.0f / (f32)(f * f);
      for (u32 j = 0; j < f; j++)
        for (u32 i = 0; i < f; i++) {
          const f32 *s = lin + ((y * f + j) * VX_TN + x * f + i) * 4;
          ar += s[0], ag += s[1], ab += s[2], aa += s[3];
        }
      u8 *o = px + (y * VX_TEX_PIXELS + x) * 4;
      o[0] = vx_linear_to_srgb8(ar * k);
      o[1] = vx_linear_to_srgb8(ag * k);
      o[2] = vx_linear_to_srgb8(ab * k);
      o[3] = aa * k > 0.5f ? 255 : 0;
    }

  u32 size = VX_TN;
  for (u32 l = 0; l < VX_TEX_MIPS; l++) {
    u32 cnt = size * size;
    if (l > 0) {
      u32 ps = size * 2;
      f32 *nl = ALLOC_ARRAY(ta, f32, cnt * 4);
      f32 *ne = ALLOC_ARRAY(ta, f32, cnt * 2);
      for (u32 y = 0; y < size; y++)
        for (u32 x = 0; x < size; x++) {
          u32 o = y * size + x;
          for (u32 k = 0; k < 4; k++) {
            u32 s = (y * 2 + (k >> 1)) * ps + (x * 2 + (k & 1));
            for (u32 c = 0; c < 4; c++) nl[o * 4 + c] += lin[s * 4 + c] * 0.25f;
            ne[o * 2] += em[s * 2] * 0.25f;
            ne[o * 2 + 1] = m_maxf(ne[o * 2 + 1], em[s * 2 + 1]);
          }
        }
      lin = nl;
      em = ne;
      if (t->cutout && coverage0 > 0) vx_fit_coverage(lin, cnt, coverage0);
    }
    u8 *A = out->albedo + vx_mip_offset(l, 4) + layer * cnt * 4;
    u8 *E = out->emissive + vx_mip_offset(l, 2) + layer * cnt * 2;
    for (u32 i = 0; i < cnt; i++) {
      A[i * 4] = vx_linear_to_srgb8(lin[i * 4]);
      A[i * 4 + 1] = vx_linear_to_srgb8(lin[i * 4 + 1]);
      A[i * 4 + 2] = vx_linear_to_srgb8(lin[i * 4 + 2]);
      A[i * 4 + 3] = vx_unorm8(lin[i * 4 + 3]);
      E[i * 2] = vx_unorm8(em[i * 2]);
      E[i * 2 + 1] = vx_unorm8(em[i * 2 + 1]);
    }
    size >>= 1;
  }
  tctx_temp_allocator_end(tmp);
}

void vx_sprite_mips(const u8 *rgba, u8 *out, u32 layer_count, u32 layer) {
  const u32 n = VX_ITEM_TEX_SIZE, nn = n * n;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;
  f32 *lin = ALLOC_ARRAY(ta, f32, nn * 4);
  for (u32 i = 0; i < nn; i++) {
    for (u32 c = 0; c < 3; c++) lin[i * 4 + c] = vx_srgb_to_linear((f32)rgba[i * 4 + c] / 255.0f);
    lin[i * 4 + 3] = rgba[i * 4 + 3] > 127 ? 1.0f : 0.0f;
  }
  vx_fill_clear(lin, n, ta);
  f32 coverage0 = vx_coverage(lin, nn, 1);
  u32 size = n, off = 0;
  for (u32 l = 0; l < VX_ITEM_TEX_MIPS; l++) {
    u32 cnt = size * size;
    if (l > 0) {
      u32 ps = size * 2;
      f32 *nl = ALLOC_ARRAY(ta, f32, cnt * 4);
      for (u32 y = 0; y < size; y++)
        for (u32 x = 0; x < size; x++)
          for (u32 k = 0; k < 4; k++) {
            u32 src = (y * 2 + (k >> 1)) * ps + (x * 2 + (k & 1));
            for (u32 c = 0; c < 4; c++) nl[(y * size + x) * 4 + c] += lin[src * 4 + c] * 0.25f;
          }
      lin = nl;
      if (coverage0 > 0) vx_fit_coverage(lin, cnt, coverage0);
    }
    u8 *A = out + off + layer * cnt * 4;
    for (u32 i = 0; i < cnt; i++) {
      for (u32 c = 0; c < 3; c++) A[i * 4 + c] = vx_linear_to_srgb8(lin[i * 4 + c]);
      A[i * 4 + 3] = vx_unorm8(lin[i * 4 + 3]);
    }
    off += layer_count * cnt * 4;
    size >>= 1;
  }
  tctx_temp_allocator_end(tmp);
}

void vx_textures_alloc(VxTextureData *out, Allocator *alloc) {
  out->albedo_size = vx_mip_offset(VX_TEX_MIPS, 4);
  out->emissive_size = vx_mip_offset(VX_TEX_MIPS, 2);
  out->albedo = ALLOC_ARRAY(alloc, u8, out->albedo_size);
  out->emissive = ALLOC_ARRAY(alloc, u8, out->emissive_size);
  out->pixels = ALLOC_ARRAY(alloc, u8, VX_TEX_LAYERS * VX_TEX_PIXELS * VX_TEX_PIXELS * 4);
  out->cutout = ALLOC_ARRAY(alloc, u8, T_COUNT);
}

void vx_textures_generate(VxTextureData *out) {
  Range(u32) r = lane_range(VX_TEX_LAYERS);
  for (u32 layer = r.min; layer < r.max; layer++) vx_build_layer(out, layer);
}

b32 vx_textures_dump(const VxTextureData *tex, const char *path) {
  // every layer's full tile (2x2 blocks) over a checker, eight per row
  const u32 cell = VX_TN, gap = 4, per_row = 8;
  const u32 rows = (VX_TEX_LAYERS + per_row - 1) / per_row;
  u32 w = per_row * (cell + gap) + gap, h = rows * (cell + gap) + gap;
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u8 *img = ALLOC_ARRAY(&ta.allocator, u8, w * h * 4);
  for (u32 i = 0; i < w * h; i++) {
    img[i * 4] = 24;
    img[i * 4 + 1] = 22;
    img[i * 4 + 2] = 30;
    img[i * 4 + 3] = 255;
  }
  for (u32 layer = 0; layer < VX_TEX_LAYERS; layer++) {
    u32 cx = (layer % per_row) * (cell + gap) + gap, cy = (layer / per_row) * (cell + gap) + gap;
    const u8 *src = tex->albedo + layer * VX_TNN * 4;
    for (u32 y = 0; y < cell; y++)
      for (u32 x = 0; x < cell; x++) {
        const u8 *s = src + (y * VX_TN + x) * 4;
        u8 *d = img + ((cy + y) * w + cx + x) * 4;
        f32 a = (f32)s[3] / 255.0f;
        f32 bg = ((x / 16 + y / 16) & 1) ? 70.0f : 110.0f;
        for (u32 c = 0; c < 3; c++) d[c] = (u8)((f32)s[c] * a + bg * (1.0f - a) + 0.5f);
      }
  }
  b32 ok = pg_dump_write_png(path, img, w, h, &ta.allocator);
  tctx_temp_allocator_end(ta);
  return ok;
}

#undef VX_TN
#undef VX_TNN
#undef VX_TB
#undef VX_PX

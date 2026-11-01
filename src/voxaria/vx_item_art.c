#include "voxaria/vx_game.h"

// parametric item pixel art: a generator per art kind draws palette slots, recoloured per tier, then outlined

#define VX_ART_N 16

// palette slots: 1-4 the material ramp dark to light, 5-6 wood, 7-10 the accent ramp, 11 white, 12 ink
enum { S_M1 = 1, S_M2, S_M3, S_M4, S_W1, S_W2, S_A1, S_A2, S_A3, S_A4, S_WHITE, S_INK, S_SLOTS };

typedef struct {
  u8 px[VX_ART_N * VX_ART_N];
} VxArtCanvas;

hz_internal const u32 VX_MAT_BASE[VX_IM_COUNT] = {
    0x9A9A9A, 0x9A6B44, 0xD9824A, 0xB9B69C, 0x9EA6B2, 0xD4DAE4, 0xF2C23E, 0x4E8CF0, 0xE0423C, 0x5ACD4A, 0xEEF0F6, 0xB060F0,
};

// the accent each art kind uses besides its material (gems, liquids, flames)
hz_internal u32 vx_art_accent(VxItemArt art, VxItemMat mat) {
  switch (art) {
  case VX_ART_WAND: return 0xFF6A2A;
  case VX_ART_POTION: return 0xE8304A;
  case VX_ART_LIFE_CRYSTAL: return 0xF04060;
  case VX_ART_MANA_CRYSTAL: return 0x4A86FF;
  case VX_ART_MIRROR: return 0x9FE8FF;
  case VX_ART_EYE: return 0xD83A3A;
  case VX_ART_BOTTLE: return 0xF4F8FF;
  case VX_ART_BOOTS: return 0xF4E7C8;
  case VX_ART_MINING_HELMET: return 0xFFF2A0;
  case VX_ART_GLOWSTICK: return 0x8CFF7A;
  case VX_ART_STAR: return 0xFFF6B0;
  default: return VX_MAT_BASE[mat];
  }
}

hz_internal void vx_art_px(VxArtCanvas *c, i32 x, i32 y, u8 s) {
  if (x >= 0 && y >= 0 && x < VX_ART_N && y < VX_ART_N) c->px[y * VX_ART_N + x] = s;
}

hz_internal void vx_art_line(VxArtCanvas *c, i32 x0, i32 y0, i32 x1, i32 y1, u8 s) {
  i32 dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
  i32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    vx_art_px(c, x0, y0, s);
    if (x0 == x1 && y0 == y1) break;
    i32 e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

hz_internal void vx_art_disc(VxArtCanvas *c, f32 cx, f32 cy, f32 r, u8 s) {
  for (i32 y = 0; y < VX_ART_N; y++)
    for (i32 x = 0; x < VX_ART_N; x++) {
      f32 dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) vx_art_px(c, x, y, s);
    }
}

// every pixel whose centre lies within w/2 of the segment: gap-free diagonals at any width
hz_internal void vx_art_stroke(VxArtCanvas *c, f32 x0, f32 y0, f32 x1, f32 y1, f32 w, u8 s) {
  f32 dx = x1 - x0, dy = y1 - y0, l2 = m_maxf(dx * dx + dy * dy, 1e-6f), r2 = w * w * 0.25f;
  for (i32 y = 0; y < VX_ART_N; y++)
    for (i32 x = 0; x < VX_ART_N; x++) {
      f32 px = x + 0.5f - x0, py = y + 0.5f - y0;
      f32 t = m_clampf((px * dx + py * dy) / l2, 0, 1);
      f32 ex = px - t * dx, ey = py - t * dy;
      if (ex * ex + ey * ey <= r2) vx_art_px(c, x, y, s);
    }
}

// a disc shaded from a light upper-left to a dark lower-right
hz_internal void vx_art_ball(VxArtCanvas *c, f32 cx, f32 cy, f32 r, u8 dark, u8 mid, u8 lite) {
  for (i32 y = 0; y < VX_ART_N; y++)
    for (i32 x = 0; x < VX_ART_N; x++) {
      f32 dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy > r * r) continue;
      f32 k = (dx + dy) / r;
      vx_art_px(c, x, y, k > 0.5f ? dark : k < -0.6f ? lite : mid);
    }
}

// a wooden handle along a diagonal, lit on its upper edge
hz_internal void vx_art_handle(VxArtCanvas *c, i32 x0, i32 y0, i32 x1, i32 y1) {
  vx_art_line(c, x0, y0, x1, y1, S_W1);
  vx_art_line(c, x0, y0 - 1, x1, y1 - 1, S_W2);
}

hz_internal void vx_art_draw(VxArtCanvas *c, VxItemArt art) {
  switch (art) {
  case VX_ART_PICKAXE: {
    vx_art_stroke(c, 2.5f, 13.5f, 10.5f, 5.5f, 1.6f, S_W1);
    vx_art_stroke(c, 2.5f, 12.8f, 10.0f, 5.2f, 0.9f, S_W2);
    // a crescent head across the handle's top, its points bending back toward the grip
    f32 px = 0, py = 0;
    for (i32 k = 0; k <= 12; k++) {
      f32 t = (f32)k / 6.0f - 1.0f, bend = t * t * 2.6f;
      f32 x = 10.6f + t * 5.6f - bend, y = 5.4f + t * 5.6f + bend - 2.0f * t;
      if (k > 0) {
        vx_art_stroke(c, px, py, x, y, 2.4f, S_M2);
        vx_art_stroke(c, px - 0.5f, py - 0.5f, x - 0.5f, y - 0.5f, 1.0f, S_M3);
      }
      px = x, py = y;
    }
    vx_art_stroke(c, 7.0f, 2.2f, 10.0f, 4.0f, 0.9f, S_M4);
    vx_art_px(c, 10, 5, S_M1);
  } break;
  case VX_ART_AXE: {
    vx_art_stroke(c, 2.5f, 13.5f, 11.5f, 4.5f, 1.6f, S_W1);
    vx_art_stroke(c, 2.5f, 12.8f, 11.0f, 4.2f, 0.9f, S_W2);
    // a broad blade off one side of the haft's top
    for (i32 y = 1; y <= 9; y++)
      for (i32 x = 8; x <= 15; x++) {
        f32 u = (f32)x - 9.5f, v = (f32)y - 4.5f;
        if (u < 0 || u * 0.55f + m_absf(v) * 0.8f > 3.8f || u + v < -1.5f) continue;
        vx_art_px(c, x, y, u > 3.6f ? S_M4 : v < -1.5f ? S_M3 : S_M2);
      }
    vx_art_stroke(c, 14.2f, 2.0f, 14.2f, 7.5f, 0.9f, S_M4);
  } break;
  case VX_ART_BROADSWORD:
  case VX_ART_SHORTSWORD: {
    b32 big = art == VX_ART_BROADSWORD;
    f32 tip = big ? 14.4f : 12.4f, base = big ? 4.6f : 6.6f, w = big ? 2.6f : 2.0f;
    f32 by = 16.0f - base, ty = 16.0f - tip;
    vx_art_stroke(c, base, by, tip, ty, w, S_M2);
    vx_art_stroke(c, base - 0.2f, by - 0.6f, tip - 0.6f, ty + 0.2f, w * 0.45f, S_M3);
    vx_art_stroke(c, base + 0.6f, by - 1.0f, tip - 1.0f, ty + 0.6f, 0.8f, S_M4);
    // cross guard, grip and pommel
    vx_art_stroke(c, base - 2.2f, by - 2.2f, base + 2.2f, by + 2.2f, 1.6f, S_M1);
    vx_art_stroke(c, base - 0.8f, by + 0.8f, 2.0f, 14.0f, 1.5f, S_W1);
    vx_art_disc(c, 1.8f, 14.2f, 1.2f, S_M3);
  } break;
  case VX_ART_BOW: {
    for (i32 t = -6; t <= 6; t++) {
      f32 u = (f32)t / 6.0f;
      i32 o = (i32)((1 - u * u) * 4.0f);
      vx_art_px(c, 8 + t + o, 8 + t - o, t > 0 ? S_W1 : S_W2);
      vx_art_px(c, 9 + t + o, 8 + t - o, S_W1);
    }
    vx_art_line(c, 3, 2, 13, 13, S_WHITE);
  } break;
  case VX_ART_ARROW: {
    vx_art_line(c, 3, 12, 11, 4, S_W2);
    vx_art_line(c, 4, 12, 11, 5, S_W1);
    vx_art_line(c, 11, 2, 13, 4, S_M4);
    vx_art_line(c, 10, 3, 13, 3, S_M3);
    vx_art_line(c, 12, 2, 12, 5, S_M3);
    vx_art_line(c, 1, 12, 3, 14, S_WHITE);
    vx_art_line(c, 2, 11, 4, 13, S_WHITE);
  } break;
  case VX_ART_WAND: {
    vx_art_handle(c, 3, 13, 10, 6);
    vx_art_ball(c, 11.5f, 4.5f, 2.6f, S_A1, S_A3, S_WHITE);
  } break;
  case VX_ART_BAR: {
    // an ingot seen from above: a light top face over a darker front
    for (i32 y = 5; y <= 11; y++)
      for (i32 x = 2; x <= 13; x++) {
        i32 sx = x - (11 - y) / 2;
        if (sx < 2 || sx > 11) continue;
        vx_art_px(c, x, y, y <= 8 ? (x + y == 13 ? S_WHITE : S_M4) : y == 11 ? S_M1 : S_M2);
      }
  } break;
  case VX_ART_GEL: {
    vx_art_ball(c, 8, 9.5f, 5.0f, S_M2, S_M3, S_M4);
    vx_art_line(c, 3, 13, 12, 13, S_M2);
    vx_art_px(c, 6, 7, S_WHITE);
    vx_art_px(c, 6, 8, S_WHITE);
  } break;
  case VX_ART_LENS: {
    vx_art_ball(c, 8, 8, 6.0f, S_A2, S_WHITE, S_WHITE);
    vx_art_ball(c, 8.5f, 8.5f, 3.2f, S_M1, S_M2, S_M3);
    vx_art_px(c, 7, 7, S_WHITE);
  } break;
  case VX_ART_STAR:
  case VX_ART_MANA_CRYSTAL: {
    b32 star = art == VX_ART_STAR;
    for (i32 y = 0; y < VX_ART_N; y++)
      for (i32 x = 0; x < VX_ART_N; x++) {
        f32 u = (x + 0.5f - 8) / 7.0f, v = (8 - (y + 0.5f)) / 7.0f;
        f32 r = m_sqrtf(u * u + v * v), ang = m_absf(m_fmodf(m_atan2f(u, v) + PI * 3.4f, PI * 0.4f) - PI * 0.2f);
        f32 qx = r * m_sinf(ang), qy = r * m_cosf(ang), ri = 0.45f;
        f32 ex = ri * m_sinf(PI * 0.2f), ey = ri * m_cosf(PI * 0.2f) - 1;
        if (ex * (qy - 1) - ey * qx > 0) continue;
        u8 s = u + v > 0.25f ? (star ? S_A4 : S_A4) : u + v > -0.35f ? S_A3 : S_A2;
        vx_art_px(c, x, y, s);
      }
    vx_art_px(c, 7, 6, S_WHITE);
  } break;
  case VX_ART_POTION: {
    vx_art_ball(c, 8, 10, 4.6f, S_A1, S_A2, S_A3);
    for (i32 y = 4; y <= 6; y++) vx_art_line(c, 7, y, 8, y, S_WHITE);
    vx_art_line(c, 6, 3, 9, 3, S_W1);
    vx_art_px(c, 6, 8, S_WHITE);
    vx_art_px(c, 6, 9, S_A4);
  } break;
  case VX_ART_LIFE_CRYSTAL: {
    // a faceted heart
    for (i32 y = 0; y < VX_ART_N; y++)
      for (i32 x = 0; x < VX_ART_N; x++) {
        f32 u = (x + 0.5f - 8) / 6.0f, v = (7 - (y + 0.5f)) / 6.0f;
        f32 a = u * u + v * v - 1;
        if (a * a * a - u * u * v * v * v > 0) continue;
        vx_art_px(c, x, y, u < -0.2f && v > 0.1f ? S_A4 : u + v > 0.3f ? S_A3 : v < -0.4f ? S_A1 : S_A2);
      }
    vx_art_px(c, 5, 5, S_WHITE);
  } break;
  case VX_ART_MIRROR: {
    vx_art_ball(c, 8, 7, 5.6f, S_M1, S_M2, S_M3);
    vx_art_ball(c, 8, 7, 4.0f, S_A2, S_A3, S_WHITE);
    vx_art_line(c, 8, 12, 8, 15, S_M2);
    vx_art_line(c, 6, 4, 6, 5, S_WHITE);
  } break;
  case VX_ART_EYE: {
    vx_art_ball(c, 8, 8, 6.2f, S_A1, S_WHITE, S_WHITE);
    vx_art_line(c, 3, 9, 6, 8, S_A2);
    vx_art_line(c, 12, 11, 10, 9, S_A2);
    vx_art_ball(c, 9, 7, 2.8f, S_M2, S_M3, S_M4);
    vx_art_disc(c, 9, 7, 1.2f, S_INK);
  } break;
  case VX_ART_HOOK: {
    vx_art_line(c, 1, 14, 7, 8, S_M2);
    for (i32 i = 0; i < 6; i += 2) vx_art_px(c, 2 + i, 13 - i, S_M4);
    // the claw: three prongs
    vx_art_line(c, 8, 7, 13, 2, S_M3);
    vx_art_line(c, 9, 7, 13, 7, S_M3);
    vx_art_line(c, 8, 6, 8, 2, S_M3);
    vx_art_px(c, 13, 3, S_M4);
    vx_art_px(c, 12, 7, S_M4);
  } break;
  case VX_ART_BOTTLE: {
    vx_art_ball(c, 8, 10, 4.8f, S_M2, S_M3, S_M4);
    vx_art_disc(c, 7.5f, 10, 2.6f, S_A4);
    vx_art_px(c, 9, 9, S_A3);
    for (i32 y = 3; y <= 5; y++) vx_art_line(c, 7, y, 8, y, S_M3);
    vx_art_line(c, 6, 2, 9, 2, S_W1);
  } break;
  case VX_ART_BOOTS: {
    for (i32 y = 3; y <= 13; y++) vx_art_line(c, 4, y, 8, y, y < 5 ? S_A4 : S_M2);
    for (i32 y = 10; y <= 13; y++) vx_art_line(c, 8, y, 12, y, S_M2);
    vx_art_line(c, 4, 13, 12, 13, S_M1);
    vx_art_line(c, 9, 4, 13, 2, S_WHITE);
    vx_art_line(c, 9, 6, 14, 5, S_WHITE);
    vx_art_line(c, 9, 8, 13, 8, S_WHITE);
  } break;
  case VX_ART_WINGS: {
    for (i32 s = 0; s < 2; s++)
      for (i32 k = 0; k < 4; k++) {
        i32 len = 6 - k, y = 4 + k * 2;
        i32 x0 = s ? 9 : 6, x1 = s ? 9 + len : 6 - len;
        vx_art_line(c, x0, y, x1, y - 2 + k / 2, k == 0 ? S_M3 : S_M4);
        vx_art_line(c, x0, y + 1, x1, y - 1 + k / 2, S_M3);
      }
    vx_art_line(c, 7, 4, 8, 4, S_M2);
  } break;
  case VX_ART_MINING_HELMET:
  case VX_ART_HELMET: {
    for (i32 y = 3; y <= 11; y++)
      for (i32 x = 2; x <= 13; x++) {
        f32 u = (x + 0.5f - 8) / 6.0f, v = (11 - (y + 0.5f)) / 8.0f;
        if (u * u + v * v > 1) continue;
        vx_art_px(c, x, y, u < -0.3f && v > 0.4f ? S_M4 : v < 0.2f ? S_M2 : S_M3);
      }
    vx_art_line(c, 1, 11, 14, 11, S_M1);
    if (art == VX_ART_MINING_HELMET) {
      vx_art_ball(c, 8, 7, 2.1f, S_A2, S_A3, S_WHITE);
    } else {
      vx_art_line(c, 5, 8, 10, 8, S_INK);
    }
  } break;
  case VX_ART_CHAINMAIL: {
    for (i32 y = 3; y <= 13; y++)
      for (i32 x = 3; x <= 12; x++) {
        if (y < 6 && x > 5 && x < 10) continue;
        if (y > 7 && (x < 4 || x > 11)) continue;
        vx_art_px(c, x, y, (x + y) % 2 ? S_M2 : S_M3);
      }
  } break;
  case VX_ART_GREAVES: {
    for (i32 y = 3; y <= 13; y++) {
      vx_art_line(c, 3, y, 6, y, y > 11 ? S_M1 : (y % 3 ? S_M3 : S_M2));
      vx_art_line(c, 9, y, 12, y, y > 11 ? S_M1 : (y % 3 ? S_M3 : S_M2));
    }
    vx_art_line(c, 3, 3, 12, 3, S_M4);
  } break;
  case VX_ART_GLOWSTICK: {
    vx_art_line(c, 4, 12, 11, 5, S_A3);
    vx_art_line(c, 5, 12, 11, 6, S_A2);
    vx_art_line(c, 4, 11, 10, 5, S_A4);
    vx_art_line(c, 3, 13, 4, 12, S_M1);
  } break;
  case VX_ART_GEM: {
    // a cut stone: a table facet on top, crown facets either side, the pavilion narrowing to the culet
    for (i32 y = 2; y <= 14; y++)
      for (i32 x = 1; x <= 14; x++) {
        f32 u = x + 0.5f - 8, v = y + 0.5f;
        b32 crown = v >= 3 && v <= 7 && m_absf(u) <= 3.5f + (v - 3) * 0.9f;
        b32 pav = v > 7 && m_absf(u) <= (14.5f - v) * 0.97f;
        if (!crown && !pav) continue;
        u8 s = crown ? (m_absf(u) < 2.5f ? S_M4 : u < 0 ? S_M3 : S_M2) : (u < -0.5f ? S_M3 : u > 1.5f ? S_M1 : S_M2);
        vx_art_px(c, x, y, s);
      }
    vx_art_px(c, 6, 4, S_WHITE);
    vx_art_px(c, 7, 4, S_WHITE);
  } break;
  case VX_ART_COIN: {
    // a coin turned a little toward the light: a rim, a raised face, a struck mark
    for (i32 y = 2; y <= 13; y++)
      for (i32 x = 3; x <= 12; x++) {
        f32 u = (x + 0.5f - 8) / 5.0f, v = (y + 0.5f - 8) / 6.0f, d = u * u + v * v;
        if (d > 1.0f) continue;
        u8 s = d > 0.62f ? (u + v < 0 ? S_M3 : S_M1) : (u + v < -0.5f ? S_M4 : (d < 0.3f ? S_M3 : S_M2));
        vx_art_px(c, x, y, s);
      }
    vx_art_line(c, 7, 6, 7, 9, S_M1);
    vx_art_line(c, 8, 6, 8, 9, S_M4);
    vx_art_px(c, 5, 4, S_WHITE);
  } break;
  default: break;
  }
}

// a ramp that cools as it darkens and warms as it lightens
hz_internal u32 vx_art_shade(u32 base, f32 k) {
  f32 r = (f32)((base >> 16) & 255), g = (f32)((base >> 8) & 255), b = (f32)(base & 255);
  if (k < 1) {
    r *= k * 0.94f, g *= k * 0.97f, b *= k * 1.06f;
  } else {
    f32 t = k - 1;
    r += (255 - r) * t, g += (240 - g) * t * 0.95f, b += (215 - b) * t * 0.9f;
  }
  u32 ri = (u32)m_clampf(r, 0, 255), gi = (u32)m_clampf(g, 0, 255), bi = (u32)m_clampf(b, 0, 255);
  return ri << 16 | gi << 8 | bi;
}

void vx_item_art_pixels(VxItemId id, u8 *rgba) {
  const VxItemInfo *it = vx_item(id);
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  VxArtCanvas *cp = ALLOC(&tmp.allocator, VxArtCanvas);
  u32 *pal = ALLOC_ARRAY(&tmp.allocator, u32, S_SLOTS);
  vx_art_draw(cp, (VxItemArt)it->art);
  const VxArtCanvas *cv = cp;
  u32 mat = VX_MAT_BASE[it->mat], acc = vx_art_accent((VxItemArt)it->art, (VxItemMat)it->mat);
  u32 wood = VX_MAT_BASE[VX_IM_WOOD];
  pal[S_M1] = vx_art_shade(mat, 0.5f), pal[S_M2] = vx_art_shade(mat, 0.75f), pal[S_M3] = mat;
  pal[S_M4] = vx_art_shade(mat, 1.35f);
  pal[S_W1] = vx_art_shade(wood, 0.7f), pal[S_W2] = wood;
  pal[S_A1] = vx_art_shade(acc, 0.5f), pal[S_A2] = vx_art_shade(acc, 0.75f), pal[S_A3] = acc;
  pal[S_A4] = vx_art_shade(acc, 1.35f);
  pal[S_WHITE] = 0xFFFDF4, pal[S_INK] = 0x1A1418;
  for (i32 y = 0; y < VX_ART_N; y++)
    for (i32 x = 0; x < VX_ART_N; x++) {
      u8 *o = rgba + (y * VX_ART_N + x) * 4;
      u8 s = cv->px[y * VX_ART_N + x];
      u32 col = 0, a = 0;
      if (s) {
        col = pal[s], a = 255;
      } else {
        // outline: an empty pixel touching the silhouette takes a dark tone of its neighbour
        u8 nb = 0;
        if (x > 0 && cv->px[y * VX_ART_N + x - 1]) nb = cv->px[y * VX_ART_N + x - 1];
        else if (x < VX_ART_N - 1 && cv->px[y * VX_ART_N + x + 1]) nb = cv->px[y * VX_ART_N + x + 1];
        else if (y > 0 && cv->px[(y - 1) * VX_ART_N + x]) nb = cv->px[(y - 1) * VX_ART_N + x];
        else if (y < VX_ART_N - 1 && cv->px[(y + 1) * VX_ART_N + x]) nb = cv->px[(y + 1) * VX_ART_N + x];
        if (nb) col = vx_art_shade(pal[nb], 0.28f), a = 255;
      }
      o[0] = (u8)(col >> 16), o[1] = (u8)(col >> 8), o[2] = (u8)col, o[3] = (u8)a;
    }
  tctx_temp_allocator_end(tmp);
}

void vx_item_icons_make(GpuTexture *out) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  const u32 K = 4, S = VX_ART_N * K;
  u8 *art = ALLOC_ARRAY(&tmp.allocator, u8, VX_ART_N * VX_ART_N * 4);
  u8 *px = ALLOC_ARRAY(&tmp.allocator, u8, S * S * 4);
  for (u32 id = B_COUNT; id < I_COUNT; id++) {
    vx_item_art_pixels((VxItemId)id, art);
    for (u32 y = 0; y < S; y++)
      for (u32 x = 0; x < S; x++) mem_cpy(px + (y * S + x) * 4, art + ((y / K) * VX_ART_N + x / K) * 4, 4);
    out[id] = gpu_make_texture(&(GpuTextureDesc){.data = px, .size = S * S * 4, .data_type = GPU_TEXTURE_DATA_RGBA8,
                                                 .width = S, .height = S, .is_srgb = true});
  }
  tctx_temp_allocator_end(tmp);
}

#undef VX_ART_N

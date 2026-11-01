#include "voxaria/vx_game.h"
#include "procgen/pg_dump.h"

// isometric block icons rasterised on the cpu from the texture pixels (canvas drawImage semantics)

#define VX_ICON_SS 4
#define VX_ICON_FACES 3

typedef struct {
  f32 a, b, c, d, e, f; // x' = a x + c y + e, y' = b x + d y + f
  f32 inv_a, inv_b, inv_c, inv_d;
  u8 *rgba; // tw x th face after tint and shade
  u32 tw, th;
} VxIconFace;

// Uint8ClampedArray store: clamp, round half to even
force_inline u8 vx_icon_u8(f32 v) {
  if (!(v > 0)) return 0;
  if (v >= 255) return 255;
  f32 f = m_floorf(v), d = v - f;
  if (d > 0.5f) return (u8)(f + 1);
  if (d < 0.5f) return (u8)f;
  return ((i32)f % 2 == 0) ? (u8)f : (u8)(f + 1);
}

// variant 0 of texture tex, shaded
hz_internal void vx_face_pixels(u8 *out, const VxTextureData *tex, u32 t, f32 shade) {
  const u8 *px = tex->pixels + vx_tex_layer[t] * 256 * 4;
  b32 cut = tex->cutout[t];
  for (u32 i = 0; i < 256; i++) {
    out[i * 4] = vx_icon_u8(px[i * 4] * shade);
    out[i * 4 + 1] = vx_icon_u8(px[i * 4 + 1] * shade);
    out[i * 4 + 2] = vx_icon_u8(px[i * 4 + 2] * shade);
    out[i * 4 + 3] = cut ? px[i * 4 + 3] : 255;
  }
}

hz_internal void vx_face_transform(VxIconFace *f, f32 a, f32 b, f32 c, f32 d, f32 e, f32 g) {
  f->a = a;
  f->b = b;
  f->c = c;
  f->d = d;
  f->e = e;
  f->f = g;
  f32 det = a * d - b * c;
  f->inv_a = d / det;
  f->inv_b = -b / det;
  f->inv_c = -c / det;
  f->inv_d = a / det;
}

// source-over in display space with 4x4 supersampled coverage (the canvas antialiases quad edges)
hz_internal void vx_icon_raster(u8 *dst, u32 size, const VxIconFace *faces, u32 count, f32 alpha) {
  f32 inv_ss = 1.0f / VX_ICON_SS;
  for (u32 py = 0; py < size; py++) {
    for (u32 px = 0; px < size; px++) {
      f32 acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
      for (u32 sy = 0; sy < VX_ICON_SS; sy++) {
        for (u32 sx = 0; sx < VX_ICON_SS; sx++) {
          f32 x = px + (sx + 0.5f) * inv_ss, y = py + (sy + 0.5f) * inv_ss;
          f32 cr = 0, cg = 0, cb = 0, ca = 0;
          for (u32 k = 0; k < count; k++) {
            const VxIconFace *f = &faces[k];
            f32 rx = x - f->e, ry = y - f->f;
            f32 u = f->inv_a * rx + f->inv_c * ry;
            f32 v = f->inv_b * rx + f->inv_d * ry;
            if (u < 0 || v < 0 || u >= (f32)f->tw || v >= (f32)f->th) continue;
            const u8 *t = f->rgba + ((u32)v * f->tw + (u32)u) * 4;
            f32 sa = t[3] / 255.0f * alpha;
            cr = t[0] * sa + cr * (1 - sa);
            cg = t[1] * sa + cg * (1 - sa);
            cb = t[2] * sa + cb * (1 - sa);
            ca = sa + ca * (1 - sa);
          }
          acc_r += cr;
          acc_g += cg;
          acc_b += cb;
          acc_a += ca;
        }
      }
      f32 n = VX_ICON_SS * VX_ICON_SS;
      u8 *o = dst + (py * size + px) * 4;
      f32 a = acc_a / n;
      // un-premultiply the accumulated colour
      o[0] = a > 0 ? (u8)m_minf(255, acc_r / n / a + 0.5f) : 0;
      o[1] = a > 0 ? (u8)m_minf(255, acc_g / n / a + 0.5f) : 0;
      o[2] = a > 0 ? (u8)m_minf(255, acc_b / n / a + 0.5f) : 0;
      o[3] = (u8)(a * 255 + 0.5f);
    }
  }
}

// texel (x, y) of tile t's first variant, shaded, wrapping into the tile
hz_internal void vx_tile_texel(u8 *out, const VxTextureData *tex, u32 t, i32 x, i32 y, f32 shade) {
  const u8 *px = tex->pixels + (vx_tex_layer[t] * 256 + (u32)(y & 15) * 16 + (u32)(x & 15)) * 4;
  out[0] = vx_icon_u8(px[0] * shade);
  out[1] = vx_icon_u8(px[1] * shade);
  out[2] = vx_icon_u8(px[2] * shade);
  out[3] = tex->cutout[t] ? px[3] : 255;
}

// a model's boxes in the cube icons' view, far boxes first; furniture is scaled so its whole footprint fits
hz_internal void vx_icon_model(u8 *img, u32 size, const VxTextureData *tex, u8 id, Allocator *ta) {
  const VxBox *boxes;
  u32 count;
  f32 W = 16, H = 16;
  if (vx_is_furniture(id)) {
    VxFurniture fu = vx_furniture[id];
    boxes = vx_furn_boxes + fu.first, count = fu.count;
    W = (f32)fu.w * 16, H = (f32)fu.h * 16;
  } else {
    boxes = vx_boxes + vx_model[id].first, count = vx_model[id].count;
  }
  f32 s = (f32)size, q = s / 16.0f * 0.94f, sc = 16.0f / m_maxf(W, H);
  f32 offx = (16 - W * sc) * 0.5f, offz = (16 - 16 * sc) * 0.5f;
  // screen steps per unit along the view's three axes: away (-z), right (+x) and down (-y)
  f32 ax = 0.5f * q, ay = -0.25f * q, bx = 0.5f * q, by = 0.25f * q, dy = 0.5f * q;
  f32 o_x = s * 0.03f, o_y = s * 0.25f;
  u32 *order = ALLOC_ARRAY(ta, u32, count);
  for (u32 i = 0; i < count; i++) {
    u32 j = i, key = (u32)boxes[i].lo[0] + boxes[i].hi[0] + boxes[i].lo[2] + boxes[i].hi[2] + boxes[i].lo[1] + boxes[i].hi[1];
    while (j > 0) {
      const VxBox *p = &boxes[order[j - 1]];
      if ((u32)p->lo[0] + p->hi[0] + p->lo[2] + p->hi[2] + p->lo[1] + p->hi[1] <= key) break;
      order[j] = order[j - 1];
      j--;
    }
    order[j] = i;
  }
  VxIconFace *faces = ALLOC_ARRAY(ta, VxIconFace, count * 3);
  u32 nf = 0;
  for (u32 i = 0; i < count; i++) {
    const VxBox *b = &boxes[order[i]];
    i32 x0 = b->lo[0], x1 = b->hi[0], y0 = b->lo[1], y1 = b->hi[1], z0 = b->lo[2], z1 = b->hi[2];
    f32 X0 = x0 * sc + offx, X1 = x1 * sc + offx, Y1 = y1 * sc, Z0 = z0 * sc + offz, Z1 = z1 * sc + offz;
    for (u32 k = 0; k < 3; k++) {
      u32 f = k == 0 ? VX_FACE_PY : k == 1 ? VX_FACE_PZ : VX_FACE_PX;
      u32 t = b->tex[f] != VX_BOX_TEX_BLOCK ? b->tex[f] : vx_face_tex[id][f];
      f32 shade = k == 0 ? 1.0f : k == 1 ? 0.78f : 0.6f;
      VxIconFace *fc = &faces[nf++];
      fc->tw = (u32)(k == 2 ? z1 - z0 : x1 - x0);
      fc->th = (u32)(k == 0 ? z1 - z0 : y1 - y0);
      fc->rgba = ALLOC_ARRAY(ta, u8, fc->tw * fc->th * 4);
      for (u32 v = 0; v < fc->th; v++)
        for (u32 u = 0; u < fc->tw; u++) {
          i32 tx = k == 2 ? z1 - 1 - (i32)u : x0 + (i32)u, ty = k == 0 ? z0 + (i32)v : 16 - (y1 - (i32)v);
          vx_tile_texel(fc->rgba + (v * fc->tw + u) * 4, tex, t, tx, ty, shade);
        }
      // each face's origin corner: far-left of the top, top-left of the front, top-front of the right side
      f32 X = k == 2 ? X1 : X0, Z = k == 0 ? Z0 : Z1;
      f32 ex = o_x + (16 - Z) * ax + X * bx, ey = o_y + (16 - Z) * ay + X * by + (16 - Y1) * dy;
      if (k == 0) vx_face_transform(fc, sc * bx, sc * by, -sc * ax, -sc * ay, ex, ey);
      else if (k == 1) vx_face_transform(fc, sc * bx, sc * by, 0, sc * dy, ex, ey);
      else vx_face_transform(fc, sc * ax, sc * ay, 0, sc * dy, ex, ey);
    }
  }
  vx_icon_raster(img, size, faces, nf, 1.0f);
}

void vx_icons_raster(u8 *pixels, const VxTextureData *tex, u32 size) {
  Range(u32) r = lane_range(B_COUNT - 1);
  vx_icons_raster_range(pixels, tex, size, r.min, r.max);
}

void vx_icons_raster_range(u8 *pixels, const VxTextureData *tex, u32 size, u32 first, u32 end) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;
  VxIconFace *faces = ALLOC_ARRAY(ta, VxIconFace, VX_ICON_FACES);
  for (u32 k = 0; k < VX_ICON_FACES; k++) {
    faces[k].rgba = ALLOC_ARRAY(ta, u8, 256 * 4);
    faces[k].tw = faces[k].th = 16;
  }
  for (u32 i = first; i < end; i++) {
    u32 id = i + 1;
    u8 *img = pixels + id * size * size * 4;
    f32 s = (f32)size, k = s / 16.0f;
    if (vx_shape[id] == VX_SHAPE_MODEL) {
      vx_icon_model(img, size, tex, (u8)id, ta);
    } else if (vx_shape[id] == VX_SHAPE_CROSS || vx_shape[id] == VX_SHAPE_TORCH) {
      vx_face_pixels(faces[0].rgba, tex, vx_face_tex[id][0], 1.0f);
      vx_face_transform(&faces[0], k * 0.8f, 0, 0, k * 0.8f, s * 0.1f, s * 0.1f);
      vx_icon_raster(img, size, faces, 1, 1.0f);
    } else {
      b32 translucent = vx_layer[id] == VX_LAYER_TRANSLUCENT || vx_shape[id] == VX_SHAPE_LIQUID;
      vx_face_pixels(faces[0].rgba, tex, vx_face_tex[id][VX_FACE_PY], 1.0f);
      vx_face_pixels(faces[1].rgba, tex, vx_face_tex[id][VX_FACE_PZ], 0.78f);
      vx_face_pixels(faces[2].rgba, tex, vx_face_tex[id][VX_FACE_PX], 0.6f);
      f32 q = k * 0.94f;
      vx_face_transform(&faces[0], 0.5f * q, -0.25f * q, 0.5f * q, 0.25f * q, s * 0.03f, s * 0.25f);
      vx_face_transform(&faces[1], 0.5f * q, 0.25f * q, 0, 0.5f * q, s * 0.03f, s * 0.25f);
      vx_face_transform(&faces[2], 0.5f * q, -0.25f * q, 0, 0.5f * q, s * 0.5f, s * 0.485f);
      vx_icon_raster(img, size, faces, VX_ICON_FACES, translucent ? 0.85f : 1.0f);
    }
  }
  tctx_temp_allocator_end(tmp);
}

void vx_icons_upload(GpuTexture *out, u8 *pixels, u32 size) {
  for (u32 id = 1; id < B_COUNT; id++)
    out[id] = gpu_make_texture(&(GpuTextureDesc){.data = pixels + id * size * size * 4, .size = size * size * 4,
                                                 .data_type = GPU_TEXTURE_DATA_RGBA8, .width = size, .height = size,
                                                 .is_srgb = true});
}

#define VX_HUD_ART 16

#define VX_HEART_W 14
#define VX_HEART_H 12
hz_internal const char *const VX_HEART_ART[VX_HEART_H] = {
    "..XXXX..XXXX..", ".XXXXXXXXXXXX.", "XXXXXXXXXXXXXX", "XXXXXXXXXXXXXX", "XXXXXXXXXXXXXX", "XXXXXXXXXXXXXX",
    ".XXXXXXXXXXXX.", "..XXXXXXXXXX..", "...XXXXXXXX...", "....XXXXXX....", ".....XXXX.....", "......XX......",
};

// inside test for the hud pictures, on a 16-pixel art grid centred at 7.5
hz_internal b32 vx_hud_inside(VxHudIcon icon, f32 x, f32 y) {
  f32 u = (x - 7.5f) / 6.6f, v = (7.0f - y) / 6.6f;
  switch (icon) {
  case VX_HUD_HEART:
  case VX_HUD_HEART_EMPTY: {
    i32 c = (i32)x - 1, r = (i32)y - 2;
    return c >= 0 && r >= 0 && c < VX_HEART_W && r < VX_HEART_H && VX_HEART_ART[r][c] == 'X';
  }
  case VX_HUD_STAR:
  case VX_HUD_STAR_EMPTY: {
    // folded into one point's sector: inside the edge from the point (0, 1) to the inner corner
    f32 r = m_sqrtf(u * u + v * v), ang = m_absf(m_fmodf(m_atan2f(u, v) + PI * 3.4f, PI * 0.4f) - PI * 0.2f);
    f32 qx = r * m_sinf(ang), qy = r * m_cosf(ang), ri = 0.47f;
    f32 ex = ri * m_sinf(PI * 0.2f), ey = ri * m_cosf(PI * 0.2f) - 1;
    return ex * (qy - 1) - ey * qx <= 0;
  }
  default: return u * u + v * v <= 0.8f;
  }
}

b32 vx_icons_dump(const VxTextureData *tex, const char *path) {
  // every item on a checker: the block icons, then the item art 4x nearest
  const u32 size = 64, gap = 8, per_row = 16;
  u32 count = I_COUNT - 1, rows = (count + per_row - 1) / per_row;
  u32 w = per_row * (size + gap) + gap, h = rows * (size + gap) + gap;
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u8 *img = ALLOC_ARRAY(&ta.allocator, u8, w * h * 4);
  u8 *blocks = ALLOC_ARRAY(&ta.allocator, u8, B_COUNT * size * size * 4);
  u8 *art = ALLOC_ARRAY(&ta.allocator, u8, 16 * 16 * 4);
  vx_icons_raster_range(blocks, tex, size, 0, B_COUNT - 1);
  for (u32 i = 0; i < w * h; i++) img[i * 4] = 24, img[i * 4 + 1] = 22, img[i * 4 + 2] = 30, img[i * 4 + 3] = 255;
  for (u32 id = 1; id < I_COUNT; id++) {
    u32 cell = id - 1, cx = (cell % per_row) * (size + gap) + gap, cy = (cell / per_row) * (size + gap) + gap;
    if (id >= B_COUNT) vx_item_art_pixels((VxItemId)id, art);
    for (u32 y = 0; y < size; y++)
      for (u32 x = 0; x < size; x++) {
        const u8 *s = id < B_COUNT ? blocks + ((id * size + y) * size + x) * 4 : art + ((y / 4) * 16 + x / 4) * 4;
        u8 *d = img + ((cy + y) * w + cx + x) * 4;
        f32 a = (f32)s[3] / 255.0f;
        f32 bg = ((x / 8 + y / 8) & 1) ? 70.0f : 110.0f;
        for (u32 c = 0; c < 3; c++) d[c] = (u8)((f32)s[c] * a + bg * (1.0f - a) + 0.5f);
      }
  }
  b32 ok = pg_dump_write_png(path, img, w, h, &ta.allocator);
  tctx_temp_allocator_end(ta);
  return ok;
}

// heart, star and bubble pixel art, 4x nearest-upscaled so the ui's filtering keeps the art pixels
void vx_hud_icons_make(GpuTexture *out) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  const u32 S = VX_HUD_ICON_SIZE, K = S / VX_HUD_ART;
  u8 *px = ALLOC_ARRAY(&tmp.allocator, u8, S * S * 4);
  u8 *in = ALLOC_ARRAY(&tmp.allocator, u8, VX_HUD_ART * VX_HUD_ART);
  for (u32 icon = 0; icon < VX_HUD_ICON_COUNT; icon++) {
    for (u32 y = 0; y < VX_HUD_ART; y++)
      for (u32 x = 0; x < VX_HUD_ART; x++) in[y * VX_HUD_ART + x] = vx_hud_inside((VxHudIcon)icon, (f32)x, (f32)y);
    b32 heart = icon <= VX_HUD_HEART_EMPTY, empty = icon == VX_HUD_HEART_EMPTY || icon == VX_HUD_STAR_EMPTY;
    u32 base = heart ? 0xE8283C : icon == VX_HUD_BUBBLE ? 0x9FD8FF : 0x3C78F0;
    u32 lite = heart ? 0xFF9A9A : icon == VX_HUD_BUBBLE ? 0xFFFFFF : 0xA8CCFF;
    u32 dark = heart ? 0x9C0E20 : icon == VX_HUD_BUBBLE ? 0x4E9AD8 : 0x2440A8;
    u32 line = heart ? 0x3A0610 : 0x0A1440;
    for (u32 y = 0; y < VX_HUD_ART; y++)
      for (u32 x = 0; x < VX_HUD_ART; x++) {
        u32 c = 0, a = 0;
        if (in[y * VX_HUD_ART + x]) {
          b32 rim = x == 0 || y == 0 || x == VX_HUD_ART - 1 || y == VX_HUD_ART - 1 || !in[y * VX_HUD_ART + x - 1] ||
                    !in[y * VX_HUD_ART + x + 1] || !in[(y - 1) * VX_HUD_ART + x] || !in[(y + 1) * VX_HUD_ART + x];
          f32 lx = (f32)x - 5.0f, ly = (f32)y - 4.0f;
          if (rim) c = line, a = 255;
          else if (empty) c = 0x1A1418, a = 150;
          else if (icon == VX_HUD_BUBBLE) c = lx * lx + ly * ly < 3 ? lite : 0, a = lx * lx + ly * ly < 3 ? 230 : 40;
          else c = lx * lx + ly * ly < 5 ? lite : (f32)x + (f32)y > 17 ? dark : base, a = 255;
          if (icon == VX_HUD_BUBBLE && rim) c = dark, a = 220;
        }
        for (u32 j = 0; j < K; j++)
          for (u32 i = 0; i < K; i++) {
            u8 *o = px + ((y * K + j) * S + x * K + i) * 4;
            o[0] = (u8)(c >> 16), o[1] = (u8)(c >> 8), o[2] = (u8)c, o[3] = (u8)a;
          }
      }
    out[icon] = gpu_make_texture(&(GpuTextureDesc){.data = px, .size = S * S * 4, .data_type = GPU_TEXTURE_DATA_RGBA8,
                                                   .width = S, .height = S, .is_srgb = true});
  }
  tctx_temp_allocator_end(tmp);
}

#undef VX_HUD_ART
#undef VX_HEART_W
#undef VX_HEART_H
#undef VX_ICON_SS
#undef VX_ICON_FACES

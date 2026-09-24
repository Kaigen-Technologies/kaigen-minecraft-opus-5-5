#include "webcraft/wc_game.h"

// isometric block icons rasterised on the cpu from the texture pixels (canvas drawImage semantics)

#define WC_ICON_SS 4
#define WC_ICON_FACES 3

typedef struct {
  f32 a, b, c, d, e, f; // x' = a x + c y + e, y' = b x + d y + f
  f32 inv_a, inv_b, inv_c, inv_d;
  u8 *rgba; // 16x16 face after tint and shade
} WcIconFace;

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

// Uint8ClampedArray store: clamp, round half to even
force_inline u8 wc_icon_u8(f32 v) {
  if (!(v > 0)) return 0;
  if (v >= 255) return 255;
  f32 f = m_floorf(v), d = v - f;
  if (d > 0.5f) return (u8)(f + 1);
  if (d < 0.5f) return (u8)f;
  return ((i32)f % 2 == 0) ? (u8)f : (u8)(f + 1);
}

hz_internal void wc_face_pixels(u8 *out, const WcTextureData *tex, u32 layer, b32 tinted, v3 tint, f32 shade) {
  const u8 *px = tex->pixels + layer * 256 * 4;
  b32 cut = tex->cutout[layer];
  for (u32 i = 0; i < 256; i++) {
    f32 a = px[i * 4 + 3];
    f32 r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
    if (tinted) {
      // opaque textures use alpha as the tint mask
      f32 m = cut ? 1.0f : a / 255.0f;
      r *= 1 - m + m * tint.x;
      g *= 1 - m + m * tint.y;
      b *= 1 - m + m * tint.z;
    }
    out[i * 4] = wc_icon_u8(r * shade);
    out[i * 4 + 1] = wc_icon_u8(g * shade);
    out[i * 4 + 2] = wc_icon_u8(b * shade);
    out[i * 4 + 3] = cut ? (u8)a : 255;
  }
}

hz_internal void wc_face_transform(WcIconFace *f, f32 a, f32 b, f32 c, f32 d, f32 e, f32 g) {
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
hz_internal void wc_icon_raster(u8 *dst, u32 size, const WcIconFace *faces, u32 count, f32 alpha) {
  f32 inv_ss = 1.0f / WC_ICON_SS;
  for (u32 py = 0; py < size; py++) {
    for (u32 px = 0; px < size; px++) {
      f32 acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
      for (u32 sy = 0; sy < WC_ICON_SS; sy++) {
        for (u32 sx = 0; sx < WC_ICON_SS; sx++) {
          f32 x = px + (sx + 0.5f) * inv_ss, y = py + (sy + 0.5f) * inv_ss;
          f32 cr = 0, cg = 0, cb = 0, ca = 0;
          for (u32 k = 0; k < count; k++) {
            const WcIconFace *f = &faces[k];
            f32 rx = x - f->e, ry = y - f->f;
            f32 u = f->inv_a * rx + f->inv_c * ry;
            f32 v = f->inv_b * rx + f->inv_d * ry;
            if (u < 0 || v < 0 || u >= 16 || v >= 16) continue;
            const u8 *t = f->rgba + ((u32)v * 16 + (u32)u) * 4;
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
      f32 n = WC_ICON_SS * WC_ICON_SS;
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

void wc_icons_raster(u8 *pixels, const WcTextureData *tex, u32 size) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;
  WcIconFace *faces = ALLOC_ARRAY(ta, WcIconFace, WC_ICON_FACES);
  for (u32 k = 0; k < WC_ICON_FACES; k++) faces[k].rgba = ALLOC_ARRAY(ta, u8, 256 * 4);
  Range(u32) r = lane_range(B_COUNT - 1);
  for (u32 i = r.min; i < r.max; i++) {
    u32 id = i + 1;
    u8 *img = pixels + id * size * size * 4;
    v3 tint = wc_icon_tint((u8)id);
    b32 has_tint = wc_tint[id] != WC_TINT_NONE;
    f32 s = (f32)size, k = s / 16.0f;
    if (wc_shape[id] == WC_SHAPE_CROSS || wc_shape[id] == WC_SHAPE_TORCH) {
      wc_face_pixels(faces[0].rgba, tex, wc_face_tex[id][0], has_tint, tint, 1.0f);
      wc_face_transform(&faces[0], k * 0.8f, 0, 0, k * 0.8f, s * 0.1f, s * 0.1f);
      wc_icon_raster(img, size, faces, 1, 1.0f);
    } else {
      u8 t = wc_tint[id];
      b32 top_tint = id == B_GRASS || t == WC_TINT_FOLIAGE || t == WC_TINT_WATER || t == WC_TINT_BIRCH ||
                     t == WC_TINT_SPRUCE;
      b32 translucent = wc_layer[id] == WC_LAYER_TRANSLUCENT || wc_shape[id] == WC_SHAPE_LIQUID;
      wc_face_pixels(faces[0].rgba, tex, wc_face_tex[id][WC_FACE_PY], top_tint, tint, 1.0f);
      wc_face_pixels(faces[1].rgba, tex, wc_face_tex[id][WC_FACE_PZ], has_tint, tint, 0.78f);
      wc_face_pixels(faces[2].rgba, tex, wc_face_tex[id][WC_FACE_PX], has_tint, tint, 0.6f);
      f32 q = k * 0.94f;
      wc_face_transform(&faces[0], 0.5f * q, -0.25f * q, 0.5f * q, 0.25f * q, s * 0.03f, s * 0.25f);
      wc_face_transform(&faces[1], 0.5f * q, 0.25f * q, 0, 0.5f * q, s * 0.03f, s * 0.25f);
      wc_face_transform(&faces[2], 0.5f * q, -0.25f * q, 0, 0.5f * q, s * 0.5f, s * 0.485f);
      wc_icon_raster(img, size, faces, WC_ICON_FACES, translucent ? 0.85f : 1.0f);
    }
  }
  tctx_temp_allocator_end(tmp);
}

void wc_icons_upload(GpuTexture *out, u8 *pixels, u32 size) {
  for (u32 id = 1; id < B_COUNT; id++)
    out[id] = gpu_make_texture(&(GpuTextureDesc){.data = pixels + id * size * size * 4, .size = size * size * 4,
                                                 .data_type = GPU_TEXTURE_DATA_RGBA8, .width = size, .height = size,
                                                 .is_srgb = true});
}

#undef WC_ICON_SS
#undef WC_ICON_FACES

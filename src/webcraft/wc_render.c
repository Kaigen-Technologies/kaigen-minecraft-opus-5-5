#include "webcraft/wc_render.h"
#include "shaders/wc_transmittance.h"
#include "shaders/wc_multiscatter.h"
#include "shaders/wc_skyview.h"
#include "shaders/wc_sky_ambient.h"
#include "shaders/wc_terrain.h"
#include "shaders/wc_entity.h"
#include "shaders/wc_shadow.h"
#include "shaders/wc_shadow_clear.h"
#include "shaders/wc_water.h"
#include "shaders/wc_ssao.h"
#include "shaders/wc_clouds.h"
#include "shaders/wc_clouds_temporal.h"
#include "shaders/wc_deferred.h"
#include "shaders/wc_atmospherics.h"
#include "shaders/wc_shafts.h"
#include "shaders/wc_cull_expand.h"
#include "shaders/wc_taa.h"
#include "shaders/wc_bloom_down.h"
#include "shaders/wc_bloom_up.h"
#include "shaders/wc_exposure.h"
#include "shaders/wc_final.h"
#include "shaders/wc_noise_base.h"
#include "shaders/wc_noise_detail.h"
#include "shaders/wc_noise_weather.h"
#include "shaders/wc_noise_down3d.h"
#include "shaders/wc_noise_down2d.h"

static_assert(sizeof(WcFrameUniforms) == 1024, "WcFrameUniforms must mirror _Frame");
static_assert(sizeof(WcFrameUniforms) == sizeof(ShaderWcTerrainFrame), "WcFrameUniforms must mirror _Frame");

#define WC_SUN_ILLUM 12.0f
#define WC_MOON_ILLUM (WC_SUN_ILLUM * 0.0045f)
#define WC_SKY_W 192
#define WC_SKY_H 108
#define WC_CLOUD_BASE 780.0f
#define WC_CLOUD_THICK 700.0f
#define WC_NEAR 0.06f
#define WC_TILT 0.42f
#define WC_SHADOW_EXTRA 220.0f
#define WC_CASCADE_MARGIN 0.15f // fraction of a cascade's reach the camera may move before it re-renders
#define WC_CASCADE_CACHED 2     // cascades from this one on are cached; nearer ones carry animated foliage
#define WC_MAX_DRAW_QUADS (WC_MAX_SECTION_QUADS * WC_SECTIONS)
#define WC_HDR GPU_TEXTURE_FORMAT_RGBA16F
#ifdef WASM
// rg11b10ufloat is only renderable behind an optional webgpu feature
#define WC_BLOOM_FORMAT GPU_TEXTURE_FORMAT_RGBA16F
#else
#define WC_BLOOM_FORMAT GPU_TEXTURE_FORMAT_R11G11B10F
#endif
#define WC_PLANES 6

// mirrors WcDraw in shaders/wc_quad.glsl
typedef struct {
  v3 origin;
  u32 _pad;
} WcDrawRecord;

typedef struct {
  v4 *p;
  u32 n;
} WcFrustum;

// ---- celestial mechanics and the cpu copy of the atmosphere transmittance ----

void wc_celestial(f32 day_time, v3 *sun, v3 *moon, m4 *star_rot) {
  f32 a = day_time * PI * 2.0f;
  f32 s = m_sinf(a), c = m_cosf(a);
  *sun = (v3){c, s * m_cosf(WC_TILT), s * m_sinf(WC_TILT)};
  *moon = (v3){-sun->x, -sun->y, -sun->z};
  // stars rotate around the celestial axis (perpendicular to the sun path plane)
  f32 x = 0.0f, y = -m_sinf(WC_TILT), z = m_cosf(WC_TILT);
  f32 ca = m_cosf(-a), sa = m_sinf(-a), t = 1.0f - ca;
  *star_rot = (m4){{{t * x * x + ca, t * x * y + sa * z, t * x * z - sa * y, 0},
                    {t * x * y - sa * z, t * y * y + ca, t * y * z + sa * x, 0},
                    {t * x * z + sa * y, t * y * z - sa * x, t * z * z + ca, 0},
                    {0, 0, 0, 1}}};
}

hz_internal f32 wc_ray_sphere(v3 ro, v3 rd, f32 r) {
  f32 b = v3_dot(ro, rd);
  f32 c = v3_dot(ro, ro) - r * r;
  if (c > 0 && b > 0) return -1;
  f32 disc = b * b - c;
  if (disc < 0) return -1;
  if (disc > b * b) return -b + m_sqrtf(disc);
  return -b - m_sqrtf(disc);
}

v3 wc_sky_transmittance(f32 altitude_m, v3 dir) {
  const f32 ground = 6.36f, atmo = 6.46f;
  v3 pos = {0, ground + m_maxf(0, altitude_m) * 1e-6f + 0.0003f, 0};
  if (wc_ray_sphere(pos, dir, ground) > 0) {
    // below the geometric horizon: fade instead of a hard cut (soft sunsets)
    v3 lifted = v3_normalize((v3){dir.x, m_maxf(0, dir.y) + 0.0005f, dir.z});
    v3 t = wc_sky_transmittance(altitude_m, lifted);
    return v3_scale(t, m_maxf(0, 1 + dir.y * 25));
  }
  f32 dist = wc_ray_sphere(pos, dir, atmo);
  f32 t = 0;
  v3 tr = {1, 1, 1};
  for (i32 i = 0; i < 40; i++) {
    f32 nt = ((i + 0.3f) / 40.0f) * dist;
    f32 dt = nt - t;
    t = nt;
    v3 p = v3_add(pos, v3_scale(dir, t));
    f32 alt_km = (v3_length(p) - ground) * 1000.0f;
    f32 rd = m_expf(-alt_km / 8.0f), md = m_expf(-alt_km / 1.2f);
    f32 oz = m_maxf(0, 1 - m_absf(alt_km - 25) / 15);
    tr.x *= m_expf(-dt * (5.802f * rd + (3.996f + 4.4f) * md + 0.65f * oz));
    tr.y *= m_expf(-dt * (13.558f * rd + (3.996f + 4.4f) * md + 1.881f * oz));
    tr.z *= m_expf(-dt * (33.1f * rd + (3.996f + 4.4f) * md + 0.085f * oz));
  }
  return tr;
}

// ---- pipelines and targets ----

typedef enum {
  WC_TARGET_COLOR, // color-only render target
  WC_TARGET_SWAPCHAIN,
} WcTargetKind;

hz_internal GpuPipeline wc_fullscreen_pipeline(const GpuPipelineDesc *src, GpuTextureFormat fmt, WcTargetKind kind,
                                               b32 additive) {
  GpuPipelineDesc d = *src;
  d.compile_async = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_write = false;
  if (kind == WC_TARGET_SWAPCHAIN) {
    d.depth_test = false;
    d.color_attachment_count = 0;
  } else {
    d.color_attachment_count = 1;
    d.color_formats[0] = fmt;
    d.depth_test = false;
    d.no_depth = true;
  }
  if (additive) {
    d.blend = (GpuBlendState){.enabled = true, .src_factor = GPU_BLEND_ONE, .dst_factor = GPU_BLEND_ONE,
                              .op = GPU_BLEND_OP_ADD, .src_factor_alpha = GPU_BLEND_ONE,
                              .dst_factor_alpha = GPU_BLEND_ONE, .op_alpha = GPU_BLEND_OP_ADD};
  }
  return gpu_make_pipeline(&d);
}

hz_internal GpuComputePipeline wc_compute_pipeline(const GpuComputePipelineDesc *src) {
  GpuComputePipelineDesc d = *src;
  d.compile_async = true;
  return gpu_make_compute_pipeline(&d);
}

hz_internal void wc_gbuffer_formats(GpuPipelineDesc *d) {
  d->color_attachment_count = 4;
  d->color_formats[0] = GPU_TEXTURE_FORMAT_RGBA8_SRGB;
  d->color_formats[1] = GPU_TEXTURE_FORMAT_RGBA16F;
  d->color_formats[2] = GPU_TEXTURE_FORMAT_RGBA8;
  d->color_formats[3] = GPU_TEXTURE_FORMAT_RGBA8;
}

void wc_render_create_pipelines(WcRenderer *r) {
  // the core tier first: async compiles start in creation order
  r->transmittance = wc_fullscreen_pipeline(shader_wc_transmittance_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->multiscatter = wc_fullscreen_pipeline(shader_wc_multiscatter_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->skyview = wc_fullscreen_pipeline(shader_wc_skyview_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->sky_ambient = wc_fullscreen_pipeline(shader_wc_sky_ambient_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);

  GpuPipelineDesc d = *shader_wc_terrain_pipeline_desc();
  wc_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->terrain = gpu_make_pipeline(&d);

  d = *shader_wc_shadow_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.depth_clip_disable = true;
  d.compile_async = true;
  r->shadow = gpu_make_pipeline(&d);

  d = *shader_wc_shadow_clear_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_ALWAYS;
  d.compile_async = true;
  r->shadow_clear = gpu_make_pipeline(&d);

  r->deferred = wc_fullscreen_pipeline(shader_wc_deferred_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);

  d = *shader_wc_water_pipeline_desc();
  d.color_attachment_count = 1;
  d.color_formats[0] = WC_HDR;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->water = gpu_make_pipeline(&d);

  d = *shader_wc_entity_pipeline_desc();
  wc_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->entity = gpu_make_pipeline(&d);
  // the held item always draws on top
  d.depth_compare = GPU_COMPARE_ALWAYS;
  r->entity_hand = gpu_make_pipeline(&d);

  r->cull_expand_cs = wc_compute_pipeline(shader_wc_cull_expand_compute_pipeline_desc());
  r->shafts = wc_fullscreen_pipeline(shader_wc_shafts_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->atmospherics = wc_fullscreen_pipeline(shader_wc_atmospherics_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->bloom_down = wc_fullscreen_pipeline(shader_wc_bloom_down_pipeline_desc(), WC_BLOOM_FORMAT, WC_TARGET_COLOR, false);
  r->bloom_up = wc_fullscreen_pipeline(shader_wc_bloom_up_pipeline_desc(), WC_BLOOM_FORMAT, WC_TARGET_COLOR, true);
  r->exposure = wc_fullscreen_pipeline(shader_wc_exposure_pipeline_desc(), GPU_TEXTURE_FORMAT_RGBA32F, WC_TARGET_COLOR, false);
  r->final = wc_fullscreen_pipeline(shader_wc_final_pipeline_desc(), GPU_TEXTURE_FORMAT_RGBA8, WC_TARGET_SWAPCHAIN, false);

  // effects: each renders as switched off until its own pipelines land
  r->ssao = wc_fullscreen_pipeline(shader_wc_ssao_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->clouds = wc_fullscreen_pipeline(shader_wc_clouds_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->clouds_temporal = wc_fullscreen_pipeline(shader_wc_clouds_temporal_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->taa = wc_fullscreen_pipeline(shader_wc_taa_pipeline_desc(), WC_HDR, WC_TARGET_COLOR, false);
  r->noise_base_cs = wc_compute_pipeline(shader_wc_noise_base_compute_pipeline_desc());
  r->noise_detail_cs = wc_compute_pipeline(shader_wc_noise_detail_compute_pipeline_desc());
  r->noise_weather_cs = wc_compute_pipeline(shader_wc_noise_weather_compute_pipeline_desc());
  r->noise_down3d_cs = wc_compute_pipeline(shader_wc_noise_down3d_compute_pipeline_desc());
  r->noise_down2d_cs = wc_compute_pipeline(shader_wc_noise_down2d_compute_pipeline_desc());
}

hz_internal u32 wc_mip_count(u32 size) {
  u32 n = 1;
  while (size > 1) {
    size >>= 1;
    n++;
  }
  return n;
}

// mips 1.. of a baked noise texture, each a box filter of the level above
hz_internal void wc_noise_mips(WcRenderer *r, GpuTexture tex, u32 size, b32 is_3d) {
  u32 mips = wc_mip_count(size);
  gpu_apply_compute_pipeline(is_3d ? r->noise_down3d_cs : r->noise_down2d_cs);
  for (u32 m = 1; m < mips; m++) {
    gpu_texture_access_mip_range(tex, m - 1, 1, GPU_ACCESS_SHADER_READ);
    u32 s = size >> m;
    GpuComputeBindings b;
    if (is_3d) {
      shader_wc_noise_down3d_apply_params(&(ShaderWcNoiseDown3dDownParams){.src_mip = (i32)(m - 1)});
      b = shader_wc_noise_down3d_bindings((ShaderWcNoiseDown3dBindings){.dst = tex, .src = tex});
    } else {
      shader_wc_noise_down2d_apply_params(&(ShaderWcNoiseDown2dDownParams){.src_mip = (i32)(m - 1)});
      b = shader_wc_noise_down2d_bindings((ShaderWcNoiseDown2dBindings){.dst = tex, .src = tex});
    }
    b.images.items[0].mip = m;
    // the read view stops above the level being written (webgpu rejects overlapping views)
    b.texture_view_mip_count[0] = m;
    gpu_apply_compute_bindings(&b);
    if (is_3d) shader_wc_noise_down3d_dispatch(s, s, s);
    else shader_wc_noise_down2d_dispatch(s, s);
  }
  gpu_texture_access(tex, GPU_ACCESS_SHADER_READ);
}

// generates the cloud noise once, before anything samples it
hz_internal void wc_bake_noise(WcRenderer *r) {
  if (r->noise_baked) return;
  gpu_apply_compute_pipeline(r->noise_base_cs);
  GpuComputeBindings b = shader_wc_noise_base_bindings((ShaderWcNoiseBaseBindings){.dst = r->noise_base});
  gpu_apply_compute_bindings(&b);
  shader_wc_noise_base_dispatch(WC_NOISE_BASE, WC_NOISE_BASE, WC_NOISE_BASE);
  gpu_apply_compute_pipeline(r->noise_detail_cs);
  b = shader_wc_noise_detail_bindings((ShaderWcNoiseDetailBindings){.dst = r->noise_detail});
  gpu_apply_compute_bindings(&b);
  shader_wc_noise_detail_dispatch(WC_NOISE_DETAIL, WC_NOISE_DETAIL, WC_NOISE_DETAIL);
  gpu_apply_compute_pipeline(r->noise_weather_cs);
  b = shader_wc_noise_weather_bindings((ShaderWcNoiseWeatherBindings){.dst = r->weather});
  gpu_apply_compute_bindings(&b);
  shader_wc_noise_weather_dispatch(WC_WEATHER_SIZE, WC_WEATHER_SIZE);
  wc_noise_mips(r, r->noise_base, WC_NOISE_BASE, true);
  wc_noise_mips(r, r->noise_detail, WC_NOISE_DETAIL, true);
  wc_noise_mips(r, r->weather, WC_WEATHER_SIZE, false);
  r->noise_baked = true;
}

hz_internal GpuRenderTarget wc_rt(const char *name, u32 w, u32 h, GpuTextureFormat fmt, b32 depth) {
  return gpu_make_render_target(&(GpuRenderTargetDesc){
      .name = name, .width = w, .height = h, .color_attachment_count = 1, .formats = {fmt}, .needs_depth = depth});
}

hz_internal void wc_bloom_sizes(WcRenderer *r, u32 w, u32 h) {
  u32 bw = w, bh = h;
  for (u32 i = 0; i < WC_BLOOM_LEVELS; i++) {
    bw = bw > 1 ? bw >> 1 : 1;
    bh = bh > 1 ? bh >> 1 : 1;
    r->bloom_w[i] = bw;
    r->bloom_h[i] = bh;
  }
}

hz_internal void wc_cloud_size(const WcRenderer *r, u32 w, u32 h, u32 *cw, u32 *ch) {
  f32 scale = r->settings.clouds >= 2 ? 0.5f : 0.25f;
  *cw = (u32)m_maxf(1.0f, m_floorf(w * scale));
  *ch = (u32)m_maxf(1.0f, m_floorf(h * scale));
}

hz_internal void wc_create_targets(WcRenderer *r, u32 w, u32 h, u32 uw, u32 uh) {
  r->w = w;
  r->h = h;
  r->gbuf = gpu_make_render_target(&(GpuRenderTargetDesc){
      .name = "wc gbuffer",
      .width = w,
      .height = h,
      .color_attachment_count = 4,
      .formats = {GPU_TEXTURE_FORMAT_RGBA8_SRGB, GPU_TEXTURE_FORMAT_RGBA16F, GPU_TEXTURE_FORMAT_RGBA8,
                  GPU_TEXTURE_FORMAT_RGBA8},
      .needs_depth = true,
  });
  // the scene renders with the g-buffer's depth (water tests and writes it): no depth of its own
  r->scene = wc_rt("wc scene", w, h, WC_HDR, false);
  r->depth_copy = gpu_make_render_target(
      &(GpuRenderTargetDesc){.name = "wc depth copy", .width = w, .height = h, .depth_only = true});
  r->scene_copy = wc_rt("wc scene copy", w, h, WC_HDR, false);
  r->hdr_b = wc_rt("wc hdr b", w, h, WC_HDR, false);
  r->ssao_rt = wc_rt("wc ssao", (w + 1) / 2, (h + 1) / 2, WC_HDR, false);
  r->shafts_rt = wc_rt("wc shafts", (w + 1) / 2, (h + 1) / 2, WC_HDR, false);
  r->history[0] = wc_rt("wc history 0", uw, uh, WC_HDR, false);
  r->history[1] = wc_rt("wc history 1", uw, uh, WC_HDR, false);
  r->uw = uw;
  r->uh = uh;
  wc_cloud_size(r, w, h, &r->cw, &r->ch);
  r->cloud_cur = wc_rt("wc clouds", (r->cw + 1) / 2, (r->ch + 1) / 2, WC_HDR, false);
  r->cloud_hist[0] = wc_rt("wc clouds hist 0", r->cw, r->ch, WC_HDR, false);
  r->cloud_hist[1] = wc_rt("wc clouds hist 1", r->cw, r->ch, WC_HDR, false);
  wc_bloom_sizes(r, uw, uh);
  for (u32 i = 0; i < WC_BLOOM_LEVELS; i++)
    r->bloom[i] = wc_rt("wc bloom", r->bloom_w[i], r->bloom_h[i], WC_BLOOM_FORMAT, false);
  r->needs_prime = true;
}

hz_internal u32 wc_scaled(u32 v, f32 scale) { return (u32)m_maxf(1.0f, m_floorf(v * scale)); }

// with taa on, a render scale below 1 upscales in the taa pass: history and bloom at the output size
hz_internal void wc_upscale_size(const WcRenderSettings *s, u32 out_w, u32 out_h, f32 dpr, u32 w, u32 h, u32 *uw,
                                 u32 *uh) {
  b32 up = s->taa && s->render_scale < 1.0f;
  f32 os = dpr > WC_MAX_DPR ? WC_MAX_DPR / dpr : 1.0f;
  *uw = up ? wc_scaled(out_w, os) : w;
  *uh = up ? wc_scaled(out_h, os) : h;
}

// the 3d view renders at most WC_MAX_DPR device px per logical px
hz_internal f32 wc_output_scale(const WcRenderSettings *s, f32 dpr) {
  return dpr > WC_MAX_DPR ? s->render_scale * WC_MAX_DPR / dpr : s->render_scale;
}

hz_internal void wc_ensure_targets(WcRenderer *r, u32 w, u32 h, u32 uw, u32 uh) {
  u32 cw, ch;
  wc_cloud_size(r, w, h, &cw, &ch);
  if (r->w == w && r->h == h && r->cw == cw && r->ch == ch && r->uw == uw && r->uh == uh) return;
  if (r->uw != uw || r->uh != uh) {
    gpu_resize_render_target(r->history[0], uw, uh);
    gpu_resize_render_target(r->history[1], uw, uh);
    wc_bloom_sizes(r, uw, uh);
    for (u32 i = 0; i < WC_BLOOM_LEVELS; i++) gpu_resize_render_target(r->bloom[i], r->bloom_w[i], r->bloom_h[i]);
    r->uw = uw;
    r->uh = uh;
  }
  if (r->w != w || r->h != h) {
    gpu_resize_render_target(r->gbuf, w, h);
    gpu_resize_render_target(r->scene, w, h);
    gpu_resize_render_target(r->depth_copy, w, h);
    gpu_resize_render_target(r->scene_copy, w, h);
    gpu_resize_render_target(r->hdr_b, w, h);
    gpu_resize_render_target(r->ssao_rt, (w + 1) / 2, (h + 1) / 2);
    gpu_resize_render_target(r->shafts_rt, (w + 1) / 2, (h + 1) / 2);
  }
  gpu_resize_render_target(r->cloud_cur, (cw + 1) / 2, (ch + 1) / 2);
  gpu_resize_render_target(r->cloud_hist[0], cw, ch);
  gpu_resize_render_target(r->cloud_hist[1], cw, ch);
  r->w = w;
  r->h = h;
  r->cw = cw;
  r->ch = ch;
  r->reset_history = true;
  r->needs_prime = true;
}

hz_internal void wc_ensure_shadow(WcRenderer *r) {
  u32 res = r->settings.shadow_res;
  if (r->shadow_res == res) return;
  if (!handle_is_valid(r->shadow_atlas)) {
    r->shadow_atlas = gpu_make_render_target(&(GpuRenderTargetDesc){
        .name = "wc shadow atlas", .width = res * 2, .height = res * 2, .depth_only = true});
    r->water_shadow = gpu_make_render_target(&(GpuRenderTargetDesc){
        .name = "wc water shadow", .width = res, .height = res, .depth_only = true});
  } else {
    gpu_resize_render_target(r->shadow_atlas, res * 2, res * 2);
    gpu_resize_render_target(r->water_shadow, res, res);
  }
  r->shadow_res = res;
  for (u32 c = 0; c < WC_CASCADES; c++) r->cascade_cache[c].valid = false;
}

void wc_render_init(WcRenderer *r, Allocator *alloc, const WcTextureData *tex, u32 out_w, u32 out_h, f32 dpr) {
  r->alloc = *alloc;
  r->timer_open = UINT32_MAX;
  // targets exist from init so wasm has realised them by the first frame
  f32 scale = wc_output_scale(&r->settings, dpr);
  u32 w = wc_scaled(out_w, scale), h = wc_scaled(out_h, scale), uw, uh;
  wc_upscale_size(&r->settings, out_w, out_h, dpr, w, h, &uw, &uh);
  wc_create_targets(r, w, h, uw, uh);
  wc_ensure_shadow(r);

  GpuTextureMipsDesc blk = {.format = GPU_TEXTURE_FORMAT_RGBA8_SRGB, .width = WC_TEX_SIZE, .height = WC_TEX_SIZE,
                            .layer_count = T_COUNT, .mip_count = WC_TEX_MIPS, .data = tex->albedo,
                            .data_size = tex->data_size, .label = "wc block albedo"};
  r->blk_albedo = gpu_make_texture_mips(&blk);
  blk.format = GPU_TEXTURE_FORMAT_RGBA8;
  blk.data = tex->normal;
  blk.label = "wc block normal";
  r->blk_normal = gpu_make_texture_mips(&blk);
  blk.data = tex->spec;
  blk.label = "wc block spec";
  r->blk_spec = gpu_make_texture_mips(&blk);

  r->noise_base = gpu_make_storage_texture_3d(WC_NOISE_BASE, WC_NOISE_BASE, WC_NOISE_BASE, GPU_TEXTURE_FORMAT_RGBA8,
                                              wc_mip_count(WC_NOISE_BASE));
  r->noise_detail = gpu_make_storage_texture_3d(WC_NOISE_DETAIL, WC_NOISE_DETAIL, WC_NOISE_DETAIL,
                                                GPU_TEXTURE_FORMAT_RGBA8, wc_mip_count(WC_NOISE_DETAIL));
  r->weather = gpu_make_storage_texture_mip(WC_WEATHER_SIZE, WC_WEATHER_SIZE, GPU_TEXTURE_FORMAT_RGBA8,
                                            wc_mip_count(WC_WEATHER_SIZE));

  r->trans_lut = wc_rt("wc transmittance", 256, 64, WC_HDR, false);
  r->ms_lut = wc_rt("wc multiscatter", 32, 32, WC_HDR, false);
  r->sky_sun = wc_rt("wc sky sun", WC_SKY_W, WC_SKY_H, WC_HDR, false);
  r->sky_moon = wc_rt("wc sky moon", WC_SKY_W, WC_SKY_H, WC_HDR, false);
  r->ambient = wc_rt("wc ambient", 6, 1, WC_HDR, false);
  r->exposure_rt[0] = wc_rt("wc exposure 0", 1, 1, GPU_TEXTURE_FORMAT_RGBA32F, false);
  r->exposure_rt[1] = wc_rt("wc exposure 1", 1, 1, GPU_TEXTURE_FORMAT_RGBA32F, false);

  // static quad index pattern: every column part draws a contiguous quad range from it
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 *idx = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, u32, WC_MAX_DRAW_QUADS * 6);
  for (u32 q = 0; q < WC_MAX_DRAW_QUADS; q++) {
    u32 v = q * 4, o = q * 6;
    idx[o] = v;
    idx[o + 1] = v + 1;
    idx[o + 2] = v + 2;
    idx[o + 3] = v;
    idx[o + 4] = v + 2;
    idx[o + 5] = v + 3;
  }
  r->quad_ib = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_INDEX, .usage = GPU_BUFFER_USAGE_STATIC,
                                                .size = WC_MAX_DRAW_QUADS * 6 * sizeof(u32), .data = idx,
                                                .label = "wc quad indices"});
  r->quad_ib_quads = WC_MAX_DRAW_QUADS;
  u32 ent_quads = WC_ENTITY_MAX_VERTS / 4;
  u16 *eidx = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, u16, ent_quads * 6);
  for (u32 q = 0; q < ent_quads; q++) {
    u16 v = (u16)(q * 4);
    u32 o = q * 6;
    eidx[o] = v;
    eidx[o + 1] = v + 1;
    eidx[o + 2] = v + 2;
    eidx[o + 3] = v;
    eidx[o + 4] = v + 2;
    eidx[o + 5] = v + 3;
  }
  r->ent_ib = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_INDEX, .usage = GPU_BUFFER_USAGE_STATIC,
                                               .size = ent_quads * 6 * sizeof(u16), .data = eidx,
                                               .label = "wc entity indices"});
  tctx_temp_allocator_end(tmp);
  r->ent_vb = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_VERTEX, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                               .size = WC_ENTITY_MAX_VERTS * WC_ENTITY_STRIDE * sizeof(f32),
                                               .label = "wc entity vertices"});
  r->draw_record_cap = WC_MAX_CHUNKS * 2;
  r->draw_records = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                     .size = WC_MAX_CHUNKS * 2 * sizeof(WcDrawRecord),
                                                     .stride = sizeof(WcDrawRecord), .label = "wc draw records"});
  r->reset_history = true;
}

void wc_render_invalidate_history(WcRenderer *r) { r->reset_history = true; }

// ---- culling ----

// planes (inside: dot >= 0) of a reverse-z clip matrix; degenerate planes (infinite far) are dropped
hz_internal WcFrustum wc_frustum(m4 m, Allocator *ta) {
  WcFrustum f = {ALLOC_ARRAY(ta, v4, WC_PLANES), 0};
  for (u32 k = 0; k < WC_PLANES; k++) {
    u32 row = k < 4 ? k / 2 : 2;
    f32 sgn = (k < 4) ? ((k & 1) ? -1.0f : 1.0f) : (k == 4 ? -1.0f : 1.0f);
    v4 p;
    if (k == 5) {
      p = (v4){m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]};
    } else {
      p = (v4){m.m[0][3] + sgn * m.m[0][row], m.m[1][3] + sgn * m.m[1][row], m.m[2][3] + sgn * m.m[2][row],
               m.m[3][3] + sgn * m.m[3][row]};
    }
    f32 len = m_sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len < 1e-6f) continue;
    f.p[f.n++] = (v4){p.x / len, p.y / len, p.z / len, p.w / len};
  }
  return f;
}

// 0 outside, 1 straddling, 2 inside every plane
hz_internal u32 wc_box_classify(const WcFrustum *f, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
  u32 r = 2;
  for (u32 k = 0; k < f->n; k++) {
    v4 p = f->p[k];
    if (p.x * (p.x >= 0 ? x1 : x0) + p.y * (p.y >= 0 ? y1 : y0) + p.z * (p.z >= 0 ? z1 : z0) + p.w < 0) return 0;
    if (p.x * (p.x >= 0 ? x0 : x1) + p.y * (p.y >= 0 ? y0 : y1) + p.z * (p.z >= 0 ? z0 : z1) + p.w < 0) r = 1;
  }
  return r;
}

hz_internal b32 wc_box_visible(const WcFrustum *f, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
  for (u32 k = 0; k < f->n; k++) {
    v4 p = f->p[k];
    if (p.x * (p.x >= 0 ? x1 : x0) + p.y * (p.y >= 0 ? y1 : y0) + p.z * (p.z >= 0 ? z1 : z0) + p.w < 0) return false;
  }
  return true;
}


// one frustum's columns: visible section range of every column's part, sorted by pool then front to back
typedef struct {
  u32 part;
  const WcFrustum *fr;
  b32 buried;
  u32 *sections;
  WcDraw *tmp;
  u64 *keys;
  u32 count;
  WcDrawList out;
} WcCullJob;

// lsd radix sort of the jobs' keys on their top 32 bits (4 x 8), then the columns in key order
hz_internal void wc_cull_sort(WcCullJob *j, u32 n, Allocator *ta) {
  u64 *a = j->keys, *b = j->keys + n;
  u32 *hist = ALLOC_ARRAY(ta, u32, 256);
  for (u32 pass = 0; pass < 4; pass++) {
    u32 shift = 32 + pass * 8;
    mem_zero(hist, sizeof(u32) * 256);
    for (u32 i = 0; i < j->count; i++) hist[(a[i] >> shift) & 255]++;
    u32 sum = 0;
    for (u32 k = 0; k < 256; k++) {
      u32 c = hist[k];
      hist[k] = sum;
      sum += c;
    }
    for (u32 i = 0; i < j->count; i++) b[hist[(a[i] >> shift) & 255]++] = a[i];
    u64 *t = a;
    a = b;
    b = t;
  }
  j->out = (WcDrawList){ALLOC_ARRAY_NO_ZERO(ta, WcDraw, j->count ? j->count : 1), j->count};
  for (u32 i = 0; i < j->count; i++) j->out.items[i] = j->tmp[a[i] & 0xffffffffu];
}

// every job in one walk over the renderables, which also writes each meshed part's draw record; returns the count
hz_internal u32 wc_cull(const WcWorld *w, WcDrawRecord *records, WcCullJob *jobs, u32 njobs, WcV3d cam, Allocator *ta) {
  u32 rec_count = 0;
  u32 n = w->renderable_count;
  for (u32 k = 0; k < njobs; k++) {
    jobs[k].tmp = ALLOC_ARRAY_NO_ZERO(ta, WcDraw, n ? n : 1);
    jobs[k].keys = ALLOC_ARRAY_NO_ZERO(ta, u64, n ? n * 2 : 2);
    jobs[k].count = 0;
  }
  const f32 m = 0.5f;
  for (u32 i = 0; i < n; i++) {
    const WcChunk *c = &w->chunks[w->renderables[i]];
    WcDrawRecord dr = {.origin = {(f32)(c->cx * 16 - cam.x), (f32)(-cam.y), (f32)(c->cz * 16 - cam.z)}};
    u32 rec0 = UINT32_MAX, rec1 = UINT32_MAX;
    if (c->parts[0].pool != WC_NO_POOL) {
      rec0 = rec_count;
      records[rec_count++] = dr;
    }
    if (c->parts[1].pool != WC_NO_POOL) {
      rec1 = rec_count;
      records[rec_count++] = dr;
    }
    f32 x0 = (f32)(c->cx * 16 - cam.x) - m, z0 = (f32)(c->cz * 16 - cam.z) - m;
    f32 x1 = x0 + 16 + 2 * m, z1 = z0 + 16 + 2 * m;
    for (u32 k = 0; k < njobs; k++) {
      WcCullJob *j = &jobs[k];
      u32 rec = j->part ? rec1 : rec0;
      if (rec == UINT32_MAX) continue;
      const WcMeshPart *p = &c->parts[j->part];
      i32 s0 = 0;
      if (j->buried && cam.y > c->surface_min - 8) {
        i32 b = (i32)m_ceilf((c->surface_min - 22) / 16.0f);
        s0 = b > 0 ? b : 0;
      }
      i32 lo = s0, hi = WC_SECTIONS - 1;
      while (lo <= hi && p->start[lo + 1] == p->start[lo]) lo++;
      while (hi >= lo && p->start[hi + 1] == p->start[hi]) hi--;
      if (lo > hi) continue;
      u32 cls = wc_box_classify(j->fr, x0, (f32)(lo * 16 - cam.y) - m, z0, x1, (f32)(hi * 16 + 16 - cam.y) + m, z1);
      if (cls == 0) continue;
      i32 first = lo, last = hi;
      if (cls == 1) {
        first = -1;
        for (i32 sy = lo; sy <= hi; sy++) {
          if (p->start[sy + 1] == p->start[sy]) continue;
          f32 y0 = (f32)(sy * 16 - cam.y) - m;
          if (!wc_box_visible(j->fr, x0, y0, z0, x1, y0 + 16 + 2 * m, z1)) continue;
          if (first < 0) first = sy;
          last = sy;
        }
        if (first < 0) continue;
      }
      f32 mx = x0 + 8 + m, mz = z0 + 8 + m;
      f32 my = (f32)((first + last + 1) * 8 - cam.y);
      f32 dist = m_sqrtf(mx * mx + my * my + mz * mz);
      if (j->sections) *j->sections += (u32)(last - first + 1);
      j->tmp[j->count] =
          (WcDraw){.part = p, .cx = c->cx, .cz = c->cz, .record = rec, .first = (u32)first, .last = (u32)last};
      u64 key = ((u64)p->pool << 20) | (u64)m_minf(dist * 16.0f, 1048575.0f);
      j->keys[j->count] = (key << 32) | j->count;
      j->count++;
    }
  }
  for (u32 k = 0; k < njobs; k++) wc_cull_sort(&jobs[k], n, ta);
  return rec_count;
}

// ---- face buckets and gpu expansion ----

typedef enum { WC_FACES_CAMERA, WC_FACES_ALL, WC_FACES_LIGHT } WcFaceTest;

// mirrors _Ranges in shaders/wc_cull_expand.glsl
typedef struct {
  u32 src, count, record, dst;
} WcRange;

typedef struct {
  WcRange *items;
  u32 count, cap;
  u32 entries;
} WcRanges;

// keep masks over the buckets: bit d (+X, -X, +Y, -Y, +Z, -Z) for plain faces, bit 6 + d for flagged, bit 12 the rest
#define WC_KEEP_ALL 0x1fffu

// directions of an axis that can face a camera at section offset lo (spans lo..lo+16); waving faces keep a margin
force_inline u32 wc_keep_axis(f32 lo, u32 axis) {
  u32 k = (lo + 1 < 0 ? 1u : 0u) | (lo + 15 > 0 ? 2u : 0u);
  u32 kf = (lo + 1 < 0.5f ? 1u : 0u) | (lo + 15 > -0.5f ? 2u : 0u);
  return (k << (axis * 2)) | (kf << (6 + axis * 2));
}

// solids occlude through their far faces alone: plain cube faces towards the light are dropped
hz_internal u32 wc_keep_light(v3 L) {
  u32 k = WC_KEEP_ALL;
  if (L.x > 0) k &= ~1u;
  if (L.x < 0) k &= ~2u;
  if (L.y > 0) k &= ~4u;
  if (L.y < 0) k &= ~8u;
  if (L.z > 0) k &= ~16u;
  if (L.z < 0) k &= ~32u;
  return k;
}

hz_internal u32 wc_ranges_bound(const WcDrawList *list) {
  u32 n = 0;
  for (u32 i = 0; i < list->count; i++) n += (list->items[i].last - list->items[i].first + 1) * WC_BUCKETS;
  return n;
}

// appends the kept buckets of every listed column as quad ranges; contiguous quads of a column merge
hz_internal WcBatchList wc_emit_ranges(WcRanges *rs, const WcDrawList *list, WcFaceTest t, WcV3d cam, v3 L,
                                       Allocator *ta) {
  WcBatchList out = {ALLOC_ARRAY_NO_ZERO(ta, WcBatch, list->count ? list->count : 1), 0};
  u32 light_keep = t == WC_FACES_LIGHT ? wc_keep_light(L) : WC_KEEP_ALL;
  for (u32 i = 0; i < list->count; i++) {
    const WcDraw *d = &list->items[i];
    const WcMeshPart *p = d->part;
    u32 before = rs->entries;
    if (t == WC_FACES_ALL) {
      u32 n = p->start[d->last + 1] - p->start[d->first];
      rs->items[rs->count++] =
          (WcRange){.src = p->base + p->start[d->first], .count = n, .record = d->record, .dst = rs->entries};
      rs->entries += n;
    } else {
      u32 col_keep = t == WC_FACES_LIGHT ? light_keep
                                          : wc_keep_axis((f32)(d->cx * 16 - cam.x), 0) |
                                                wc_keep_axis((f32)(d->cz * 16 - cam.z), 2) | (1u << WC_BUCKET_OTHER);
      u32 first_range = rs->count;
      for (u32 s = d->first; s <= d->last; s++) {
        const u16 *m = p->meta + s * WC_SECTION_META;
        u32 keep = t == WC_FACES_LIGHT ? col_keep : col_keep | wc_keep_axis((f32)(s * 16.0 - cam.y), 1);
        u32 bits = m[WC_META_MASK] & keep;
        u32 sec_q = p->base + p->start[s];
        while (bits) {
          u32 b = (u32)hz_ctzll(bits);
          bits &= bits - 1;
          u32 q = sec_q + m[b], n = m[b + 1] - m[b];
          WcRange *last = rs->count > first_range ? &rs->items[rs->count - 1] : NULL;
          if (last && last->src + last->count == q) last->count += n;
          else rs->items[rs->count++] = (WcRange){.src = q, .count = n, .record = d->record, .dst = rs->entries};
          rs->entries += n;
        }
      }
    }
    u32 added = rs->entries - before;
    if (!added) continue;
    WcBatch *b = out.count ? &out.items[out.count - 1] : NULL;
    if (b && b->pool == p->pool && b->first + b->count == before) b->count += added;
    else out.items[out.count++] = (WcBatch){.pool = p->pool, .first = before, .count = added};
  }
  return out;
}

// capacity for n elements of size bytes, doubled when it grows; the old buffer's release is deferred by the gpu layer
hz_internal void wc_ensure_buffer(GpuBuffer *buf, u32 *cap, u32 n, u32 size, GpuBufferUsage usage, const char *label) {
  if (n <= *cap && handle_is_valid(*buf)) return;
  u32 c = *cap ? *cap : 65536;
  while (c < n) c *= 2;
  if (handle_is_valid(*buf)) gpu_destroy_buffer(*buf);
  *buf = gpu_make_buffer(&(GpuBufferDesc){
      .type = GPU_BUFFER_STRUCTURED, .usage = usage, .size = c * size, .stride = size, .label = label});
  *cap = c;
}

// ---- cascades ----

// camera-centred and world-anchored, so turning never invalidates a cascade: the lighting picks cascades by distance
hz_internal void wc_cascade_fit(WcCascadeCache *cc, WcV3d cam, v3 L, f32 half, u32 res) {
  f32 texel = (2 * half) / res;
  v3 up = m_absf(L.y) > 0.99f ? (v3){0, 0, 1} : (v3){0, 1, 0};
  v3 zx = L;
  v3 xx = v3_normalize(v3_cross(up, zx));
  v3 yx = v3_cross(zx, xx);
  // texel snapping happens in world space (double) so the grid is stable far from the origin
  f64 lx = cam.x * xx.x + cam.y * xx.y + cam.z * xx.z;
  f64 ly = cam.x * yx.x + cam.y * yx.y + cam.z * yx.z;
  f64 lz = cam.x * zx.x + cam.y * zx.y + cam.z * zx.z;
  lx = m_floor(lx / texel + 0.5) * texel;
  ly = m_floor(ly / texel + 0.5) * texel;
  cc->cx = xx.x * lx + yx.x * ly + zx.x * lz;
  cc->cy = xx.y * lx + yx.y * ly + zx.y * lz;
  cc->cz = xx.z * lx + yx.z * ly + zx.z * lz;
  f32 depth = 2 * half + WC_SHADOW_EXTRA;
  m4 view = m4_lookat(v3_scale(L, half + WC_SHADOW_EXTRA), (v3){0, 0, 0}, yx);
  cc->vp = m4_mul(m4_ortho(-half, half, -half, half, 0, depth), view);
  cc->L = L;
  cc->half = half;
  cc->texel = texel;
  cc->depth = depth;
  cc->valid = true;
}

// a mesh change inside the box the cascade was rendered over (its footprint stretches toward the light)
hz_internal b32 wc_cascade_touched(const WcCascadeCache *cc, const WcWorld *w) {
  if (w->mesh_changes_overflow) return true;
  f32 lxz = m_sqrtf(cc->L.x * cc->L.x + cc->L.z * cc->L.z);
  f64 reach = cc->half * 1.4143 + (cc->half + WC_SHADOW_EXTRA) * lxz + 12.0;
  for (u32 i = 0; i < w->mesh_change_count; i++) {
    f64 dx = w->mesh_changes[i * 2] * 16.0 + 8.0 - cc->cx, dz = w->mesh_changes[i * 2 + 1] * 16.0 + 8.0 - cc->cz;
    if (dx * dx + dz * dz <= reach * reach) return true;
  }
  return false;
}

// ---- passes ----

void wc_render_timer_begin(WcRenderer *r, WcGpuGroup g) {
  if (!r->gpu_timing || r->timer_used >= WC_GPU_TIMERS) return;
  u32 i = r->timer_used++;
  if (i >= r->timer_count) {
    r->timers[i] = gpu_create_timer("wc pass");
    r->timer_count = i + 1;
  }
  r->timer_group[i] = (u8)g;
  gpu_timer_begin(r->timers[i]);
  r->timer_open = i;
}

void wc_render_timer_end(WcRenderer *r) {
  if (r->timer_open == UINT32_MAX) return;
  gpu_timer_end(r->timers[r->timer_open]);
  r->timer_open = UINT32_MAX;
}

b32 wc_render_gpu_times(WcRenderer *r, f32 *group_ms, f32 *frame_ms) {
  for (u32 g = 0; g < WC_GPU_GROUP_COUNT; g++) group_ms[g] = 0;
  *frame_ms = 0;
  if (!r->gpu_timing || gpu_timer_count() == 0) return false;
  f32 sum = 0, span = 0;
  for (u32 i = 0; i < r->timer_used; i++) {
    f32 ms = gpu_timer_result_ms(r->timers[i]);
    group_ms[r->timer_group[i]] += ms;
    sum += ms;
    GpuTimerSpan sp = gpu_timer_result_span(r->timers[i]);
    if (sp.valid && sp.end_ms > span) span = sp.end_ms;
  }
  *frame_ms = span > 0 ? span : sum;
  return true;
}

hz_internal void wc_pass(WcRenderer *r, WcGpuGroup g, GpuPassDesc desc) {
  wc_render_timer_begin(r, g);
  gpu_begin_pass(&desc);
}

hz_internal void wc_begin(WcRenderer *r, WcGpuGroup g, GpuRenderTarget rt) {
  wc_pass(r, g, (GpuPassDesc){.render_target = rt});
}

hz_internal void wc_end(WcRenderer *r) {
  gpu_end_pass();
  wc_render_timer_end(r);
}

hz_internal void wc_frame_uniforms(const WcRenderer *r) {
  gpu_apply_uniforms(0, (void *)&r->frame, sizeof(WcFrameUniforms));
}

force_inline GpuTexture wc_color(GpuRenderTarget rt, u32 i) { return gpu_get_render_target_attachment(rt, i); }
force_inline GpuTexture wc_depth(GpuRenderTarget rt) { return gpu_get_render_target_depth(rt); }

typedef enum { WC_TERRAIN_GBUFFER, WC_TERRAIN_SHADOW, WC_TERRAIN_WATER } WcTerrainPass;

// one draw per pool batch; the first instance carries the batch's first visible entry
hz_internal u32 wc_draw_terrain(WcRenderer *r, const WcWorld *w, const WcBatchList *list, WcTerrainPass pass) {
  const WcMeshStore *ms = w->meshes;
  u32 draws = 0;
  for (u32 i = 0; i < list->count; i++) {
    const WcBatch *b = &list->items[i];
    GpuBuffer pool = ms->pools[b->pool].buf;
    if (pass == WC_TERRAIN_GBUFFER) {
      shader_wc_terrain_apply_bindings((ShaderWcTerrainBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .albedo_tex = r->blk_albedo, .normal_tex = r->blk_normal, .spec_tex = r->blk_spec});
    } else if (pass == WC_TERRAIN_SHADOW) {
      shader_wc_shadow_apply_bindings((ShaderWcShadowBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .albedo_tex = r->blk_albedo});
    } else {
      shader_wc_water_apply_bindings((ShaderWcWaterBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .scene_color_tex = wc_color(r->scene_copy, 0),
          .scene_depth_tex = wc_depth(r->depth_copy), .albedo_tex = r->blk_albedo,
          .sky_sun_tex = wc_color(r->sky_sun, 0), .sky_moon_tex = wc_color(r->sky_moon, 0),
          .ambient_tex = wc_color(r->ambient, 0), .shadow_cmp_tex = wc_depth(r->shadow_atlas),
          .shadow_raw_tex = wc_depth(r->shadow_atlas)});
    }
    // the shared index buffer addresses quad_ib_quads quads, so long batches split
    for (u32 done = 0; done < b->count; done += r->quad_ib_quads) {
      u32 n = b->count - done < r->quad_ib_quads ? b->count - done : r->quad_ib_quads;
      u32 first = b->first + done;
#ifndef WASM
      gpu_set_push_constants((void *)&first, sizeof(u32));
#endif
      gpu_draw_indexed(n * 6, 1, 0, first);
      draws++;
    }
  }
  return draws;
}

hz_internal void wc_fullscreen_draw(void) { gpu_draw(3, 1); }

// ---- frame ----

hz_internal f32 wc_halton(u32 i, u32 b) {
  f32 f = 1, r = 0;
  while (i > 0) {
    f /= (f32)b;
    r += f * (f32)(i % b);
    i /= b;
  }
  return r;
}

force_inline f32 wc_smooth(f32 e0, f32 e1, f32 x) {
  f32 t = m_clampf((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}

hz_internal void wc_clear_rt(GpuRenderTarget rt, ColorF32 c) {
  gpu_begin_pass(&(GpuPassDesc){.render_target = rt, .clear_color = c});
  gpu_end_pass();
}

// new targets hold undefined memory: give every sampled target defined contents once
hz_internal void wc_prime_targets(WcRenderer *r) {
  if (!r->needs_prime) return;
  ColorF32 zero = {0, 0, 0, 0};
  wc_clear_rt(r->gbuf, zero);
  wc_clear_rt(r->scene, zero);
  wc_clear_rt(r->scene_copy, zero);
  wc_clear_rt(r->hdr_b, zero);
  wc_clear_rt(r->ssao_rt, (ColorF32){1, 1, 1, 1});
  wc_clear_rt(r->history[0], zero);
  wc_clear_rt(r->history[1], zero);
  wc_clear_rt(r->cloud_cur, zero);
  wc_clear_rt(r->cloud_hist[0], (ColorF32){0, 0, 0, 1});
  wc_clear_rt(r->cloud_hist[1], (ColorF32){0, 0, 0, 1});
  for (u32 i = 0; i < WC_BLOOM_LEVELS; i++) wc_clear_rt(r->bloom[i], zero);
  r->needs_prime = false;
  // clearing these on a resize would wipe the built luts and the adapted exposure
  if (r->fixed_primed) return;
  wc_clear_rt(r->trans_lut, zero);
  wc_clear_rt(r->ms_lut, zero);
  wc_clear_rt(r->sky_sun, zero);
  wc_clear_rt(r->sky_moon, zero);
  wc_clear_rt(r->ambient, zero);
  wc_clear_rt(r->exposure_rt[0], zero);
  wc_clear_rt(r->exposure_rt[1], zero);
  r->fixed_primed = true;
}

hz_internal void wc_build_luts(WcRenderer *r) {
  if (r->luts_built) return;
  wc_begin(r, WC_GPU_SKY, r->trans_lut);
  gpu_apply_pipeline(r->transmittance);
  wc_fullscreen_draw();
  wc_end(r);
  wc_begin(r, WC_GPU_SKY, r->ms_lut);
  gpu_apply_pipeline(r->multiscatter);
  shader_wc_multiscatter_apply_bindings((ShaderWcMultiscatterBindings){.trans_lut = wc_color(r->trans_lut, 0)});
  wc_fullscreen_draw();
  wc_end(r);
  r->luts_built = true;
}

typedef enum {
  WC_EFFECT_SSAO = 1u << 0,
  WC_EFFECT_CLOUDS = 1u << 1,
  WC_EFFECT_TAA = 1u << 2,
} WcEffect;

hz_internal b32 wc_noise_pipelines_ready(WcRenderer *r) {
  return gpu_compute_pipeline_is_ready(r->noise_base_cs) && gpu_compute_pipeline_is_ready(r->noise_detail_cs) &&
         gpu_compute_pipeline_is_ready(r->noise_weather_cs) && gpu_compute_pipeline_is_ready(r->noise_down3d_cs) &&
         gpu_compute_pipeline_is_ready(r->noise_down2d_cs);
}

// effects whose pipelines are compiled; the rest render as if their setting were off
hz_internal u32 wc_effects_ready(WcRenderer *r) {
  u32 m = 0;
  if (gpu_pipeline_is_ready(r->ssao)) m |= WC_EFFECT_SSAO;
  if (r->noise_baked && gpu_pipeline_is_ready(r->clouds) && gpu_pipeline_is_ready(r->clouds_temporal))
    m |= WC_EFFECT_CLOUDS;
  if (gpu_pipeline_is_ready(r->taa)) m |= WC_EFFECT_TAA;
  return m;
}

b32 wc_render_ready(WcRenderer *r) {
  if (!r->core_ready)
    r->core_ready =
        gpu_pipeline_is_ready(r->transmittance) && gpu_pipeline_is_ready(r->multiscatter) &&
        gpu_pipeline_is_ready(r->skyview) && gpu_pipeline_is_ready(r->sky_ambient) &&
        gpu_pipeline_is_ready(r->terrain) && gpu_pipeline_is_ready(r->shadow) &&
        gpu_pipeline_is_ready(r->shadow_clear) && gpu_pipeline_is_ready(r->deferred) &&
        gpu_pipeline_is_ready(r->water) && gpu_pipeline_is_ready(r->entity) &&
        gpu_pipeline_is_ready(r->entity_hand) && gpu_pipeline_is_ready(r->shafts) &&
        gpu_compute_pipeline_is_ready(r->cull_expand_cs) &&
        gpu_pipeline_is_ready(r->atmospherics) &&
        gpu_pipeline_is_ready(r->bloom_down) && gpu_pipeline_is_ready(r->bloom_up) &&
        gpu_pipeline_is_ready(r->exposure) && gpu_pipeline_is_ready(r->final);
  if (r->core_ready && !r->pipelines_ready)
    r->pipelines_ready = wc_noise_pipelines_ready(r) &&
                         wc_effects_ready(r) == (WC_EFFECT_SSAO | WC_EFFECT_CLOUDS | WC_EFFECT_TAA);
  return r->core_ready;
}

void wc_render_frame(WcRenderer *r, const WcWorld *world, const WcFrameInput *in, const WcEntityGeometry *ents,
                     u32 out_w, u32 out_h) {
  const WcRenderSettings *s = &r->settings;
  f32 scale = wc_output_scale(s, in->dpr);
  u32 w = wc_scaled(out_w, scale);
  u32 h = wc_scaled(out_h, scale);
  u32 uw, uh;
  wc_upscale_size(s, out_w, out_h, in->dpr, w, h, &uw, &uh);
  wc_ensure_targets(r, w, h, uw, uh);
  wc_ensure_shadow(r);
  wc_prime_targets(r);
  r->stats = (WcRenderStats){0};
  r->timer_used = 0;
  if (!wc_render_ready(r)) {
    gpu_begin_pass(&(GpuPassDesc){.clear_color = {0, 0, 0, 1}});
    gpu_end_pass();
    return;
  }
  if (!r->noise_baked && wc_noise_pipelines_ready(r)) wc_bake_noise(r);
  // effects still compiling render as switched off; one switching on restarts the histories it feeds
  WcRenderSettings eff = r->settings;
  u32 effects = r->pipelines_ready ? (WC_EFFECT_SSAO | WC_EFFECT_CLOUDS | WC_EFFECT_TAA) : wc_effects_ready(r);
  if (!(effects & WC_EFFECT_SSAO)) eff.ssao = false;
  if (!(effects & WC_EFFECT_CLOUDS)) eff.clouds = 0;
  // ssr reprojects the taa history, which nothing writes until taa runs
  if (!(effects & WC_EFFECT_TAA)) eff.taa = eff.ssr = false;
  if (effects & ~r->effects_on) r->reset_history = true;
  r->effects_on = effects;
  s = &eff;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;

  // ---- camera ----
  WcV3d cam = in->cam;
  f32 cp = m_cosf(in->pitch);
  v3 fwd = {-m_sinf(in->yaw) * cp, m_sinf(in->pitch), -m_cosf(in->yaw) * cp};
  m4 view = m4_look((v3){0, 0, 0}, fwd, (v3){0, 1, 0});
  f32 fov_y = in->fov * PI / 180.0f;
  f32 aspect = (f32)w / (f32)h;
  f32 far_d = m_maxf(420.0f, s->render_distance * 16 * 1.6f + 96);
  m4 proj_nj = m4_perspective_inf(fov_y, aspect, WC_NEAR);
  m4 proj = proj_nj;
  f32 jx = 0, jy = 0;
  if (s->taa) {
    u32 hi = r->frame_index % 8 + 1;
    jx = (wc_halton(hi, 2) - 0.5f) * 2.0f / w;
    jy = (wc_halton(hi, 3) - 0.5f) * 2.0f / h;
  }
  proj.m[2][0] += jx;
  proj.m[2][1] += jy;
  m4 view_proj = m4_mul(proj, view);
  m4 view_proj_nj = m4_mul(proj_nj, view);
  m4 inv_view_proj = m4_inv(view_proj);
  v3 delta = {(f32)(cam.x - r->prev_cam.x), (f32)(cam.y - r->prev_cam.y), (f32)(cam.z - r->prev_cam.z)};
  if (v3_length(delta) > 8) r->reset_history = true;
  // no previous frame yet: reproject onto the current one
  if (r->frame_index == 0) r->prev_view_proj = view_proj_nj;

  // ---- sun / moon ----
  v3 sun, moon;
  m4 star_rot;
  wc_celestial(in->day_time, &sun, &moon, &star_rot);
  // quantized light direction keeps shadow texels stable
  v3 qsun, qmoon;
  m4 qstar;
  wc_celestial(m_floorf(in->day_time * 8192.0f + 0.5f) / 8192.0f, &qsun, &qmoon, &qstar);
  f32 alt = (f32)cam.y - 62.0f;
  v3 sun_col = v3_scale(wc_sky_transmittance(alt, sun), WC_SUN_ILLUM);
  v3 moon_t = wc_sky_transmittance(alt, moon);
  v3 moon_col = {moon_t.x * WC_MOON_ILLUM * 0.82f, moon_t.y * WC_MOON_ILLUM * 0.9f, moon_t.z * WC_MOON_ILLUM * 1.08f};
  b32 sun_up = sun.y > -0.035f;
  v3 L = sun_up ? qsun : qmoon;
  // the cached cascades step the light 8x less often: each step re-renders them, and their texels hide 1/1024 of a day
  v3 fsun, fmoon;
  m4 fstar;
  wc_celestial(m_floorf(in->day_time * 1024.0f + 0.5f) / 1024.0f, &fsun, &fmoon, &fstar);
  v3 L_far = sun_up ? fsun : fmoon;
  // fade the shadow-casting light at the sun/moon switch so there is no pop
  f32 light_fade = sun_up ? wc_smooth(-0.035f, 0.03f, sun.y) : wc_smooth(0.035f, 0.1f, moon.y);
  v3 light_col = v3_scale(sun_up ? sun_col : moon_col, light_fade);
  f32 night = m_clampf((-sun.y - 0.02f) * 6.0f, 0, 1);

  // ---- cascades: the near two carry animated foliage and re-render every frame, the far two are cached ----
  f32 D = s->shadow_distance;
  u32 refresh = 0;
  b32 far_refreshed = false;
  for (u32 c = 0; c < WC_CASCADES; c++) {
    f32 split = D * (c == 0 ? 0.07f : c == 1 ? 0.2f : c == 2 ? 0.46f : 1.0f);
    f32 half = split * (1 + WC_CASCADE_MARGIN);
    WcCascadeCache *cc = &r->cascade_cache[c];
    v3 Lc = c < WC_CASCADE_CACHED ? L : L_far;
    if (r->reset_history) cc->valid = false;
    b32 stale = c < WC_CASCADE_CACHED || cc->half != half || cc->L.x != Lc.x || cc->L.y != Lc.y || cc->L.z != Lc.z;
    if (!stale && cc->valid) {
      f64 dx = cam.x - cc->cx, dy = cam.y - cc->cy, dz = cam.z - cc->cz;
      f64 limit = split * WC_CASCADE_MARGIN;
      stale = dx * dx + dy * dy + dz * dz > limit * limit || wc_cascade_touched(cc, world);
    }
    // an invalid cascade renders now; a valid but stale far one waits if another far one refreshed this frame
    if (!cc->valid || (stale && (c < WC_CASCADE_CACHED || !far_refreshed))) {
      wc_cascade_fit(cc, cam, Lc, half, s->shadow_res);
      refresh |= 1u << c;
      if (c >= WC_CASCADE_CACHED) far_refreshed = true;
    }
    r->cascade_vp[c] = m4_translated(cc->vp, (v3){(f32)(cam.x - cc->cx), (f32)(cam.y - cc->cy), (f32)(cam.z - cc->cz)});
    r->cascade_far[c] = split;
    r->cascade_texel[c] = cc->texel;
    r->cascade_depth[c] = cc->depth;
  }

  // ---- frame uniforms ----
  WcFrameUniforms *F = &r->frame;
  F->view = view;
  F->proj = proj;
  F->view_proj = view_proj;
  F->inv_view_proj = inv_view_proj;
  F->prev_view_proj = r->prev_view_proj;
  F->view_proj_nj = view_proj_nj;
  for (u32 c = 0; c < WC_CASCADES; c++) F->shadow_vp[c] = r->cascade_vp[c];
  F->star_rot = star_rot;
  F->cascade_splits = (v4){r->cascade_far[0], r->cascade_far[1], r->cascade_far[2], r->cascade_far[3]};
  F->cascade_texel = (v4){r->cascade_texel[0], r->cascade_texel[1], r->cascade_texel[2], r->cascade_texel[3]};
  F->cascade_depth = (v4){r->cascade_depth[0], r->cascade_depth[1], r->cascade_depth[2], r->cascade_depth[3]};
  F->cam_pos = (v4){(f32)cam.x, (f32)cam.y, (f32)cam.z, in->time};
  F->cam_delta = (v4){delta.x, delta.y, delta.z, (f32)(r->frame_index % 4096)};
  F->sun_dir = (v4){sun.x, sun.y, sun.z, sun_up ? 1.0f : 0.0f};
  F->moon_dir = (v4){moon.x, moon.y, moon.z, 1};
  F->light_dir = (v4){L.x, L.y, L.z, sun_up ? 1.0f : 0.0f};
  F->sun_color = (v4){sun_col.x, sun_col.y, sun_col.z, 0};
  F->moon_color = (v4){moon_col.x, moon_col.y, moon_col.z, 0};
  F->light_color = (v4){light_col.x, light_col.y, light_col.z, 1};
  F->res = (v4){(f32)w, (f32)h, 1.0f / w, 1.0f / h};
  F->jitter = (v4){jx, jy, 0, 0};
  f32 dt_ = in->day_time;
  f32 morning = m_expf(-m_powf((dt_ - 0.02f) / 0.05f, 2)) + m_expf(-m_powf((dt_ - 1.0f) / 0.03f, 2));
  f32 evening = m_expf(-m_powf((dt_ - 0.47f) / 0.04f, 2));
  f32 fog_density = 0.00055f + morning * 0.0055f + evening * 0.0022f + night * 0.0008f + in->rain * 0.0035f;
  F->fog = (v4){96, (f32)(s->render_distance * 16), fog_density, 26 + (1 - morning) * 26};
  F->world = (v4){in->underwater ? 1.0f : 0.0f, in->eye_sky, WC_NEAR, far_d};
  r->cloud_time += in->dt;
  f32 coverage = in->cloud_coverage + (0.96f - in->cloud_coverage) * in->rain;
  F->cloud = (v4){s->clouds > 0 ? coverage : 0, WC_CLOUD_BASE, WC_CLOUD_THICK, r->cloud_time};
  F->settings = (v4){s->shadows ? 1.0f : 0.0f, s->ssao ? 1.0f : 0.0f, (f32)s->shadow_res, s->ssr ? 1.0f : 0.0f};
  F->weather = (v4){in->rain, in->wetness, 1 + in->rain * 1.5f, s->volumetrics ? 1.0f : 0.0f};
  F->sky = (v4){WC_SUN_ILLUM, WC_MOON_ILLUM, 0.00035f + night * 0.0004f, night};
  F->extra = (v4){in->snow, in->snow_cover, in->flash, (f32)r->debug_view};

  // ---- draw records and culling ----
  WcDrawRecord *records = ALLOC_ARRAY_NO_ZERO(ta, WcDrawRecord, world->renderable_count * 2 + 1);
  WcFrustum cam_fr = wc_frustum(view_proj_nj, ta);
  // jobs: camera opaque, camera translucent, then each refreshed cascade and the water casters
  WcCullJob *jobs = ALLOC_ARRAY(ta, WcCullJob, WC_CASCADES + 3);
  jobs[0] = (WcCullJob){.part = 0, .fr = &cam_fr, .sections = &r->stats.sections};
  jobs[1] = (WcCullJob){.part = 1, .fr = &cam_fr};
  u32 njobs = 2;
  u32 *cascade_job = ALLOC_ARRAY(ta, u32, WC_CASCADES);
  u32 water_job = 0;
  if (s->shadows) {
    for (u32 c = 0; c < WC_CASCADES; c++) {
      if (!(refresh & (1u << c))) continue;
      WcFrustum *fr = ALLOC(ta, WcFrustum);
      *fr = wc_frustum(r->cascade_vp[c], ta);
      // cached cascades keep buried casters: the camera may be underground before they refresh again
      cascade_job[c] = njobs;
      jobs[njobs++] = (WcCullJob){.part = 0, .fr = fr, .buried = c >= 1 && c < WC_CASCADE_CACHED};
    }
    WcFrustum *fr1 = ALLOC(ta, WcFrustum);
    *fr1 = wc_frustum(r->cascade_vp[1], ta);
    water_job = njobs;
    jobs[njobs++] = (WcCullJob){.part = 1, .fr = fr1};
  }
  u32 rec_count = wc_cull(world, records, jobs, njobs, cam, ta);
  if (rec_count) gpu_update_buffer(r->draw_records, records, rec_count * sizeof(WcDrawRecord));
  WcDrawList opaque_cols = jobs[0].out, trans_cols = jobs[1].out;
  WcDrawList *shadow_cols = ALLOC_ARRAY(ta, WcDrawList, WC_CASCADES);
  WcDrawList water_caster_cols = {0};
  if (s->shadows) {
    for (u32 c = 0; c < WC_CASCADES; c++)
      if (refresh & (1u << c)) shadow_cols[c] = jobs[cascade_job[c]].out;
    water_caster_cols = jobs[water_job].out;
  }
  // face buckets per pass: the camera's back faces (the translucent pipeline draws both sides) and light-facing solids
  u32 bound = wc_ranges_bound(&opaque_cols) + wc_ranges_bound(&trans_cols) + wc_ranges_bound(&water_caster_cols);
  for (u32 c = 0; c < WC_CASCADES; c++) bound += wc_ranges_bound(&shadow_cols[c]);
  WcRanges rs = {ALLOC_ARRAY_NO_ZERO(ta, WcRange, bound ? bound : 1), 0, bound, 0};
  v3 Lc = r->cascade_cache[0].L;
  WcBatchList opaque = wc_emit_ranges(&rs, &opaque_cols, WC_FACES_CAMERA, cam, Lc, ta);
  WcBatchList trans = wc_emit_ranges(&rs, &trans_cols, WC_FACES_ALL, cam, Lc, ta);
  r->stats.quads = rs.entries;
  WcBatchList *shadow_lists = ALLOC_ARRAY(ta, WcBatchList, WC_CASCADES);
  for (u32 c = 0; c < WC_CASCADES; c++)
    shadow_lists[c] = wc_emit_ranges(&rs, &shadow_cols[c], WC_FACES_LIGHT, cam, r->cascade_cache[c].L, ta);
  WcBatchList water_casters = wc_emit_ranges(&rs, &water_caster_cols, WC_FACES_ALL, cam, Lc, ta);
  if (rs.count) {
    wc_ensure_buffer(&r->cull_ranges, &r->cull_range_cap, rs.count, sizeof(WcRange), GPU_BUFFER_USAGE_CPU_WRITE,
                     "wc cull ranges");
    wc_ensure_buffer(&r->visible, &r->visible_cap, rs.entries, 2 * sizeof(u32), GPU_BUFFER_USAGE_GPU_WRITE,
                     "wc visible quads");
    gpu_update_buffer(r->cull_ranges, rs.items, rs.count * sizeof(WcRange));
    u32 gx = rs.count < 65535 ? rs.count : 65535;
    gpu_apply_compute_pipeline(r->cull_expand_cs);
    shader_wc_cull_expand_apply_params(&(ShaderWcCullExpandExpandParams){.range_count = rs.count, .groups_x = gx});
    GpuComputeBindings eb =
        shader_wc_cull_expand_bindings((ShaderWcCullExpandBindings){.ranges = r->cull_ranges, .visible = r->visible});
    gpu_apply_compute_bindings(&eb);
    gpu_dispatch(gx, (rs.count + gx - 1) / gx, 1);
    gpu_buffer_access(r->visible, GPU_ACCESS_SHADER_READ);
  }
  if (ents->vert_count) gpu_update_buffer(r->ent_vb, ents->verts, ents->vert_count * WC_ENTITY_STRIDE * sizeof(f32));

  // ---- sky luts ----
  wc_build_luts(r);
  GpuTexture trans_tex = wc_color(r->trans_lut, 0), ms_tex = wc_color(r->ms_lut, 0);
  f32 sky_h = m_clampf((f32)cam.y - 62.0f, 0.0f, 3000.0f);
  b32 sky_moved = !r->sky_valid || m_fabsf(sky_h - r->sky_height) > 1.0f;
  for (u32 b = 0; b < 2; b++) {
    v3 body = b ? moon : sun;
    f32 alt = m_asinf(m_clampf(body.y, -1.0f, 1.0f));
    f32 *cached = b ? &r->sky_alt_moon : &r->sky_alt_sun;
    if (!sky_moved && m_fabsf(alt - *cached) <= 5e-4f) continue;
    *cached = alt;
    wc_begin(r, WC_GPU_SKY, b ? r->sky_moon : r->sky_sun);
    gpu_apply_pipeline(r->skyview);
    wc_frame_uniforms(r);
    shader_wc_skyview_apply_p(&(ShaderWcSkyviewSkyViewParams){.body_dir = {body.x, body.y, body.z, 0}});
    shader_wc_skyview_apply_bindings((ShaderWcSkyviewBindings){.trans_lut = trans_tex, .ms_lut = ms_tex});
    wc_fullscreen_draw();
    wc_end(r);
  }
  if (sky_moved) r->sky_height = sky_h;
  r->sky_valid = true;
  GpuTexture sky_sun = wc_color(r->sky_sun, 0), sky_moon = wc_color(r->sky_moon, 0);
  wc_begin(r, WC_GPU_SKY, r->ambient);
  gpu_apply_pipeline(r->sky_ambient);
  wc_frame_uniforms(r);
  shader_wc_sky_ambient_apply_bindings((ShaderWcSkyAmbientBindings){.sky_sun_tex = sky_sun, .sky_moon_tex = sky_moon});
  wc_fullscreen_draw();
  wc_end(r);
  GpuTexture ambient = wc_color(r->ambient, 0);

  // ---- shadow atlas: 4 cascades in quadrants, water surfaces in their own map ----
  if (s->shadows) {
    u32 R = s->shadow_res;
    // cached quadrants keep their depth: each re-rendered one clears only itself
    wc_pass(r, WC_GPU_SHADOW, (GpuPassDesc){.render_target = r->shadow_atlas, .no_clear_depth = true});
    for (u32 c = 0; c < WC_CASCADES; c++) {
      if (!(refresh & (1u << c))) continue;
      gpu_set_viewports(&(GpuViewport){.x = (f32)((c & 1) * R), .y = (f32)((c >> 1) * R), .w = (f32)R,
                                       .h = (f32)R, .min_depth = 0, .max_depth = 1},
                        1);
      gpu_apply_pipeline(r->shadow_clear);
      wc_fullscreen_draw();
      gpu_apply_pipeline(r->shadow);
      wc_frame_uniforms(r);
      shader_wc_shadow_apply_p(&(ShaderWcShadowShadowParams){.light_vp = r->cascade_vp[c]});
      r->stats.shadow_draw_calls += wc_draw_terrain(r, world, &shadow_lists[c], WC_TERRAIN_SHADOW);
    }
    wc_end(r);
    wc_begin(r, WC_GPU_SHADOW, r->water_shadow);
    gpu_apply_pipeline(r->shadow);
    wc_frame_uniforms(r);
    shader_wc_shadow_apply_p(&(ShaderWcShadowShadowParams){.light_vp = r->cascade_vp[1]});
    wc_draw_terrain(r, world, &water_casters, WC_TERRAIN_SHADOW);
    wc_end(r);
  }

  // ---- g-buffer ----
  wc_begin(r, WC_GPU_GBUFFER, r->gbuf);
  gpu_apply_pipeline(r->terrain);
  wc_frame_uniforms(r);
  r->stats.draw_calls += wc_draw_terrain(r, world, &opaque, WC_TERRAIN_GBUFFER);
  if (ents->vert_count) {
    ShaderWcEntityBindings eb = {.vertex_buffer = r->ent_vb, .index_buffer = r->ent_ib,
                                 .index_format = GPU_INDEX_FORMAT_U16, .albedo_tex = r->blk_albedo,
                                 .normal_tex = r->blk_normal, .spec_tex = r->blk_spec};
    if (ents->hand_start > 0) {
      gpu_apply_pipeline(r->entity);
      wc_frame_uniforms(r);
      shader_wc_entity_apply_p(&(ShaderWcEntityEntityParams){.normal_rot = m4_identity()});
      shader_wc_entity_apply_bindings(eb);
      gpu_draw_indexed(ents->hand_start / 4 * 6, 1, 0, 0);
    }
    if (ents->vert_count > ents->hand_start) {
      gpu_apply_pipeline(r->entity_hand);
      wc_frame_uniforms(r);
      shader_wc_entity_apply_p(&(ShaderWcEntityEntityParams){.normal_rot = ents->hand_rot});
      shader_wc_entity_apply_bindings(eb);
      gpu_draw_indexed((ents->vert_count - ents->hand_start) / 4 * 6, 1, ents->hand_start / 4 * 6, 0);
    }
  }
  wc_end(r);
  GpuTexture gdepth = wc_depth(r->gbuf);
  GpuTexture galbedo = wc_color(r->gbuf, 0), gnormal = wc_color(r->gbuf, 1);
  GpuTexture glight = wc_color(r->gbuf, 2), gspec = wc_color(r->gbuf, 3);

  // ---- ssao ----
  if (s->ssao) {
    wc_begin(r, WC_GPU_SSAO, r->ssao_rt);
    gpu_apply_pipeline(r->ssao);
    wc_frame_uniforms(r);
    shader_wc_ssao_apply_bindings((ShaderWcSsaoBindings){.depth_tex = gdepth, .gnormal_tex = gnormal});
    wc_fullscreen_draw();
    wc_end(r);
  }

  // ---- clouds: one texel per 2x2 block marched per frame, the rest reprojected ----
  u32 cloud_cur = r->cloud_idx;
  if (s->clouds > 0) {
    wc_begin(r, WC_GPU_CLOUDS, r->cloud_cur);
    gpu_apply_pipeline(r->clouds);
    wc_frame_uniforms(r);
    shader_wc_clouds_apply_p(&(ShaderWcCloudsCloudParams){.low_res = {(f32)r->cw, (f32)r->ch, 0, 0}});
    shader_wc_clouds_apply_bindings((ShaderWcCloudsBindings){
        .noise_base_tex = r->noise_base, .noise_detail_tex = r->noise_detail, .weather_tex = r->weather,
        .sky_sun_tex = sky_sun, .sky_moon_tex = sky_moon, .ambient_tex = ambient});
    wc_fullscreen_draw();
    wc_end(r);
    wc_begin(r, WC_GPU_CLOUDS, r->cloud_hist[cloud_cur]);
    gpu_apply_pipeline(r->clouds_temporal);
    wc_frame_uniforms(r);
    shader_wc_clouds_temporal_apply_p(&(ShaderWcCloudsTemporalCloudTemporalParams){
        .low_res = {(f32)((r->cw + 1) / 2), (f32)((r->ch + 1) / 2), r->reset_history ? 1.0f : 0.0f, 0}});
    shader_wc_clouds_temporal_apply_bindings((ShaderWcCloudsTemporalBindings){
        .current_tex = wc_color(r->cloud_cur, 0), .history_tex = wc_color(r->cloud_hist[1 - cloud_cur], 0)});
    wc_fullscreen_draw();
    wc_end(r);
  } else {
    wc_pass(r, WC_GPU_CLOUDS, (GpuPassDesc){.render_target = r->cloud_hist[cloud_cur], .clear_color = {0, 0, 0, 1}});
    wc_end(r);
  }
  GpuTexture clouds = wc_color(r->cloud_hist[cloud_cur], 0);
  r->cloud_idx = 1 - r->cloud_idx;

  // ---- deferred lighting into the scene target (depth already copied in) ----
  GpuTexture shadow_depth = wc_depth(r->shadow_atlas);
  wc_pass(r, WC_GPU_DEFERRED, (GpuPassDesc){.render_target = r->scene, .no_clear_color = true, .no_clear_depth = true});
  gpu_apply_pipeline(r->deferred);
  wc_frame_uniforms(r);
  shader_wc_deferred_apply_bindings((ShaderWcDeferredBindings){
      .galbedo_tex = galbedo, .gnormal_tex = gnormal, .glight_tex = glight, .gspec_tex = gspec,
      .depth_tex = gdepth, .ssao_tex = wc_color(r->ssao_rt, 0), .clouds_tex = clouds, .weather_tex = r->weather,
      .water_shadow_tex = wc_depth(r->water_shadow), .prev_color_tex = wc_color(r->history[1 - r->history_idx], 0),
      .sky_sun_tex = sky_sun, .sky_moon_tex = sky_moon, .ambient_tex = ambient, .shadow_cmp_tex = shadow_depth,
      .shadow_raw_tex = shadow_depth});
  wc_fullscreen_draw();
  wc_end(r);

  // ---- translucent water and ice (forward, refracting a copy of the lit scene) ----
  GpuTexture scene_color = wc_color(r->scene, 0);
  if (trans.count) {
    gpu_copy_texture(wc_color(r->scene_copy, 0), scene_color);
    gpu_copy_texture(wc_depth(r->depth_copy), gdepth);
    wc_pass(r, WC_GPU_WATER, (GpuPassDesc){.render_target = r->scene, .depth_target = r->gbuf, .no_clear_color = true,
                                           .no_clear_depth = true});
    gpu_apply_pipeline(r->water);
    wc_frame_uniforms(r);
    r->stats.draw_calls += wc_draw_terrain(r, world, &trans, WC_TERRAIN_WATER);
    wc_end(r);
  }
  GpuTexture scene_depth = gdepth;

  // ---- atmospherics: the shadowed near-fog march runs at half res, the composite at full ----
  if (s->volumetrics || in->underwater) {
    wc_begin(r, WC_GPU_ATMOS, r->shafts_rt);
    gpu_apply_pipeline(r->shafts);
    wc_frame_uniforms(r);
    shader_wc_shafts_apply_bindings(
        (ShaderWcShaftsBindings){.depth_tex = scene_depth, .shadow_cmp_tex = shadow_depth});
    wc_fullscreen_draw();
    wc_end(r);
  }
  wc_begin(r, WC_GPU_ATMOS, r->hdr_b);
  gpu_apply_pipeline(r->atmospherics);
  wc_frame_uniforms(r);
  shader_wc_atmospherics_apply_bindings((ShaderWcAtmosphericsBindings){
      .color_tex = scene_color, .depth_tex = scene_depth, .sky_sun_tex = sky_sun, .sky_moon_tex = sky_moon,
      .ambient_tex = ambient, .shafts_tex = wc_color(r->shafts_rt, 0)});
  wc_fullscreen_draw();
  wc_end(r);

  // ---- taa ----
  GpuTexture resolved = wc_color(r->hdr_b, 0);
  if (s->taa) {
    u32 cur = r->history_idx;
    wc_begin(r, WC_GPU_TAA, r->history[cur]);
    gpu_apply_pipeline(r->taa);
    wc_frame_uniforms(r);
    shader_wc_taa_apply_p(&(ShaderWcTaaTaaParams){
        .params = {r->reset_history ? 1.0f : 0.0f, uw != w || uh != h ? 1.0f : 0.0f, 0, 0},
        .out_res = {(f32)uw, (f32)uh, 1.0f / uw, 1.0f / uh}});
    shader_wc_taa_apply_bindings((ShaderWcTaaBindings){.color_tex = resolved,
                                                       .history_tex = wc_color(r->history[1 - cur], 0),
                                                       .depth_tex = scene_depth, .glight_tex = glight});
    wc_fullscreen_draw();
    wc_end(r);
    resolved = wc_color(r->history[cur], 0);
    r->history_idx = 1 - cur;
  }

  // ---- bloom down chain, exposure from the 1/4 level, bloom up chain ----
  GpuTexture src = resolved;
  u32 sw = s->taa ? uw : w, sh = s->taa ? uh : h;
  for (u32 i = 0; i < WC_BLOOM_LEVELS; i++) {
    wc_begin(r, WC_GPU_BLOOM, r->bloom[i]);
    gpu_apply_pipeline(r->bloom_down);
    shader_wc_bloom_down_apply_p(&(ShaderWcBloomDownBloomParams){.params = {1.0f / sw, 1.0f / sh, i == 0 ? 1.0f : 0.0f, 0}});
    shader_wc_bloom_down_apply_bindings((ShaderWcBloomDownBindings){.src_tex = src});
    wc_fullscreen_draw();
    wc_end(r);
    src = wc_color(r->bloom[i], 0);
    sw = r->bloom_w[i];
    sh = r->bloom_h[i];
  }
  u32 ei = r->frame_index % 2;
  wc_begin(r, WC_GPU_BLOOM, r->exposure_rt[ei]);
  gpu_apply_pipeline(r->exposure);
  shader_wc_exposure_apply_p(&(ShaderWcExposureExposureParams){
      .params = {m_minf(0.1f, in->dt), 0.02f, in->underwater ? 5.0f : 11.0f, 0.0f}});
  shader_wc_exposure_apply_bindings((ShaderWcExposureBindings){.src_tex = wc_color(r->bloom[1], 0),
                                                               .prev_tex = wc_color(r->exposure_rt[1 - ei], 0)});
  wc_fullscreen_draw();
  wc_end(r);
  for (u32 i = WC_BLOOM_LEVELS - 1; i > 0; i--) {
    wc_pass(r, WC_GPU_BLOOM, (GpuPassDesc){.render_target = r->bloom[i - 1], .no_clear_color = true});
    gpu_apply_pipeline(r->bloom_up);
    shader_wc_bloom_up_apply_p(&(ShaderWcBloomUpBloomUpParams){
        .params = {1.0f / r->bloom_w[i], 1.0f / r->bloom_h[i], 1.0f, 0}});
    shader_wc_bloom_up_apply_bindings((ShaderWcBloomUpBindings){.src_tex = wc_color(r->bloom[i], 0)});
    wc_fullscreen_draw();
    wc_end(r);
  }

  // ---- final composite to the swapchain ----
  wc_pass(r, WC_GPU_FINAL, (GpuPassDesc){.clear_color = {0, 0, 0, 1}});
  gpu_apply_pipeline(r->final);
  wc_frame_uniforms(r);
  ShaderWcFinalFinalParams fp = {.grade = {s->bloom ? 0.055f : 0.0f, s->taa ? 0.35f : 0.0f, 1.18f, 1.22f}};
  if (in->has_selection) {
    fp.sel_min = (v4){(f32)(in->sel.x - cam.x), (f32)(in->sel.y - cam.y), (f32)(in->sel.z - cam.z), 1};
    fp.sel_max = (v4){(f32)(in->sel.x + 1 - cam.x), (f32)(in->sel.y + 1 - cam.y), (f32)(in->sel.z + 1 - cam.z), 0};
  }
  // debug views 1..6 show raw values at a fixed exposure
  if (r->debug_view >= 1 && r->debug_view <= 6) fp.sel_max.w = 0.72f;
  fp.crosshair = (v4){(f32)out_w, (f32)out_h, m_maxf(in->dpr, 1.0f), in->crosshair ? 1.0f : 0.0f};
  shader_wc_final_apply_p(&fp);
  shader_wc_final_apply_bindings((ShaderWcFinalBindings){.color_tex = resolved,
                                                         .bloom_tex = wc_color(r->bloom[0], 0),
                                                         .exposure_tex = wc_color(r->exposure_rt[ei], 0),
                                                         .depth_tex = scene_depth});
  wc_fullscreen_draw();
  wc_end(r);

  // ---- bookkeeping ----
  r->prev_view_proj = view_proj_nj;
  r->prev_cam = cam;
  r->frame_index++;
  r->reset_history = false;
  tctx_temp_allocator_end(tmp);
}

#undef WC_PLANES

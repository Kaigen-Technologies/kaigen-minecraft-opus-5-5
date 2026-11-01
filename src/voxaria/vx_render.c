#include "voxaria/vx_render.h"
#include "shaders/vx_terrain.h"
#include "shaders/vx_far.h"
#include "shaders/vx_ocean.h"
#include "shaders/vx_entity.h"
#include "shaders/vx_entity_shadow.h"
#include "shaders/vx_particle.h"
#include "shaders/vx_box.h"
#include "shaders/vx_box_shadow.h"
#include "shaders/vx_jelly.h"
#include "shaders/vx_vfx.h"
#include "shaders/vx_shadow.h"
#include "shaders/vx_shadow_clear.h"
#include "shaders/vx_water.h"
#include "shaders/vx_ssao.h"
#include "shaders/vx_clouds.h"
#include "shaders/vx_clouds_temporal.h"
#include "shaders/vx_deferred.h"
#include "shaders/vx_atmospherics.h"
#include "shaders/vx_shafts.h"
#include "shaders/vx_cull_expand.h"
#include "shaders/vx_light_upload.h"
#include "shaders/vx_taa.h"
#include "shaders/vx_bloom_down.h"
#include "shaders/vx_bloom_up.h"
#include "shaders/vx_exposure.h"
#include "shaders/vx_final.h"
#include "shaders/vx_noise_weather.h"
#include "shaders/vx_noise_down2d.h"

static_assert(sizeof(VxFrameUniforms) == sizeof(ShaderVxTerrainFrame), "VxFrameUniforms must mirror _Frame");
static_assert(sizeof(VxPointLightUniforms) == sizeof(ShaderVxDeferredLights), "VxPointLightUniforms must mirror _Lights");

#define VX_CLOUD_BASE 236.0f
#define VX_CLOUD_THICK 30.0f
#define VX_BLOOM_THRESHOLD 1.6f
#define VX_BLOOM_STRENGTH 0.16f
#define VX_EXPOSURE_MIN 0.75f
#define VX_EXPOSURE_MAX 1.4f
#define VX_EXPOSURE_COMP 1.5f
#define VX_NEAR 0.06f
#define VX_FAR_EDGE 2400.0f // blocks: where the sea meets the horizon haze
#define VX_TILT 0.42f
#define VX_SHADOW_EXTRA 220.0f
#define VX_CASCADE_MARGIN 0.15f // fraction of a cascade's reach the camera may move before it re-renders
#define VX_CASCADE_CACHED 2     // cascades from this one on are cached; nearer ones carry animated foliage
#define VX_MAX_DRAW_QUADS (VX_MAX_SECTION_QUADS * VX_SECTIONS)
#define VX_HDR GPU_TEXTURE_FORMAT_RGBA16F
#ifdef WASM
// rg11b10ufloat is only renderable behind an optional webgpu feature
#define VX_BLOOM_FORMAT GPU_TEXTURE_FORMAT_RGBA16F
// light volume uploads per frame: sections with data, and uniform ones (a header each)
#define VX_VOL_DATA_BUDGET 48
#else
#define VX_BLOOM_FORMAT GPU_TEXTURE_FORMAT_R11G11B10F
#define VX_VOL_DATA_BUDGET 128
#endif
#define VX_VOL_UNIFORM_BUDGET 2048
#define VX_VOL_SECTION_WORDS (VX_SECTION_VOLUME / 2)
#define VX_PLANES 6

// mirrors VxDraw in shaders/vx_quad.glsl
typedef struct {
  v3 origin;
  u32 _pad;
} VxDrawRecord;

typedef struct {
  v4 *p;
  u32 n;
} VxFrustum;

// ---- celestial mechanics ----

void vx_celestial(f32 day_time, v3 *sun, v3 *moon, m4 *star_rot) {
  f32 a = day_time * PI * 2.0f;
  f32 s = m_sinf(a), c = m_cosf(a);
  *sun = (v3){c, s * m_cosf(VX_TILT), s * m_sinf(VX_TILT)};
  *moon = (v3){-sun->x, -sun->y, -sun->z};
  // stars rotate around the celestial axis (perpendicular to the sun path plane)
  f32 x = 0.0f, y = -m_sinf(VX_TILT), z = m_cosf(VX_TILT);
  f32 ca = m_cosf(-a), sa = m_sinf(-a), t = 1.0f - ca;
  *star_rot = (m4){{{t * x * x + ca, t * x * y + sa * z, t * x * z - sa * y, 0},
                    {t * x * y - sa * z, t * y * y + ca, t * y * z + sa * x, 0},
                    {t * x * z + sa * y, t * y * z - sa * x, t * z * z + ca, 0},
                    {0, 0, 0, 1}}};
}

// ---- pipelines and targets ----

typedef enum {
  VX_TARGET_COLOR, // color-only render target
  VX_TARGET_SWAPCHAIN,
} VxTargetKind;

hz_internal GpuPipeline vx_fullscreen_pipeline(const GpuPipelineDesc *src, GpuTextureFormat fmt, VxTargetKind kind,
                                               b32 additive) {
  GpuPipelineDesc d = *src;
  d.compile_async = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_write = false;
  if (kind == VX_TARGET_SWAPCHAIN) {
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

hz_internal GpuComputePipeline vx_compute_pipeline(const GpuComputePipelineDesc *src) {
  GpuComputePipelineDesc d = *src;
  d.compile_async = true;
  return gpu_make_compute_pipeline(&d);
}

// rt0 albedo + vertex ao, rt1 octahedral normal, emission and flags
hz_internal void vx_gbuffer_formats(GpuPipelineDesc *d) {
  d->color_attachment_count = 2;
  d->color_formats[0] = GPU_TEXTURE_FORMAT_RGBA8_SRGB;
  d->color_formats[1] = GPU_TEXTURE_FORMAT_RGB10A2;
}

void vx_render_create_pipelines(VxRenderer *r) {
  // the core tier first: async compiles start in creation order
  GpuPipelineDesc d = *shader_vx_terrain_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->terrain = gpu_make_pipeline(&d);

  d = *shader_vx_far_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->far_terrain = gpu_make_pipeline(&d);

  d = *shader_vx_ocean_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->ocean = gpu_make_pipeline(&d);

  d = *shader_vx_shadow_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.depth_clip_disable = true;
  d.compile_async = true;
  r->shadow = gpu_make_pipeline(&d);

  d = *shader_vx_entity_shadow_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.depth_clip_disable = true;
  d.compile_async = true;
  r->entity_shadow = gpu_make_pipeline(&d);

  d = *shader_vx_box_shadow_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.depth_clip_disable = true;
  d.compile_async = true;
  r->box_shadow = gpu_make_pipeline(&d);

  d = *shader_vx_shadow_clear_pipeline_desc();
  d.color_attachment_count = 0;
  d.color_write_disabled = true;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_ALWAYS;
  d.compile_async = true;
  r->shadow_clear = gpu_make_pipeline(&d);

  r->deferred = vx_fullscreen_pipeline(shader_vx_deferred_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);

  d = *shader_vx_water_pipeline_desc();
  d.color_attachment_count = 1;
  d.color_formats[0] = VX_HDR;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->water = gpu_make_pipeline(&d);

  d = *shader_vx_entity_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->entity = gpu_make_pipeline(&d);
  // the held item always draws on top
  d.depth_compare = GPU_COMPARE_ALWAYS;
  r->entity_hand = gpu_make_pipeline(&d);

  d = *shader_vx_box_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->box = gpu_make_pipeline(&d);

  // jelly: front faces only, the far side comes from the exact thickness through the box
  d = *shader_vx_jelly_pipeline_desc();
  d.color_attachment_count = 1;
  d.color_formats[0] = VX_HDR;
  d.cull_mode = GPU_CULL_BACK;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->jelly = gpu_make_pipeline(&d);

  // forward sprites, premultiplied: a glow writes zero alpha and adds, smoke covers what is behind it
  d = *shader_vx_vfx_pipeline_desc();
  d.color_attachment_count = 1;
  d.color_formats[0] = VX_HDR;
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = false;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.blend = (GpuBlendState){.enabled = true, .src_factor = GPU_BLEND_ONE, .dst_factor = GPU_BLEND_ONE_MINUS_SRC_ALPHA,
                            .op = GPU_BLEND_OP_ADD, .src_factor_alpha = GPU_BLEND_ONE,
                            .dst_factor_alpha = GPU_BLEND_ONE_MINUS_SRC_ALPHA, .op_alpha = GPU_BLEND_OP_ADD};
  d.compile_async = true;
  r->vfx = gpu_make_pipeline(&d);

  d = *shader_vx_particle_pipeline_desc();
  vx_gbuffer_formats(&d);
  d.cull_mode = GPU_CULL_NONE;
  d.depth_test = true;
  d.depth_write = true;
  d.depth_compare = GPU_COMPARE_GREATER;
  d.compile_async = true;
  r->particle = gpu_make_pipeline(&d);

  r->cull_expand_cs = vx_compute_pipeline(shader_vx_cull_expand_compute_pipeline_desc());
  r->vol.upload_cs = vx_compute_pipeline(shader_vx_light_upload_compute_pipeline_desc());
  r->shafts = vx_fullscreen_pipeline(shader_vx_shafts_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->atmospherics = vx_fullscreen_pipeline(shader_vx_atmospherics_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->bloom_down = vx_fullscreen_pipeline(shader_vx_bloom_down_pipeline_desc(), VX_BLOOM_FORMAT, VX_TARGET_COLOR, false);
  r->bloom_up = vx_fullscreen_pipeline(shader_vx_bloom_up_pipeline_desc(), VX_BLOOM_FORMAT, VX_TARGET_COLOR, true);
  r->exposure = vx_fullscreen_pipeline(shader_vx_exposure_pipeline_desc(), GPU_TEXTURE_FORMAT_RGBA32F, VX_TARGET_COLOR, false);
  r->final = vx_fullscreen_pipeline(shader_vx_final_pipeline_desc(), GPU_TEXTURE_FORMAT_RGBA8, VX_TARGET_SWAPCHAIN, false);

  // effects: each renders as switched off until its own pipelines land
  r->ssao = vx_fullscreen_pipeline(shader_vx_ssao_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->clouds = vx_fullscreen_pipeline(shader_vx_clouds_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->clouds_temporal = vx_fullscreen_pipeline(shader_vx_clouds_temporal_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->taa = vx_fullscreen_pipeline(shader_vx_taa_pipeline_desc(), VX_HDR, VX_TARGET_COLOR, false);
  r->noise_weather_cs = vx_compute_pipeline(shader_vx_noise_weather_compute_pipeline_desc());
  r->noise_down2d_cs = vx_compute_pipeline(shader_vx_noise_down2d_compute_pipeline_desc());
}

hz_internal u32 vx_mip_count(u32 size) {
  u32 n = 1;
  while (size > 1) {
    size >>= 1;
    n++;
  }
  return n;
}

// generates the cloud weather map and its mips once, before anything samples it
hz_internal void vx_bake_noise(VxRenderer *r) {
  if (r->noise_baked) return;
  gpu_apply_compute_pipeline(r->noise_weather_cs);
  GpuComputeBindings b = shader_vx_noise_weather_bindings((ShaderVxNoiseWeatherBindings){.dst = r->weather});
  gpu_apply_compute_bindings(&b);
  shader_vx_noise_weather_dispatch(VX_WEATHER_SIZE, VX_WEATHER_SIZE);
  u32 mips = vx_mip_count(VX_WEATHER_SIZE);
  gpu_apply_compute_pipeline(r->noise_down2d_cs);
  for (u32 m = 1; m < mips; m++) {
    gpu_texture_access_mip_range(r->weather, m - 1, 1, GPU_ACCESS_SHADER_READ);
    u32 s = VX_WEATHER_SIZE >> m;
    shader_vx_noise_down2d_apply_params(&(ShaderVxNoiseDown2dDownParams){.src_mip = (i32)(m - 1)});
    b = shader_vx_noise_down2d_bindings((ShaderVxNoiseDown2dBindings){.dst = r->weather, .src = r->weather});
    b.images.items[0].mip = m;
    // the read view stops above the level being written (webgpu rejects overlapping views)
    b.texture_view_mip_count[0] = m;
    gpu_apply_compute_bindings(&b);
    shader_vx_noise_down2d_dispatch(s, s);
  }
  gpu_texture_access(r->weather, GPU_ACCESS_SHADER_READ);
  r->noise_baked = true;
}

hz_internal GpuRenderTarget vx_rt(const char *name, u32 w, u32 h, GpuTextureFormat fmt, b32 depth) {
  return gpu_make_render_target(&(GpuRenderTargetDesc){
      .name = name, .width = w, .height = h, .color_attachment_count = 1, .formats = {fmt}, .needs_depth = depth});
}

hz_internal void vx_bloom_sizes(VxRenderer *r, u32 w, u32 h) {
  u32 bw = w, bh = h;
  for (u32 i = 0; i < VX_BLOOM_LEVELS; i++) {
    bw = bw > 1 ? bw >> 1 : 1;
    bh = bh > 1 ? bh >> 1 : 1;
    r->bloom_w[i] = bw;
    r->bloom_h[i] = bh;
  }
}

hz_internal void vx_cloud_size(const VxRenderer *r, u32 w, u32 h, u32 *cw, u32 *ch) {
  f32 scale = r->settings.clouds >= 2 ? 0.5f : 0.25f;
  *cw = (u32)m_maxf(1.0f, m_floorf(w * scale));
  *ch = (u32)m_maxf(1.0f, m_floorf(h * scale));
}

hz_internal void vx_create_targets(VxRenderer *r, u32 w, u32 h, u32 uw, u32 uh) {
  r->w = w;
  r->h = h;
  r->gbuf = gpu_make_render_target(&(GpuRenderTargetDesc){
      .name = "vx gbuffer",
      .width = w,
      .height = h,
      .color_attachment_count = 2,
      .formats = {GPU_TEXTURE_FORMAT_RGBA8_SRGB, GPU_TEXTURE_FORMAT_RGB10A2},
      .needs_depth = true,
  });
  // the scene renders with the g-buffer's depth (water tests and writes it): no depth of its own
  r->scene = vx_rt("vx scene", w, h, VX_HDR, false);
  r->depth_copy = gpu_make_render_target(
      &(GpuRenderTargetDesc){.name = "vx depth copy", .width = w, .height = h, .depth_only = true});
  r->scene_copy = vx_rt("vx scene copy", w, h, VX_HDR, false);
  r->hdr_b = vx_rt("vx hdr b", w, h, VX_HDR, false);
  r->ssao_rt = vx_rt("vx ssao", (w + 1) / 2, (h + 1) / 2, VX_HDR, false);
  r->shafts_rt = vx_rt("vx shafts", (w + 1) / 2, (h + 1) / 2, VX_HDR, false);
  r->history[0] = vx_rt("vx history 0", uw, uh, VX_HDR, false);
  r->history[1] = vx_rt("vx history 1", uw, uh, VX_HDR, false);
  r->uw = uw;
  r->uh = uh;
  vx_cloud_size(r, w, h, &r->cw, &r->ch);
  r->cloud_cur = vx_rt("vx clouds", (r->cw + 1) / 2, (r->ch + 1) / 2, VX_HDR, false);
  r->cloud_hist[0] = vx_rt("vx clouds hist 0", r->cw, r->ch, VX_HDR, false);
  r->cloud_hist[1] = vx_rt("vx clouds hist 1", r->cw, r->ch, VX_HDR, false);
  vx_bloom_sizes(r, uw, uh);
  for (u32 i = 0; i < VX_BLOOM_LEVELS; i++)
    r->bloom[i] = vx_rt("vx bloom", r->bloom_w[i], r->bloom_h[i], VX_BLOOM_FORMAT, false);
  r->needs_prime = true;
}

hz_internal u32 vx_scaled(u32 v, f32 scale) { return (u32)m_maxf(1.0f, m_floorf(v * scale)); }

// with taa on, a render scale below 1 upscales in the taa pass: history and bloom at the output size
hz_internal void vx_upscale_size(const VxRenderSettings *s, u32 out_w, u32 out_h, f32 dpr, u32 w, u32 h, u32 *uw,
                                 u32 *uh) {
  b32 up = s->taa && s->render_scale < 1.0f;
  f32 os = dpr > VX_MAX_DPR ? VX_MAX_DPR / dpr : 1.0f;
  *uw = up ? vx_scaled(out_w, os) : w;
  *uh = up ? vx_scaled(out_h, os) : h;
}

// the 3d view renders at most VX_MAX_DPR device px per logical px
hz_internal f32 vx_output_scale(const VxRenderSettings *s, f32 dpr) {
  return dpr > VX_MAX_DPR ? s->render_scale * VX_MAX_DPR / dpr : s->render_scale;
}

hz_internal void vx_ensure_targets(VxRenderer *r, u32 w, u32 h, u32 uw, u32 uh) {
  u32 cw, ch;
  vx_cloud_size(r, w, h, &cw, &ch);
  if (r->w == w && r->h == h && r->cw == cw && r->ch == ch && r->uw == uw && r->uh == uh) return;
  if (r->uw != uw || r->uh != uh) {
    gpu_resize_render_target(r->history[0], uw, uh);
    gpu_resize_render_target(r->history[1], uw, uh);
    vx_bloom_sizes(r, uw, uh);
    for (u32 i = 0; i < VX_BLOOM_LEVELS; i++) gpu_resize_render_target(r->bloom[i], r->bloom_w[i], r->bloom_h[i]);
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

hz_internal void vx_ensure_shadow(VxRenderer *r) {
  u32 res = r->settings.shadow_res;
  if (r->shadow_res == res) return;
  if (!handle_is_valid(r->shadow_atlas)) {
    r->shadow_atlas = gpu_make_render_target(&(GpuRenderTargetDesc){
        .name = "vx shadow atlas", .width = res * 2, .height = res * 2, .depth_only = true});
  } else {
    gpu_resize_render_target(r->shadow_atlas, res * 2, res * 2);
  }
  r->shadow_res = res;
  for (u32 c = 0; c < VX_CASCADES; c++) r->cascade_cache[c].valid = false;
}

// ---- light volume ----

// capacity for n elements of size bytes, doubled when it grows; the old buffer's release is deferred by the gpu layer
hz_internal void vx_ensure_buffer(GpuBuffer *buf, u32 *cap, u32 n, u32 size, GpuBufferUsage usage, const char *label) {
  if (n <= *cap && handle_is_valid(*buf)) return;
  u32 c = *cap ? *cap : 65536;
  while (c < n) c *= 2;
  if (handle_is_valid(*buf)) gpu_destroy_buffer(*buf);
  *buf = gpu_make_buffer(&(GpuBufferDesc){
      .type = GPU_BUFFER_STRUCTURED, .usage = usage, .size = c * size, .stride = size, .label = label});
  *cap = c;
}


hz_internal void vx_lightvol_init(VxLightVolume *v, Allocator *alloc) {
  for (u32 l = 0; l < 2; l++)
    v->tex[l] = gpu_make_storage_texture_3d(VX_VOL_N, VX_VOL_N, VX_VOL_N, GPU_TEXTURE_FORMAT_RGBA8, 1);
  v->dyn = gpu_make_storage_texture_3d(VX_DYN_SIZE, VX_DYN_SIZE, VX_DYN_SIZE, GPU_TEXTURE_FORMAT_RGBA8, 1);
  v->slots[0] = ALLOC_ARRAY(alloc, VxVolSlot, VX_VOL_SLOTS0);
  v->slots[1] = ALLOC_ARRAY(alloc, VxVolSlot, VX_VOL_SLOTS1);
  // every slot starts as an impossible section so the first window uploads all of it
  for (u32 l = 0; l < 2; l++)
    for (u32 i = 0; i < (u32)(l ? VX_VOL_SLOTS1 : VX_VOL_SLOTS0); i++) v->slots[l][i] = (VxVolSlot){.uid = UINT32_MAX};
  v->dyn_data = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                 .size = VX_DYN_SIZE * VX_DYN_SIZE * VX_DYN_SIZE * sizeof(u32),
                                                 .stride = sizeof(u32), .label = "vx dyn light"});
  // sized for a full frame's budget up front: a buffer recreated mid-frame on webgpu loses that frame's upload
  v->headers = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                .size = (VX_VOL_DATA_BUDGET + VX_VOL_UNIFORM_BUDGET) * 4 * sizeof(u32),
                                                .stride = 4 * sizeof(u32), .label = "vx light headers"});
  v->data = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                             .size = VX_VOL_DATA_BUDGET * VX_VOL_SECTION_WORDS * sizeof(u32),
                                             .stride = sizeof(u32), .label = "vx light data"});
}

typedef struct {
  u32 slot;
  u32 uniform_value;
  const u16 *src;
  VxVolSlot want;
} VxVolUpload;

// a section's light: its chunk's words when lit, open sky when nothing is loaded there
hz_internal VxVolSlot vx_vol_want(const VxWorld *w, i32 sx, i32 sy, i32 sz, const u16 **src) {
  *src = NULL;
  const VxChunk *c = vx_world_chunk(w, sx, sz);
  if (!c || c->state != VX_CHUNK_LIT || sy < 0 || sy >= VX_SECTIONS) return (VxVolSlot){0};
  *src = c->light + sy * VX_SECTION_VOLUME;
  return (VxVolSlot){.uid = c->uid, .version = c->light_version[sy], .sx = sx, .sy = sy, .sz = sz};
}

force_inline b32 vx_vol_same(const VxVolSlot *a, const VxVolSlot *b) {
  if (a->uid != b->uid) return false;
  if (a->uid == 0) return true;
  return a->version == b->version && a->sx == b->sx && a->sy == b->sy && a->sz == b->sz;
}

// one uniform value when every voxel of the section agrees
hz_internal b32 vx_vol_uniform(const u16 *src, u32 *value) {
  u16 v = src[0];
  for (u32 i = 1; i < VX_SECTION_VOLUME; i++)
    if (src[i] != v) return false;
  *value = v;
  return true;
}

// collects one level's changed slots in rings outward from the camera's section, so near light lands first
hz_internal u32 vx_lightvol_collect(VxLightVolume *v, const VxWorld *w, u32 level, iv3 cs, VxVolUpload *out,
                                    u32 *data_left, u32 *uniform_left) {
  i32 span = level ? VX_VOL_SPAN1 : VX_VOL_SPAN0;
  iv3 o = v->origin[level];
  iv3 c = {cs.x - o.x, cs.y - o.y, cs.z - o.z};
  u32 n = 0;
  for (i32 ring = 0; ring < span; ring++) {
    i32 z0 = MAX(-ring, -c.z), z1 = MIN(ring, span - 1 - c.z);
    i32 y0 = MAX(-ring, -c.y), y1 = MIN(ring, span - 1 - c.y);
    i32 x0 = MAX(-ring, -c.x), x1 = MIN(ring, span - 1 - c.x);
    for (i32 dz = z0; dz <= z1; dz++)
      for (i32 dy = y0; dy <= y1; dy++) {
        b32 shell = dz == -ring || dz == ring || dy == -ring || dy == ring;
        for (i32 dx = x0; dx <= x1; dx++) {
          // inside the shell's z and y faces only the two x ends belong to this ring
          if (!shell && dx != -ring && dx != ring) {
            dx = ring - 1;
            continue;
          }
          i32 sx = o.x + c.x + dx, sy = o.y + c.y + dy, sz = o.z + c.z + dz;
          u32 slot = (u32)((sx & (span - 1)) + (sy & (span - 1)) * span + (sz & (span - 1)) * span * span);
          const u16 *src;
          VxVolSlot want = vx_vol_want(w, sx, sy, sz, &src);
          if (vx_vol_same(&v->slots[level][slot], &want)) continue;
          u32 uv = VX_LIGHT_SKY_FULL;
          b32 uni = !src || vx_vol_uniform(src, &uv);
          u32 *left = uni ? uniform_left : data_left;
          if (*left == 0) continue;
          (*left)--;
          out[n++] = (VxVolUpload){.slot = slot, .uniform_value = uv, .src = uni ? NULL : src, .want = want};
        }
      }
  }
  return n;
}

// writes one level's upload headers and data words; returns the data words used
hz_internal u32 vx_lightvol_pack(VxLightVolume *v, u32 level, const VxVolUpload *ups, u32 n, u32 *hdr, u32 *data,
                                 u32 words) {
  u32 span = level ? VX_VOL_SPAN1 : VX_VOL_SPAN0;
  u32 texels = level ? 8 : 16;
  for (u32 i = 0; i < n; i++) {
    const VxVolUpload *u = &ups[i];
    u32 tx = (u->slot % span) * texels, ty = (u->slot / span % span) * texels, tz = (u->slot / (span * span)) * texels;
    u32 *h = hdr + i * 4;
    h[0] = tx | (ty << 8) | (tz << 16);
    h[1] = u->src ? words : 0;
    h[2] = u->src ? 0 : u->uniform_value | 0x80000000u;
    h[3] = 0;
    if (u->src) {
      mem_cpy(data + words, u->src, VX_SECTION_VOLUME * sizeof(u16));
      words += VX_VOL_SECTION_WORDS;
    }
    if (v->slots[level][u->slot].uid == UINT32_MAX) v->filled[level]++;
    v->slots[level][u->slot] = u->want;
  }
  return words;
}

// re-uploads the window sections whose light changed or that scrolled in, within a per-frame budget
hz_internal void vx_lightvol_update(VxRenderer *r, const VxWorld *w, VxV3d cam, Allocator *ta) {
  VxLightVolume *v = &r->vol;
  iv3 cs = {vx_floor_i(cam.x) >> 4, vx_floor_i(cam.y) >> 4, vx_floor_i(cam.z) >> 4};
  i32 oy = MIN(MAX(cs.y - VX_VOL_SPAN0 / 2, 0), VX_SECTIONS - VX_VOL_SPAN0);
  v->origin[0] = (iv3){cs.x - VX_VOL_SPAN0 / 2, oy, cs.z - VX_VOL_SPAN0 / 2};
  v->origin[1] = (iv3){cs.x - VX_VOL_SPAN1 / 2, 0, cs.z - VX_VOL_SPAN1 / 2};
  cs.y = MIN(MAX(cs.y, 0), VX_SECTIONS - 1);
  v->has_origin = true;
  u32 data_left = VX_VOL_DATA_BUDGET, uniform_left = VX_VOL_UNIFORM_BUDGET;
  VxVolUpload *ups0 = ALLOC_ARRAY_NO_ZERO(ta, VxVolUpload, VX_VOL_SLOTS0);
  VxVolUpload *ups1 = ALLOC_ARRAY_NO_ZERO(ta, VxVolUpload, VX_VOL_SLOTS1);
  u32 n0 = vx_lightvol_collect(v, w, 0, cs, ups0, &data_left, &uniform_left);
  u32 n1 = vx_lightvol_collect(v, w, 1, cs, ups1, &data_left, &uniform_left);
  u32 data_sections = VX_VOL_DATA_BUDGET - data_left;
  v->uploaded_sections = n0 + n1;
  v->uploaded_uniform = VX_VOL_UNIFORM_BUDGET - uniform_left;
  if (n0 + n1 == 0) return;
  u32 *hdr = ALLOC_ARRAY_NO_ZERO(ta, u32, (n0 + n1) * 4);
  u32 *data = ALLOC_ARRAY_NO_ZERO(ta, u32, MAX(data_sections, 1) * VX_VOL_SECTION_WORDS);
  u32 words = vx_lightvol_pack(v, 0, ups0, n0, hdr, data, 0);
  words = vx_lightvol_pack(v, 1, ups1, n1, hdr + n0 * 4, data, words);
  gpu_update_buffer(v->headers, hdr, (n0 + n1) * 4 * sizeof(u32));
  if (words) gpu_update_buffer(v->data, data, words * sizeof(u32));
  gpu_apply_compute_pipeline(v->upload_cs);
  for (u32 l = 0; l < 2; l++) {
    u32 n = l ? n1 : n0;
    if (!n) continue;
    shader_vx_light_upload_apply_params(
        &(ShaderVxLightUploadUploadParams){.count = n, .mode = l, .first = l ? n0 : 0});
    GpuComputeBindings b = shader_vx_light_upload_bindings(
        (ShaderVxLightUploadBindings){.headers = v->headers, .data = v->data, .dst = v->tex[l]});
    gpu_apply_compute_bindings(&b);
    gpu_dispatch(n, 1, 1);
  }
  gpu_texture_access(v->tex[0], GPU_ACCESS_SHADER_READ);
  gpu_texture_access(v->tex[1], GPU_ACCESS_SHADER_READ);
}

// the held-light window: uploaded whole when the game's flood fill changed (zeros once, so it is never undefined)
hz_internal void vx_lightvol_dyn(VxRenderer *r, const VxFrameInput *in, Allocator *ta) {
  VxLightVolume *v = &r->vol;
  v->dyn_on = in->dyn_light != NULL;
  u32 bytes = VX_DYN_SIZE * VX_DYN_SIZE * VX_DYN_SIZE * sizeof(u32);
  const void *src = NULL;
  if (v->dyn_on && in->dyn_version != v->dyn_version) src = in->dyn_light;
  else if (!v->dyn_primed) src = ALLOC_ARRAY(ta, u8, bytes);
  if (!src) return;
  if (v->dyn_on) {
    v->dyn_version = in->dyn_version;
    v->dyn_origin = in->dyn_origin;
  }
  v->dyn_primed = true;
  gpu_update_buffer(v->dyn_data, (void *)src, bytes);
  gpu_apply_compute_pipeline(v->upload_cs);
  shader_vx_light_upload_apply_params(&(ShaderVxLightUploadUploadParams){.count = 1, .mode = 2, .first = 0});
  // mode 2 reads only the data words; the same buffer stands in for the headers
  GpuComputeBindings b = shader_vx_light_upload_bindings(
      (ShaderVxLightUploadBindings){.headers = v->dyn_data, .data = v->dyn_data, .dst = v->dyn});
  gpu_apply_compute_bindings(&b);
  gpu_dispatch(1, 1, 1);
  gpu_texture_access(v->dyn, GPU_ACCESS_SHADER_READ);
}

void vx_render_init(VxRenderer *r, Allocator *alloc, const VxTextureData *tex, u32 out_w, u32 out_h, f32 dpr) {
  r->alloc = *alloc;
  r->timer_open = UINT32_MAX;
  // targets exist from init so wasm has realised them by the first frame
  f32 scale = vx_output_scale(&r->settings, dpr);
  u32 w = vx_scaled(out_w, scale), h = vx_scaled(out_h, scale), uw, uh;
  vx_upscale_size(&r->settings, out_w, out_h, dpr, w, h, &uw, &uh);
  vx_create_targets(r, w, h, uw, uh);
  vx_ensure_shadow(r);

  GpuTextureMipsDesc blk = {.format = GPU_TEXTURE_FORMAT_RGBA8_SRGB, .width = VX_TEX_SIZE, .height = VX_TEX_SIZE,
                            .layer_count = VX_TEX_LAYERS, .mip_count = VX_TEX_MIPS, .data = tex->albedo,
                            .data_size = tex->albedo_size, .label = "vx tile albedo"};
  r->blk_albedo = gpu_make_texture_mips(&blk);
  blk.format = GPU_TEXTURE_FORMAT_RG8;
  blk.data = tex->emissive;
  blk.data_size = tex->emissive_size;
  blk.label = "vx tile emission";
  r->blk_emissive = gpu_make_texture_mips(&blk);
  {
    TempAllocator it = tctx_temp_allocator_begin(NULL);
    GpuTextureMipsDesc items = {.format = GPU_TEXTURE_FORMAT_RGBA8_SRGB, .width = VX_ITEM_TEX_SIZE,
                                .height = VX_ITEM_TEX_SIZE, .layer_count = VX_ITEM_ART_LAYERS,
                                .mip_count = VX_ITEM_TEX_MIPS, .label = "vx item art"};
    items.data_size = gpu_texture_mips_data_size(&items);
    u8 *data = ALLOC_ARRAY(&it.allocator, u8, items.data_size);
    u8 *art = ALLOC_ARRAY(&it.allocator, u8, VX_ITEM_TEX_SIZE * VX_ITEM_TEX_SIZE * 4);
    for (u32 l = 0; l < VX_ITEM_ART_LAYERS; l++) {
      vx_item_art_pixels((VxItemId)(B_COUNT + l), art);
      vx_sprite_mips(art, data, VX_ITEM_ART_LAYERS, l);
    }
    items.data = data;
    r->item_tex = gpu_make_texture_mips(&items);
    tctx_temp_allocator_end(it);
  }

  r->cut_tex = gpu_make_texture(&(GpuTextureDesc){.data_type = GPU_TEXTURE_DATA_RGBA8, .width = VX_CUT_W, .height = VX_HEIGHT});
  r->weather = gpu_make_storage_texture_mip(VX_WEATHER_SIZE, VX_WEATHER_SIZE, GPU_TEXTURE_FORMAT_RGBA8,
                                            vx_mip_count(VX_WEATHER_SIZE));
  vx_lightvol_init(&r->vol, alloc);
  r->exposure_rt[0] = vx_rt("vx exposure 0", 1, 1, GPU_TEXTURE_FORMAT_RGBA32F, false);
  r->exposure_rt[1] = vx_rt("vx exposure 1", 1, 1, GPU_TEXTURE_FORMAT_RGBA32F, false);

  // static quad index pattern: every column part draws a contiguous quad range from it
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 *idx = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, u32, VX_MAX_DRAW_QUADS * 6);
  for (u32 q = 0; q < VX_MAX_DRAW_QUADS; q++) {
    u32 v = q * 4, o = q * 6;
    idx[o] = v;
    idx[o + 1] = v + 1;
    idx[o + 2] = v + 2;
    idx[o + 3] = v;
    idx[o + 4] = v + 2;
    idx[o + 5] = v + 3;
  }
  r->quad_ib = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_INDEX, .usage = GPU_BUFFER_USAGE_STATIC,
                                                .size = VX_MAX_DRAW_QUADS * 6 * sizeof(u32), .data = idx,
                                                .label = "vx quad indices"});
  r->quad_ib_quads = VX_MAX_DRAW_QUADS;
  u32 ent_quads = VX_ENTITY_MAX_VERTS / 4;
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
                                               .label = "vx entity indices"});
  tctx_temp_allocator_end(tmp);
  r->ent_vb = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_VERTEX, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                               .size = VX_ENTITY_MAX_VERTS * VX_ENTITY_STRIDE * sizeof(f32),
                                               .label = "vx entity vertices"});
  r->part_buf = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                 .size = VX_PARTICLE_CAP * sizeof(VxParticleGpu),
                                                 .label = "vx particles"});
  r->box_buf = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                .size = VX_BOX_CAP * sizeof(VxBoxGpu), .label = "vx boxes"});
  r->jelly_buf = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                  .size = VX_JELLY_CAP * sizeof(VxBoxGpu), .label = "vx jelly"});
  r->vfx_buf = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                .size = VX_PARTICLE_CAP * sizeof(VxVfxGpu), .label = "vx vfx"});
  r->near_mask = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                  .size = VX_FAR_MASK_U32 * sizeof(u32), .stride = sizeof(u32),
                                                  .label = "vx near mask"});
  r->draw_record_cap = VX_MAX_CHUNKS * 2;
  r->draw_records = gpu_make_buffer(&(GpuBufferDesc){.type = GPU_BUFFER_STRUCTURED, .usage = GPU_BUFFER_USAGE_CPU_WRITE,
                                                     .size = VX_MAX_CHUNKS * 2 * sizeof(VxDrawRecord),
                                                     .stride = sizeof(VxDrawRecord), .label = "vx draw records"});
  r->reset_history = true;
}

void vx_render_invalidate_history(VxRenderer *r) { r->reset_history = true; }

// ---- culling ----

// planes (inside: dot >= 0) of a reverse-z clip matrix; degenerate planes (infinite far) are dropped
hz_internal VxFrustum vx_frustum(m4 m, Allocator *ta) {
  VxFrustum f = {ALLOC_ARRAY(ta, v4, VX_PLANES), 0};
  for (u32 k = 0; k < VX_PLANES; k++) {
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
hz_internal u32 vx_box_classify(const VxFrustum *f, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
  u32 r = 2;
  for (u32 k = 0; k < f->n; k++) {
    v4 p = f->p[k];
    if (p.x * (p.x >= 0 ? x1 : x0) + p.y * (p.y >= 0 ? y1 : y0) + p.z * (p.z >= 0 ? z1 : z0) + p.w < 0) return 0;
    if (p.x * (p.x >= 0 ? x0 : x1) + p.y * (p.y >= 0 ? y0 : y1) + p.z * (p.z >= 0 ? z0 : z1) + p.w < 0) r = 1;
  }
  return r;
}

hz_internal b32 vx_box_visible(const VxFrustum *f, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
  for (u32 k = 0; k < f->n; k++) {
    v4 p = f->p[k];
    if (p.x * (p.x >= 0 ? x1 : x0) + p.y * (p.y >= 0 ? y1 : y0) + p.z * (p.z >= 0 ? z1 : z0) + p.w < 0) return false;
  }
  return true;
}


// one frustum's columns: visible section range of every column's part, sorted by pool then front to back
typedef struct {
  u32 part;
  const VxFrustum *fr;
  b32 buried;
  f32 max_z; // camera-relative: columns starting past it are cut away (camera jobs in the cutaway)
  u32 *sections;
  VxDraw *tmp;
  u64 *keys;
  u32 count;
  VxDrawList out;
} VxCullJob;

// lsd radix sort of the jobs' keys on their top 32 bits (4 x 8), then the columns in key order
hz_internal void vx_cull_sort(VxCullJob *j, u32 n, Allocator *ta) {
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
  j->out = (VxDrawList){ALLOC_ARRAY_NO_ZERO(ta, VxDraw, j->count ? j->count : 1), j->count};
  for (u32 i = 0; i < j->count; i++) j->out.items[i] = j->tmp[a[i] & 0xffffffffu];
}

// every job in one walk over the renderables, which also writes each meshed part's draw record; returns the count
hz_internal u32 vx_cull(const VxWorld *w, VxDrawRecord *records, VxCullJob *jobs, u32 njobs, VxV3d cam, Allocator *ta) {
  u32 rec_count = 0;
  u32 n = w->renderable_count;
  for (u32 k = 0; k < njobs; k++) {
    jobs[k].tmp = ALLOC_ARRAY_NO_ZERO(ta, VxDraw, n ? n : 1);
    jobs[k].keys = ALLOC_ARRAY_NO_ZERO(ta, u64, n ? n * 2 : 2);
    jobs[k].count = 0;
  }
  const f32 m = 0.5f;
  for (u32 i = 0; i < n; i++) {
    const VxChunk *c = &w->chunks[w->renderables[i]];
    VxDrawRecord dr = {.origin = {(f32)(c->cx * 16 - cam.x), (f32)(-cam.y), (f32)(c->cz * 16 - cam.z)}};
    u32 rec0 = UINT32_MAX, rec1 = UINT32_MAX;
    if (c->parts[0].pool != VX_NO_POOL) {
      rec0 = rec_count;
      records[rec_count++] = dr;
    }
    if (c->parts[1].pool != VX_NO_POOL) {
      rec1 = rec_count;
      records[rec_count++] = dr;
    }
    f32 x0 = (f32)(c->cx * 16 - cam.x) - m, z0 = (f32)(c->cz * 16 - cam.z) - m;
    f32 x1 = x0 + 16 + 2 * m, z1 = z0 + 16 + 2 * m;
    for (u32 k = 0; k < njobs; k++) {
      VxCullJob *j = &jobs[k];
      u32 rec = j->part ? rec1 : rec0;
      if (rec == UINT32_MAX || z0 + m >= j->max_z) continue;
      const VxMeshPart *p = &c->parts[j->part];
      i32 s0 = 0;
      if (j->buried && cam.y > c->surface_min - 8) {
        i32 b = (i32)m_ceilf((c->surface_min - 22) / 16.0f);
        s0 = b > 0 ? b : 0;
      }
      i32 lo = s0, hi = VX_SECTIONS - 1;
      while (lo <= hi && p->start[lo + 1] == p->start[lo]) lo++;
      while (hi >= lo && p->start[hi + 1] == p->start[hi]) hi--;
      if (lo > hi) continue;
      u32 cls = vx_box_classify(j->fr, x0, (f32)(lo * 16 - cam.y) - m, z0, x1, (f32)(hi * 16 + 16 - cam.y) + m, z1);
      if (cls == 0) continue;
      i32 first = lo, last = hi;
      if (cls == 1) {
        first = -1;
        for (i32 sy = lo; sy <= hi; sy++) {
          if (p->start[sy + 1] == p->start[sy]) continue;
          f32 y0 = (f32)(sy * 16 - cam.y) - m;
          if (!vx_box_visible(j->fr, x0, y0, z0, x1, y0 + 16 + 2 * m, z1)) continue;
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
          (VxDraw){.part = p, .cx = c->cx, .cz = c->cz, .record = rec, .first = (u32)first, .last = (u32)last};
      u64 key = ((u64)p->pool << 20) | (u64)m_minf(dist * 16.0f, 1048575.0f);
      j->keys[j->count] = (key << 32) | j->count;
      j->count++;
    }
  }
  for (u32 k = 0; k < njobs; k++) vx_cull_sort(&jobs[k], n, ta);
  return rec_count;
}

// ---- face buckets and gpu expansion ----

typedef enum { VX_FACES_CAMERA, VX_FACES_ALL, VX_FACES_LIGHT } VxFaceTest;

// mirrors _Ranges in shaders/vx_cull_expand.glsl
typedef struct {
  u32 src, count, record, dst;
} VxRange;

typedef struct {
  VxRange *items;
  u32 count, cap;
  u32 entries;
} VxRanges;

// keep masks over the buckets: bit d (+X, -X, +Y, -Y, +Z, -Z) for plain faces, bit 6 + d for flagged, bit 12 the rest
#define VX_KEEP_ALL 0x1fffu

// directions of an axis that can face a camera at section offset lo (spans lo..lo+16); waving faces keep a margin
force_inline u32 vx_keep_axis(f32 lo, u32 axis) {
  u32 k = (lo + 1 < 0 ? 1u : 0u) | (lo + 15 > 0 ? 2u : 0u);
  u32 kf = (lo + 1 < 0.5f ? 1u : 0u) | (lo + 15 > -0.5f ? 2u : 0u);
  return (k << (axis * 2)) | (kf << (6 + axis * 2));
}

// solids occlude through their far faces alone: plain cube faces towards the light are dropped
hz_internal u32 vx_keep_light(v3 L) {
  u32 k = VX_KEEP_ALL;
  if (L.x > 0) k &= ~1u;
  if (L.x < 0) k &= ~2u;
  if (L.y > 0) k &= ~4u;
  if (L.y < 0) k &= ~8u;
  if (L.z > 0) k &= ~16u;
  if (L.z < 0) k &= ~32u;
  return k;
}

hz_internal u32 vx_ranges_bound(const VxDrawList *list) {
  u32 n = 0;
  for (u32 i = 0; i < list->count; i++) n += (list->items[i].last - list->items[i].first + 1) * VX_BUCKETS;
  return n;
}

// appends the kept buckets of every listed column as quad ranges; contiguous quads of a column merge
hz_internal VxBatchList vx_emit_ranges(VxRanges *rs, const VxDrawList *list, VxFaceTest t, VxV3d cam, v3 L,
                                       Allocator *ta) {
  VxBatchList out = {ALLOC_ARRAY_NO_ZERO(ta, VxBatch, list->count ? list->count : 1), 0};
  u32 light_keep = t == VX_FACES_LIGHT ? vx_keep_light(L) : VX_KEEP_ALL;
  for (u32 i = 0; i < list->count; i++) {
    const VxDraw *d = &list->items[i];
    const VxMeshPart *p = d->part;
    u32 before = rs->entries;
    if (t == VX_FACES_ALL) {
      u32 n = p->start[d->last + 1] - p->start[d->first];
      rs->items[rs->count++] =
          (VxRange){.src = p->base + p->start[d->first], .count = n, .record = d->record, .dst = rs->entries};
      rs->entries += n;
    } else {
      u32 col_keep = t == VX_FACES_LIGHT ? light_keep
                                          : vx_keep_axis((f32)(d->cx * 16 - cam.x), 0) |
                                                vx_keep_axis((f32)(d->cz * 16 - cam.z), 2) | (1u << VX_BUCKET_OTHER);
      u32 first_range = rs->count;
      for (u32 s = d->first; s <= d->last; s++) {
        const u16 *m = p->meta + s * VX_SECTION_META;
        u32 keep = t == VX_FACES_LIGHT ? col_keep : col_keep | vx_keep_axis((f32)(s * 16.0 - cam.y), 1);
        u32 bits = m[VX_META_MASK] & keep;
        u32 sec_q = p->base + p->start[s];
        while (bits) {
          u32 b = (u32)hz_ctzll(bits);
          bits &= bits - 1;
          u32 q = sec_q + m[b], n = m[b + 1] - m[b];
          VxRange *last = rs->count > first_range ? &rs->items[rs->count - 1] : NULL;
          if (last && last->src + last->count == q) last->count += n;
          else rs->items[rs->count++] = (VxRange){.src = q, .count = n, .record = d->record, .dst = rs->entries};
          rs->entries += n;
        }
      }
    }
    u32 added = rs->entries - before;
    if (!added) continue;
    VxBatch *b = out.count ? &out.items[out.count - 1] : NULL;
    if (b && b->pool == p->pool && b->first + b->count == before) b->count += added;
    else out.items[out.count++] = (VxBatch){.pool = p->pool, .first = before, .count = added};
  }
  return out;
}

// ---- cascades ----

// camera-centred and world-anchored, so turning never invalidates a cascade: the lighting picks cascades by distance
hz_internal void vx_cascade_fit(VxCascadeCache *cc, VxV3d cam, v3 L, f32 half, u32 res) {
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
  f32 depth = 2 * half + VX_SHADOW_EXTRA;
  m4 view = m4_lookat(v3_scale(L, half + VX_SHADOW_EXTRA), (v3){0, 0, 0}, yx);
  cc->vp = m4_mul(m4_ortho(-half, half, -half, half, 0, depth), view);
  cc->L = L;
  cc->half = half;
  cc->texel = texel;
  cc->depth = depth;
  cc->valid = true;
}

// a mesh change inside the box the cascade was rendered over (its footprint stretches toward the light)
hz_internal b32 vx_cascade_touched(const VxCascadeCache *cc, const VxWorld *w) {
  if (w->mesh_changes_overflow) return true;
  f32 lxz = m_sqrtf(cc->L.x * cc->L.x + cc->L.z * cc->L.z);
  f64 reach = cc->half * 1.4143 + (cc->half + VX_SHADOW_EXTRA) * lxz + 12.0;
  for (u32 i = 0; i < w->mesh_change_count; i++) {
    f64 dx = w->mesh_changes[i * 2] * 16.0 + 8.0 - cc->cx, dz = w->mesh_changes[i * 2 + 1] * 16.0 + 8.0 - cc->cz;
    if (dx * dx + dz * dz <= reach * reach) return true;
  }
  return false;
}

// ---- passes ----

void vx_render_timer_begin(VxRenderer *r, VxGpuGroup g) {
  if (!r->gpu_timing || r->timer_used >= VX_GPU_TIMERS) return;
  u32 i = r->timer_used++;
  if (i >= r->timer_count) {
    r->timers[i] = gpu_create_timer("vx pass");
    r->timer_count = i + 1;
  }
  r->timer_group[i] = (u8)g;
  gpu_timer_begin(r->timers[i]);
  r->timer_open = i;
}

void vx_render_timer_end(VxRenderer *r) {
  if (r->timer_open == UINT32_MAX) return;
  gpu_timer_end(r->timers[r->timer_open]);
  r->timer_open = UINT32_MAX;
}

b32 vx_render_gpu_times(VxRenderer *r, f32 *group_ms, f32 *frame_ms) {
  for (u32 g = 0; g < VX_GPU_GROUP_COUNT; g++) group_ms[g] = 0;
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

hz_internal void vx_pass(VxRenderer *r, VxGpuGroup g, GpuPassDesc desc) {
  vx_render_timer_begin(r, g);
  gpu_begin_pass(&desc);
}

hz_internal void vx_begin(VxRenderer *r, VxGpuGroup g, GpuRenderTarget rt) {
  vx_pass(r, g, (GpuPassDesc){.render_target = rt});
}

hz_internal void vx_end(VxRenderer *r) {
  gpu_end_pass();
  vx_render_timer_end(r);
}

hz_internal void vx_frame_uniforms(const VxRenderer *r) {
  gpu_apply_uniforms(0, (void *)&r->frame, sizeof(VxFrameUniforms));
}

force_inline GpuTexture vx_color(GpuRenderTarget rt, u32 i) { return gpu_get_render_target_attachment(rt, i); }
force_inline GpuTexture vx_depth(GpuRenderTarget rt) { return gpu_get_render_target_depth(rt); }

typedef enum { VX_TERRAIN_GBUFFER, VX_TERRAIN_SHADOW, VX_TERRAIN_WATER } VxTerrainPass;

// one draw per pool batch; the first instance carries the batch's first visible entry
hz_internal u32 vx_draw_terrain(VxRenderer *r, const VxWorld *w, const VxBatchList *list, VxTerrainPass pass) {
  const VxMeshStore *ms = w->meshes;
  u32 draws = 0;
  for (u32 i = 0; i < list->count; i++) {
    const VxBatch *b = &list->items[i];
    GpuBuffer pool = ms->pools[b->pool].buf;
    if (pass == VX_TERRAIN_GBUFFER) {
      shader_vx_terrain_apply_bindings((ShaderVxTerrainBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .albedo_tex = r->blk_albedo, .emissive_tex = r->blk_emissive});
    } else if (pass == VX_TERRAIN_SHADOW) {
      shader_vx_shadow_apply_bindings((ShaderVxShadowBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .albedo_tex = r->blk_albedo});
    } else {
      shader_vx_water_apply_bindings((ShaderVxWaterBindings){
          .index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32, .quads = pool, .draws = r->draw_records,
          .visible = r->visible, .scene_color_tex = vx_color(r->scene_copy, 0),
          .scene_depth_tex = vx_depth(r->depth_copy), .vol0_tex = r->vol.tex[0], .vol1_tex = r->vol.tex[1],
          .dyn_tex = r->vol.dyn, .shadow_cmp_tex = vx_depth(r->shadow_atlas)});
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

hz_internal void vx_fullscreen_draw(void) { gpu_draw(3, 1); }

// the far terrain's regions in view that still have a chunk no chunk mesh covers; returns the draws
hz_internal u32 vx_draw_far(VxRenderer *r, const VxWorld *w, const VxFrustum *fr, const u32 *mask, VxV3d cam) {
  const VxFar *f = &w->far_field;
  u32 draws = 0;
  i32 ox = (i32)mask[0], oz = (i32)mask[1];
  for (u32 i = 0; i < VX_FAR_REGION_COUNT; i++) {
    const VxMeshPart *p = &f->regions[i];
    u32 n = p->start[VX_SECTIONS];
    if (p->pool == VX_NO_POOL || n == 0) continue;
    i32 cx0 = (i32)(i % VX_FAR_REGIONS) * VX_FAR_REGION + VX_FAR_C0, cz0 = (i32)(i / VX_FAR_REGIONS) * VX_FAR_REGION + VX_FAR_C0;
    b32 open = false;
    for (i32 z = cz0; z < cz0 + VX_FAR_REGION && !open; z++)
      for (i32 x = cx0; x < cx0 + VX_FAR_REGION && !open; x++) {
        i32 mx = x - ox, mz = z - oz;
        if (mx < 0 || mz < 0 || mx >= VX_FAR_MASK_SIDE || mz >= VX_FAR_MASK_SIDE) {
          open = true;
          break;
        }
        u32 bit = (u32)(mz * VX_FAR_MASK_SIDE + mx);
        open = !(mask[2 + (bit >> 5)] & (1u << (bit & 31)));
      }
    if (!open) continue;
    f32 x0 = (f32)(cx0 * 16 - cam.x), z0 = (f32)(cz0 * 16 - cam.z);
    f32 ext = (f32)(VX_FAR_REGION * 16);
    if (!vx_box_visible(fr, x0, (f32)(f->region_lo[i] - cam.y), z0, x0 + ext, (f32)(f->region_hi[i] + 1 - cam.y), z0 + ext))
      continue;
    shader_vx_far_apply_bindings((ShaderVxFarBindings){.index_buffer = r->quad_ib, .index_format = GPU_INDEX_FORMAT_U32,
                                                       .quads = w->meshes->pools[p->pool].buf, .cover = r->near_mask,
                                                       .albedo_tex = r->blk_albedo, .emissive_tex = r->blk_emissive});
    for (u32 done = 0; done < n; done += r->quad_ib_quads) {
      u32 k = n - done < r->quad_ib_quads ? n - done : r->quad_ib_quads;
      u32 first = p->base + done;
#ifndef WASM
      gpu_set_push_constants((void *)&first, sizeof(u32));
#endif
      gpu_draw_indexed(k * 6, 1, 0, first);
      draws++;
    }
  }
  return draws;
}


// ---- frame ----

hz_internal f32 vx_halton(u32 i, u32 b) {
  f32 f = 1, r = 0;
  while (i > 0) {
    f /= (f32)b;
    r += f * (f32)(i % b);
    i /= b;
  }
  return r;
}

force_inline f32 vx_smooth(f32 e0, f32 e1, f32 x) {
  f32 t = m_clampf((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}

force_inline v4 vx_v4(v3 v, f32 w) { return (v4){v.x, v.y, v.z, w}; }

hz_internal void vx_clear_rt(GpuRenderTarget rt, ColorF32 c) {
  gpu_begin_pass(&(GpuPassDesc){.render_target = rt, .clear_color = c});
  gpu_end_pass();
}

// new targets hold undefined memory: give every sampled target defined contents once
hz_internal void vx_prime_targets(VxRenderer *r) {
  if (!r->needs_prime) return;
  ColorF32 zero = {0, 0, 0, 0};
  vx_clear_rt(r->gbuf, zero);
  vx_clear_rt(r->scene, zero);
  vx_clear_rt(r->scene_copy, zero);
  vx_clear_rt(r->hdr_b, zero);
  vx_clear_rt(r->ssao_rt, (ColorF32){1, 1, 1, 1});
  vx_clear_rt(r->history[0], zero);
  vx_clear_rt(r->history[1], zero);
  vx_clear_rt(r->cloud_cur, zero);
  vx_clear_rt(r->cloud_hist[0], (ColorF32){0, 0, 0, 1});
  vx_clear_rt(r->cloud_hist[1], (ColorF32){0, 0, 0, 1});
  for (u32 i = 0; i < VX_BLOOM_LEVELS; i++) vx_clear_rt(r->bloom[i], zero);
  r->needs_prime = false;
  // clearing these on a resize would wipe the adapted exposure
  if (r->fixed_primed) return;
  vx_clear_rt(r->exposure_rt[0], zero);
  vx_clear_rt(r->exposure_rt[1], zero);
  r->fixed_primed = true;
}

typedef enum {
  VX_EFFECT_SSAO = 1u << 0,
  VX_EFFECT_CLOUDS = 1u << 1,
  VX_EFFECT_TAA = 1u << 2,
} VxEffect;

hz_internal b32 vx_noise_pipelines_ready(VxRenderer *r) {
  return gpu_compute_pipeline_is_ready(r->noise_weather_cs) && gpu_compute_pipeline_is_ready(r->noise_down2d_cs);
}

// effects whose pipelines are compiled; the rest render as if their setting were off
hz_internal u32 vx_effects_ready(VxRenderer *r) {
  u32 m = 0;
  if (gpu_pipeline_is_ready(r->ssao)) m |= VX_EFFECT_SSAO;
  if (r->noise_baked && gpu_pipeline_is_ready(r->clouds) && gpu_pipeline_is_ready(r->clouds_temporal))
    m |= VX_EFFECT_CLOUDS;
  if (gpu_pipeline_is_ready(r->taa)) m |= VX_EFFECT_TAA;
  return m;
}

b32 vx_render_ready(VxRenderer *r) {
  if (!r->core_ready)
    r->core_ready =
        gpu_pipeline_is_ready(r->terrain) && gpu_pipeline_is_ready(r->shadow) && gpu_pipeline_is_ready(r->far_terrain) &&
        gpu_pipeline_is_ready(r->ocean) &&
        gpu_pipeline_is_ready(r->shadow_clear) && gpu_pipeline_is_ready(r->deferred) &&
        gpu_pipeline_is_ready(r->water) && gpu_pipeline_is_ready(r->entity) &&
        gpu_pipeline_is_ready(r->entity_hand) && gpu_pipeline_is_ready(r->entity_shadow) &&
        gpu_pipeline_is_ready(r->particle) && gpu_pipeline_is_ready(r->shafts) &&
        gpu_pipeline_is_ready(r->box) && gpu_pipeline_is_ready(r->box_shadow) && gpu_pipeline_is_ready(r->jelly) && gpu_pipeline_is_ready(r->vfx) &&
        gpu_compute_pipeline_is_ready(r->cull_expand_cs) && gpu_compute_pipeline_is_ready(r->vol.upload_cs) &&
        gpu_pipeline_is_ready(r->atmospherics) && gpu_pipeline_is_ready(r->bloom_down) &&
        gpu_pipeline_is_ready(r->bloom_up) && gpu_pipeline_is_ready(r->exposure) && gpu_pipeline_is_ready(r->final);
  if (r->core_ready && !r->pipelines_ready)
    r->pipelines_ready = vx_noise_pipelines_ready(r) &&
                         vx_effects_ready(r) == (VX_EFFECT_SSAO | VX_EFFECT_CLOUDS | VX_EFFECT_TAA);
  return r->core_ready;
}

void vx_render_frame(VxRenderer *r, const VxWorld *world, const VxFrameInput *in, const VxEntityGeometry *ents,
                     u32 out_w, u32 out_h) {
  const VxRenderSettings *s = &r->settings;
  f32 scale = vx_output_scale(s, in->dpr);
  u32 w = vx_scaled(out_w, scale);
  u32 h = vx_scaled(out_h, scale);
  u32 uw, uh;
  vx_upscale_size(s, out_w, out_h, in->dpr, w, h, &uw, &uh);
  vx_ensure_targets(r, w, h, uw, uh);
  vx_ensure_shadow(r);
  vx_prime_targets(r);
  r->stats = (VxRenderStats){0};
  r->timer_used = 0;
  if (!vx_render_ready(r)) {
    gpu_begin_pass(&(GpuPassDesc){.clear_color = {0, 0, 0, 1}});
    gpu_end_pass();
    return;
  }
  if (!r->noise_baked && vx_noise_pipelines_ready(r)) vx_bake_noise(r);
  // effects still compiling render as switched off; one switching on restarts the histories it feeds
  VxRenderSettings eff = r->settings;
  u32 effects = r->pipelines_ready ? (VX_EFFECT_SSAO | VX_EFFECT_CLOUDS | VX_EFFECT_TAA) : vx_effects_ready(r);
  if (!(effects & VX_EFFECT_SSAO)) eff.ssao = false;
  if (!(effects & VX_EFFECT_CLOUDS)) eff.clouds = 0;
  if (!(effects & VX_EFFECT_TAA)) eff.taa = false;
  if (effects & ~r->effects_on) r->reset_history = true;
  r->effects_on = effects;
  s = &eff;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  Allocator *ta = &tmp.allocator;

  // ---- camera ----
  VxV3d cam = in->cam;
  f32 cp = m_cosf(in->pitch);
  v3 fwd = {-m_sinf(in->yaw) * cp, m_sinf(in->pitch), -m_cosf(in->yaw) * cp};
  m4 view = m4_look((v3){0, 0, 0}, fwd, (v3){0, 1, 0});
  f32 fov_y = in->fov * PI / 180.0f;
  f32 aspect = (f32)w / (f32)h;
  f32 far_d = m_maxf(420.0f, s->render_distance * 16 * 1.6f + 96);
  m4 proj_nj = m4_perspective_inf(fov_y, aspect, VX_NEAR);
  m4 proj = proj_nj;
  f32 jx = 0, jy = 0;
  if (s->taa) {
    u32 hi = r->frame_index % 8 + 1;
    jx = (vx_halton(hi, 2) - 0.5f) * 2.0f / w;
    jy = (vx_halton(hi, 3) - 0.5f) * 2.0f / h;
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

  // ---- sun / moon and the frame's look ----
  v3 sun, moon;
  m4 star_rot;
  vx_celestial(in->day_time, &sun, &moon, &star_rot);
  // quantized light direction keeps shadow texels stable
  v3 qsun, qmoon;
  m4 qstar;
  vx_celestial(m_floorf(in->day_time * 8192.0f + 0.5f) / 8192.0f, &qsun, &qmoon, &qstar);
  b32 sun_up = sun.y > -0.035f;
  v3 L = sun_up ? qsun : qmoon;
  // the cached cascades step the light 8x less often: each step re-renders them, and their texels hide 1/1024 of a day
  v3 fsun, fmoon;
  m4 fstar;
  vx_celestial(m_floorf(in->day_time * 1024.0f + 0.5f) / 1024.0f, &fsun, &fmoon, &fstar);
  v3 L_far = sun_up ? fsun : fmoon;
  // fade the shadow-casting light at the sun/moon switch so there is no pop
  f32 light_fade = sun_up ? vx_smooth(-0.035f, 0.03f, sun.y) : vx_smooth(0.035f, 0.1f, moon.y);
  VxLook look = vx_look_compute(in, sun.y);
  f32 overcast = 1.0f - 0.45f * in->rain;
  v3 light_col = v3_scale(look.direct, light_fade * overcast);

  // ---- cascades: the near two carry animated foliage and re-render every frame, the far two are cached ----
  f32 D = s->shadow_distance;
  u32 refresh = 0;
  b32 far_refreshed = false;
  for (u32 c = 0; c < VX_CASCADES; c++) {
    f32 split = D * (c == 0 ? 0.07f : c == 1 ? 0.2f : c == 2 ? 0.46f : 1.0f);
    f32 half = split * (1 + VX_CASCADE_MARGIN);
    VxCascadeCache *cc = &r->cascade_cache[c];
    v3 Lc = c < VX_CASCADE_CACHED ? L : L_far;
    if (r->reset_history) cc->valid = false;
    b32 stale = c < VX_CASCADE_CACHED || cc->half != half || cc->L.x != Lc.x || cc->L.y != Lc.y || cc->L.z != Lc.z;
    if (!stale && cc->valid) {
      f64 dx = cam.x - cc->cx, dy = cam.y - cc->cy, dz = cam.z - cc->cz;
      f64 limit = split * VX_CASCADE_MARGIN;
      stale = dx * dx + dy * dy + dz * dz > limit * limit || vx_cascade_touched(cc, world);
    }
    // an invalid cascade renders now; a valid but stale far one waits if another far one refreshed this frame
    if (!cc->valid || (stale && (c < VX_CASCADE_CACHED || !far_refreshed))) {
      vx_cascade_fit(cc, cam, Lc, half, s->shadow_res);
      refresh |= 1u << c;
      if (c >= VX_CASCADE_CACHED) far_refreshed = true;
    }
    r->cascade_vp[c] = m4_translated(cc->vp, (v3){(f32)(cam.x - cc->cx), (f32)(cam.y - cc->cy), (f32)(cam.z - cc->cz)});
    r->cascade_far[c] = split;
    r->cascade_texel[c] = cc->texel;
    r->cascade_depth[c] = cc->depth;
  }

  // ---- light volume: changed sections and the held-light window ----
  vx_lightvol_update(r, world, cam, ta);
  vx_lightvol_dyn(r, in, ta);
  if (in->cut && in->cut_light && in->cut_version != r->cut_version) {
    r->cut_version = in->cut_version;
    gpu_update_texture_region(r->cut_tex, 0, 0, VX_CUT_W, VX_HEIGHT, (const u8 *)in->cut_light);
  }
  const VxLightVolume *vol = &r->vol;

  // ---- frame uniforms ----
  VxFrameUniforms *F = &r->frame;
  F->view = view;
  F->proj = proj;
  F->view_proj = view_proj;
  F->inv_view_proj = inv_view_proj;
  F->prev_view_proj = r->prev_view_proj;
  F->view_proj_nj = view_proj_nj;
  for (u32 c = 0; c < VX_CASCADES; c++) F->shadow_vp[c] = r->cascade_vp[c];
  F->star_rot = star_rot;
  F->cascade_splits = (v4){r->cascade_far[0], r->cascade_far[1], r->cascade_far[2], r->cascade_far[3]};
  F->cascade_texel = (v4){r->cascade_texel[0], r->cascade_texel[1], r->cascade_texel[2], r->cascade_texel[3]};
  F->cascade_depth = (v4){r->cascade_depth[0], r->cascade_depth[1], r->cascade_depth[2], r->cascade_depth[3]};
  F->cam_pos = (v4){(f32)cam.x, (f32)cam.y, (f32)cam.z, in->time};
  F->cam_delta = (v4){delta.x, delta.y, delta.z, (f32)(r->frame_index % 4096)};
  F->sun_dir = vx_v4(sun, vx_smooth(-0.05f, 0.05f, sun.y));
  F->moon_dir = vx_v4(moon, in->moon_phase);
  F->light_dir = vx_v4(L, sun_up ? 1.0f : 0.0f);
  F->light_color = vx_v4(light_col, light_fade > 0 ? 1.0f : 0.0f);
  F->res = (v4){(f32)w, (f32)h, 1.0f / w, 1.0f / h};
  F->jitter = (v4){jx, jy, 0, 0};
  // the far terrain carries the island and the sea to the horizon: the world's edge is the haze far past it
  F->fog = (v4){look.fog_density, look.fog_falloff, VX_FAR_EDGE, look.under_density};
  F->fog_color = vx_v4(look.fog_color, m_clampf(in->underground, 0, 1));
  F->world = (v4){in->underwater ? 1.0f : 0.0f, in->eye_sky, VX_NEAR, far_d};
  r->cloud_time += in->dt;
  f32 coverage = in->cloud_coverage + (0.96f - in->cloud_coverage) * in->rain;
  F->cloud = (v4){s->clouds > 0 ? coverage : 0, VX_CLOUD_BASE, VX_CLOUD_THICK, r->cloud_time};
  F->settings = (v4){s->shadows ? 1.0f : 0.0f, s->ssao ? 1.0f : 0.0f, (f32)s->shadow_res, s->soft_shadows ? 1.0f : 0.0f};
  F->weather = (v4){in->rain, in->wetness, 1 + in->rain * 1.5f, s->volumetrics ? 1.0f : 0.0f};
  F->sky_zenith = vx_v4(look.zenith, look.night);
  F->sky_horizon = vx_v4(look.horizon, look.stars);
  F->sky_glow = vx_v4(look.glow, look.aurora);
  F->amb_sky = vx_v4(v3_scale(look.amb_sky, overcast), 0);
  F->amb_ground = vx_v4(v3_scale(look.amb_ground, overcast), (f32)s->lighting);
  F->shade = vx_v4(look.shade, 0);
  F->under_color = vx_v4(look.under_color, look.heat);
  F->vol0 = (v4){(f32)(vol->origin[0].x * 16), (f32)(vol->origin[0].y * 16), (f32)(vol->origin[0].z * 16),
                 vol->filled[0] == VX_VOL_SLOTS0 ? 1.0f : 0.0f};
  F->vol1 = (v4){(f32)(vol->origin[1].x * 16), 0, (f32)(vol->origin[1].z * 16),
                 vol->filled[1] == VX_VOL_SLOTS1 ? 1.0f : 0.0f};
  F->dyn = (v4){(f32)vol->dyn_origin.x, (f32)vol->dyn_origin.y, (f32)vol->dyn_origin.z, vol->dyn_on ? 1.0f : 0.0f};
  F->extra = (v4){in->snow, in->snow_cover, in->flash, (f32)r->debug_view};
  F->star_a = vx_v4(in->star_start, in->star_progress);
  F->star_b = vx_v4(in->star_heading, 0);
  F->under_glow = vx_v4(look.under_glow, 0);
  static_assert(VX_BIOME_COUNT == 8, "biome_a/biome_b carry 8 weights");
  F->biome_a = (v4){in->biome_w[0], in->biome_w[1], in->biome_w[2], in->biome_w[3]};
  F->biome_b = (v4){in->biome_w[4], in->biome_w[5], in->biome_w[6], in->biome_w[7]};
  F->cut = (v4){in->cut_plane, in->cut ? 1.0f : 0.0f, (f32)in->cut_x0, 0};

  // short-lived point lights, camera-relative
  VxPointLightUniforms *pl = ALLOC(ta, VxPointLightUniforms);
  u32 np = MIN(in->point_count, VX_MAX_POINT_LIGHTS);
  for (u32 i = 0; i < np; i++) {
    const VxPointLight *p = &in->points[i];
    pl->pos_radius[i] = (v4){(f32)(p->pos.x - cam.x), (f32)(p->pos.y - cam.y), (f32)(p->pos.z - cam.z), p->radius};
    pl->color[i] = vx_v4(p->color, 0);
  }
  pl->count = (v4){(f32)np, 0, 0, 0};

  // ---- draw records and culling ----
  VxDrawRecord *records = ALLOC_ARRAY_NO_ZERO(ta, VxDrawRecord, world->renderable_count * 2 + 1);
  VxFrustum cam_fr = vx_frustum(view_proj_nj, ta);
  u32 *near_mask = ALLOC_ARRAY_NO_ZERO(ta, u32, VX_FAR_MASK_U32);
  vx_world_near_mask(world, vx_floor_i(cam.x) >> 4, vx_floor_i(cam.z) >> 4, near_mask);
  gpu_update_buffer(r->near_mask, near_mask, VX_FAR_MASK_U32 * sizeof(u32));
  // jobs: camera opaque, camera translucent, then each refreshed cascade
  VxCullJob *jobs = ALLOC_ARRAY(ta, VxCullJob, VX_CASCADES + 2);
  f32 cut_z = in->cut ? (f32)(in->cut_plane - in->cam.z) : 1e30f;
  jobs[0] = (VxCullJob){.part = 0, .fr = &cam_fr, .sections = &r->stats.sections, .max_z = cut_z};
  jobs[1] = (VxCullJob){.part = 1, .fr = &cam_fr, .max_z = cut_z};
  u32 njobs = 2;
  u32 *cascade_job = ALLOC_ARRAY(ta, u32, VX_CASCADES);
  if (s->shadows) {
    for (u32 c = 0; c < VX_CASCADES; c++) {
      if (!(refresh & (1u << c))) continue;
      VxFrustum *fr = ALLOC(ta, VxFrustum);
      *fr = vx_frustum(r->cascade_vp[c], ta);
      // cached cascades keep buried casters: the camera may be underground before they refresh again
      cascade_job[c] = njobs;
      jobs[njobs++] = (VxCullJob){.part = 0, .fr = fr, .buried = c >= 1 && c < VX_CASCADE_CACHED, .max_z = 1e30f};
    }
  }
  u32 rec_count = vx_cull(world, records, jobs, njobs, cam, ta);
  if (rec_count) gpu_update_buffer(r->draw_records, records, rec_count * sizeof(VxDrawRecord));
  VxDrawList opaque_cols = jobs[0].out, trans_cols = jobs[1].out;
  VxDrawList *shadow_cols = ALLOC_ARRAY(ta, VxDrawList, VX_CASCADES);
  if (s->shadows)
    for (u32 c = 0; c < VX_CASCADES; c++)
      if (refresh & (1u << c)) shadow_cols[c] = jobs[cascade_job[c]].out;
  // face buckets per pass: the camera's back faces (the translucent pipeline draws both sides) and light-facing solids
  u32 bound = vx_ranges_bound(&opaque_cols) + vx_ranges_bound(&trans_cols);
  for (u32 c = 0; c < VX_CASCADES; c++) bound += vx_ranges_bound(&shadow_cols[c]);
  VxRanges rs = {ALLOC_ARRAY_NO_ZERO(ta, VxRange, bound ? bound : 1), 0, bound, 0};
  v3 Lc = r->cascade_cache[0].L;
  VxBatchList opaque = vx_emit_ranges(&rs, &opaque_cols, VX_FACES_CAMERA, cam, Lc, ta);
  VxBatchList trans = vx_emit_ranges(&rs, &trans_cols, VX_FACES_ALL, cam, Lc, ta);
  r->stats.quads = rs.entries;
  VxBatchList *shadow_lists = ALLOC_ARRAY(ta, VxBatchList, VX_CASCADES);
  for (u32 c = 0; c < VX_CASCADES; c++)
    shadow_lists[c] = vx_emit_ranges(&rs, &shadow_cols[c], VX_FACES_LIGHT, cam, r->cascade_cache[c].L, ta);
  if (rs.count) {
    vx_ensure_buffer(&r->cull_ranges, &r->cull_range_cap, rs.count, sizeof(VxRange), GPU_BUFFER_USAGE_CPU_WRITE,
                     "vx cull ranges");
    vx_ensure_buffer(&r->visible, &r->visible_cap, rs.entries, 2 * sizeof(u32), GPU_BUFFER_USAGE_GPU_WRITE,
                     "vx visible quads");
    gpu_update_buffer(r->cull_ranges, rs.items, rs.count * sizeof(VxRange));
    u32 gx = rs.count < 65535 ? rs.count : 65535;
    gpu_apply_compute_pipeline(r->cull_expand_cs);
    shader_vx_cull_expand_apply_params(&(ShaderVxCullExpandExpandParams){.range_count = rs.count, .groups_x = gx});
    GpuComputeBindings eb =
        shader_vx_cull_expand_bindings((ShaderVxCullExpandBindings){.ranges = r->cull_ranges, .visible = r->visible});
    gpu_apply_compute_bindings(&eb);
    gpu_dispatch(gx, (rs.count + gx - 1) / gx, 1);
    gpu_buffer_access(r->visible, GPU_ACCESS_SHADER_READ);
  }
  if (ents->vert_count) gpu_update_buffer(r->ent_vb, ents->verts, ents->vert_count * VX_ENTITY_STRIDE * sizeof(f32));
  if (ents->part_count) gpu_update_buffer(r->part_buf, ents->parts, ents->part_count * sizeof(VxParticleGpu));
  if (ents->box_count) gpu_update_buffer(r->box_buf, ents->boxes, ents->box_count * sizeof(VxBoxGpu));
  if (ents->jelly_count) gpu_update_buffer(r->jelly_buf, ents->jelly, ents->jelly_count * sizeof(VxBoxGpu));
  if (ents->vfx_count) gpu_update_buffer(r->vfx_buf, ents->vfx, ents->vfx_count * sizeof(VxVfxGpu));
  // the effect sprites are baked once, the first frame the renderer draws
  if (!r->sprites_baked) {
    pg_bake_state_init(&r->bake);
    r->sprites = pg_bake_sprite_atlas(&r->bake, &(PgSpriteAtlasDesc){.name = "vx sprites", .size = 512, .cols = 4, .seed = 7});
    r->sprites_baked = true;
  }

  // ---- shadow atlas: 4 cascades in quadrants ----
  if (s->shadows) {
    u32 R = s->shadow_res;
    // cached quadrants keep their depth: each re-rendered one clears only itself
    vx_pass(r, VX_GPU_SHADOW, (GpuPassDesc){.render_target = r->shadow_atlas, .no_clear_depth = true});
    for (u32 c = 0; c < VX_CASCADES; c++) {
      if (!(refresh & (1u << c))) continue;
      gpu_set_viewports(&(GpuViewport){.x = (f32)((c & 1) * R), .y = (f32)((c >> 1) * R), .w = (f32)R,
                                       .h = (f32)R, .min_depth = 0, .max_depth = 1},
                        1);
      gpu_apply_pipeline(r->shadow_clear);
      vx_fullscreen_draw();
      gpu_apply_pipeline(r->shadow);
      vx_frame_uniforms(r);
      shader_vx_shadow_apply_p(&(ShaderVxShadowShadowParams){.light_vp = r->cascade_vp[c]});
      r->stats.shadow_draw_calls += vx_draw_terrain(r, world, &shadow_lists[c], VX_TERRAIN_SHADOW);
      // moving casters only go into the cascades that re-render every frame
      if (c < VX_CASCADE_CACHED && ents->hand_start > 0) {
        gpu_apply_pipeline(r->entity_shadow);
        vx_frame_uniforms(r);
        shader_vx_entity_shadow_apply_p(&(ShaderVxEntityShadowEntityShadowParams){.light_vp = r->cascade_vp[c]});
        shader_vx_entity_shadow_apply_bindings((ShaderVxEntityShadowBindings){
            .vertex_buffer = r->ent_vb, .index_buffer = r->ent_ib, .index_format = GPU_INDEX_FORMAT_U16,
            .albedo_tex = r->blk_albedo, .item_tex = r->item_tex});
        gpu_draw_indexed(ents->hand_start / 4 * 6, 1, 0, 0);
        r->stats.shadow_draw_calls++;
      }
      if (c < VX_CASCADE_CACHED && ents->box_count) {
        gpu_apply_pipeline(r->box_shadow);
        vx_frame_uniforms(r);
        shader_vx_box_shadow_apply_p(&(ShaderVxBoxShadowBoxShadowParams){.light_vp = r->cascade_vp[c]});
        shader_vx_box_shadow_apply_bindings((ShaderVxBoxShadowBindings){.boxes = r->box_buf});
        gpu_draw(ents->box_count * 36, 1);
        r->stats.shadow_draw_calls++;
      }
      // jelly casts like a solid: the gel is dense enough to shade what it sits on
      if (c < VX_CASCADE_CACHED && ents->jelly_count) {
        gpu_apply_pipeline(r->box_shadow);
        vx_frame_uniforms(r);
        shader_vx_box_shadow_apply_p(&(ShaderVxBoxShadowBoxShadowParams){.light_vp = r->cascade_vp[c]});
        shader_vx_box_shadow_apply_bindings((ShaderVxBoxShadowBindings){.boxes = r->jelly_buf});
        gpu_draw(ents->jelly_count * 36, 1);
        r->stats.shadow_draw_calls++;
      }
    }
    vx_end(r);
  }

  // ---- g-buffer ----
  vx_begin(r, VX_GPU_GBUFFER, r->gbuf);
  gpu_apply_pipeline(r->terrain);
  vx_frame_uniforms(r);
  r->stats.draw_calls += vx_draw_terrain(r, world, &opaque, VX_TERRAIN_GBUFFER);
  // the island past the chunk meshes, then the sea round it, both behind the near terrain's depth; the underworld's roof hides both
  if (in->underworld < 0.5f) {
    gpu_apply_pipeline(r->far_terrain);
    vx_frame_uniforms(r);
    r->stats.draw_calls += vx_draw_far(r, world, &cam_fr, near_mask, cam);
    gpu_apply_pipeline(r->ocean);
    vx_frame_uniforms(r);
    shader_vx_ocean_apply_bindings((ShaderVxOceanBindings){.cover = r->near_mask});
    gpu_draw(6, 1);
    r->stats.draw_calls++;
  }
  if (ents->part_count) {
    gpu_apply_pipeline(r->particle);
    vx_frame_uniforms(r);
    shader_vx_particle_apply_bindings((ShaderVxParticleBindings){.parts = r->part_buf, .albedo_tex = r->blk_albedo});
    gpu_draw(ents->part_count * 6, 1);
    r->stats.draw_calls++;
  }
  u32 boxes_drawn = ents->puppet_drawn ? ents->box_count : ents->box_puppet_start;
  if (boxes_drawn) {
    gpu_apply_pipeline(r->box);
    vx_frame_uniforms(r);
    shader_vx_box_apply_bindings((ShaderVxBoxBindings){
        .boxes = r->box_buf, .albedo_tex = r->blk_albedo, .emissive_tex = r->blk_emissive});
    gpu_draw(boxes_drawn * 36, 1);
    r->stats.draw_calls++;
  }
  if (ents->vert_count) {
    ShaderVxEntityBindings eb = {.vertex_buffer = r->ent_vb, .index_buffer = r->ent_ib,
                                 .index_format = GPU_INDEX_FORMAT_U16, .albedo_tex = r->blk_albedo,
                                 .emissive_tex = r->blk_emissive, .item_tex = r->item_tex};
    u32 world_end = ents->puppet_drawn ? ents->hand_start : ents->puppet_start;
    if (world_end > 0) {
      gpu_apply_pipeline(r->entity);
      vx_frame_uniforms(r);
      shader_vx_entity_apply_bindings(eb);
      gpu_draw_indexed(world_end / 4 * 6, 1, 0, 0);
    }
    if (ents->vert_count > ents->hand_start) {
      gpu_apply_pipeline(r->entity_hand);
      vx_frame_uniforms(r);
      shader_vx_entity_apply_bindings(eb);
      gpu_draw_indexed((ents->vert_count - ents->hand_start) / 4 * 6, 1, ents->hand_start / 4 * 6, 0);
    }
  }
  vx_end(r);
  GpuTexture gdepth = vx_depth(r->gbuf);
  GpuTexture gb0 = vx_color(r->gbuf, 0), gb1 = vx_color(r->gbuf, 1);

  // ---- ssao ----
  if (s->ssao) {
    vx_begin(r, VX_GPU_SSAO, r->ssao_rt);
    gpu_apply_pipeline(r->ssao);
    vx_frame_uniforms(r);
    shader_vx_ssao_apply_bindings((ShaderVxSsaoBindings){.depth_tex = gdepth, .gnormal_tex = gb1});
    vx_fullscreen_draw();
    vx_end(r);
  }

  // ---- clouds: one texel per 2x2 block traced per frame, the rest reprojected ----
  u32 cloud_cur = r->cloud_idx;
  if (s->clouds > 0) {
    vx_begin(r, VX_GPU_CLOUDS, r->cloud_cur);
    gpu_apply_pipeline(r->clouds);
    vx_frame_uniforms(r);
    shader_vx_clouds_apply_p(&(ShaderVxCloudsCloudParams){.low_res = {(f32)r->cw, (f32)r->ch, 0, 0}});
    shader_vx_clouds_apply_bindings((ShaderVxCloudsBindings){.weather_tex = r->weather});
    vx_fullscreen_draw();
    vx_end(r);
    vx_begin(r, VX_GPU_CLOUDS, r->cloud_hist[cloud_cur]);
    gpu_apply_pipeline(r->clouds_temporal);
    vx_frame_uniforms(r);
    shader_vx_clouds_temporal_apply_p(&(ShaderVxCloudsTemporalCloudTemporalParams){
        .low_res = {(f32)((r->cw + 1) / 2), (f32)((r->ch + 1) / 2), r->reset_history ? 1.0f : 0.0f, 0}});
    shader_vx_clouds_temporal_apply_bindings((ShaderVxCloudsTemporalBindings){
        .current_tex = vx_color(r->cloud_cur, 0), .history_tex = vx_color(r->cloud_hist[1 - cloud_cur], 0)});
    vx_fullscreen_draw();
    vx_end(r);
  } else {
    vx_pass(r, VX_GPU_CLOUDS, (GpuPassDesc){.render_target = r->cloud_hist[cloud_cur], .clear_color = {0, 0, 0, 1}});
    vx_end(r);
  }
  GpuTexture clouds = vx_color(r->cloud_hist[cloud_cur], 0);
  r->cloud_idx = 1 - r->cloud_idx;

  // ---- deferred lighting into the scene target ----
  GpuTexture shadow_depth = vx_depth(r->shadow_atlas);
  vx_pass(r, VX_GPU_DEFERRED, (GpuPassDesc){.render_target = r->scene, .no_clear_color = true, .no_clear_depth = true});
  gpu_apply_pipeline(r->deferred);
  vx_frame_uniforms(r);
  shader_vx_deferred_apply_pl((ShaderVxDeferredLights *)pl);
  shader_vx_deferred_apply_bindings((ShaderVxDeferredBindings){
      .gb0_tex = gb0, .gb1_tex = gb1, .depth_tex = gdepth, .ssao_tex = vx_color(r->ssao_rt, 0), .clouds_tex = clouds,
      .weather_tex = r->weather, .vol0_tex = vol->tex[0], .vol1_tex = vol->tex[1], .dyn_tex = vol->dyn,
      .shadow_cmp_tex = shadow_depth, .shadow_raw_tex = shadow_depth, .cut_tex = r->cut_tex});
  vx_fullscreen_draw();
  vx_end(r);

  // ---- liquids (forward, refracting a copy of the lit scene) ----
  GpuTexture scene_color = vx_color(r->scene, 0);
  if (trans.count) {
    gpu_copy_texture(vx_color(r->scene_copy, 0), scene_color);
    gpu_copy_texture(vx_depth(r->depth_copy), gdepth);
    vx_pass(r, VX_GPU_WATER, (GpuPassDesc){.render_target = r->scene, .depth_target = r->gbuf, .no_clear_color = true,
                                           .no_clear_depth = true});
    gpu_apply_pipeline(r->water);
    vx_frame_uniforms(r);
    r->stats.draw_calls += vx_draw_terrain(r, world, &trans, VX_TERRAIN_WATER);
    vx_end(r);
  }

  // ---- jelly (forward, refracting the lit scene with its liquids) ----
  if (ents->jelly_count) {
    gpu_copy_texture(vx_color(r->scene_copy, 0), scene_color);
    gpu_copy_texture(vx_depth(r->depth_copy), gdepth);
    vx_pass(r, VX_GPU_WATER, (GpuPassDesc){.render_target = r->scene, .depth_target = r->gbuf, .no_clear_color = true,
                                           .no_clear_depth = true});
    gpu_apply_pipeline(r->jelly);
    vx_frame_uniforms(r);
    shader_vx_jelly_apply_bindings((ShaderVxJellyBindings){
        .boxes = r->jelly_buf, .scene_color_tex = vx_color(r->scene_copy, 0), .scene_depth_tex = vx_depth(r->depth_copy),
        .vol0_tex = vol->tex[0], .vol1_tex = vol->tex[1], .dyn_tex = vol->dyn, .shadow_cmp_tex = shadow_depth});
    gpu_draw(ents->jelly_count * 36, 1);
    r->stats.draw_calls++;
    vx_end(r);
  }
  GpuTexture scene_depth = gdepth;

  // ---- atmospherics: the shadowed near-fog march runs at half res, the composite at full ----
  if (s->volumetrics || in->underwater) {
    vx_begin(r, VX_GPU_ATMOS, r->shafts_rt);
    gpu_apply_pipeline(r->shafts);
    vx_frame_uniforms(r);
    shader_vx_shafts_apply_bindings((ShaderVxShaftsBindings){.depth_tex = scene_depth, .shadow_cmp_tex = shadow_depth});
    vx_fullscreen_draw();
    vx_end(r);
  }
  vx_begin(r, VX_GPU_ATMOS, r->hdr_b);
  gpu_apply_pipeline(r->atmospherics);
  vx_frame_uniforms(r);
  shader_vx_atmospherics_apply_bindings((ShaderVxAtmosphericsBindings){
      .color_tex = scene_color, .depth_tex = scene_depth, .shafts_tex = vx_color(r->shafts_rt, 0)});
  vx_fullscreen_draw();
  vx_end(r);

  // ---- forward sprites over the fogged scene, faded against a copy of the final depth ----
  if (ents->vfx_count) {
    gpu_copy_texture(vx_depth(r->depth_copy), gdepth);
    vx_pass(r, VX_GPU_WATER, (GpuPassDesc){.render_target = r->hdr_b, .depth_target = r->gbuf, .no_clear_color = true,
                                           .no_clear_depth = true});
    gpu_apply_pipeline(r->vfx);
    vx_frame_uniforms(r);
    shader_vx_vfx_apply_bindings((ShaderVxVfxBindings){
        .vfx = r->vfx_buf, .sprite_tex = r->sprites.texture, .scene_depth_tex = vx_depth(r->depth_copy),
        .vol0_tex = vol->tex[0], .vol1_tex = vol->tex[1], .dyn_tex = vol->dyn, .shadow_cmp_tex = shadow_depth});
    gpu_draw(ents->vfx_count * 6, 1);
    r->stats.draw_calls++;
    vx_end(r);
  }

  // ---- taa ----
  GpuTexture resolved = vx_color(r->hdr_b, 0);
  if (s->taa) {
    u32 cur = r->history_idx;
    vx_begin(r, VX_GPU_TAA, r->history[cur]);
    gpu_apply_pipeline(r->taa);
    vx_frame_uniforms(r);
    shader_vx_taa_apply_p(&(ShaderVxTaaTaaParams){
        .params = {r->reset_history ? 1.0f : 0.0f, uw != w || uh != h ? 1.0f : 0.0f, 0, 0},
        .out_res = {(f32)uw, (f32)uh, 1.0f / uw, 1.0f / uh}});
    shader_vx_taa_apply_bindings((ShaderVxTaaBindings){.color_tex = resolved,
                                                       .history_tex = vx_color(r->history[1 - cur], 0),
                                                       .depth_tex = scene_depth, .gb1_tex = gb1});
    vx_fullscreen_draw();
    vx_end(r);
    resolved = vx_color(r->history[cur], 0);
    r->history_idx = 1 - cur;
  }

  // ---- bloom down chain (bright light only), exposure from the 1/4 level, bloom up chain ----
  GpuTexture src = resolved;
  u32 sw = s->taa ? uw : w, sh = s->taa ? uh : h;
  for (u32 i = 0; i < VX_BLOOM_LEVELS; i++) {
    vx_begin(r, VX_GPU_BLOOM, r->bloom[i]);
    gpu_apply_pipeline(r->bloom_down);
    shader_vx_bloom_down_apply_p(&(ShaderVxBloomDownBloomParams){
        .params = {1.0f / sw, 1.0f / sh, i == 0 ? 1.0f : 0.0f, VX_BLOOM_THRESHOLD}});
    shader_vx_bloom_down_apply_bindings((ShaderVxBloomDownBindings){.src_tex = src});
    vx_fullscreen_draw();
    vx_end(r);
    src = vx_color(r->bloom[i], 0);
    sw = r->bloom_w[i];
    sh = r->bloom_h[i];
  }
  u32 ei = r->frame_index % 2;
  vx_begin(r, VX_GPU_BLOOM, r->exposure_rt[ei]);
  gpu_apply_pipeline(r->exposure);
  // a narrow range: caves stay dark and noon never washes out, as in a hand-lit 2d game
  shader_vx_exposure_apply_p(&(ShaderVxExposureExposureParams){
      .params = {m_minf(0.1f, in->dt), VX_EXPOSURE_MIN, VX_EXPOSURE_MAX, VX_EXPOSURE_COMP}});
  shader_vx_exposure_apply_bindings((ShaderVxExposureBindings){.src_tex = vx_color(r->bloom[1], 0),
                                                               .prev_tex = vx_color(r->exposure_rt[1 - ei], 0)});
  vx_fullscreen_draw();
  vx_end(r);
  for (u32 i = VX_BLOOM_LEVELS - 1; i > 0; i--) {
    vx_pass(r, VX_GPU_BLOOM, (GpuPassDesc){.render_target = r->bloom[i - 1], .no_clear_color = true});
    gpu_apply_pipeline(r->bloom_up);
    shader_vx_bloom_up_apply_p(&(ShaderVxBloomUpBloomUpParams){
        .params = {1.0f / r->bloom_w[i], 1.0f / r->bloom_h[i], 1.0f, 0}});
    shader_vx_bloom_up_apply_bindings((ShaderVxBloomUpBindings){.src_tex = vx_color(r->bloom[i], 0)});
    vx_fullscreen_draw();
    vx_end(r);
  }

  // ---- final composite to the swapchain ----
  vx_pass(r, VX_GPU_FINAL, (GpuPassDesc){.clear_color = {0, 0, 0, 1}});
  gpu_apply_pipeline(r->final);
  vx_frame_uniforms(r);
  ShaderVxFinalFinalParams fp = {
      .grade = {s->bloom ? VX_BLOOM_STRENGTH : 0.0f, s->taa ? 0.35f : 0.0f, look.saturation, look.hue},
      .lift = vx_v4(look.lift, 0.3f),
      .gain = vx_v4(look.gain, look.gamma),
  };
  if (in->has_selection) {
    fp.sel_min = (v4){(f32)(in->sel.x - cam.x), (f32)(in->sel.y - cam.y), (f32)(in->sel.z - cam.z), 1};
    fp.sel_max = (v4){(f32)(in->sel.x + 1 - cam.x), (f32)(in->sel.y + 1 - cam.y), (f32)(in->sel.z + 1 - cam.z), 0};
  }
  // debug views show raw values at a fixed exposure
  if (r->debug_view >= 1) fp.sel_max.w = 1.0f;
  fp.crosshair = (v4){(f32)out_w, (f32)out_h, m_maxf(in->dpr, 1.0f), in->crosshair ? 1.0f : 0.0f};
  shader_vx_final_apply_p(&fp);
  shader_vx_final_apply_bindings((ShaderVxFinalBindings){.color_tex = resolved,
                                                         .bloom_tex = vx_color(r->bloom[0], 0),
                                                         .exposure_tex = vx_color(r->exposure_rt[ei], 0),
                                                         .depth_tex = scene_depth});
  vx_fullscreen_draw();
  vx_end(r);

  // ---- bookkeeping ----
  r->prev_view_proj = view_proj_nj;
  r->prev_cam = cam;
  r->frame_index++;
  r->reset_history = false;
  tctx_temp_allocator_end(tmp);
}

#undef VX_PLANES

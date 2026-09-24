#ifndef WC_RENDER_H
#define WC_RENDER_H

#include "webcraft/wc.h"

// ---- procedural block textures ----

#define WC_TEX_SIZE 16
#define WC_TEX_MIPS 5

typedef struct {
  u8 *albedo; // srgb rgba, gpu_make_texture_mips layout (mip by mip, all layers)
  u8 *normal; // tangent normal + height
  u8 *spec;   // smoothness, metalness, subsurface, emission
  u32 data_size;
  u8 *pixels; // mip-0 srgba per layer (icons)
  u8 *cutout; // per layer
} WcTextureData;

u32 wc_texture_mip_offset(u32 mip);
void wc_textures_alloc(WcTextureData *out, Allocator *alloc);
// every lane builds its share of the layers; lanes sync before reading the result
void wc_textures_generate(WcTextureData *out);

// ---- tileable cloud noise, baked on the gpu (Perlin-Worley 3D base, Worley 3D detail, 2D weather map) ----

#define WC_NOISE_BASE 128
#define WC_NOISE_DETAIL 32
#define WC_WEATHER_SIZE 512

// ---- renderer ----

typedef struct {
  u32 render_distance;
  f32 render_scale;
  b32 shadows;
  u32 shadow_res;
  f32 shadow_distance;
  b32 ssao;
  b32 volumetrics;
  u32 clouds; // 0 off, 1 fast, 2 fancy
  b32 ssr;
  b32 taa;
  b32 bloom;
  f32 fov;
} WcRenderSettings;

typedef struct {
  WcV3d cam;
  f32 yaw, pitch, fov;
  f32 time, day_time, dt;
  b32 underwater;
  f32 eye_sky;
  b32 has_selection;
  iv3 sel;
  f32 cloud_coverage;
  f32 rain, wetness, snow, snow_cover, flash;
  b32 crosshair;
  f32 dpr;
} WcFrameInput;

// mirrors _Frame in shaders/wc_frame.glsl
typedef struct {
  m4 view, proj, view_proj, inv_view_proj, prev_view_proj, view_proj_nj;
  m4 shadow_vp[4];
  m4 star_rot;
  v4 cascade_splits, cascade_texel, cascade_depth;
  v4 cam_pos, cam_delta;
  v4 sun_dir, moon_dir, light_dir;
  v4 sun_color, moon_color, light_color;
  v4 res, jitter, fog, world, cloud, settings, weather, sky, extra;
} WcFrameUniforms;

// a column part's sections [first, last] that passed the frustum; record indexes the per-frame draw records
typedef struct {
  const WcMeshPart *part;
  i32 cx, cz;
  u32 record;
  u32 first, last;
} WcDraw;

typedef struct {
  WcDraw *items;
  u32 count;
} WcDrawList;

// visible quad entries [first, first + count) of one pool, expanded on the gpu
typedef struct {
  u32 pool;
  u32 first;
  u32 count;
} WcBatch;

typedef struct {
  WcBatch *items;
  u32 count;
} WcBatchList;

#define WC_CASCADES 4
#define WC_MAX_DPR 1.5f
#define WC_BLOOM_LEVELS 6
#define WC_ENTITY_STRIDE 13
#define WC_ENTITY_MAX_VERTS 12000

typedef struct {
  u32 draw_calls, shadow_draw_calls, sections;
  u32 quads; // drawn from the camera (opaque and translucent) after culling
} WcRenderStats;

// gpu profiling: every pass gets its own timer and is reported under one of these stages
typedef enum {
  WC_GPU_SKY,
  WC_GPU_SHADOW,
  WC_GPU_GBUFFER,
  WC_GPU_SSAO,
  WC_GPU_CLOUDS,
  WC_GPU_DEFERRED,
  WC_GPU_WATER,
  WC_GPU_ATMOS,
  WC_GPU_TAA,
  WC_GPU_BLOOM,
  WC_GPU_FINAL,
  WC_GPU_UI,
  WC_GPU_GROUP_COUNT,
} WcGpuGroup;

#define WC_GPU_TIMERS 40

// a shadow cascade's depth stays valid while the camera stays inside its margin and nothing in it changed
typedef struct {
  f64 cx, cy, cz; // world point it is centred on, texel-snapped in light space
  v3 L;           // quantised light direction it was rendered with
  m4 vp;          // light view-projection of positions relative to (cx, cy, cz)
  f32 half, texel, depth;
  b32 valid;
} WcCascadeCache;

typedef struct {
  WcRenderSettings settings;
  u32 debug_view;
  WcRenderStats stats;

  GpuPipeline transmittance, multiscatter, skyview, sky_ambient;
  GpuPipeline terrain, entity, entity_hand, shadow, shadow_clear, water;
  GpuPipeline ssao, clouds, clouds_temporal, deferred, shafts, atmospherics, taa;
  GpuPipeline bloom_down, bloom_up, exposure, final;

  GpuTexture blk_albedo, blk_normal, blk_spec;
  GpuTexture noise_base, noise_detail, weather; // full mip chains, written once by the noise bake
  GpuComputePipeline noise_base_cs, noise_detail_cs, noise_weather_cs, noise_down3d_cs, noise_down2d_cs;
  b32 noise_baked;

  GpuRenderTarget trans_lut, ms_lut, sky_sun, sky_moon, ambient;
  GpuRenderTarget exposure_rt[2];
  b32 luts_built;
  // the sky-view luts depend on the body's altitude and the camera height only; they re-render past a tolerance
  f32 sky_alt_sun, sky_alt_moon, sky_height;
  b32 sky_valid;
  b32 needs_prime;       // the resolution-sized targets were (re)created
  b32 fixed_primed;      // the fixed-size sky, ambient and exposure targets, cleared once: nothing resizes them
  b32 core_ready;      // the frame renders; effects still compiling render as switched off
  b32 pipelines_ready; // every pipeline, effects included
  u32 effects_on;      // WcEffect bits active last frame; one switching on restarts the histories

  GpuRenderTarget shadow_atlas, water_shadow;
  u32 shadow_res;

  u32 w, h, cw, ch;
  u32 uw, uh; // taa history and bloom size: the output size when taa upscales (render_scale < 1), else w, h
  GpuRenderTarget gbuf, scene, scene_copy, hdr_b, ssao_rt, shafts_rt; // shafts_rt: half res
  GpuRenderTarget depth_copy; // opaque depth for water to sample while it writes the shared depth
  GpuRenderTarget history[2];
  GpuRenderTarget cloud_cur, cloud_hist[2];
  GpuRenderTarget bloom[WC_BLOOM_LEVELS];
  u32 bloom_w[WC_BLOOM_LEVELS], bloom_h[WC_BLOOM_LEVELS];

  GpuBuffer quad_ib;
  u32 quad_ib_quads;
  GpuComputePipeline cull_expand_cs;
  GpuBuffer cull_ranges, visible; // per-frame quad ranges and their expansion, one (quad, record) per quad
  u32 cull_range_cap, visible_cap;
  GpuBuffer draw_records;
  u32 draw_record_cap;
  GpuBuffer ent_vb, ent_ib;

  WcFrameUniforms frame;
  u32 frame_index, history_idx, cloud_idx;

  b32 gpu_timing;
  GpuTimer timers[WC_GPU_TIMERS];
  u8 timer_group[WC_GPU_TIMERS];
  u32 timer_count, timer_used, timer_open;
  b32 reset_history;
  m4 prev_view_proj;
  WcV3d prev_cam;
  m4 cascade_vp[WC_CASCADES];
  f32 cascade_far[WC_CASCADES], cascade_texel[WC_CASCADES], cascade_depth[WC_CASCADES];
  f32 cloud_time;
  WcCascadeCache cascade_cache[WC_CASCADES];

  Allocator alloc;
} WcRenderer;

typedef struct {
  f32 *verts; // WC_ENTITY_STRIDE floats per vertex
  u32 vert_count;
  u32 hand_start;
  m4 hand_rot;
} WcEntityGeometry;

// first thing at startup: on wasm the pipelines compile while the rest of init and the first frames run
void wc_render_create_pipelines(WcRenderer *r);
// settings must be filled first; creates every target at the initial output size (device px)
void wc_render_init(WcRenderer *r, Allocator *alloc, const WcTextureData *tex, u32 out_w, u32 out_h, f32 dpr);
void wc_render_frame(WcRenderer *r, const WcWorld *world, const WcFrameInput *in, const WcEntityGeometry *ents,
                     u32 out_w, u32 out_h);
void wc_render_invalidate_history(WcRenderer *r);
// false while wasm is still compiling the core pipelines; frames until then only clear
b32 wc_render_ready(WcRenderer *r);
// times the next pass when gpu_timing is on; pair with wc_render_timer_end after gpu_end_pass
void wc_render_timer_begin(WcRenderer *r, WcGpuGroup g);
void wc_render_timer_end(WcRenderer *r);
// latest resolved per-stage gpu ms and whole-frame span; false while no timing is available
b32 wc_render_gpu_times(WcRenderer *r, f32 *group_ms, f32 *frame_ms);

// sun and moon directions for a 0..1 day time (0 = sunrise), plus the star rotation
void wc_celestial(f32 day_time, v3 *sun, v3 *moon, m4 *star_rot);
// fraction of sunlight reaching an observer at altitude_m along dir
v3 wc_sky_transmittance(f32 altitude_m, v3 dir);

#endif

#ifndef VX_RENDER_H
#define VX_RENDER_H

#include "voxaria/vx_items.h"
#include "voxaria/vx.h"
#include "procgen/pg_bake.h"
#include "procgen/pg_vfx.h"

// ---- procedural tile textures ----

// a layer covers 2x2 blocks at 64 texels per block
#define VX_TEX_SIZE 128
#define VX_TEX_MIPS 8
// the cpu copy per layer (icons, map colours): one block at 16x16
#define VX_TEX_PIXELS 16
// item sprites stay 16x16 pixel art
#define VX_ITEM_TEX_SIZE 16
#define VX_ITEM_TEX_MIPS 5

typedef struct {
  u8 *albedo;   // srgb rgba, gpu_make_texture_mips layout (mip by mip, all layers)
  u8 *emissive; // rg8, same layout: emission strength, glint mask
  u32 albedo_size, emissive_size;
  u8 *pixels; // VX_TEX_PIXELS^2 srgba per layer: one block of the tile (icons, map colours)
  u8 *cutout; // per texture
} VxTextureData;

void vx_textures_alloc(VxTextureData *out, Allocator *alloc);
// every lane builds its share of the layers; lanes sync before reading the result
void vx_textures_generate(VxTextureData *out);
// desktop: every layer as one labelled-by-position png grid (one row per texture, one column per variant)
b32 vx_textures_dump(const VxTextureData *tex, const char *path);

// ---- cloud weather map, baked once on the gpu ----

#define VX_WEATHER_SIZE 512

// ---- renderer ----

typedef enum {
  VX_LIGHTING_COLOR,
  VX_LIGHTING_WHITE,
  VX_LIGHTING_RETRO,
  VX_LIGHTING_TRIPPY,
  VX_LIGHTING_COUNT,
} VxLightingMode;

typedef struct {
  u32 render_distance;
  f32 render_scale;
  b32 shadows;
  u32 shadow_res;
  f32 shadow_distance;
  b32 soft_shadows;
  b32 ssao;
  b32 volumetrics;
  u32 clouds; // 0 off, 1 fast, 2 fancy
  u32 lighting; // VxLightingMode
  b32 taa;
  b32 bloom;
  f32 fov;
} VxRenderSettings;

#define VX_MAX_POINT_LIGHTS 64
#define VX_DYN_SIZE 32
// the cutaway's light map: VX_CUT_W columns of the cut slab from a left edge, every height, rgba8 (rgb block light, a sky)
#define VX_CUT_W 128

// short-lived unshadowed light (sparks, spells, projectiles)
typedef struct {
  VxV3d pos;
  f32 radius;
  v3 color;
} VxPointLight;

typedef struct {
  VxV3d cam;
  f32 yaw, pitch, fov;
  f32 time, day_time, dt;
  b32 underwater;
  f32 eye_sky;
  b32 has_selection;
  iv3 sel;
  f32 cloud_coverage, moon_phase;
  f32 rain, wetness, snow, snow_cover, flash;
  b32 crosshair;
  f32 dpr;
  // where the camera is: smoothed surface-biome weights, how far underground and how deep in the underworld (0..1)
  f32 biome_w[VX_BIOME_COUNT];
  f32 underground, underworld;
  f32 lava_heat; // open lava near the camera (0..1)
  // a falling star crossing the sky: start direction, heading, progress 0..1 (< 0 none)
  v3 star_start, star_heading;
  f32 star_progress;
  const VxPointLight *points;
  u32 point_count;
  // the cutaway: nothing past z = cut_plane is drawn, and faces on the plane are lit from cut_light (re-uploaded when cut_version changes)
  b32 cut;
  f32 cut_plane;
  i32 cut_x0;
  const u32 *cut_light;
  u32 cut_version;
  // the held-light window: VX_DYN_SIZE^3 rgba8 texels from origin, re-uploaded when dyn_version changes; NULL when dark
  const u32 *dyn_light;
  iv3 dyn_origin;
  u32 dyn_version;
} VxFrameInput;

// mirrors _Frame in shaders/vx_frame.glsl
typedef struct {
  m4 view, proj, view_proj, inv_view_proj, prev_view_proj, view_proj_nj;
  m4 shadow_vp[4];
  m4 star_rot;
  v4 cascade_splits, cascade_texel, cascade_depth;
  v4 cam_pos, cam_delta;
  v4 sun_dir, moon_dir, light_dir, light_color;
  v4 res, jitter, fog, fog_color, world, cloud, settings, weather;
  v4 sky_zenith, sky_horizon, sky_glow, amb_sky, amb_ground, shade, under_color;
  v4 vol0, vol1, dyn, extra;
  v4 star_a, star_b;
  v4 under_glow;
  v4 biome_a, biome_b;
  v4 cut;
} VxFrameUniforms;

// mirrors _Lights in shaders/vx_deferred.glsl
typedef struct {
  v4 pos_radius[VX_MAX_POINT_LIGHTS];
  v4 color[VX_MAX_POINT_LIGHTS];
  v4 count;
} VxPointLightUniforms;

// ---- light volume: two toroidal 128^3 levels fed section by section from the chunks' voxel light ----

#define VX_VOL_N 128
#define VX_VOL_SPAN0 8  // level 0: 8^3 sections, a texel per voxel
#define VX_VOL_SPAN1 16 // level 1: 16^3 sections, a texel per 2^3 voxels
#define VX_VOL_SLOTS0 (VX_VOL_SPAN0 * VX_VOL_SPAN0 * VX_VOL_SPAN0)
#define VX_VOL_SLOTS1 (VX_VOL_SPAN1 * VX_VOL_SPAN1 * VX_VOL_SPAN1)

// what a slot holds: the chunk instance (0 = not loaded), the section and its light version
typedef struct {
  u32 uid;
  u32 version;
  i32 sx, sy, sz;
} VxVolSlot;

typedef struct {
  GpuTexture tex[2];
  GpuTexture dyn;
  GpuComputePipeline upload_cs;
  GpuBuffer headers, data, dyn_data;
  VxVolSlot *slots[2];
  iv3 origin[2]; // window origin in sections
  b32 has_origin;
  u32 filled[2]; // slots written at least once: a level is sampled once all of them are
  u32 dyn_version;
  b32 dyn_primed;
  b32 dyn_on;
  iv3 dyn_origin;
  // last frame's uploads, for the log and the stats line
  u32 uploaded_sections, uploaded_uniform;
} VxLightVolume;

// a column part's sections [first, last] that passed the frustum; record indexes the per-frame draw records
typedef struct {
  const VxMeshPart *part;
  i32 cx, cz;
  u32 record;
  u32 first, last;
} VxDraw;

typedef struct {
  VxDraw *items;
  u32 count;
} VxDrawList;

// visible quad entries [first, first + count) of one pool, expanded on the gpu
typedef struct {
  u32 pool;
  u32 first;
  u32 count;
} VxBatch;

typedef struct {
  VxBatch *items;
  u32 count;
} VxBatchList;

#define VX_CASCADES 4
#define VX_MAX_DPR 1.5f
#define VX_BLOOM_LEVELS 6
#define VX_ENTITY_STRIDE 16
#define VX_ENTITY_MAX_VERTS 32000

typedef struct {
  u32 draw_calls, shadow_draw_calls, sections;
  u32 quads; // drawn from the camera (opaque and translucent) after culling
} VxRenderStats;

// gpu profiling: every pass gets its own timer and is reported under one of these stages
typedef enum {
  VX_GPU_UPLOAD,
  VX_GPU_SHADOW,
  VX_GPU_GBUFFER,
  VX_GPU_SSAO,
  VX_GPU_CLOUDS,
  VX_GPU_DEFERRED,
  VX_GPU_WATER,
  VX_GPU_ATMOS,
  VX_GPU_TAA,
  VX_GPU_BLOOM,
  VX_GPU_FINAL,
  VX_GPU_UI,
  VX_GPU_GROUP_COUNT,
} VxGpuGroup;

#define VX_GPU_TIMERS 40

// a shadow cascade's depth stays valid while the camera stays inside its margin and nothing in it changed
typedef struct {
  f64 cx, cy, cz; // world point it is centred on, texel-snapped in light space
  v3 L;           // quantised light direction it was rendered with
  m4 vp;          // light view-projection of positions relative to (cx, cy, cz)
  f32 half, texel, depth;
  b32 valid;
} VxCascadeCache;

typedef struct {
  VxRenderSettings settings;
  u32 debug_view;
  VxRenderStats stats;

  GpuPipeline terrain, entity, entity_hand, entity_shadow, particle, shadow, shadow_clear, water;
  GpuPipeline box, box_shadow, jelly, vfx;
  GpuPipeline far_terrain, ocean;
  GpuBuffer near_mask; // VX_FAR_MASK_U32: the chunks whose own meshes draw, which the far terrain leaves out
  PgBakeState bake;
  PgSpriteAtlas sprites; // baked on the first frame
  b32 sprites_baked;
  GpuBuffer part_buf, box_buf, jelly_buf, vfx_buf;
  GpuPipeline ssao, clouds, clouds_temporal, deferred, shafts, atmospherics, taa;
  GpuPipeline bloom_down, bloom_up, exposure, final;

  GpuTexture blk_albedo, blk_emissive, item_tex;
  GpuTexture weather; // full mip chain, written once by the noise bake
  GpuComputePipeline noise_weather_cs, noise_down2d_cs;
  b32 noise_baked;
  VxLightVolume vol;
  GpuTexture cut_tex; // the cutaway light map, VX_CUT_W x VX_HEIGHT
  u32 cut_version;

  GpuRenderTarget exposure_rt[2];
  b32 needs_prime;       // the resolution-sized targets were (re)created
  b32 fixed_primed;      // the fixed-size exposure targets, cleared once: nothing resizes them
  b32 core_ready;      // the frame renders; effects still compiling render as switched off
  b32 pipelines_ready; // every pipeline, effects included
  u32 effects_on;      // VxEffect bits active last frame; one switching on restarts the histories

  GpuRenderTarget shadow_atlas;
  u32 shadow_res;

  u32 w, h, cw, ch;
  u32 uw, uh; // taa history and bloom size: the output size when taa upscales (render_scale < 1), else w, h
  GpuRenderTarget gbuf, scene, scene_copy, hdr_b, ssao_rt, shafts_rt; // shafts_rt: half res
  GpuRenderTarget depth_copy; // opaque depth for water to sample while it writes the shared depth
  GpuRenderTarget history[2];
  GpuRenderTarget cloud_cur, cloud_hist[2];
  GpuRenderTarget bloom[VX_BLOOM_LEVELS];
  u32 bloom_w[VX_BLOOM_LEVELS], bloom_h[VX_BLOOM_LEVELS];

  GpuBuffer quad_ib;
  u32 quad_ib_quads;
  GpuComputePipeline cull_expand_cs;
  GpuBuffer cull_ranges, visible; // per-frame quad ranges and their expansion, one (quad, record) per quad
  u32 cull_range_cap, visible_cap;
  GpuBuffer draw_records;
  u32 draw_record_cap;
  GpuBuffer ent_vb, ent_ib;

  VxFrameUniforms frame;
  u32 frame_index, history_idx, cloud_idx;

  b32 gpu_timing;
  GpuTimer timers[VX_GPU_TIMERS];
  u8 timer_group[VX_GPU_TIMERS];
  u32 timer_count, timer_used, timer_open;
  b32 reset_history;
  m4 prev_view_proj;
  VxV3d prev_cam;
  m4 cascade_vp[VX_CASCADES];
  f32 cascade_far[VX_CASCADES], cascade_texel[VX_CASCADES], cascade_depth[VX_CASCADES];
  f32 cloud_time;
  VxCascadeCache cascade_cache[VX_CASCADES];

  Allocator alloc;
} VxRenderer;

// one particle billboard, as shaders/vx_particle.glsl reads it
typedef struct {
  f32 x, y, z, size; // camera-relative centre, edge length in blocks
  u32 tint;          // rgba8: colour and coverage
  u32 misc;          // tile layer | flags << 12
  f32 spin;          // radians in the view plane
  u32 _pad;
} VxParticleGpu;
#define VX_PART_GLOW 1u

// one forward-shaded sprite, as shaders/vx_vfx.glsl reads it
typedef struct {
  f32 x, y, z, size; // camera-relative centre, width in blocks
  f32 dx, dy, dz, stretch; // a streak's world direction and extra length behind the centre
  u32 tint;          // srgb rgba8, alpha its opacity
  u32 misc;          // sprite | flags << 8
  f32 spin;          // radians in the view plane
  f32 glow;          // emission strength of a glowing sprite
} VxVfxGpu;
#define VX_VFX_GLOW 1u // additive and self-lit; otherwise alpha-blended and lit by the world
#define VX_VFX_SQUARE 15u // the crisp pixel sparkle with its halo, drawn without the atlas
#ifdef WASM
#define VX_PARTICLE_CAP 4096
#else
#define VX_PARTICLE_CAP 16384
#endif

// one instanced box, as shaders/vx_box_common.glsl reads it
typedef struct {
  v4 r0, r1, r2; // rows of the camera-relative affine map of the unit cube onto the box
  u32 tint;      // srgb rgba8; alpha flashes it white
  u32 misc;      // tile layer | flags << 12 (4 self-lit, 8 near fade, 16 hot glow)
  u32 pad0, pad1;
} VxBoxGpu;
#define VX_BOX_SELF_LIT 4u
#define VX_BOX_NEAR_FADE 8u // dissolves where it crosses the camera: creatures, never the player
#define VX_BOX_HOT_GLOW 16u // the full emission range, past the bloom threshold: glowing feather tips
#define VX_BOX_CAP 8192
#define VX_JELLY_CAP 512

typedef struct {
  f32 *verts; // VX_ENTITY_STRIDE floats per vertex
  u32 vert_count;
  VxParticleGpu *parts;
  u32 part_count;
  u32 puppet_start; // [puppet_start, hand_start): the player's puppet; shadowed always, drawn when puppet_drawn
  b32 puppet_drawn;
  u32 hand_start;
  VxBoxGpu *boxes;
  u32 box_count;
  u32 box_puppet_start; // [box_puppet_start, box_count): the player's puppet, drawn when puppet_drawn
  VxBoxGpu *jelly;      // translucent boxes, forward shaded after the opaque scene
  u32 jelly_count;
  VxVfxGpu *vfx;        // forward sprites: glows additive, smoke and splashes lit and blended
  u32 vfx_count;
} VxEntityGeometry;

// first thing at startup: on wasm the pipelines compile while the rest of init and the first frames run
void vx_render_create_pipelines(VxRenderer *r);
// settings must be filled first; creates every target at the initial output size (device px)
void vx_render_init(VxRenderer *r, Allocator *alloc, const VxTextureData *tex, u32 out_w, u32 out_h, f32 dpr);
void vx_render_frame(VxRenderer *r, const VxWorld *world, const VxFrameInput *in, const VxEntityGeometry *ents,
                     u32 out_w, u32 out_h);
void vx_render_invalidate_history(VxRenderer *r);
// false while wasm is still compiling the core pipelines; frames until then only clear
b32 vx_render_ready(VxRenderer *r);
// times the next pass when gpu_timing is on; pair with vx_render_timer_end after gpu_end_pass
void vx_render_timer_begin(VxRenderer *r, VxGpuGroup g);
void vx_render_timer_end(VxRenderer *r);
// latest resolved per-stage gpu ms and whole-frame span; false while no timing is available
b32 vx_render_gpu_times(VxRenderer *r, f32 *group_ms, f32 *frame_ms);

// the art direction of one frame: time-of-day sky and light, biome fog and grade, underground tint
typedef struct {
  v3 zenith, horizon, glow;
  v3 amb_sky, amb_ground, shade;
  v3 direct; // the sun's or moon's light before the horizon fade
  v3 fog_color, under_color, under_glow;
  f32 night, stars, aurora;
  f32 fog_density, fog_falloff, under_density, heat;
  v3 lift, gain;
  f32 gamma, saturation, hue;
} VxLook;

// sun_y is the sun's height (-1..1); the biome weights and depth zones come from the frame input
VxLook vx_look_compute(const VxFrameInput *in, f32 sun_y);

// sun and moon directions for a 0..1 day time (0 = sunrise), plus the star rotation
void vx_celestial(f32 day_time, v3 *sun, v3 *moon, m4 *star_rot);

#endif

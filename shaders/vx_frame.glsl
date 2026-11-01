// per-frame uniforms shared by every voxaria pass (mirrors VxFrameUniforms)
layout(binding = 0) uniform _Frame {
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 prev_view_proj;
    mat4 view_proj_nj;
    mat4 shadow_vp[4];
    mat4 star_rot;
    vec4 cascade_splits;
    vec4 cascade_texel;
    vec4 cascade_depth;
    vec4 cam_pos;      // xyz camera, w time
    vec4 cam_delta;    // xyz camera motion, w frame index
    vec4 sun_dir;      // w: sun above the horizon (0..1)
    vec4 moon_dir;     // w: moon phase 0..1 (0 full)
    vec4 light_dir;    // shadow-casting body
    vec4 light_color;  // direct light of that body, w > 0 when it casts
    vec4 res;          // w, h, 1/w, 1/h
    vec4 jitter;
    vec4 fog;          // distance density, height falloff, horizon distance, underground density
    vec4 fog_color;    // surface fog colour, w underground blend at the eye
    vec4 world;        // underwater, eye sky light, near plane, far plane
    vec4 cloud;        // coverage, base altitude, thickness, time
    vec4 settings;     // shadows, ssao, shadow resolution, soft shadows
    vec4 weather;      // rain, wetness, wind, volumetrics
    vec4 sky_zenith;   // rgb, w night 0..1
    vec4 sky_horizon;  // rgb, w star visibility
    vec4 sky_glow;     // sun-side horizon glow rgb, w aurora strength
    vec4 amb_sky;      // sky ambient (upper hemisphere)
    vec4 amb_ground;   // ground bounce, w lighting mode (0 colour, 1 white, 2 retro, 3 trippy)
    vec4 shade;        // tint of the shadow side, w underground fog tint strength
    vec4 under_color;  // underground fog colour, w heat distortion
    vec4 vol0;         // light volume level 0 window origin (blocks), w valid
    vec4 vol1;         // level 1 window origin, w valid
    vec4 dyn;          // dynamic light window origin, w > 0 while it holds light
    vec4 extra;        // snow fraction, snow cover, lightning flash, debug view
    vec4 star_a;       // falling star: start direction, w progress 0..1 (< 0 none)
    vec4 star_b;       // its heading (unit, across the sky), w unused
    vec4 under_glow;   // the underworld lava sea's warm uplight, w unused
    vec4 biome_a;      // surface biome weights at the camera: forest, desert, snow, jungle
    vec4 biome_b;      // corruption, ocean, mushroom, hallow
    vec4 cut;          // the cutaway: x the plane's world z, y > 0.5 while on, z the light map's left edge
} F;

// the cutaway removes everything past the plane; faces on it are the cut
bool vx_cut_away(vec3 world) { return F.cut.y > 0.5 && world.z > F.cut.x + 0.002; }
// distance along a view ray where the air begins: at the plane in the cutaway, else at the eye
float vx_cut_start(vec3 dir) { return F.cut.y > 0.5 && dir.z < -0.01 ? max((F.cam_pos.z - F.cut.x) / -dir.z, 0.0) : 0.0; }

float ign_t(vec2 p) { return ign(p + float(int(F.cam_delta.w) % 64) * vec2(5.588238)); }

// camera-relative position from a top-left screen uv and a reverse-z depth
vec3 reconstruct_rel(vec2 uv, float depth) {
    vec4 p = F.inv_view_proj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return p.xyz / p.w;
}
vec3 view_ray(vec2 uv) { return normalize(reconstruct_rel(uv, 0.5)); }
// clip w (view distance along the axis) of a reverse-z infinite-projection depth
float linear_depth(float d) { return F.world.z / max(d, 1e-9); }
vec2 clip_to_uv(vec4 clip) {
    vec2 n = clip.xy / clip.w;
    return vec2(n.x * 0.5 + 0.5, 0.5 - n.y * 0.5);
}
ivec2 uv_to_texel(vec2 uv) { return clamp(ivec2(uv * F.res.xy), ivec2(0), ivec2(F.res.xy) - 1); }
// offset inside its 2x2 block of the full-res texel a half-res texel samples; cycles all four over 4 frames
ivec2 half_res_offset() {
    int f = int(F.cam_delta.w) & 3;
    return ivec2(f & 1, (f >> 1) ^ (f & 1));
}
ivec2 half_res_texel(ivec2 h) { return min(h * 2 + half_res_offset(), ivec2(F.res.xy) - 1); }

// g-buffer: rt0 albedo + ao, rt1 octahedral normal (rg), packed glow and glint (b), flags (a, 2 bits)
#define VX_GB_SOLID 0
#define VX_GB_FOLIAGE 1
#define VX_GB_HAND 2
// open water past the chunk meshes, shaded as the water pass shades deep water
#define VX_GB_WATER 3
#define VX_DEEP_WATER vec3(0.06, 0.2, 0.5)
// blocks from the world origin to the distant mountain range; the open sea stops there
#define VX_RING_R 2100.0
#define VX_GB_EMISSION_RANGE 4.0
// rt1.b packs glow (6 bits over 0..VX_GB_EMISSION_RANGE) and glint (4 bits): glint sparkles with received light
float gb_pack_emission(float glow, float glint) {
    float g = floor(saturate(glow / VX_GB_EMISSION_RANGE) * 63.0 + 0.5);
    float s = floor(saturate(glint) * 15.0 + 0.5);
    return (g * 16.0 + s) / 1023.0;
}
vec2 gb_unpack_emission(float b) {
    float v = floor(b * 1023.0 + 0.5);
    return vec2(floor(v / 16.0) / 63.0 * VX_GB_EMISSION_RANGE, mod(v, 16.0) / 15.0);
}
int gb_flags(float a) { return int(a * 3.0 + 0.5); }

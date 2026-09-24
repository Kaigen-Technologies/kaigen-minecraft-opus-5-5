// per-frame uniforms shared by every webcraft pass (mirrors WcFrameUniforms)
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
    vec4 sun_dir;      // w: sun above the horizon
    vec4 moon_dir;
    vec4 light_dir;    // shadow-casting body
    vec4 sun_color;
    vec4 moon_color;
    vec4 light_color;
    vec4 res;          // w, h, 1/w, 1/h
    vec4 jitter;
    vec4 fog;          // near-field distance, horizon distance, density, height falloff
    vec4 world;        // underwater, eye sky light, near plane, far plane
    vec4 cloud;        // coverage, base, thickness, time
    vec4 settings;     // shadows, ssao, shadow resolution, ssr
    vec4 weather;      // rain, wetness, wind, volumetrics
    vec4 sky;          // sun illuminance, moon illuminance, night glow, night
    vec4 extra;        // snow fraction, snow cover, lightning flash, debug view
} F;

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

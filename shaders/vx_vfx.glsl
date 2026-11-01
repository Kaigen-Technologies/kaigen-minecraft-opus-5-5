// forward sprites: baked atlas shapes or a crisp pixel square with a halo; glows add, smoke and splashes blend lit by the world
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
// record: xyz camera-relative, size | xyz streak direction, stretch | tint rgba8, sprite | flags << 8, spin, glow
layout(set = 1, binding = 7) readonly buffer _Vfx { uvec4 v[]; } vfx;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_misc;
layout(location = 2) flat out vec4 v_tint;
layout(location = 3) out vec3 v_rel;
layout(location = 4) flat out float v_glow;

void main() {
    uint vi = uint(gl_VertexIndex);
    uint pi = vi / 6u;
    uint c = vi % 6u;
    // two triangles: 0 1 2, 0 2 3
    uint k = c < 3u ? c : (c == 3u ? 0u : c - 2u);
    vec2 cr = vec2(float(k == 1u || k == 2u), float(k >= 2u));
    uvec4 a = vfx.v[pi * 3u];
    uvec4 b = vfx.v[pi * 3u + 1u];
    uvec4 m = vfx.v[pi * 3u + 2u];
    vec3 p = uintBitsToFloat(a.xyz);
    float size = uintBitsToFloat(a.w);
    vec3 dir = uintBitsToFloat(b.xyz);
    float stretch = uintBitsToFloat(b.w);
    float spin = uintBitsToFloat(m.z);
    vec3 right = vec3(F.view[0][0], F.view[1][0], F.view[2][0]);
    vec3 up = vec3(F.view[0][1], F.view[1][1], F.view[2][1]);
    vec3 rel;
    if (stretch > 0.0) {
        // a streak lies along its direction as seen on screen, its head at p and its tail behind
        vec3 fwd = normalize(p);
        vec3 d = dir - fwd * dot(dir, fwd);
        vec3 u = length(d) > 1e-4 ? normalize(d) : up;
        vec3 w = normalize(cross(fwd, u));
        float len = size + stretch;
        rel = p + u * (cr.x * len - (len - size * 0.5)) + w * (cr.y - 0.5) * size;
    } else {
        vec2 o = (cr - 0.5) * size;
        float cs = cos(spin), sn = sin(spin);
        o = vec2(o.x * cs - o.y * sn, o.x * sn + o.y * cs);
        rel = p + right * o.x + up * o.y;
    }
    gl_Position = F.view_proj * vec4(rel, 1.0);
    // the streak painter runs along its local y, so a streak's length maps to v
    v_uv = stretch > 0.0 ? cr.yx : vec2(cr.x, 1.0 - cr.y);
    v_misc = m.y;
    vec4 t = unpackUnorm4x8(m.x);
    v_tint = vec4(pow(t.rgb, vec3(2.2)), t.a);
    v_rel = rel;
    v_glow = uintBitsToFloat(m.w);
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2D sprite_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D scene_depth_tex;
layout(set = 1, binding = 2) uniform texture3D vol0_tex;
layout(set = 1, binding = 3) uniform texture3D vol1_tex;
layout(set = 1, binding = 5) uniform texture3D dyn_tex;
@depth
layout(set = 1, binding = 6) uniform texture2D shadow_cmp_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#include "vx_sky.glsl"
#include "vx_lighting.glsl"
#include "vx_volume.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_misc;
layout(location = 2) flat in vec4 v_tint;
layout(location = 3) in vec3 v_rel;
layout(location = 4) flat in float v_glow;
layout(location = 0) out vec4 frag_color;

const float SPRITE_COLS = 4.0;

void main() {
    uint sprite = v_misc & 255u;
    bool glow = ((v_misc >> 8u) & 1u) != 0u;
    vec3 detail;
    float cov;
    if (sprite == 15u) {
        // a hard pixel square and the soft bloom around it, as Terraria's sparkles read
        vec2 q = abs(v_uv - 0.5) * 2.0;
        float core = step(max(q.x, q.y), 0.4);
        float halo = exp(-dot(q, q) * 4.0) * (glow ? 0.55 : 0.0);
        cov = max(core, halo);
        detail = vec3(core + halo * 0.6);
    } else {
        vec2 cell = vec2(float(sprite % 4u), float(sprite / 4u));
        vec4 s = texture(sampler2D(sprite_tex, smp_linear), (cell + v_uv) / SPRITE_COLS);
        cov = s.a;
        // the painters clamp only coverage; a negative detail would subtract light from the scene
        detail = max(s.rgb, vec3(0.0));
    }
    // soft where it meets the scene, so puffs never show a hard line against the ground
    float dist = length(v_rel);
    float sd = texelFetch(sampler2D(scene_depth_tex, smp_clamp), ivec2(gl_FragCoord.xy), 0).r;
    float scene_dist = sd <= 0.0 ? 1e6 : length(reconstruct_rel(gl_FragCoord.xy * F.res.zw, sd));
    float fade = saturate((scene_dist - dist) / 0.35);
    float a = cov * v_tint.a * fade;
    if (a < 0.003) discard;
    if (glow) {
        frag_color = vec4(v_tint.rgb * detail * v_glow * a, 0.0);
        return;
    }
    // lit like the world around it: sky and coloured block light from the volume, the sun where the sky is open
    vec3 world = v_rel + F.cam_pos.xyz;
    vec4 vl = vol_sample(world, false);
    float sky = vl.a;
    vec3 blk = block_curve(max(vl.rgb, dyn_sample(world)));
    float sun = F.light_color.w > 0.0 ? smoothstep(0.7, 0.95, sky) : 0.0;
    vec3 light = F.amb_sky.rgb * sky_curve(sky) + F.light_color.rgb * sun * 0.55 + blk;
    frag_color = vec4(v_tint.rgb * detail * light * a, a);
}
@end

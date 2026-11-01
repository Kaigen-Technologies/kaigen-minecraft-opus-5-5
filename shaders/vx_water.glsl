// forward liquids: depth-ramped colour, refraction, sky reflection, shoreline foam, pixel sparkles, voxel light
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
#include "vx_quad.glsl"

layout(location = 0) out vec3 v_rel;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec2 v_uv;
layout(location = 3) flat out uint v_flags;
layout(location = 4) flat out uint v_face;
layout(location = 5) flat out uint v_layer;
layout(location = 6) flat out uint v_bits;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    VxDraw dr = draws.d[ent.y];
    VxQuadVertex q = vx_quad_vertex(ent.x, vi & 3u, dr.origin);
    vec3 rel = dr.origin + q.local;
    vec3 world = rel + F.cam_pos.xyz;
    if ((q.flags & 8u) != 0u && q.face == 2u && fract(world.y) > 0.75) {
        float t = F.cam_pos.w;
        rel.y += (sin(world.x * 0.8 + t * 1.4) * sin(world.z * 0.7 + t * 1.1) - 1.0) * 0.025;
    }
    gl_Position = F.view_proj * vec4(rel, 1.0);
    v_rel = rel;
    v_world = rel + F.cam_pos.xyz;
    v_uv = q.uv;
    v_flags = q.flags;
    v_face = q.face;
    v_layer = q.layer;
    v_bits = q.bits;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2D scene_color_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D scene_depth_tex;
layout(set = 1, binding = 2) uniform texture3D vol0_tex;
layout(set = 1, binding = 3) uniform texture3D vol1_tex;
layout(set = 1, binding = 4) uniform texture3D dyn_tex;
@depth
layout(set = 1, binding = 5) uniform texture2D shadow_cmp_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#include "vx_sky.glsl"
#include "vx_lighting.glsl"
#include "vx_volume.glsl"

layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec2 v_uv;
layout(location = 3) flat in uint v_flags;
layout(location = 4) flat in uint v_face;
layout(location = 5) flat in uint v_layer;
layout(location = 6) flat in uint v_bits;
layout(location = 0) out vec4 frag_color;

float scene_depth_at(vec2 uv) { return texelFetch(sampler2D(scene_depth_tex, smp_clamp), uv_to_texel(uv), 0).r; }

// gentle waves on the art grid, as an xz slope
vec2 water_slope(vec2 p) {
    float t = F.cam_pos.w;
    vec2 q = floor(p * 8.0) / 8.0;
    float b = sin((q.x + q.y) * 2.1 + t * 2.3) * 0.5;
    return vec2(cos(q.x * 1.3 + t * 1.7) + b, cos(q.y * 1.1 - t * 1.3) - b) * 0.06;
}

// the shimmer: slow iridescent swirls that glow on their own, a pale rim at the shore, twinkling pixel sparkles
vec3 shimmer_color(vec3 wp, uint face, vec3 light, vec3 refr, float thick, float sh, vec3 N, vec3 V) {
    float t = F.cam_pos.w;
    vec2 p = face == 2u ? wp.xz : vec2(face < 2u ? wp.z : wp.x, wp.y);
    vec2 w = p * 0.07 + vec2(sin(p.y * 0.11 + t * 0.21), cos(p.x * 0.09 - t * 0.17)) * 0.8;
    float b1 = sin(w.x * 5.0 + sin(w.y * 3.7 + t * 0.3) * 1.7 + t * 0.25) * 0.5 + 0.5;
    float b2 = sin(w.y * 4.4 - sin(w.x * 3.1 - t * 0.22) * 1.9 - t * 0.18) * 0.5 + 0.5;
    vec3 c = mix(vec3(0.3, 0.17, 0.62), vec3(0.58, 0.34, 0.8), smoothstep(0.55, 0.98, b1) * 0.8);
    c = mix(c, vec3(0.24, 0.38, 0.88), smoothstep(0.7, 1.0, b2) * 0.45);
    vec3 col = c * (0.31 + 0.13 * min(light, vec3(1.5)));
    col = mix(col, vec3(0.95, 0.88, 1.0) * (0.8 + 0.3 * min(light, vec3(1.5))), (1.0 - smoothstep(0.0, 0.7, thick)) * 0.75);
    vec2 ap = floor(p * 8.0);
    float s = hash12(ap + vec2(17.0, 3.0));
    float tw = step(0.982, s) * (0.5 + 0.5 * sin(t * (2.0 + s * 3.0) + s * 60.0));
    col += mix(vec3(1.0, 0.92, 1.0), vec3(0.7, 0.95, 1.0), fract(s * 37.0)) * tw * 1.6;
    float g = pow(saturate(dot(reflect(-F.light_dir.xyz, N), V)), 40.0);
    col += F.light_color.rgb * sh * g * 0.3;
    return mix(refr, col, saturate(thick * 2.5 + 0.35));
}

void main() {
    bool is_water = (v_flags & 8u) != 0u;
    bool is_honey = (v_flags & 32u) != 0u;
    float dist = length(v_rel);
    vec3 V = -v_rel / dist;
    vec3 Ng = face_normal(v_face);
    vec3 N = Ng;
    if (v_face == 2u) {
        vec2 s = water_slope(v_world.xz) * (is_honey ? 0.3 : 1.0);
        N = normalize(vec3(-s.x, 1.0, -s.y));
    }
    bool below = !gl_FrontFacing;
    if (below) {
        N = -N;
        Ng = -Ng;
    }
    vec2 suv = gl_FragCoord.xy * F.res.zw;

    // refraction of the lit scene behind the surface
    float scene_depth_raw = texelFetch(sampler2D(scene_depth_tex, smp_clamp), ivec2(gl_FragCoord.xy), 0).r;
    vec3 scene_rel = reconstruct_rel(suv, scene_depth_raw);
    float thick0 = scene_depth_raw <= 0.0 ? 200.0 : max(0.0, length(scene_rel) - dist);
    vec2 refr_off = (N.xz - Ng.xz) * 0.9 / max(1.0, dist * 0.35) * saturate(thick0 * 0.8);
    vec2 ruv = clamp(suv + refr_off * vec2(F.res.y * F.res.z, 1.0) * 0.12, vec2(0.001), vec2(0.999));
    float r_depth_raw = scene_depth_at(ruv);
    vec3 r_rel = reconstruct_rel(ruv, r_depth_raw);
    if (r_depth_raw > 0.0 && length(r_rel) < dist) {
        ruv = suv;
        r_depth_raw = scene_depth_raw;
        r_rel = scene_rel;
    }
    vec3 refr = textureLod(sampler2D(scene_color_tex, smp_clamp), ruv, 0.0).rgb;
    float thick = r_depth_raw <= 0.0 ? 200.0 : max(0.0, length(r_rel) - dist);

    // light at the surface: sun through the shadow map, sky and coloured block light from the volume
    vec4 vl = vol_sample(v_world + vec3(0.0, 0.5, 0.0), false);
    float sky = vl.a;
    vec3 blk = block_curve(max(vl.rgb, dyn_sample(v_world + vec3(0.0, 0.5, 0.0))));
    float sh = F.light_color.w > 0.0 ? shadow_pcf(v_rel, Ng, dist, dot(Ng, F.light_dir.xyz), gl_FragCoord.xy) : 0.0;
    if (sh < 0.0) sh = smoothstep(0.75, 0.95, sky);
    sh *= smoothstep(0.05, 0.4, sky);
    vec3 light = F.amb_sky.rgb * sky_curve(sky) + F.light_color.rgb * sh * 0.5 + blk;

    vec3 shallow = is_honey ? vec3(1.0, 0.62, 0.12) : vec3(0.18, 0.72, 0.83);
    vec3 deep = is_honey ? vec3(0.55, 0.24, 0.02) : vec3(0.06, 0.2, 0.5);
    float depth_k = 1.0 - exp(-thick * (is_honey ? 0.9 : 0.22));
    vec3 body = mix(shallow, deep, depth_k) * light;
    vec3 color;
    if (below) {
        color = mix(refr, body, 0.6);
    } else {
        vec3 T = exp(-thick * (is_honey ? vec3(0.4, 0.9, 2.5) : vec3(0.45, 0.12, 0.08)));
        vec3 under = refr * T + body * (1.0 - T);
        vec3 R = reflect(-V, N);
        R.y = abs(R.y) + 0.02;
        vec3 refl = sky_gradient(normalize(R)) * mix(0.08, 1.0, sky_curve(sky));
        float NoV = saturate(dot(N, V));
        float fres = (is_honey ? 0.04 : 0.02) + 0.98 * pow(1.0 - NoV, 5.0);
        color = mix(under, refl, fres * (is_honey ? 0.4 : 1.0));
        if (v_face == 2u) {
            // shoreline foam in art pixels
            vec2 ap = floor(v_world.xz * 8.0);
            float foam = step(thick0, 0.35 + 0.25 * hash12(ap + floor(F.cam_pos.w * 2.0)));
            color = mix(color, light * vec3(0.95, 1.0, 1.0), foam * 0.7 * (is_honey ? 0.0 : 1.0));
            // pixel sparkles where the sun glints
            float g = pow(saturate(dot(reflect(-F.light_dir.xyz, N), V)), 60.0);
            float tw = step(0.93, hash12(ap + floor(F.cam_pos.w * 6.0) * 13.0));
            color += F.light_color.rgb * sh * g * tw * 3.0;
            // churning white water where a waterfall lands
            if ((v_bits & 2u) != 0u && !is_honey) {
                float churn = step(0.45, hash12(ap + floor(F.cam_pos.w * 9.0) * 7.0));
                color = mix(color, light * vec3(1.0, 1.04, 1.06) + 0.04, churn * 0.85);
            }
        }
        // a falling sheet: streaks of foam racing down in art pixels, brighter toward the bottom
        if ((v_bits & 1u) != 0u && !is_honey) {
            vec2 sp = vec2(v_face < 2u ? v_world.z : v_world.x, v_world.y);
            vec2 ap = floor(sp * 8.0);
            float col = hash12(vec2(ap.x, 3.0));
            float streak = step(0.7, fract(ap.y * 0.07 + F.cam_pos.w * (2.2 + col * 1.6) + col));
            vec3 sheet = mix(color, body * 1.25, 0.55);
            color = mix(sheet, light * vec3(0.9, 0.97, 1.0), streak * 0.5);
        }
    }
    if ((v_bits & 4u) != 0u) color = shimmer_color(v_world, v_face, light, refr, thick0, sh, N, V);
    if (vx_cut_away(v_world)) discard;
    frag_color = vec4(color, 1.0);
}
@end

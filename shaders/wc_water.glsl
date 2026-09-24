// forward water and ice: waves, refraction, ssr, fresnel, shoreline foam, total internal reflection
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
#include "wc_quad.glsl"

layout(location = 0) out vec3 v_rel;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_tint;
layout(location = 4) out vec2 v_light;
layout(location = 5) flat out uint v_flags;
layout(location = 6) flat out uint v_face;
layout(location = 7) flat out uint v_layer;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    WcDraw dr = draws.d[ent.y];
    WcQuadVertex q = wc_quad_vertex(ent.x, vi & 3u);
    vec3 rel = dr.origin + q.local;
    vec3 world = rel + F.cam_pos.xyz;
    uint tex_flags = q.tex_flags;
    uint flags = tex_flags >> 10u;
    uint face = q.face;
    if ((flags & 8u) != 0u && face == 2u && fract(world.y) > 0.5) {
        float t = F.cam_pos.w;
        rel.y += (sin(world.x * 0.8 + t * 1.4) * sin(world.z * 0.7 + t * 1.1) - 1.0) * 0.025;
    }
    gl_Position = F.view_proj * vec4(rel, 1.0);
    v_rel = rel;
    v_world = rel + F.cam_pos.xyz;
    v_uv = q.uv;
    v_tint = pow(q.tint, vec3(2.2));
    v_light = vec2(q.sky, q.block);
    v_flags = flags;
    v_face = face;
    v_layer = tex_flags & 1023u;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2D scene_color_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D scene_depth_tex;
layout(set = 1, binding = 2) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 3) uniform texture2D sky_sun_tex;
layout(set = 1, binding = 4) uniform texture2D sky_moon_tex;
layout(set = 1, binding = 5) uniform texture2D ambient_tex;
@depth
layout(set = 1, binding = 6) uniform texture2D shadow_cmp_tex;
@unfilterable
layout(set = 1, binding = 7) uniform texture2D shadow_raw_tex;
@aniso
layout(set = 1, binding = 16) uniform sampler smp_block;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#define WC_SHADOW_FULL
#include "wc_sky.glsl"
#include "wc_lighting.glsl"

layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_tint;
layout(location = 4) in vec2 v_light;
layout(location = 5) flat in uint v_flags;
layout(location = 6) flat in uint v_face;
layout(location = 7) flat in uint v_layer;
layout(location = 0) out vec4 frag_color;

float scene_depth_at(vec2 uv) { return texelFetch(sampler2D(scene_depth_tex, smp_clamp), uv_to_texel(uv), 0).r; }

#define DRAG 0.28
vec2 wavedx(vec2 position, vec2 direction, float frequency, float timeshift) {
    float x = dot(direction, position) * frequency + timeshift;
    float wave = exp(sin(x) - 1.0);
    return vec2(wave, -wave * cos(x));
}
float water_height(vec2 position, int iterations) {
    float t = F.cam_pos.w;
    float iter = 0.0;
    float frequency = 1.0;
    float time_mul = 1.6;
    float weight = 1.0;
    float sum_values = 0.0;
    float sum_weights = 0.0;
    for (int i = 0; i < 24; i++) {
        if (i >= iterations) break;
        vec2 p = vec2(sin(iter), cos(iter));
        vec2 res = wavedx(position, p, frequency, t * time_mul);
        position += p * res.y * weight * DRAG;
        sum_values += res.x * weight;
        sum_weights += weight;
        weight = mix(weight, 0.0, 0.2);
        frequency *= 1.18;
        time_mul *= 1.07;
        iter += 1232.399963;
    }
    return sum_values / sum_weights;
}
vec3 water_normal(vec2 p, float dist) {
    int it = dist < 24.0 ? 20 : dist < 64.0 ? 12 : 7;
    float e = 0.04;
    vec2 q = p * 0.55;
    float h = water_height(q, it);
    float hx = water_height(q + vec2(e, 0.0), it);
    float hz = water_height(q + vec2(0.0, e), it);
    float strength = 0.22 * mix(1.0, 0.35, saturate(dist / 120.0));
    return normalize(vec3((h - hx) * strength / e, 1.0, (h - hz) * strength / e));
}

vec3 ssr(vec3 p0, vec3 r, float jitter, out float hit_mask) {
    hit_mask = 0.0;
    if (F.settings.w < 0.5) return vec3(0.0);
    float step_len = 0.35 + jitter * 0.35;
    vec3 p = p0;
    float travelled = 0.0;
    for (int i = 0; i < 40; i++) {
        p += r * step_len;
        travelled += step_len;
        step_len *= 1.13;
        vec4 clip = F.view_proj * vec4(p, 1.0);
        if (clip.w <= 0.05) break;
        vec2 uv = clip_to_uv(clip);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float diff = clip.w - linear_depth(scene_depth_at(uv));
        if (diff > 0.02 && diff < max(0.6, step_len * 2.5)) {
            vec3 a = p - r * step_len;
            vec3 b = p;
            for (int k = 0; k < 5; k++) {
                vec3 m = (a + b) * 0.5;
                vec4 mc = F.view_proj * vec4(m, 1.0);
                if (mc.w > linear_depth(scene_depth_at(clip_to_uv(mc)))) b = m;
                else a = m;
            }
            vec2 huv = clip_to_uv(F.view_proj * vec4(b, 1.0));
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), huv) * smoothstep(vec2(1.0), vec2(0.92), huv);
            hit_mask = edge.x * edge.y * smoothstep(80.0, 40.0, travelled);
            if (scene_depth_at(huv) <= 0.0) hit_mask = 0.0;
            return textureLod(sampler2D(scene_color_tex, smp_clamp), huv, 0.0).rgb;
        }
    }
    return vec3(0.0);
}

void main() {
    // derivatives before any divergence (wgsl uniformity)
    vec2 duv_x = dFdx(v_uv);
    vec2 duv_y = dFdy(v_uv);
    bool is_water = (v_flags & 8u) != 0u;
    bool is_ice = (v_flags & 32u) != 0u;
    float dist = length(v_rel);
    vec3 V = -v_rel / dist;
    vec3 Ng = face_normal(v_face);
    vec3 N = Ng;
    if (is_water && v_face == 2u) N = water_normal(v_world.xz, dist);
    bool below = !gl_FrontFacing;
    if (below) {
        N = -N;
        Ng = -Ng;
    }
    vec2 suv = gl_FragCoord.xy * F.res.zw;
    float jitter = ign_t(gl_FragCoord.xy);
    float sky_l = sky_curve(v_light.x);

    // refraction
    float scene_depth_raw = texelFetch(sampler2D(scene_depth_tex, smp_clamp), ivec2(gl_FragCoord.xy), 0).r;
    vec3 scene_rel = reconstruct_rel(suv, scene_depth_raw);
    float thick0 = scene_depth_raw <= 0.0 ? 200.0 : max(0.0, length(scene_rel) - dist);
    vec2 refr_off = (N.xz - Ng.xz) * 0.9 / max(1.0, dist * 0.35) * saturate(thick0 * 0.8);
    if (is_ice) refr_off = N.xz * 0.01;
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

    vec3 L = F.light_dir.xyz;
    float thickness_unused;
    float sh = shadow_full(v_rel, Ng, dist, dot(Ng, L), gl_FragCoord.xy, thickness_unused);
    if (sh < 0.0) sh = smoothstep(0.75, 0.95, v_light.x);
    vec3 ambient = ambient_cube(vec3(0.0, 1.0, 0.0)) * sky_l + torch_color() * block_curve(v_light.y) * 0.5;

    vec3 color;
    if (is_water) {
        // absorption: red goes first; the biome tint shifts the balance slightly
        vec3 absorb = vec3(0.39, 0.085, 0.07) * mix(vec3(1.0), 1.2 - v_tint, 0.5);
        vec3 scatter_col = v_tint * v_tint * 0.9 * (ambient + F.light_color.rgb * sh * 0.06);
        if (!below) {
            vec3 T = exp(-absorb * thick);
            refr = refr * T + scatter_col * (1.0 - T);
        }
        vec3 R = reflect(-V, N);
        if (!below && R.y < 0.02) R.y = abs(R.y) + 0.02;
        vec3 refl = sky_reflect(R) * mix(0.15, 1.0, sky_l);
        float hit;
        vec3 ssr_col = ssr(v_rel + Ng * 0.02, R, jitter, hit);
        refl = mix(refl, ssr_col, hit);
        float NoV = saturate(dot(N, V));
        float fres = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
        if (below) {
            // under water looking up: total internal reflection beyond the critical angle
            float sin_t2 = (1.0 - NoV * NoV) * 1.33 * 1.33;
            fres = sin_t2 >= 1.0 ? 1.0 : mix(fres, 1.0, smoothstep(0.7, 1.0, sin_t2));
            refl = scatter_col * 1.5;
        }
        vec3 spec = specular_ggx(N, V, L, 0.06, vec3(0.02)) * F.light_color.rgb * sh * saturate(dot(N, L));
        color = mix(refr, refl, fres) + spec;
        // shoreline foam
        if (!below && v_face == 2u) {
            float foam = smoothstep(0.35, 0.0, thick0) * (0.5 + 0.5 * sin(v_world.x * 3.1 + v_world.z * 2.3 + F.cam_pos.w * 1.7));
            color += foam * 0.25 * (ambient + F.light_color.rgb * sh * saturate(L.y) / PI);
        }
    } else {
        vec3 tint = textureGrad(sampler2DArray(albedo_tex, smp_block), vec3(v_uv, float(v_layer)), duv_x, duv_y).rgb;
        vec3 T = exp(-(1.0 - tint) * 1.2 * min(thick, 3.0));
        vec3 body = refr * T * 0.9 + tint * ambient * 0.15;
        vec3 R = reflect(-V, N);
        vec3 refl = sky_reflect(R) * mix(0.15, 1.0, sky_l);
        float hit;
        vec3 ssr_col = ssr(v_rel + Ng * 0.02, R, jitter, hit);
        refl = mix(refl, ssr_col, hit);
        float NoV = saturate(dot(N, V));
        float fres = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
        vec3 diffuse = tint * (ambient + F.light_color.rgb * sh * saturate(dot(N, L)) / PI) * 0.35;
        color = mix(body + diffuse, refl, fres) + specular_ggx(N, V, L, 0.12, vec3(0.02)) * F.light_color.rgb * sh * saturate(dot(N, L));
    }
    frag_color = vec4(color, 1.0);
}
@end

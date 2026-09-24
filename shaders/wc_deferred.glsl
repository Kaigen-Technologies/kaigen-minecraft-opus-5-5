// deferred lighting: sun/moon with pcss shadows, subsurface, sky ambient, glossy ssr, rain, snow, caustics
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D galbedo_tex;
layout(set = 1, binding = 1) uniform texture2D gnormal_tex;
layout(set = 1, binding = 2) uniform texture2D glight_tex;
layout(set = 1, binding = 3) uniform texture2D gspec_tex;
@unfilterable
layout(set = 1, binding = 4) uniform texture2D depth_tex;
layout(set = 1, binding = 5) uniform texture2D ssao_tex;
layout(set = 1, binding = 6) uniform texture2D clouds_tex;
layout(set = 1, binding = 7) uniform texture2D weather_tex;
@unfilterable
layout(set = 1, binding = 8) uniform texture2D water_shadow_tex;
layout(set = 1, binding = 9) uniform texture2D prev_color_tex;
layout(set = 1, binding = 10) uniform texture2D sky_sun_tex;
layout(set = 1, binding = 11) uniform texture2D sky_moon_tex;
layout(set = 1, binding = 12) uniform texture2D ambient_tex;
@depth
layout(set = 1, binding = 13) uniform texture2D shadow_cmp_tex;
@unfilterable
layout(set = 1, binding = 14) uniform texture2D shadow_raw_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#define WC_SHADOW_FULL
#include "wc_sky.glsl"
#include "wc_lighting.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float depth_at(vec2 uv) { return texelFetch(sampler2D(depth_tex, smp_clamp), uv_to_texel(uv), 0).r; }

float cloud_shadow(vec3 world) {
    if (F.cloud.x <= 0.0) return 1.0;
    vec3 L = F.light_dir.xyz;
    if (L.y <= 0.02) return 1.0;
    float h = F.cloud.y + F.cloud.z * 0.35;
    vec3 p = world + L * ((h - world.y) / L.y);
    vec2 wuv = (p.xz + vec2(F.cloud.w * 9.0, F.cloud.w * 3.0)) / 12000.0;
    float cov = smoothstep(0.3, 0.72, textureLod(sampler2D(weather_tex, smp_linear), wuv, 0.0).r + (F.cloud.x - 0.42) * 0.9);
    return mix(1.0, 0.22, smoothstep(0.35, 0.8, cov));
}

float vnoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

// raindrop ripples: expanding rings in a jittered grid, as an xz normal perturbation
vec2 rain_ripples(vec2 p, float t) {
    vec2 acc = vec2(0.0);
    for (int layer = 0; layer < 2; layer++) {
        vec2 q = p * (layer == 0 ? 1.6 : 2.3) + float(layer) * 7.3;
        vec2 cell = floor(q);
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                vec2 c = cell + vec2(float(i), float(j));
                float h = hash12(c + float(layer) * 31.0);
                vec2 center = c + vec2(hash12(c * 1.7 + 3.1), hash12(c * 2.3 + 1.7));
                float phase = fract(t * (0.9 + h * 0.6) + h);
                vec2 d = q - center;
                float r = length(d);
                float ring = r - phase * 0.9;
                float w = exp(-ring * ring * 180.0) * (1.0 - phase) * step(r, 0.95);
                acc += (r > 1e-4 ? d / r : vec2(0.0)) * w * sin(ring * 60.0);
            }
        }
    }
    return acc * 0.5;
}

float caustic_pattern(vec2 p, float t) {
    vec2 i = p;
    float c = 1.0;
    float inten = 0.005;
    for (int n = 0; n < 5; n++) {
        float tt = t * (1.0 - (3.5 / float(n + 1)));
        i = p + vec2(cos(tt - i.x) + sin(tt + i.y), sin(tt - i.y) + cos(tt + i.x));
        c += 1.0 / length(vec2(p.x / (sin(i.x + tt) / inten), p.y / (cos(i.y + tt) / inten)));
    }
    c /= 5.0;
    c = 1.17 - pow(c, 1.4);
    return pow(abs(c), 8.0);
}

// light transmitted to an underwater point; 1 when not under water
vec3 underwater_light(vec3 rel, vec3 world, out float depth_in_water) {
    depth_in_water = 0.0;
    if (F.settings.x < 0.5) return vec3(1.0);
    vec3 s = shadow_coord(1, rel);
    if (any(lessThan(s.xy, vec2(0.002))) || any(greaterThan(s.xy, vec2(0.998))) || s.z <= 0.0) return vec3(1.0);
    float wd = texelFetch(sampler2D(water_shadow_tex, smp_clamp), ivec2(s.xy * F.settings.z), 0).r;
    if (wd <= s.z + 0.0002) return vec3(1.0);
    float d = (wd - s.z) * F.cascade_depth[1];
    depth_in_water = d;
    vec3 entry = world + F.light_dir.xyz * d;
    float t = F.cam_pos.w * 0.9;
    float c = caustic_pattern(entry.xz * 0.72 + vec2(3.1, 1.7), t) + caustic_pattern(entry.xz * 1.1 - vec2(1.3, 4.2), t * 1.15) * 0.6;
    float fade = smoothstep(0.05, 1.2, d);
    float caus = mix(1.0, 0.55 + min(c, 2.5) * 0.55, fade * exp(-d * 0.1));
    return exp(-vec3(0.39, 0.085, 0.07) * d * 1.2) * caus;
}

vec3 glossy_ssr(vec3 p0, vec3 r, float jitter, out float hit_mask) {
    hit_mask = 0.0;
    float step_len = 0.18 + jitter * 0.18;
    vec3 p = p0;
    float travelled = 0.0;
    for (int i = 0; i < 28; i++) {
        p += r * step_len;
        travelled += step_len;
        step_len *= 1.16;
        vec4 clip = F.view_proj * vec4(p, 1.0);
        if (clip.w <= 0.05) break;
        vec2 uv = clip_to_uv(clip);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float diff = clip.w - linear_depth(depth_at(uv));
        if (diff > 0.01 && diff < max(0.35, step_len * 2.0)) {
            vec3 a = p - r * step_len;
            vec3 b = p;
            for (int k = 0; k < 4; k++) {
                vec3 m = (a + b) * 0.5;
                vec4 mc = F.view_proj * vec4(m, 1.0);
                if (mc.w > linear_depth(depth_at(clip_to_uv(mc)))) b = m;
                else a = m;
            }
            // reproject the hit into the previous frame's resolved image
            vec2 puv = clip_to_uv(F.prev_view_proj * vec4(b + F.cam_delta.xyz, 1.0));
            vec2 edge = smoothstep(vec2(0.0), vec2(0.07), puv) * smoothstep(vec2(1.0), vec2(0.93), puv);
            hit_mask = edge.x * edge.y * smoothstep(40.0, 20.0, travelled);
            return textureLod(sampler2D(prev_color_tex, smp_clamp), puv, 0.0).rgb;
        }
    }
    return vec3(0.0);
}

vec3 sky_background(vec3 dir, vec2 uv) {
    // below the horizon keep the horizon haze so the world edge melts away
    vec3 col = horizon_color(dir);
    float horizon_fade = smoothstep(-0.02, 0.08, dir.y);
    float night = F.sky.w;
    if (night > 0.0) col += star_field(dir) * night * horizon_fade * 0.05;
    col += moon_disk(dir) * F.sky.y * 900.0 * smoothstep(-0.03, 0.03, dir.y) * 0.02;
    // sun disk with limb darkening
    float c = dot(dir, F.sun_dir.xyz);
    float sun_r = 0.0085;
    float r = sqrt(max(0.0, 2.0 * (1.0 - c)));
    if (r < sun_r * 1.05) {
        float mu = sqrt(max(0.0, 1.0 - sq(r / sun_r)));
        float limb = 1.0 - 0.6 * (1.0 - pow(mu, 0.8));
        float disk = smoothstep(sun_r * 1.05, sun_r * 0.95, r);
        vec3 sun_rad = min(F.sun_color.rgb / (PI * sun_r * sun_r), vec3(6000.0));
        col += sun_rad * disk * limb * smoothstep(-0.02, 0.01, dir.y);
    }
    vec4 cl = textureLod(sampler2D(clouds_tex, smp_clamp), uv, 0.0);
    return col * cl.a + cl.rgb;
}

// half-res ao at this pixel: bilinear across its neighbours, the best depth and normal match where one disagrees
float ssao_at(ivec2 q, float lin, vec3 ng) {
    ivec2 o = half_res_offset();
    ivec2 hmax = (ivec2(F.res.xy) + 1) / 2 - 1;
    ivec2 d = q - o;
    ivec2 base = ivec2(floor(vec2(d) * 0.5));
    vec2 fr = vec2(d - base * 2) * 0.5;
    float acc = 0.0;
    float nearest = 1.0;
    float best = 1e30;
    bool edge = false;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            vec4 s = texelFetch(sampler2D(ssao_tex, smp_clamp), clamp(base + ivec2(i, j), ivec2(0), hmax), 0);
            float w = (i == 0 ? 1.0 - fr.x : fr.x) * (j == 0 ? 1.0 - fr.y : fr.y);
            float err = abs(s.g - lin) / lin * 10.0 + (1.0 - dot(oct_decode(s.ba), ng));
            if (err < best) {
                best = err;
                nearest = s.r;
            }
            if (w > 0.0 && err > 0.1) edge = true;
            acc += s.r * w;
        }
    return edge ? nearest : acc;
}

void main() {
    vec2 uv = v_uv;
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(sampler2D(depth_tex, smp_clamp), px, 0).r;
    if (depth <= 0.0) {
        frag_color = vec4(sky_background(view_ray(uv), uv), 1.0);
        return;
    }
    vec3 rel = reconstruct_rel(uv, depth);
    float dist = length(rel);
    vec3 V = -rel / dist;
    vec3 world = rel + F.cam_pos.xyz;

    vec3 albedo = texelFetch(sampler2D(galbedo_tex, smp_clamp), px, 0).rgb;
    vec4 nrm = texelFetch(sampler2D(gnormal_tex, smp_clamp), px, 0);
    vec3 N = oct_decode(nrm.xy);
    vec3 Ng = oct_decode(nrm.zw);
    vec4 lm = texelFetch(sampler2D(glight_tex, smp_clamp), px, 0);
    float sky_l = lm.r;
    float block_l = lm.g;
    float vao = lm.b;
    int flags = int(lm.a * 255.0 + 0.5);
    bool plant = (flags & 64) != 0;
    bool leaves = (flags & 2) != 0;
    bool hand = (flags & 128) != 0;
    vec4 sp = texelFetch(sampler2D(gspec_tex, smp_clamp), px, 0);
    float smoothness = sp.r;
    float metal = sp.g;
    float sss = sp.b;
    float emissive = sp.a;

    // rain: wet, darker surfaces with puddles on exposed flat ground
    float wet = F.weather.y * smoothstep(0.8, 0.97, sky_l) * (plant || leaves || hand ? 0.0 : 1.0);
    if (wet > 0.001) {
        float porous = (1.0 - metal) * (1.0 - smoothness);
        albedo *= mix(1.0, 0.58, wet * porous);
        smoothness = mix(smoothness, 0.78, wet * 0.75);
        if (Ng.y > 0.9) {
            float pn = vnoise2(world.xz * 0.21) * 0.65 + vnoise2(world.xz * 0.63 + 11.0) * 0.35;
            float puddle = smoothstep(0.5, 0.6, pn + (1.0 - F.weather.y) * 0.4) * wet;
            if (puddle > 0.0) {
                vec2 rip = rain_ripples(world.xz, F.cam_pos.w) * F.weather.x;
                N = normalize(mix(N, normalize(vec3(rip.x, 1.0, rip.y)), puddle));
                smoothness = mix(smoothness, 0.97, puddle);
                albedo *= mix(1.0, 0.75, puddle);
            }
        }
    }
    // snow cover on exposed upward-facing surfaces
    if (F.extra.y > 0.001 && !hand) {
        float up = plant ? 0.8 : leaves ? smoothstep(-0.2, 0.6, Ng.y) : smoothstep(0.55, 0.95, Ng.y);
        float grain = vnoise2(world.xz * 2.7) * 0.3 + 0.7;
        float cover = saturate(F.extra.y * 1.25 * grain - (1.0 - smoothstep(0.85, 0.98, sky_l))) * up;
        albedo = mix(albedo, vec3(0.86, 0.9, 0.95), cover);
        smoothness = mix(smoothness, 0.35, cover);
        sss = mix(sss, 0.5, cover);
        metal *= 1.0 - cover;
        N = normalize(mix(N, Ng, cover * 0.7));
    }
    float roughness = sq(1.0 - smoothness);

    vec3 L = F.light_dir.xyz;
    float NoL = dot(N, L);
    float NgoL = dot(Ng, L);
    float NoV = saturate(dot(N, V));

    float thickness = 0.0;
    float shadow = 0.0;
    bool wants_light = F.light_color.w > 0.0 && (NgoL > 0.0 || sss > 0.05 || plant);
    if (wants_light) {
        shadow = shadow_full(rel, plant ? vec3(0.0, 1.0, 0.0) : Ng, dist, plant ? 1.0 : NgoL, gl_FragCoord.xy, thickness);
        if (shadow < 0.0) {
            shadow = smoothstep(0.78, 0.95, sky_l);
            thickness = shadow > 0.5 ? 0.0 : 4.0;
        }
        shadow *= cloud_shadow(world);
    }
    float water_depth;
    vec3 water_t = underwater_light(rel, world, water_depth);
    // light never reaches surfaces cut off from the sky (caves beyond the shadow range)
    shadow *= smoothstep(0.02, 0.3, sky_l);

    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse_color = albedo * (1.0 - metal);
    vec3 light_col = F.light_color.rgb;

    float NoLc = saturate(NoL);
    if (plant) NoLc = 0.45 + 0.55 * saturate(L.y * 0.8 + 0.2);
    vec3 direct = (diffuse_color / PI + specular_ggx(N, V, L, max(roughness, 0.03), F0) * (leaves || plant ? 0.25 : 1.0)) *
                  NoLc * light_col * shadow * water_t;

    // subsurface transmission (leaves, grass, snow)
    if (sss > 0.05 && F.light_color.w > 0.0) {
        float VoL = dot(-V, L);
        float trans = exp(-thickness * (leaves ? 0.9 : 1.6)) * smoothstep(0.02, 0.3, sky_l);
        float forward = pow(saturate(VoL), 5.0) * 2.2 + 0.3;
        float back = NgoL < 0.0 || plant ? 1.0 : 0.35;
        direct += diffuse_color * light_col * sss * trans * forward * back / PI * cloud_shadow(world);
    }

    // ambient
    float ssao = F.settings.y > 0.5 && !hand ? ssao_at(px, linear_depth(depth), Ng) : 1.0;
    float occ = ao_curve(vao) * ssao;
    float sky_f = sky_curve(sky_l);
    vec3 ambient_n = plant ? mix(ambient_cube(N), ambient_cube(vec3(0.0, 1.0, 0.0)), 0.5) : ambient_cube(N);
    vec3 under_tint = water_depth > 0.0 ? mix(vec3(1.0), vec3(0.45, 0.75, 0.85), saturate(water_depth * 0.3)) : vec3(1.0);
    vec3 amb_diffuse = diffuse_color * ambient_n * sky_f * occ * under_tint;
    // specular ambient (sky reflection)
    vec3 R = reflect(-V, N);
    float spec_occ = saturate(pow(NoV + occ, exp2(-16.0 * roughness - 1.0)) - 1.0 + occ);
    vec3 env_spec = sky_reflect(R) * sky_f;
    if (smoothness > 0.55 && !hand && F.settings.w > 0.5) {
        float hit;
        vec3 ssr_col = glossy_ssr(rel + Ng * 0.03, R, ign_t(gl_FragCoord.xy), hit);
        env_spec = mix(env_spec, ssr_col, hit * smoothstep(0.55, 0.8, smoothness));
    }
    // rough organic surfaces are heavily micro-shadowed; smooth and metal keep the sky sheen
    float rough_fade = mix(1.0, sq(1.0 - roughness), 0.85);
    // foliage is a jumble of tiny leaves: almost no coherent sky reflection
    float foliage_spec = plant ? 0.0 : leaves ? 0.08 : 1.0;
    vec3 amb_spec = env_spec * env_brdf_approx(F0, roughness, NoV) * spec_occ * rough_fade * foliage_spec;

    // block light (torches, lava, glowstone)
    vec3 block_col = torch_color() * block_curve(block_l);
    vec3 block_diffuse = diffuse_color * block_col * ao_curve(vao) * mix(1.0, ssao, 0.6);
    vec3 block_spec = block_col * env_brdf_approx(F0, roughness, NoV) * 0.3 * ao_curve(vao);

    // tiny minimum so caves are not pure black
    vec3 min_amb = diffuse_color * vec3(0.006, 0.007, 0.01) * occ;
    vec3 emit = albedo * emissive * emissive * 5.0;

    vec3 color = direct + amb_diffuse + amb_spec + block_diffuse + block_spec + min_amb + emit;
    int debug_view = int(F.extra.w + 0.5);
    if (debug_view > 0) {
        if (debug_view == 1) color = albedo;
        else if (debug_view == 2) color = N * 0.5 + 0.5;
        else if (debug_view == 3) color = vec3(shadow);
        else if (debug_view == 4) color = vec3(occ);
        else if (debug_view == 5) color = vec3(sky_l, block_l, 0.0);
        else if (debug_view == 6) color = vec3(ssao);
        else if (debug_view == 7) color = direct;
        else if (debug_view == 8) color = amb_diffuse;
    }
    frag_color = vec4(color, 1.0);
}
@end

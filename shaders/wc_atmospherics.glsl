// height fog, aerial perspective, volumetric light shafts, underwater fog, rain and snow
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D color_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D depth_tex;
layout(set = 1, binding = 2) uniform texture2D sky_sun_tex;
layout(set = 1, binding = 3) uniform texture2D sky_moon_tex;
layout(set = 1, binding = 4) uniform texture2D ambient_tex;
@unfilterable
layout(set = 1, binding = 5) uniform texture2D shafts_tex;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;

#include "wc_sky.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float fog_density(float y) { return F.fog.z * exp(-max(y - 62.0, 0.0) / F.fog.w); }

float snow_layer(vec3 dir, float scene_dist, float radius, float seed) {
    float hl = length(dir.xz);
    if (hl < 1e-3) return 0.0;
    float t = radius / hl;
    if (t > scene_dist) return 0.0;
    vec3 p = dir * t;
    float ang = atan(p.z, p.x);
    float y = p.y + F.cam_pos.y;
    float time = F.cam_pos.w;
    vec2 g = vec2(ang * radius * 1.3, y * 0.8 + time * (0.45 + seed * 0.05));
    vec2 cell = floor(g);
    float h = hash12(cell + seed * 13.0);
    if (h < 0.55) return 0.0;
    g.x += sin(time * (0.7 + h) + h * 30.0) * 0.25;
    vec2 f = fract(g) - 0.5;
    vec2 c = (vec2(hash12(cell * 1.9 + seed), hash12(cell * 2.7 - seed)) - 0.5) * 0.5;
    return smoothstep(0.14, 0.02, length(f - c)) * (0.6 + 0.4 * h);
}

float rain_layer(vec3 dir, float scene_dist, float radius, float seed) {
    float hl = length(dir.xz);
    if (hl < 1e-3) return 0.0;
    float t = radius / hl;
    if (t > scene_dist) return 0.0;
    vec3 p = dir * t;
    float ang = atan(p.z, p.x);
    float y = p.y + F.cam_pos.y;
    vec2 g = vec2(ang * radius * 2.2, y * 0.35 + F.cam_pos.w * (2.6 + seed * 0.3));
    vec2 cell = floor(g);
    vec2 f = fract(g);
    float h = hash12(cell + seed * 17.0);
    if (h < 0.45) return 0.0;
    float x = hash12(cell * 1.3 + seed);
    float streak = smoothstep(0.07, 0.0, abs(f.x - x)) * smoothstep(0.0, 0.08, f.y) * smoothstep(0.55, 0.15, f.y);
    return streak * (0.5 + 0.5 * h);
}

// half-res march at this pixel: bilinear across its neighbours, the nearest march length where they disagree
vec2 shafts_at(ivec2 q, float march) {
    ivec2 o = half_res_offset();
    ivec2 hmax = (ivec2(F.res.xy) + 1) / 2 - 1;
    ivec2 d = q - o;
    ivec2 base = ivec2(floor(vec2(d) * 0.5));
    vec2 fr = vec2(d - base * 2) * 0.5;
    vec2 acc = vec2(0.0);
    vec2 nearest = vec2(0.0);
    float best = 1e30;
    bool edge = false;
    float tol = max(march, 0.5) * 0.1;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            vec3 s = texelFetch(sampler2D(shafts_tex, smp_clamp), clamp(base + ivec2(i, j), ivec2(0), hmax), 0).rgb;
            float w = (i == 0 ? 1.0 - fr.x : fr.x) * (j == 0 ? 1.0 - fr.y : fr.y);
            float err = abs(s.b - march);
            if (err < best) {
                best = err;
                nearest = s.rg;
            }
            if (w > 0.0 && err > tol) edge = true;
            acc += s.rg * w;
        }
    return edge ? nearest : acc;
}

// optical depth of the height fog between distances t0 and t1 along dir
float fog_od(vec3 dir, float t0, float t1) {
    float y0 = F.cam_pos.y + dir.y * t0;
    float len = max(t1 - t0, 0.0);
    float k = 1.0 / F.fog.w;
    float d0 = fog_density(y0);
    float dy = dir.y;
    if (abs(dy) < 1e-3 || y0 < 62.0) return d0 * len;
    return max(d0 * (1.0 - exp(-dy * len * k)) / (dy * k), 0.0);
}

void main() {
    vec2 uv = v_uv;
    vec3 color = textureLod(sampler2D(color_tex, smp_clamp), uv, 0.0).rgb;
    float depth = texelFetch(sampler2D(depth_tex, smp_clamp), ivec2(gl_FragCoord.xy), 0).r;
    bool sky = depth <= 0.0;
    vec3 dir = view_ray(uv);
    float dist = sky ? 1e5 : length(reconstruct_rel(uv, depth));
    vec3 L = F.light_dir.xyz;
    float eye_sky = F.world.y;

    if (F.world.x > 0.5) {
        // under water: absorption, in-scattering and light shafts from the surface
        vec3 water_col = vec3(0.1, 0.35, 0.45);
        vec3 absorb = vec3(0.32, 0.075, 0.06);
        float d = min(dist, 96.0);
        vec3 T = exp(-absorb * d * 1.1);
        vec3 amb = ambient_cube(vec3(0.0, 1.0, 0.0)) * (0.3 + 0.7 * eye_sky) + F.light_color.rgb * 0.05;
        vec3 inscatter = water_col * amb * 0.5;
        float shafts = shafts_at(ivec2(gl_FragCoord.xy), min(dist, 32.0)).x * hg_phase(dot(dir, L), 0.55) * 1.4;
        frag_color = vec4(color * T + (inscatter + water_col * F.light_color.rgb * shafts * 0.12) * (1.0 - T), 1.0);
        return;
    }

    // ambient haze takes the colour of the horizon sky (fog and sky always agree); the sun adds glow and shafts
    vec3 haze = horizon_color(dir);
    float cos_t = dot(dir, L);
    float phase = mix(hg_phase(cos_t, 0.72), hg_phase(cos_t, -0.1), 0.3);
    vec3 sun_fog = F.light_color.rgb * phase;
    vec3 amb_fog = haze * eye_sky;

    // near field: shadowed volumetric march (god rays), marched at half res
    float max_d = min(dist, F.fog.x);
    vec3 inscatter = vec3(0.0);
    float trans = 1.0;
    if (F.weather.w > 0.5) {
        vec2 m = shafts_at(ivec2(gl_FragCoord.xy), max_d);
        trans = m.y;
        inscatter = sun_fog * m.x + amb_fog * (1.0 - trans);
    } else {
        max_d = 0.0;
    }

    // far field: analytic height fog; below-horizon sky pixels stand in for terrain beyond the world
    float horizon_end = mix(F.fog.y, 3000.0, smoothstep(-0.02, 0.08, dir.y));
    float end_d = sky ? horizon_end : dist;
    vec3 s_far = sun_fog * 0.25 * smoothstep(0.3, 0.9, eye_sky) + amb_fog;
    float Tr = exp(-fog_od(dir, max_d, end_d));
    vec3 result = color * trans * Tr + inscatter + trans * s_far * (1.0 - Tr);

    // world edge: blend terrain into exactly what a sky pixel in this direction shows
    if (!sky) {
        float horiz = length((dir * dist).xz);
        float edge = smoothstep(F.fog.y * 0.8, F.fog.y * 0.98, horiz);
        if (edge > 0.0) {
            float TrE = exp(-fog_od(dir, max_d, horizon_end));
            vec3 sky_look = haze * trans * TrE + inscatter + trans * s_far * (1.0 - TrE);
            result = mix(result, sky_look, edge);
        }
    }
    // rain streaks and snowflakes (only when the camera is outdoors)
    if (F.weather.x > 0.01 && eye_sky > 0.3) {
        float outdoor = smoothstep(0.3, 0.8, eye_sky);
        float snow = F.extra.x;
        if (snow < 0.99) {
            float r = rain_layer(dir, dist, 1.6, 1.0);
            r += rain_layer(dir, dist, 3.1, 2.0) * 0.8;
            r += rain_layer(dir, dist, 5.5, 3.0) * 0.6;
            r += rain_layer(dir, dist, 9.0, 4.0) * 0.4;
            vec3 rain_col = ambient_cube(vec3(0.0, 1.0, 0.0)) * 1.4 + F.light_color.rgb * 0.05;
            result = mix(result, rain_col, saturate(r * 0.22 * F.weather.x * outdoor * (1.0 - snow)));
        }
        if (snow > 0.01) {
            float f = snow_layer(dir, dist, 1.4, 1.0);
            f += snow_layer(dir, dist, 2.6, 2.0) * 0.85;
            f += snow_layer(dir, dist, 4.5, 3.0) * 0.7;
            f += snow_layer(dir, dist, 7.5, 4.0) * 0.5;
            f += snow_layer(dir, dist, 12.0, 5.0) * 0.35;
            vec3 snow_col = ambient_cube(vec3(0.0, 1.0, 0.0)) * 2.2 + F.light_color.rgb * 0.08;
            result = mix(result, snow_col, saturate(f * 0.75 * F.weather.x * outdoor * snow));
        }
    }
    frag_color = vec4(result, 1.0);
}
@end

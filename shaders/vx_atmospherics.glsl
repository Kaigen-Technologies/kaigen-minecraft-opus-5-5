// biome fog (distance + height), underground fog, light shafts, underwater fog, rain and snow
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D color_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D depth_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D shafts_tex;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;

#include "vx_sky.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

// surface fog thins with height above sea level
float fog_density(float y) { return F.fog.x * exp(-max(y - 140.0, 0.0) * F.fog.y); }

// optical depth of the height fog between distances t0 and t1 along dir
float fog_od(vec3 dir, float t0, float t1) {
    float y0 = F.cam_pos.y + dir.y * t0;
    float len = max(t1 - t0, 0.0);
    float k = F.fog.y;
    float d0 = fog_density(y0);
    if (abs(dir.y) < 1e-3 || y0 < 140.0 || k <= 0.0) return d0 * len;
    return max(d0 * (1.0 - exp(-dir.y * len * k)) / (dir.y * k), 0.0);
}

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
    // square flakes: Terraria's snow is pixel art
    vec2 d = abs(f - c);
    return step(max(d.x, d.y), 0.09) * (0.6 + 0.4 * h);
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
    float streak = step(abs(f.x - x), 0.05) * step(0.08, f.y) * step(f.y, 0.5);
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

void main() {
    vec2 uv = v_uv;
    vec4 scene = textureLod(sampler2D(color_tex, smp_clamp), uv, 0.0);
    vec3 color = scene.rgb;
    float depth = texelFetch(sampler2D(depth_tex, smp_clamp), ivec2(gl_FragCoord.xy), 0).r;
    bool sky = depth <= 0.0;
    bool ring = sky && scene.a < 0.75;
    vec3 dir = view_ray(uv);
    float dist = sky ? 1e5 : length(reconstruct_rel(uv, depth));
    // in the cutaway the air between the camera and the plane is not there: every medium starts at the plane
    float t0 = sky ? vx_cut_start(dir) : min(vx_cut_start(dir), dist);
    float air = dist - t0;
    vec3 L = F.light_dir.xyz;

    if (F.world.x > 0.5) {
        // under water: absorption toward a deep teal, the volume's light carried by the ambient
        vec3 water_col = vec3(0.06, 0.3, 0.42);
        vec3 absorb = vec3(0.3, 0.08, 0.06);
        float d = min(air, 80.0);
        vec3 T = exp(-absorb * d);
        vec3 amb = F.amb_sky.rgb * (0.25 + 0.75 * F.world.y) + F.light_color.rgb * 0.04;
        float shafts = F.weather.w > 0.5 ? shafts_at(ivec2(gl_FragCoord.xy), min(air, 32.0)).x : 0.0;
        frag_color = vec4(color * T + (water_col * amb * 0.6 + water_col * F.light_color.rgb * shafts * 0.12) * (1.0 - T), 1.0);
        return;
    }

    // surface fog takes the horizon's colour, tinted by the biome; light shafts add sun-coloured in-scatter
    vec3 haze = mix(sky_haze(dir), F.fog_color.rgb, 0.35);
    float horizon_end = mix(F.fog.z, 3000.0, smoothstep(-0.02, 0.08, dir.y));
    float end_d = sky ? horizon_end : dist;
    vec3 result = color;
    float march = 0.0;
    float Ts = 1.0;
    vec3 inscatter = vec3(0.0);
    if (F.weather.w > 0.5 && !sky) {
        march = min(air, 96.0);
        vec2 m = shafts_at(ivec2(gl_FragCoord.xy), march);
        Ts = m.y;
        float phase = 0.6 + 2.2 * pow(saturate(dot(dir, L)), 6.0);
        inscatter = F.light_color.rgb * m.x * phase * 0.4 + haze * (1.0 - Ts);
    }
    // the sky gradient already paints the atmosphere above the horizon band: fog there would only wash it out
    float sky_clear = smoothstep(0.0, 0.15, dir.y);
    float Tr = ring ? 1.0 : sky ? mix(exp(-fog_od(dir, t0 + march, end_d)), 1.0, sky_clear) : exp(-fog_od(dir, t0 + march, end_d));
    vec3 surface = result * Ts * Tr + inscatter + Ts * haze * (1.0 - Tr);
    // aerial perspective at any height: past near range the land fades toward the horizon's blue, as in S1 and S10
    if (!sky) {
        float Ta = exp(-max(air - 80.0, 0.0) * 0.0040);
        surface = mix(mix(sky_haze(dir), F.sky_zenith.rgb, 0.7), surface, Ta);
    }
    // world edge: terrain blends into exactly what a sky pixel in this direction shows
    if (!sky) {
        float horiz = length((dir * dist).xz);
        float edge = smoothstep(F.fog.z * 0.8, F.fog.z * 0.98, horiz);
        if (edge > 0.0) {
            float TrE = mix(exp(-fog_od(dir, t0 + march, horizon_end)), 1.0, sky_clear);
            vec3 sky_look = haze * Ts * TrE + inscatter + Ts * haze * (1.0 - TrE);
            surface = mix(surface, sky_look, edge);
        }
    }
    // underground: a dark, biome-tinted fog that eats the distance
    float ug = F.fog_color.w;
    if (ug > 0.0) {
        // underground nothing of the sky shows: open edges of the world fade into the fog
        float Tu = sky ? 0.0 : exp(-air * F.fog.w);
        vec3 under = color * Tu + F.under_color.rgb * (1.0 - Tu);
        surface = mix(surface, under, ug);
    }
    result = surface;

    // rain streaks and snowflakes (only when the camera is outdoors)
    float eye_sky = F.world.y;
    if (F.weather.x > 0.01 && eye_sky > 0.3) {
        float outdoor = smoothstep(0.3, 0.8, eye_sky);
        float snow = F.extra.x;
        if (snow < 0.99) {
            float r = rain_layer(dir, dist, 1.6, 1.0);
            r += rain_layer(dir, dist, 3.1, 2.0) * 0.8;
            r += rain_layer(dir, dist, 5.5, 3.0) * 0.6;
            r += rain_layer(dir, dist, 9.0, 4.0) * 0.4;
            vec3 rain_col = F.amb_sky.rgb * 1.2 + F.light_color.rgb * 0.05;
            result = mix(result, rain_col, saturate(r * 0.22 * F.weather.x * outdoor * (1.0 - snow)));
        }
        if (snow > 0.01) {
            float f = snow_layer(dir, dist, 1.4, 1.0);
            f += snow_layer(dir, dist, 2.6, 2.0) * 0.85;
            f += snow_layer(dir, dist, 4.5, 3.0) * 0.7;
            f += snow_layer(dir, dist, 7.5, 4.0) * 0.5;
            f += snow_layer(dir, dist, 12.0, 5.0) * 0.35;
            vec3 snow_col = F.amb_sky.rgb * 1.8 + F.light_color.rgb * 0.15;
            result = mix(result, snow_col, saturate(f * 0.75 * F.weather.x * outdoor * snow));
        }
    }
    frag_color = vec4(result, 1.0);
}
@end

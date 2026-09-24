// volumetric clouds (Schneider/Horizon style), raymarched for one texel per 2x2 block of the cloud target
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _CloudParams {
    vec4 low_res; // cloud target w, h
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture3D noise_base_tex;
layout(set = 1, binding = 1) uniform texture3D noise_detail_tex;
layout(set = 1, binding = 2) uniform texture2D weather_tex;
layout(set = 1, binding = 3) uniform texture2D sky_sun_tex;
layout(set = 1, binding = 4) uniform texture2D sky_moon_tex;
layout(set = 1, binding = 5) uniform texture2D ambient_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
#include "wc_sky.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

#define EARTH_R 6360000.0

vec2 wind_offset() { return vec2(F.cloud.w * 9.0, F.cloud.w * 3.0); }
float remap(float v, float lo, float hi, float nlo, float nhi) { return nlo + (v - lo) * (nhi - nlo) / (hi - lo); }

float height_gradient(float h, float type) {
    // stratocumulus -> cumulus profile
    float top = mix(0.45, 1.0, type);
    return smoothstep(0.0, 0.08, h) * smoothstep(top, top * 0.55, h);
}

float cloud_density(vec3 p, float h, bool detail, float lod) {
    vec2 wuv = (p.xz + wind_offset()) / 12000.0;
    vec2 wm = textureLod(sampler2D(weather_tex, smp_linear), wuv, 0.0).rg;
    float cov = smoothstep(0.3, 0.72, wm.r + (F.cloud.x - 0.42) * 0.9);
    if (cov <= 0.01) return 0.0;
    float grad = height_gradient(h, wm.g);
    if (grad <= 0.0) return 0.0;
    vec3 q = (p + vec3(wind_offset().x, 0.0, wind_offset().y) * 1.4 + vec3(0.0, F.cloud.w * 2.0, 0.0)) * vec3(0.00017, 0.00024, 0.00017);
    vec4 n = textureLod(sampler3D(noise_base_tex, smp_linear), q, lod);
    float fbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = remap(n.r, fbm - 1.0, 1.0, 0.0, 1.0) * grad;
    float shape = saturate(remap(base, 1.0 - cov * 0.62, 1.0, 0.0, 1.0)) * cov;
    if (shape <= 0.0) return 0.0;
    if (detail) {
        vec3 dq = p * 0.0036 + vec3(F.cloud.w * 0.012, F.cloud.w * -0.006, 0.0);
        vec3 d = textureLod(sampler3D(noise_detail_tex, smp_linear), dq, 0.0).rgb;
        float dfbm = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
        float modulation = mix(dfbm, 1.0 - dfbm, saturate(h * 5.0));
        shape = remap(shape, modulation * 0.42, 1.0, 0.0, 1.0);
    }
    return max(shape, 0.0);
}

// spherical cloud shell at altitude h (earth curvature makes the horizon right)
vec2 shell_hit(float y, vec3 rd, float h) {
    float b = (EARTH_R + y) * rd.y;
    float cc = (y - h) * (2.0 * EARTH_R + y + h);
    float disc = b * b - cc;
    if (disc < 0.0) return vec2(-1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

void main() {
    ivec2 ct = min(ivec2(gl_FragCoord.xy) * 2 + half_res_offset(), ivec2(P.low_res.xy) - 1);
    vec3 dir = view_ray((vec2(ct) + 0.5) / P.low_res.xy);
    float base = F.cloud.y;
    float top = F.cloud.y + F.cloud.z;
    if (F.cloud.x <= 0.0) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 ro = vec3(0.0, F.cam_pos.y, 0.0);
    vec2 hb = shell_hit(ro.y, dir, base);
    vec2 ht = shell_hit(ro.y, dir, top);
    float t0;
    float t1;
    if (ro.y < base) {
        if (dir.y < -0.05) {
            frag_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        t0 = hb.y;
        t1 = ht.y;
    } else if (ro.y > top) {
        if (ht.x < 0.0) {
            frag_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        t0 = ht.x;
        t1 = hb.x > 0.0 ? hb.x : ht.y;
    } else {
        t0 = 0.0;
        t1 = hb.x > 0.0 ? hb.x : ht.y;
    }
    t1 = min(t1, t0 + 12000.0);
    if (t1 <= t0 || t0 > 60000.0) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    bool sun_lit = F.sun_dir.y > -0.05;
    vec3 L = sun_lit ? F.sun_dir.xyz : F.moon_dir.xyz;
    vec3 light_col = sun_lit ? F.sun_color.rgb : F.moon_color.rgb;
    float cos_t = dot(dir, L);
    float jitter = ign_t(vec2(ct));
    float span = t1 - t0;
    float dt = span / 56.0;
    float t = t0 + dt * jitter;
    float T = 1.0;
    vec3 scat = vec3(0.0);
    float sigma = 0.06;
    vec3 amb_top = ambient_cube(vec3(0.0, 1.0, 0.0)) * 1.15;
    vec3 amb_bot = ambient_cube(vec3(0.0, -1.0, 0.0)) * 0.7 + amb_top * 0.15;
    float wsum = 0.0;
    float tsum = 0.0;
    for (int i = 0; i < 56; i++) {
        vec3 p = ro + dir * t;
        float alt = p.y + dot(p.xz, p.xz) / (2.0 * EARTH_R);
        float h = (alt - base) / (top - base);
        vec3 wp = vec3(p.x + F.cam_pos.x, alt, p.z + F.cam_pos.z);
        if (h >= 0.0 && h <= 1.0) {
            float d = cloud_density(wp, h, true, 0.0);
            if (d > 0.002) {
                // light march towards the sun
                float od = 0.0;
                float ls = (top - base) * 0.09;
                vec3 lp = wp;
                for (int j = 0; j < 6; j++) {
                    float stepl = ls * (float(j) * 0.6 + 1.0);
                    lp += L * stepl;
                    float lh = (lp.y - base) / (top - base);
                    if (lh > 1.0) break;
                    od += cloud_density(lp, lh, j < 2, float(j) * 0.5) * stepl;
                }
                // multiple scattering approximation (Wrenninge)
                vec3 sun = vec3(0.0);
                float a = 1.0;
                float b = 1.0;
                float c = 1.0;
                for (int o = 0; o < 4; o++) {
                    float ph = mix(hg_phase(cos_t, 0.75 * c), hg_phase(cos_t, -0.25 * c), 0.3);
                    sun += vec3(a * exp(-od * sigma * b) * ph);
                    a *= 0.55;
                    b *= 0.35;
                    c *= 0.5;
                }
                float powder = 1.0 - exp(-d * sigma * 120.0);
                sun *= mix(1.0, powder * 2.0, 0.6);
                vec3 amb = mix(amb_bot, amb_top, h * h) * (0.4 + 0.6 * h);
                vec3 S = (light_col * sun + amb) * sigma * d;
                float st = exp(-d * sigma * dt);
                scat += T * (S - S * st) / (sigma * d);
                tsum += t * T * (1.0 - st);
                wsum += T * (1.0 - st);
                T *= st;
                if (T < 0.01) break;
            }
        }
        t += dt;
    }
    // aerial perspective: distant clouds fade into the sky
    float dist = wsum > 0.0 ? tsum / wsum : t1;
    float fade = exp(-dist / 18000.0);
    vec3 sky_col = sky_radiance(dir);
    scat = mix(sky_col * (1.0 - T), scat, fade);
    float horizon = smoothstep(0.0, 0.06, dir.y + 0.02);
    scat *= horizon;
    T = mix(1.0, T, horizon);
    frag_color = vec4(scat, T);
}
@end

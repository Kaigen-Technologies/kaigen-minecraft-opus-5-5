// atmosphere multiple-scattering lut (32x32), built once
#include "wc_common.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
#include "wc_atmo.glsl"
layout(set = 1, binding = 0) uniform texture2D trans_lut;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

#define MS_STEPS 20
#define SQRT_SAMPLES 8

vec3 spherical_dir(float theta, float phi) {
    float cos_phi = cos(phi);
    float sin_phi = sin(phi);
    return vec3(sin_phi * sin(theta), cos_phi, sin_phi * cos(theta));
}

vec3 trans_at(vec3 pos, vec3 sun_dir) { return textureLod(sampler2D(trans_lut, smp_clamp), lut_uv(pos, sun_dir), 0.0).rgb; }

void mul_scatt_values(vec3 pos, vec3 sun_dir, out vec3 lum_total, out vec3 fms) {
    lum_total = vec3(0.0);
    fms = vec3(0.0);
    float inv_samples = 1.0 / float(SQRT_SAMPLES * SQRT_SAMPLES);
    for (int i = 0; i < SQRT_SAMPLES; i++) {
        for (int j = 0; j < SQRT_SAMPLES; j++) {
            float theta = 2.0 * PI * (float(i) + 0.5) / float(SQRT_SAMPLES);
            float phi = safeacos(1.0 - 2.0 * (float(j) + 0.5) / float(SQRT_SAMPLES));
            vec3 ray_dir = spherical_dir(theta, phi);
            float atmo_dist = ray_intersect_sphere(pos, ray_dir, ATMO_RADIUS_MM);
            float ground_dist = ray_intersect_sphere(pos, ray_dir, GROUND_RADIUS_MM);
            float t_max = ground_dist > 0.0 ? ground_dist : atmo_dist;
            float cos_theta = dot(ray_dir, sun_dir);
            float mie = mie_phase(cos_theta);
            float rayleigh = rayleigh_phase(-cos_theta);
            vec3 lum = vec3(0.0);
            vec3 lum_factor = vec3(0.0);
            vec3 transmittance = vec3(1.0);
            float t = 0.0;
            for (int s = 0; s < MS_STEPS; s++) {
                float new_t = ((float(s) + 0.3) / float(MS_STEPS)) * t_max;
                float dt = new_t - t;
                t = new_t;
                vec3 new_pos = pos + t * ray_dir;
                vec3 rayleigh_scattering, extinction;
                float mie_scattering;
                get_scattering_values(new_pos, rayleigh_scattering, mie_scattering, extinction);
                vec3 sample_t = exp(-dt * extinction);
                vec3 scattering_no_phase = rayleigh_scattering + mie_scattering;
                vec3 scattering_f = (scattering_no_phase - scattering_no_phase * sample_t) / extinction;
                lum_factor += transmittance * scattering_f;
                vec3 sun_t = trans_at(new_pos, sun_dir);
                vec3 in_scattering = (rayleigh_scattering * rayleigh + mie_scattering * mie) * sun_t;
                vec3 scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
                lum += scattering_integral * transmittance;
                transmittance *= sample_t;
            }
            if (ground_dist > 0.0) {
                vec3 hit_pos = pos + ground_dist * ray_dir;
                if (dot(pos, sun_dir) > 0.0) {
                    hit_pos = normalize(hit_pos) * GROUND_RADIUS_MM;
                    lum += transmittance * GROUND_ALBEDO * trans_at(hit_pos, sun_dir);
                }
            }
            fms += lum_factor * inv_samples;
            lum_total += lum * inv_samples;
        }
    }
}

void main() {
    float sun_cos_theta = 2.0 * v_uv.x - 1.0;
    float sun_theta = safeacos(sun_cos_theta);
    float height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, v_uv.y);
    vec3 pos = vec3(0.0, height, 0.0);
    vec3 sun_dir = normalize(vec3(0.0, sun_cos_theta, -sin(sun_theta)));
    vec3 lum, f_ms;
    mul_scatt_values(pos, sun_dir, lum, f_ms);
    frag_color = vec4(lum / (1.0 - f_ms), 1.0);
}
@end

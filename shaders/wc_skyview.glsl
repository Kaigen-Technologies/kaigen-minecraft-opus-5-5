// sky-view lut (192x108) for one light body; rendered for the sun and the moon each frame
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
#define WC_ATMO_VIEW
#include "wc_atmo.glsl"
layout(binding = 1) uniform _SkyViewParams {
    vec4 body_dir;
} P;
layout(set = 1, binding = 0) uniform texture2D trans_lut;
layout(set = 1, binding = 1) uniform texture2D ms_lut;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

#define SCATTER_STEPS 30

vec3 raymarch_scattered_light(vec3 pos, vec3 ray_dir, vec3 sun_dir, float t_max) {
    float cos_theta = dot(ray_dir, sun_dir);
    float mie = mie_phase(cos_theta);
    float rayleigh = rayleigh_phase(-cos_theta);
    vec3 lum = vec3(0.0);
    vec3 transmittance = vec3(1.0);
    float t = 0.0;
    for (int i = 0; i < SCATTER_STEPS; i++) {
        // quadratic step distribution: more samples near the viewer
        float new_t = t_max * sq((float(i) + 0.3) / float(SCATTER_STEPS));
        float dt = new_t - t;
        t = new_t;
        vec3 new_pos = pos + t * ray_dir;
        vec3 rayleigh_scattering, extinction;
        float mie_scattering;
        get_scattering_values(new_pos, rayleigh_scattering, mie_scattering, extinction);
        vec3 sample_t = exp(-dt * extinction);
        vec2 uv = lut_uv(new_pos, sun_dir);
        vec3 sun_t = textureLod(sampler2D(trans_lut, smp_clamp), uv, 0.0).rgb;
        vec3 psi_ms = textureLod(sampler2D(ms_lut, smp_clamp), uv, 0.0).rgb;
        vec3 in_scattering = rayleigh_scattering * (rayleigh * sun_t + psi_ms) + mie_scattering * (mie * sun_t + psi_ms);
        vec3 scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
        lum += scattering_integral * transmittance;
        transmittance *= sample_t;
    }
    return lum;
}

void main() {
    float azimuth = (v_uv.x - 0.5) * 2.0 * PI;
    float adj_v;
    if (v_uv.y < 0.5) {
        float coord = 1.0 - 2.0 * v_uv.y;
        adj_v = -coord * coord;
    } else {
        float coord = v_uv.y * 2.0 - 1.0;
        adj_v = coord * coord;
    }
    vec3 view_pos = atmo_view_pos();
    float height = length(view_pos);
    vec3 up = view_pos / height;
    float horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height) - 0.5 * PI;
    float altitude_angle = adj_v * 0.5 * PI - horizon_angle;
    float cos_alt = cos(altitude_angle);
    vec3 ray_dir = vec3(cos_alt * sin(azimuth), sin(altitude_angle), -cos_alt * cos(azimuth));
    float sun_altitude = (0.5 * PI) - acos(clamp(dot(P.body_dir.xyz, up), -1.0, 1.0));
    vec3 sun_dir = vec3(0.0, sin(sun_altitude), -cos(sun_altitude));
    float atmo_dist = ray_intersect_sphere(view_pos, ray_dir, ATMO_RADIUS_MM);
    float ground_dist = ray_intersect_sphere(view_pos, ray_dir, GROUND_RADIUS_MM);
    float t_max = ground_dist < 0.0 ? atmo_dist : ground_dist;
    frag_color = vec4(raymarch_scattered_light(view_pos, ray_dir, sun_dir, t_max), 1.0);
}
@end

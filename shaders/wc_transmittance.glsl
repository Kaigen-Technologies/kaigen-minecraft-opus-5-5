// atmosphere transmittance lut (256x64), built once
#include "wc_common.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
#include "wc_atmo.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 sun_transmittance(vec3 pos, vec3 sun_dir) {
    if (ray_intersect_sphere(pos, sun_dir, GROUND_RADIUS_MM) > 0.0) return vec3(0.0);
    float atmo_dist = ray_intersect_sphere(pos, sun_dir, ATMO_RADIUS_MM);
    float t = 0.0;
    vec3 transmittance = vec3(1.0);
    for (int i = 0; i < 40; i++) {
        float new_t = ((float(i) + 0.3) / 40.0) * atmo_dist;
        float dt = new_t - t;
        t = new_t;
        vec3 rs, ext;
        float ms;
        get_scattering_values(pos + t * sun_dir, rs, ms, ext);
        transmittance *= exp(-dt * ext);
    }
    return transmittance;
}

void main() {
    float sun_cos_theta = 2.0 * v_uv.x - 1.0;
    float sun_theta = safeacos(sun_cos_theta);
    float height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, v_uv.y);
    vec3 pos = vec3(0.0, height, 0.0);
    vec3 sun_dir = normalize(vec3(0.0, sun_cos_theta, -sin(sun_theta)));
    frag_color = vec4(sun_transmittance(pos, sun_dir), 1.0);
}
@end

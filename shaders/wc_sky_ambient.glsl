// irradiance ambient cube: 6 texels (+x -x +y -y +z -z) storing irradiance / PI
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
#define WC_ATMO_VIEW
#include "wc_atmo.glsl"
layout(set = 1, binding = 0) uniform texture2D sky_sun_tex;
layout(set = 1, binding = 1) uniform texture2D sky_moon_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 sky_radiance_raw(vec3 d) {
    return textureLod(sampler2D(sky_sun_tex, smp_clamp), sky_lut_uv(d, F.sun_dir.xyz), 0.0).rgb * F.sky.x +
           textureLod(sampler2D(sky_moon_tex, smp_clamp), sky_lut_uv(d, F.moon_dir.xyz), 0.0).rgb * F.sky.y +
           F.sky.z * vec3(0.5, 0.6, 1.0);
}

vec3 face_dir(int face) {
    if (face == 0) return vec3(1.0, 0.0, 0.0);
    if (face == 1) return vec3(-1.0, 0.0, 0.0);
    if (face == 2) return vec3(0.0, 1.0, 0.0);
    if (face == 3) return vec3(0.0, -1.0, 0.0);
    if (face == 4) return vec3(0.0, 0.0, 1.0);
    return vec3(0.0, 0.0, -1.0);
}

void main() {
    int face = int(gl_FragCoord.x);
    vec3 D = face_dir(face);
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 ground_e = F.sun_color.rgb * max(F.sun_dir.y, 0.0) + F.moon_color.rgb * max(F.moon_dir.y, 0.0);
    vec3 ground = vec3(0.16, 0.15, 0.13) * (ground_e / PI + sky_radiance_raw(vec3(0.0, 1.0, 0.0)) * 0.6);
    for (int i = 0; i < 12; i++) {
        float ct = 1.0 - (float(i) + 0.5) / 12.0 * 2.0;
        float st = sqrt(max(0.0, 1.0 - ct * ct));
        for (int j = 0; j < 24; j++) {
            float ph = (float(j) + 0.5) / 24.0 * 2.0 * PI;
            vec3 w = vec3(st * cos(ph), ct, st * sin(ph));
            vec3 L = w.y > -0.02 ? sky_radiance_raw(normalize(vec3(w.x, max(w.y, 0.0), w.z))) : ground;
            sum += L * max(dot(w, D), 0.0);
            wsum += 1.0;
        }
    }
    // integral of L cos over the sphere: sum * (4 PI / N)
    vec3 E = sum * (4.0 * PI / wsum);
    E = mix(E, vec3(luminance(E)) * vec3(0.55, 0.58, 0.64), F.weather.x * 0.85);
    E += F.extra.z * vec3(0.75, 0.8, 1.0) * PI * (face == 3 ? 0.25 : 0.8);
    frag_color = vec4(E / PI, 1.0);
}
@end

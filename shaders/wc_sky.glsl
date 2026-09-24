// sky radiance, ambient cube, stars and moon; needs sky_sun_tex, sky_moon_tex, ambient_tex, smp_clamp
#define WC_ATMO_VIEW
#include "wc_atmo.glsl"

vec3 ambient_cube(vec3 n) {
    vec3 n2 = n * n;
    vec3 cx = texelFetch(sampler2D(ambient_tex, smp_clamp), ivec2(n.x >= 0.0 ? 0 : 1, 0), 0).rgb;
    vec3 cy = texelFetch(sampler2D(ambient_tex, smp_clamp), ivec2(n.y >= 0.0 ? 2 : 3, 0), 0).rgb;
    vec3 cz = texelFetch(sampler2D(ambient_tex, smp_clamp), ivec2(n.z >= 0.0 ? 4 : 5, 0), 0).rgb;
    return n2.x * cx + n2.y * cy + n2.z * cz;
}

vec3 sky_radiance(vec3 d) {
    vec3 c = textureLod(sampler2D(sky_sun_tex, smp_clamp), sky_lut_uv(d, F.sun_dir.xyz), 0.0).rgb * F.sky.x +
             textureLod(sampler2D(sky_moon_tex, smp_clamp), sky_lut_uv(d, F.moon_dir.xyz), 0.0).rgb * F.sky.y +
             F.sky.z * vec3(0.5, 0.6, 1.0);
    // overcast: grey, flatter sky while it rains
    c = mix(c, vec3(luminance(c)) * vec3(0.62, 0.66, 0.72), F.weather.x * 0.85);
    // lightning flash
    return c + F.extra.z * vec3(0.75, 0.8, 1.0) * (0.6 + 0.4 * saturate(d.y + 0.3));
}

// sky colour for directions at or below the horizon beyond the loaded world
vec3 horizon_color(vec3 dir) {
    vec3 c = sky_radiance(normalize(vec3(dir.x, max(dir.y, 0.0) + 0.002, dir.z)));
    if (dir.y < 0.0) c *= mix(1.0, 0.8, smoothstep(0.0, -0.4, dir.y));
    return c;
}

// sky as seen in reflections: below the horizon fade to a dim ground colour
vec3 sky_reflect(vec3 d) {
    vec3 s = sky_radiance(normalize(vec3(d.x, max(d.y, 0.02), d.z)));
    float below = smoothstep(0.0, -0.25, d.y);
    vec3 ground = (F.sun_color.rgb * max(F.sun_dir.y, 0.0) / PI + ambient_cube(vec3(0.0, 1.0, 0.0))) * vec3(0.2, 0.2, 0.16);
    return mix(s, ground, below);
}

float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash13(i);
    float b = hash13(i + vec3(1.0, 0.0, 0.0));
    float c = hash13(i + vec3(0.0, 1.0, 0.0));
    float d = hash13(i + vec3(1.0, 1.0, 0.0));
    float e = hash13(i + vec3(0.0, 0.0, 1.0));
    float g = hash13(i + vec3(1.0, 0.0, 1.0));
    float h = hash13(i + vec3(0.0, 1.0, 1.0));
    float k = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y), mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z);
}
float fbm3n(vec3 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        s += vnoise3(p) * a;
        p = p * 2.03 + 1.7;
        a *= 0.5;
    }
    return s;
}

vec3 star_field(vec3 dir) {
    vec3 d = mat3(F.star_rot) * dir;
    vec3 color = vec3(0.0);
    // point stars on two hashed grids
    for (int layer = 0; layer < 2; layer++) {
        float scale = layer == 0 ? 170.0 : 340.0;
        vec3 p = d * scale;
        vec3 cell = floor(p);
        vec3 f = fract(p) - 0.5;
        vec3 h = hash33(cell + float(layer) * 17.0);
        float present = step(layer == 0 ? 0.972 : 0.982, h.x);
        vec3 off = (hash33(cell * 1.7 + 3.0) - 0.5) * 0.5;
        float dd = length(f - off);
        float twinkle = 0.75 + 0.25 * sin(F.cam_pos.w * (2.0 + h.z * 5.0) + h.y * 40.0);
        float b = present * smoothstep(0.32, 0.0, dd) * (layer == 0 ? 2.8 : 1.1) * (0.15 + pow(h.y, 3.0)) * twinkle;
        vec3 tint = mix(vec3(1.0, 0.8, 0.6), vec3(0.72, 0.84, 1.0), h.z);
        color += tint * b;
    }
    // milky way: soft glowing band with darker dust lanes
    vec3 axis = normalize(vec3(0.35, 0.25, 0.9));
    float lat = dot(d, axis);
    float band = exp(-lat * lat * 16.0);
    if (band > 0.01) {
        float n = fbm3n(d * 4.0);
        float fine = fbm3n(d * 14.0 + 11.0);
        float dust = smoothstep(0.42, 0.62, fbm3n(d * 7.0 + 3.0)) * exp(-lat * lat * 70.0);
        float glow = band * smoothstep(0.3, 0.75, n) * (0.6 + 0.8 * fine);
        color += (vec3(0.55, 0.62, 0.95) * glow * 0.3 + vec3(1.0, 0.86, 0.72) * exp(-lat * lat * 45.0) * n * 0.18) * (1.0 - dust * 0.85);
    }
    return color;
}

vec3 moon_disk(vec3 dir) {
    vec3 m = F.moon_dir.xyz;
    float c = dot(dir, m);
    float moon_r = 0.0155;
    float r = sqrt(max(0.0, 2.0 * (1.0 - c)));
    if (r > moon_r * 1.2) return vec3(0.0);
    vec3 up = abs(m.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tx = normalize(cross(up, m));
    vec3 ty = cross(m, tx);
    vec2 uv = vec2(dot(dir, tx), dot(dir, ty)) / moon_r;
    float rr = dot(uv, uv);
    float disk = smoothstep(1.0, 0.92, rr);
    // craters
    vec2 g = uv * 3.0;
    float cr = 0.0;
    for (int i = 0; i < 3; i++) {
        vec2 cell = floor(g);
        vec2 f = fract(g) - 0.5;
        float h = hash12(cell + float(i) * 13.1);
        cr += smoothstep(0.35, 0.1, length(f - (vec2(h, fract(h * 7.3)) - 0.5) * 0.4)) * 0.25 * step(0.4, h);
        g *= 2.1;
    }
    float mare = hash12(floor(uv * 2.0 + 3.0)) * 0.25;
    float lum = (0.9 - cr - mare) * (0.75 + 0.25 * sqrt(max(0.0, 1.0 - rr)));
    return vec3(0.95, 0.96, 1.0) * lum * disk;
}

// webcraft shared shader utilities (no resource declarations)
@ctype mat4 mat4
@ctype vec4 vec4
@ctype vec3 vec3
@ctype vec2 vec2
@ctype float f32
@ctype int i32
@ctype uint u32

#define PI 3.14159265359
#define saturate(a) clamp(a, 0.0, 1.0)

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float sq(float x) { return x * x; }

// fullscreen triangle; uv (0,0) is the top-left texel
vec4 wc_fullscreen(int vertex_index, out vec2 uv) {
    uv = vec2(float((uint(vertex_index) << 1u) & 2u), float(uint(vertex_index) & 2u));
    return vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}

vec2 oct_wrap(vec2 v) { return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }
vec2 oct_encode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 r = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    return r;
}
vec3 oct_decode(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash13(vec3 p3) { p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
vec3 hash33(vec3 p3) { p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yxz + 33.33); return fract((p3.xxy + p3.yxx) * p3.zyx); }

// block face frames: 0..5 = +x -x +y -y +z -z, 6 = plant (up)
void face_basis(uint face, out vec3 n, out vec3 t, out vec3 b) {
    if (face == 0u) { n = vec3(1.0, 0.0, 0.0); t = vec3(0.0, 0.0, -1.0); b = vec3(0.0, -1.0, 0.0); }
    else if (face == 1u) { n = vec3(-1.0, 0.0, 0.0); t = vec3(0.0, 0.0, 1.0); b = vec3(0.0, -1.0, 0.0); }
    else if (face == 2u) { n = vec3(0.0, 1.0, 0.0); t = vec3(1.0, 0.0, 0.0); b = vec3(0.0, 0.0, 1.0); }
    else if (face == 3u) { n = vec3(0.0, -1.0, 0.0); t = vec3(-1.0, 0.0, 0.0); b = vec3(0.0, 0.0, 1.0); }
    else if (face == 4u) { n = vec3(0.0, 0.0, 1.0); t = vec3(1.0, 0.0, 0.0); b = vec3(0.0, -1.0, 0.0); }
    else if (face == 5u) { n = vec3(0.0, 0.0, -1.0); t = vec3(-1.0, 0.0, 0.0); b = vec3(0.0, -1.0, 0.0); }
    else { n = vec3(0.0, 1.0, 0.0); t = vec3(1.0, 0.0, 0.0); b = vec3(0.0, 0.0, 1.0); }
}
vec3 face_normal(uint face) {
    vec3 n, t, b;
    face_basis(face, n, t, b);
    return n;
}

float hg_phase(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

vec3 wave_leaves(vec3 p, float t, float strength) {
    float a = sin(t * 1.6 + p.x * 0.55 + p.z * 0.35 + p.y * 0.2);
    float b = sin(t * 2.4 + p.z * 0.8 + p.y * 0.5 + p.x * 0.1);
    float c = sin(t * 4.3 + (p.x + p.y + p.z) * 1.3);
    return vec3(a * 0.04 + c * 0.012, c * 0.018 + b * 0.01, b * 0.035) * strength;
}
vec3 wave_plant(vec3 p, float t, float strength) {
    float gust = sin(t * 0.8 + p.x * 0.06 + p.z * 0.045) * 0.5 + 0.5;
    float w = sin(t * 2.1 + p.x * 0.8 + p.z * 0.6) + 0.4 * sin(t * 3.7 + p.x * 1.7);
    float v = cos(t * 1.8 + p.z * 0.7 + p.x * 0.3);
    return vec3(w * 0.07, 0.0, v * 0.055) * (0.4 + gust) * strength;
}

// tileable cloud noise shared by the noise bakes: integer hashes, worley and perlin with periodic lattices
#ifndef WC_NOISE_COMMON_GLSL
#define WC_NOISE_COMMON_GLSL

#define WC_WORLEY_SEED 0x5bd1e995u
#define WC_PERLIN_SEED 0x27d4eb2fu

// floor modulo without %, which GLSL leaves undefined for negative operands (NVIDIA Vulkan returns other cells)
int wc_nmod(int a, int m) { return a - m * int(floor(float(a) / float(m))); }

uint wc_hash3i(int x, int y, int z, uint seed) {
    uint h = seed ^ (uint(x) * 0x27d4eb2du) ^ (uint(y) * 0x9e3779b1u) ^ (uint(z) * 0x165667b1u);
    h = (h ^ (h >> 16u)) * 0x85ebca6bu;
    h = (h ^ (h >> 13u)) * 0xc2b2ae35u;
    return h ^ (h >> 16u);
}

// tileable worley; 1 near feature points
float wc_worley(vec3 p, int period) {
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    float min_d = 1e9;
    for (int dz = -1; dz <= 1; dz++) {
        int cz = wc_nmod(i.z + dz, period);
        for (int dy = -1; dy <= 1; dy++) {
            int cy = wc_nmod(i.y + dy, period);
            for (int dx = -1; dx <= 1; dx++) {
                uint h = wc_hash3i(wc_nmod(i.x + dx, period), cy, cz, WC_WORLEY_SEED);
                vec3 o = vec3(float(dx), float(dy), float(dz)) +
                         vec3(float(h & 1023u), float((h >> 10u) & 1023u), float((h >> 20u) & 1023u)) * (1.0 / 1024.0) - f;
                min_d = min(min_d, dot(o, o));
            }
        }
    }
    return 1.0 - clamp(sqrt(min_d), 0.0, 1.0);
}

float wc_fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// improved-perlin edge gradients, scaled to unit length by the caller
float wc_grad(uint h, vec3 p) {
    switch (h % 12u) {
    case 0u: return p.x + p.y;
    case 1u: return -p.x + p.y;
    case 2u: return p.x - p.y;
    case 3u: return -p.x - p.y;
    case 4u: return p.x + p.z;
    case 5u: return -p.x + p.z;
    case 6u: return p.x - p.z;
    case 7u: return -p.x - p.z;
    case 8u: return p.y + p.z;
    case 9u: return -p.y + p.z;
    case 10u: return p.y - p.z;
    default: return -p.y - p.z;
    }
}

float wc_perlin(vec3 p, int period) {
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    int x0 = wc_nmod(i.x, period), x1 = wc_nmod(i.x + 1, period);
    int y0 = wc_nmod(i.y, period), y1 = wc_nmod(i.y + 1, period);
    int z0 = wc_nmod(i.z, period), z1 = wc_nmod(i.z + 1, period);
    float n000 = wc_grad(wc_hash3i(x0, y0, z0, WC_PERLIN_SEED), f);
    float n100 = wc_grad(wc_hash3i(x1, y0, z0, WC_PERLIN_SEED), f - vec3(1.0, 0.0, 0.0));
    float n010 = wc_grad(wc_hash3i(x0, y1, z0, WC_PERLIN_SEED), f - vec3(0.0, 1.0, 0.0));
    float n110 = wc_grad(wc_hash3i(x1, y1, z0, WC_PERLIN_SEED), f - vec3(1.0, 1.0, 0.0));
    float n001 = wc_grad(wc_hash3i(x0, y0, z1, WC_PERLIN_SEED), f - vec3(0.0, 0.0, 1.0));
    float n101 = wc_grad(wc_hash3i(x1, y0, z1, WC_PERLIN_SEED), f - vec3(1.0, 0.0, 1.0));
    float n011 = wc_grad(wc_hash3i(x0, y1, z1, WC_PERLIN_SEED), f - vec3(0.0, 1.0, 1.0));
    float n111 = wc_grad(wc_hash3i(x1, y1, z1, WC_PERLIN_SEED), f - vec3(1.0, 1.0, 1.0));
    vec3 u = vec3(wc_fade(f.x), wc_fade(f.y), wc_fade(f.z));
    float a = mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y);
    float b = mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y);
    return mix(a, b, u.z) * 0.70710678;
}

float wc_perlin_fbm(vec3 p, int period, int oct) {
    float s = 0.0, a = 0.5, norm = 0.0;
    for (int i = 0; i < oct; i++) {
        s += wc_perlin(p, period) * a;
        norm += a;
        a *= 0.5;
        p *= 2.0;
        period *= 2;
    }
    return s / norm;
}

float wc_worley_fbm(vec3 p, int period) {
    return wc_worley(p, period) * 0.625 + wc_worley(p * 2.0, period * 2) * 0.25 + wc_worley(p * 4.0, period * 4) * 0.125;
}

// worley at 4, 8, 16, 32 and 64 cells; w_k = fbm of octaves k..k+2
vec3 wc_worley_octaves(vec3 p) {
    float W0 = wc_worley(p * 4.0, 4);
    float W1 = wc_worley(p * 8.0, 8);
    float W2 = wc_worley(p * 16.0, 16);
    float W3 = wc_worley(p * 32.0, 32);
    float W4 = wc_worley(p * 64.0, 64);
    return vec3(W0 * 0.625 + W1 * 0.25 + W2 * 0.125, W1 * 0.625 + W2 * 0.25 + W3 * 0.125,
                W2 * 0.625 + W3 * 0.25 + W4 * 0.125);
}

#endif

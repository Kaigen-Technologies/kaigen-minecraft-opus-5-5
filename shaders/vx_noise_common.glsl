// tileable cloud noise shared by the noise bakes: integer hashes, worley and perlin with periodic lattices
#ifndef VX_NOISE_COMMON_GLSL
#define VX_NOISE_COMMON_GLSL

#define VX_WORLEY_SEED 0x5bd1e995u
#define VX_PERLIN_SEED 0x27d4eb2fu

// floor modulo without %, which GLSL leaves undefined for negative operands (NVIDIA Vulkan returns other cells)
int vx_nmod(int a, int m) { return a - m * int(floor(float(a) / float(m))); }

uint vx_hash3i(int x, int y, int z, uint seed) {
    uint h = seed ^ (uint(x) * 0x27d4eb2du) ^ (uint(y) * 0x9e3779b1u) ^ (uint(z) * 0x165667b1u);
    h = (h ^ (h >> 16u)) * 0x85ebca6bu;
    h = (h ^ (h >> 13u)) * 0xc2b2ae35u;
    return h ^ (h >> 16u);
}

// tileable worley; 1 near feature points
float vx_worley(vec3 p, int period) {
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    float min_d = 1e9;
    for (int dz = -1; dz <= 1; dz++) {
        int cz = vx_nmod(i.z + dz, period);
        for (int dy = -1; dy <= 1; dy++) {
            int cy = vx_nmod(i.y + dy, period);
            for (int dx = -1; dx <= 1; dx++) {
                uint h = vx_hash3i(vx_nmod(i.x + dx, period), cy, cz, VX_WORLEY_SEED);
                vec3 o = vec3(float(dx), float(dy), float(dz)) +
                         vec3(float(h & 1023u), float((h >> 10u) & 1023u), float((h >> 20u) & 1023u)) * (1.0 / 1024.0) - f;
                min_d = min(min_d, dot(o, o));
            }
        }
    }
    return 1.0 - clamp(sqrt(min_d), 0.0, 1.0);
}

float vx_fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// improved-perlin edge gradients, scaled to unit length by the caller
float vx_grad(uint h, vec3 p) {
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

float vx_perlin(vec3 p, int period) {
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    int x0 = vx_nmod(i.x, period), x1 = vx_nmod(i.x + 1, period);
    int y0 = vx_nmod(i.y, period), y1 = vx_nmod(i.y + 1, period);
    int z0 = vx_nmod(i.z, period), z1 = vx_nmod(i.z + 1, period);
    float n000 = vx_grad(vx_hash3i(x0, y0, z0, VX_PERLIN_SEED), f);
    float n100 = vx_grad(vx_hash3i(x1, y0, z0, VX_PERLIN_SEED), f - vec3(1.0, 0.0, 0.0));
    float n010 = vx_grad(vx_hash3i(x0, y1, z0, VX_PERLIN_SEED), f - vec3(0.0, 1.0, 0.0));
    float n110 = vx_grad(vx_hash3i(x1, y1, z0, VX_PERLIN_SEED), f - vec3(1.0, 1.0, 0.0));
    float n001 = vx_grad(vx_hash3i(x0, y0, z1, VX_PERLIN_SEED), f - vec3(0.0, 0.0, 1.0));
    float n101 = vx_grad(vx_hash3i(x1, y0, z1, VX_PERLIN_SEED), f - vec3(1.0, 0.0, 1.0));
    float n011 = vx_grad(vx_hash3i(x0, y1, z1, VX_PERLIN_SEED), f - vec3(0.0, 1.0, 1.0));
    float n111 = vx_grad(vx_hash3i(x1, y1, z1, VX_PERLIN_SEED), f - vec3(1.0, 1.0, 1.0));
    vec3 u = vec3(vx_fade(f.x), vx_fade(f.y), vx_fade(f.z));
    float a = mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y);
    float b = mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y);
    return mix(a, b, u.z) * 0.70710678;
}

float vx_perlin_fbm(vec3 p, int period, int oct) {
    float s = 0.0, a = 0.5, norm = 0.0;
    for (int i = 0; i < oct; i++) {
        s += vx_perlin(p, period) * a;
        norm += a;
        a *= 0.5;
        p *= 2.0;
        period *= 2;
    }
    return s / norm;
}

float vx_worley_fbm(vec3 p, int period) {
    return vx_worley(p, period) * 0.625 + vx_worley(p * 2.0, period * 2) * 0.25 + vx_worley(p * 4.0, period * 4) * 0.125;
}

// worley at 4, 8, 16, 32 and 64 cells; w_k = fbm of octaves k..k+2
vec3 vx_worley_octaves(vec3 p) {
    float W0 = vx_worley(p * 4.0, 4);
    float W1 = vx_worley(p * 8.0, 8);
    float W2 = vx_worley(p * 16.0, 16);
    float W3 = vx_worley(p * 32.0, 32);
    float W4 = vx_worley(p * 64.0, 64);
    return vec3(W0 * 0.625 + W1 * 0.25 + W2 * 0.125, W1 * 0.625 + W2 * 0.25 + W3 * 0.125,
                W2 * 0.625 + W3 * 0.25 + W4 * 0.125);
}

#endif

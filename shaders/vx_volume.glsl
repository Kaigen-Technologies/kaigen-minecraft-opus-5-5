// voxel light volume: two toroidal 128^3 levels (1 and 2 blocks per texel) plus the 32^3 dynamic window
// needs vol0_tex, vol1_tex, dyn_tex, smp_linear (repeat) and smp_clamp; rgb block light and a sky light, 0..1

#define VX_VOL_N 128.0
#define VX_DYN_N 32.0

// fraction of the way inside a window of n blocks, fading over the last `fade` blocks of each side
float vol_inside(vec3 p, vec3 origin, float n, float fade) {
    vec3 a = p - origin;
    vec3 k = smoothstep(vec3(0.0), vec3(fade), a) * smoothstep(vec3(n), vec3(n - fade), a);
    return k.x * k.y * k.z;
}

vec4 vol_level0(vec3 p) { return textureLod(sampler3D(vol0_tex, smp_linear), p / VX_VOL_N, 0.0); }
vec4 vol_level1(vec3 p) { return textureLod(sampler3D(vol1_tex, smp_linear), p / (2.0 * VX_VOL_N), 0.0); }

// stepped retro look: the nearest voxel of level 0 (level 1 beyond it), quantised to Terraria's light levels
vec4 vol_nearest(vec3 p) {
    ivec3 v = ivec3(floor(p));
    vec4 a = texelFetch(sampler3D(vol0_tex, smp_linear), v & 127, 0);
    vec4 b = texelFetch(sampler3D(vol1_tex, smp_linear), (v >> 1) & 127, 0);
    vec4 r = vol_inside(p, F.vol0.xyz, VX_VOL_N, 1.0) > 0.5 ? a : b;
    return floor(r * 6.0 + 0.5) / 6.0;
}

// p is a world position (blocks); above the world is open sky, below it is rock
vec4 vol_sample(vec3 p, bool nearest) {
    if (p.y >= 256.0) return vec4(0.0, 0.0, 0.0, 1.0);
    if (p.y < 0.0) return vec4(0.0);
    float w1 = F.vol1.w * vol_inside(vec3(p.x, 128.0, p.z), vec3(F.vol1.x, 0.0, F.vol1.z), 2.0 * VX_VOL_N, 8.0);
    if (w1 <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    if (nearest) return vol_nearest(p);
    float w0 = F.vol0.w * vol_inside(p, F.vol0.xyz, VX_VOL_N, 4.0);
    vec4 far_l = vol_level1(p);
    vec4 l = w0 >= 1.0 ? vol_level0(p) : mix(far_l, vol_level0(p), w0);
    return mix(vec4(0.0, 0.0, 0.0, 1.0), l, w1);
}

// held lights: a small flood-filled window that follows the player
vec3 dyn_sample(vec3 p) {
    if (F.dyn.w <= 0.0) return vec3(0.0);
    vec3 a = (p - F.dyn.xyz) / VX_DYN_N;
    if (any(lessThan(a, vec3(0.0))) || any(greaterThanEqual(a, vec3(1.0)))) return vec3(0.0);
    return textureLod(sampler3D(dyn_tex, smp_clamp), a, 0.0).rgb;
}

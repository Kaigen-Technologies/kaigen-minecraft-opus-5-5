// terrain vertex pulling (vertex shaders only): quad records, per-column draw records, the frame's visible list
struct VxDraw {
    vec3 origin;
    uint _pad;
};
layout(set = 1, binding = 0) readonly buffer _Quads { uint v[]; } quads;
layout(set = 1, binding = 1) readonly buffer _Draws { VxDraw d[]; } draws;
layout(set = 1, binding = 2) readonly buffer _Visible { uvec2 v[]; } visible;

#include "vx_instance.glsl"

// record layout: src/voxaria/vx.h (VX_QUAD_U32)
#define VX_QUAD_U32 3u
#define VX_QUAD_CUBE 0u
#define VX_QUAD_CROSS 1u
#define VX_QUAD_BOX 2u
#define VX_QUAD_LIQUID 3u
#define VX_WAVE_NONE 0u
#define VX_WAVE_LEAVES 1u
#define VX_WAVE_PLANT 2u

struct VxQuadVertex {
    vec3 local; // column-relative position in blocks
    vec2 uv;
    uint layer;
    uint flags;
    uint face;
    uint wave;
    uint map;   // 1: a cube or model face, whose tile is sampled in world space
    uint bits;  // the record's shape bits (21..23)
    float ao;
};

vec2 vx_corner_uv(uint c) { return vec2(c == 1u || c == 2u ? 1.0 : 0.0, c < 2u ? 1.0 : 0.0); }

float vx_hash_block(vec3 b) {
    vec3 p3 = fract(b * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// corner cr (x | y<<1 | z<<2) of a model box: lo xyz then hi-1 xyz, 4 bits each, in 1/16 block
vec3 vx_box_corner(uint w2, uint cr) {
    vec3 lo = vec3(float(w2 & 15u), float((w2 >> 4u) & 15u), float((w2 >> 8u) & 15u));
    vec3 hi = vec3(float((w2 >> 12u) & 15u), float((w2 >> 16u) & 15u), float((w2 >> 20u) & 15u)) + 1.0;
    return mix(lo, hi, vec3(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u))) * (1.0 / 16.0);
}

// column-relative position of corner c (the cube diagonal flip applied) / k (as drawn) of the quad
vec3 vx_quad_local(uint w0, uint w2, uint c, uint k, vec3 origin) {
    vec3 blk = vec3(float(w0 & 15u), float((w0 >> 8u) & 255u), float((w0 >> 4u) & 15u));
    uint face = (w0 >> 16u) & 7u;
    uint shape = (w0 >> 19u) & 3u;
    if (shape == VX_QUAD_CUBE) {
        uint cr = vx_face_corner(face, c);
        return blk + vec3(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u));
    }
    if (shape == VX_QUAD_CROSS) {
        bool e = (w0 & (1u << 22u)) != 0u ? (k == 0u || k == 3u) : (k == 1u || k == 2u);
        // a per-block jitter, from the world position so it is stable across remeshes
        vec3 wb = blk + floor(origin + F.cam_pos.xyz + vec3(0.5));
        float ox = (vx_hash_block(wb) - 0.5) * 0.35;
        float oz = (vx_hash_block(wb + vec3(17.0, 3.0, 9.0)) - 0.5) * 0.35;
        float ax = e ? 0.94 : 0.06;
        float az = ((w0 & (1u << 21u)) != 0u) != e ? 0.94 : 0.06;
        vec2 xz = floor((blk.xz + vec2(ax + ox, az + oz)) * 64.0 + 0.5) * (1.0 / 64.0);
        return vec3(xz.x, blk.y + (k < 2u ? 0.0 : 1.0), xz.y);
    }
    uint cr = vx_face_corner(face, k);
    if (shape == VX_QUAD_BOX) return blk + vx_box_corner(w2, cr);
    // liquid: each top corner's height (x | z << 1, 5 bits each in 1/16 block)
    uint ci = (cr & 1u) | ((cr >> 1u) & 2u);
    float top = float((w2 >> (ci * 5u)) & 31u) * (1.0 / 16.0);
    return blk + vec3(float(cr & 1u), (cr & 2u) != 0u ? top : 0.0, float(cr >> 2u));
}

vec2 vx_quad_uv(uint w0, uint w2, uint c, uint k) {
    uint shape = (w0 >> 19u) & 3u;
    if (shape == VX_QUAD_CUBE) return vx_corner_uv(c);
    if (shape == VX_QUAD_CROSS) {
        bool e = (w0 & (1u << 22u)) != 0u ? (k == 0u || k == 3u) : (k == 1u || k == 2u);
        return vec2(e ? 1.0 : 0.0, k < 2u ? 1.0 : 0.0);
    }
    vec2 cuv = vx_corner_uv(k);
    if (shape == VX_QUAD_BOX) {
        // the box face shows the part of the tile a full cube face would show there
        uint face = (w0 >> 16u) & 7u;
        uint cr = vx_face_corner(face, k);
        vec3 bits = vec3(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u));
        vec3 p = vx_box_corner(w2, cr);
        uint ua = face < 2u ? 2u : 0u;
        uint va = face == 2u || face == 3u ? 2u : 1u;
        float u = cuv.x == bits[ua] ? p[ua] : 1.0 - p[ua];
        float v = cuv.y == bits[va] ? p[va] : 1.0 - p[va];
        return vec2(u, v);
    }
    return cuv;
}

uint vx_quad_wave(uint w0, uint w1, uint k) {
    if (((w1 >> 10u) & 2u) != 0u) return VX_WAVE_LEAVES;
    return ((w0 >> 19u) & 3u) == VX_QUAD_CROSS && (w0 & (1u << 23u)) != 0u && k >= 2u ? VX_WAVE_PLANT : VX_WAVE_NONE;
}

// corner k (0..3) of quad q of a draw at origin
VxQuadVertex vx_quad_vertex(uint q, uint k, vec3 origin) {
    uint o = q * VX_QUAD_U32;
    uint w0 = quads.v[o];
    uint w1 = quads.v[o + 1u];
    uint w2 = quads.v[o + 2u];
    uint c = k;
    if (((w0 >> 19u) & 3u) == VX_QUAD_CUBE) {
        // the diagonal runs along the brighter corners, like the mesher's flip
        uvec4 ao = (uvec4(w1) >> uvec4(16u, 18u, 20u, 22u)) & 3u;
        c = ao.x + ao.z < ao.y + ao.w ? (k + 1u) & 3u : k;
    }
    VxQuadVertex v;
    v.local = vx_quad_local(w0, w2, c, k, origin);
    v.uv = vx_quad_uv(w0, w2, c, k);
    v.layer = w1 & 1023u;
    v.flags = (w1 >> 10u) & 63u;
    v.face = (w0 >> 16u) & 7u;
    v.wave = vx_quad_wave(w0, w1, k);
    uint shape = (w0 >> 19u) & 3u;
    // 2: a box face flagged local (bit 21), showing one block's quarter of its tile
    v.map = shape == VX_QUAD_BOX && (w0 & (1u << 21u)) != 0u ? 2u : shape == VX_QUAD_CUBE || shape == VX_QUAD_BOX ? 1u : 0u;
    v.bits = (w0 >> 21u) & 7u;
    v.ao = float((w1 >> (16u + 2u * c)) & 3u) / 3.0;
    return v;
}

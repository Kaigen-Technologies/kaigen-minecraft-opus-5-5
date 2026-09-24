// terrain vertex pulling (vertex shaders only): quad records, per-column draw records, the frame's visible list
struct WcDraw {
    vec3 origin;
    uint _pad;
};
layout(set = 1, binding = 0) readonly buffer _Quads { uint v[]; } quads;
layout(set = 1, binding = 1) readonly buffer _Draws { WcDraw d[]; } draws;
layout(set = 1, binding = 2) readonly buffer _Visible { uvec2 v[]; } visible;

// the instance index is the draw's first visible entry; d3d12 excludes StartInstanceLocation, so it is pushed
#ifdef SHADER_BACKEND_SPIRV
#define HZ_INSTANCE_INDEX gl_InstanceIndex
#else
#ifdef SHADER_BACKEND_HLSL
layout(push_constant) uniform _BaseInstance {
    uint base_instance;
} _hz;
#define HZ_INSTANCE_INDEX (gl_InstanceIndex + _hz.base_instance)
#else
#define HZ_INSTANCE_INDEX gl_InstanceIndex
#endif
#endif

#define WC_QUAD_U32 5u
#define WC_QUAD_CUBE 0u
#define WC_QUAD_CROSS 1u
#define WC_QUAD_TORCH 2u
#define WC_QUAD_LIQUID 3u
#define WC_WAVE_NONE 0u
#define WC_WAVE_LEAVES 1u
#define WC_WAVE_PLANT 2u

struct WcQuadVertex {
    vec3 local; // column-relative position in blocks
    vec2 uv;
    uint tex_flags;
    uint face;
    uint wave;
    float ao;
    float sky;
    float block;
    vec3 tint; // srgb
};

// corner c of cube face f as x | y<<1 | z<<2, CCW seen from outside: BL, BR, TR, TL (wc_mesh.c's corner table)
uint wc_face_corner(uint f, uint c) {
    uvec3 t = uvec3(0x5a0ecdu, 0x2254feu, 0x681decu);
    return (t[f >> 1u] >> ((f & 1u) * 12u + c * 3u)) & 7u;
}

vec2 wc_corner_uv(uint c) { return vec2(c == 1u || c == 2u ? 1.0 : 0.0, c < 2u ? 1.0 : 0.0); }

// column-relative position of corner c (the cube diagonal flip applied) / k (as drawn) of the quad at word o
vec3 wc_quad_local(uint o, uint w0, uint c, uint k) {
    vec3 blk = vec3(float(w0 & 15u), float((w0 >> 8u) & 255u), float((w0 >> 4u) & 15u));
    uint face = (w0 >> 16u) & 7u;
    uint shape = (w0 >> 19u) & 3u;
    if (shape == WC_QUAD_CUBE) {
        uint cr = wc_face_corner(face, c);
        return blk + vec3(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u));
    }
    if (shape == WC_QUAD_CROSS) {
        bool e = (w0 & (1u << 22u)) != 0u ? (k == 0u || k == 3u) : (k == 1u || k == 2u);
        bool centred = (w0 & (1u << 24u)) != 0u;
        float ox = centred ? 0.0 : (float(quads.v[o + 1u] >> 24u) / 255.0 - 0.5) * 0.35;
        float oz = centred ? 0.0 : (float(quads.v[o + 4u] >> 24u) / 255.0 - 0.5) * 0.35;
        float ax = e ? 0.94 : 0.06;
        float az = ((w0 & (1u << 21u)) != 0u) != e ? 0.94 : 0.06;
        vec2 xz = floor((blk.xz + vec2(ax + ox, az + oz)) * 64.0 + 0.5) * (1.0 / 64.0);
        return vec3(xz.x, blk.y + (k < 2u ? 0.0 : 1.0), xz.y);
    }
    uint cr = wc_face_corner(face, k);
    if (shape == WC_QUAD_TORCH)
        return blk + vec3((cr & 1u) != 0u ? 0.5625 : 0.4375, (cr & 2u) != 0u ? 0.625 : 0.0, (cr & 4u) != 0u ? 0.5625 : 0.4375);
    float top = (w0 & (1u << 21u)) != 0u ? 1.0 : 0.875;
    return blk + vec3(float(cr & 1u), (cr & 2u) != 0u ? top : 0.0, float(cr >> 2u));
}

vec2 wc_quad_uv(uint w0, uint c, uint k) {
    uint shape = (w0 >> 19u) & 3u;
    if (shape == WC_QUAD_CUBE) return wc_corner_uv(c);
    if (shape == WC_QUAD_CROSS) {
        bool e = (w0 & (1u << 22u)) != 0u ? (k == 0u || k == 3u) : (k == 1u || k == 2u);
        return vec2(e ? 1.0 : 0.0, k < 2u ? 1.0 : 0.0);
    }
    vec2 cuv = wc_corner_uv(k);
    if (shape == WC_QUAD_TORCH)
        return vec2(cuv.x == 0.0 ? 7.0 : 9.0, cuv.y == 0.0 ? 6.0 : (((w0 >> 16u) & 7u) == 2u ? 8.0 : 16.0)) * (1.0 / 16.0);
    return cuv;
}

uint wc_quad_wave(uint w0, uint w1, uint k) {
    if (((w1 >> 10u) & 2u) != 0u) return WC_WAVE_LEAVES;
    return ((w0 >> 19u) & 3u) == WC_QUAD_CROSS && (w0 & (1u << 23u)) != 0u && k >= 2u ? WC_WAVE_PLANT : WC_WAVE_NONE;
}

// corner k (0..3) of quad q of the current draw
WcQuadVertex wc_quad_vertex(uint q, uint k) {
    uint o = q * WC_QUAD_U32;
    uint w0 = quads.v[o];
    uint w1 = quads.v[o + 1u];
    uint w2 = quads.v[o + 2u];
    uint w3 = quads.v[o + 3u];
    uint w4 = quads.v[o + 4u];
    uint c = k;
    if (((w0 >> 19u) & 3u) == WC_QUAD_CUBE) {
        // the diagonal runs along the brighter corners, like the mesher's flip
        uvec4 ao = (uvec4(w1) >> uvec4(16u, 18u, 20u, 22u)) & 3u;
        uvec4 lt = ((uvec4(w2) >> uvec4(0u, 8u, 16u, 24u)) & 255u) + ((uvec4(w3) >> uvec4(0u, 8u, 16u, 24u)) & 255u);
        bool flip = ao.x + ao.z < ao.y + ao.w || (ao.x + ao.z == ao.y + ao.w && lt.x + lt.z < lt.y + lt.w);
        c = flip ? (k + 1u) & 3u : k;
    }
    WcQuadVertex v;
    v.local = wc_quad_local(o, w0, c, k);
    v.uv = wc_quad_uv(w0, c, k);
    v.tex_flags = w1 & 0xffffu;
    v.face = (w0 >> 16u) & 7u;
    v.wave = wc_quad_wave(w0, w1, k);
    v.ao = float((w1 >> (16u + 2u * c)) & 3u) / 3.0;
    v.sky = float((w2 >> (8u * c)) & 255u) / 255.0;
    v.block = float((w3 >> (8u * c)) & 255u) / 255.0;
    v.tint = vec3(float(w4 & 255u), float((w4 >> 8u) & 255u), float((w4 >> 16u) & 255u)) / 255.0;
    return v;
}

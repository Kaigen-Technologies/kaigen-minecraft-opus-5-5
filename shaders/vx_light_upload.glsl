// scatters voxel light into a light volume texture: one workgroup per 16^3 section, or the whole dynamic window
@ctype uint u32

@cs
layout(set = 0, binding = 0) uniform _UploadParams {
    uint count; // sections in this dispatch
    uint mode;  // 0 level 0 (a texel per voxel), 1 level 1 (a texel per 2^3 voxels, their max), 2 the 32^3 dynamic window
    uint first; // first header of this dispatch
    uint _pad;
} params;

// x: toroidal texel origin x | y << 8 | z << 16; y: first data word; z: uniform light | 1 << 31 when the section is uniform
layout(set = 1, binding = 1) readonly buffer _Headers {
    uvec4 h[];
} headers;

// light words (r | g << 4 | b << 8 | sky << 12), two per u32, section order y << 8 | z << 4 | x; rgba8 per texel for mode 2
layout(set = 1, binding = 2) readonly buffer _Data {
    uint d[];
} data;

// d3d12 maps compute images to u0..n-1: they take the lowest bindings
layout(set = 1, binding = 0, rgba8) writeonly uniform image3D dst;

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

vec4 unpack_light(uint v) {
    return vec4(float(v & 15u), float((v >> 4u) & 15u), float((v >> 8u) & 15u), float((v >> 12u) & 15u)) / 15.0;
}

uint voxel(uint base, uint i) {
    uint w = data.d[base + (i >> 1u)];
    return (i & 1u) != 0u ? w >> 16u : w & 0xffffu;
}

void main() {
    uint lid = gl_LocalInvocationID.x;
    if (params.mode == 2u) {
        for (uint k = 0u; k < 512u; k++) {
            uint i = lid + k * 64u;
            imageStore(dst, ivec3(int(i & 31u), int(i >> 10u), int((i >> 5u) & 31u)), unpackUnorm4x8(data.d[i]));
        }
        return;
    }
    uint s = gl_WorkGroupID.x;
    if (s >= params.count) return;
    uvec4 hd = headers.h[params.first + s];
    ivec3 o = ivec3(int(hd.x & 255u), int((hd.x >> 8u) & 255u), int((hd.x >> 16u) & 255u));
    bool uni = (hd.z & 0x80000000u) != 0u;
    uint uv = hd.z & 0xffffu;
    if (params.mode == 0u) {
        for (uint k = 0u; k < 64u; k++) {
            uint i = lid + k * 64u;
            uint v = uni ? uv : voxel(hd.y, i);
            imageStore(dst, o + ivec3(int(i & 15u), int(i >> 8u), int((i >> 4u) & 15u)), unpack_light(v));
        }
        return;
    }
    for (uint k = 0u; k < 8u; k++) {
        uint t = lid + k * 64u;
        ivec3 q = ivec3(int(t & 7u), int(t >> 6u), int((t >> 3u) & 7u));
        uvec4 m = uvec4(uv & 15u, (uv >> 4u) & 15u, (uv >> 8u) & 15u, (uv >> 12u) & 15u);
        if (!uni) {
            m = uvec4(0u);
            for (uint c = 0u; c < 8u; c++) {
                uvec3 v3 = uvec3(q) * 2u + uvec3(c & 1u, c >> 2u, (c >> 1u) & 1u);
                uint v = voxel(hd.y, (v3.y << 8u) | (v3.z << 4u) | v3.x);
                m = max(m, uvec4(v & 15u, (v >> 4u) & 15u, (v >> 8u) & 15u, (v >> 12u) & 15u));
            }
        }
        imageStore(dst, o + q, vec4(m) / 15.0);
    }
}
@end

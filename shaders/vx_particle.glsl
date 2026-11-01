// particle billboards into the g-buffer: pixel-art sprites from the tile array, alpha as a stable dither, optional glow
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
// record: xyz camera-relative, size | tint rgba8, layer 12 | flags 8 << 12, spin (radians), pad
layout(set = 1, binding = 0) readonly buffer _Parts { uvec4 v[]; } parts;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out vec4 v_tint;
layout(location = 4) out vec3 v_rel;

void main() {
    uint vi = uint(gl_VertexIndex);
    uint pi = vi / 6u;
    uint c = vi % 6u;
    // two triangles: 0 1 2, 0 2 3
    uint k = c < 3u ? c : (c == 3u ? 0u : c - 2u);
    vec2 cr = vec2(float(k == 1u || k == 2u), float(k >= 2u));
    uvec4 a = parts.v[pi * 2u];
    uvec4 b = parts.v[pi * 2u + 1u];
    vec3 p = uintBitsToFloat(a.xyz);
    float size = uintBitsToFloat(a.w);
    float spin = uintBitsToFloat(b.z);
    vec3 right = vec3(F.view[0][0], F.view[1][0], F.view[2][0]);
    vec3 up = vec3(F.view[0][1], F.view[1][1], F.view[2][1]);
    vec2 o = (cr - 0.5) * size;
    float cs = cos(spin), sn = sin(spin);
    o = vec2(o.x * cs - o.y * sn, o.x * sn + o.y * cs);
    vec3 rel = p + right * o.x + up * o.y;
    gl_Position = F.view_proj * vec4(rel, 1.0);
    v_uv = vec2(cr.x, 1.0 - cr.y);
    v_layer = b.y & 4095u;
    v_flags = (b.y >> 12u) & 255u;
    v_tint = unpackUnorm4x8(b.x);
    v_rel = rel;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 16) uniform sampler smp_block;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_flags;
layout(location = 3) flat in vec4 v_tint;
layout(location = 4) in vec3 v_rel;

layout(location = 0) out vec4 o_gb0;
layout(location = 1) out vec4 o_gb1;

#define VX_PART_GLOW 1u

void main() {
    // one block of the 2x2-block tile
    vec4 alb = texture(sampler2DArray(albedo_tex, smp_block), vec3(clamp(v_uv, 0.0, 1.0) * 0.5, float(v_layer)));
    float alpha = alb.a * v_tint.a;
    // 4x4 ordered dither: partial alpha keeps a fixed pattern that taa resolves into translucency
    ivec2 q = ivec2(gl_FragCoord.xy) & 3;
    int bi = (q.x & 1) * 2 + (q.y & 1) * 3 - ((q.x & 1) & (q.y & 1)) * 4;
    int bayer = bi * 4 + ((q.x >> 1) & 1) * 2 + ((q.y >> 1) & 1) * 3 - (((q.x >> 1) & 1) & ((q.y >> 1) & 1)) * 4;
    if (alpha <= (float(bayer) + 0.5) / 16.0) discard;
    vec3 V = normalize(-v_rel);
    // half toward the sky so sun and sky light reach the side facing the camera
    vec3 n = normalize(V + vec3(0.0, 1.0, 0.0));
    float glow = (v_flags & VX_PART_GLOW) != 0u ? 2.2 : 0.0;
    o_gb0 = vec4(alb.rgb * v_tint.rgb, 1.0);
    o_gb1 = vec4(oct_encode(n) * 0.5 + 0.5, gb_pack_emission(glow, 0.0), float(VX_GB_FOLIAGE) / 3.0);
}
@end

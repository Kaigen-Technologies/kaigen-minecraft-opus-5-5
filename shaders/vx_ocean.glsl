// the open sea: one plane at the water surface out past the horizon, left to the chunk meshes where they draw
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
layout(location = 0) out vec3 v_world;

void main() {
    uint c = uint(gl_VertexIndex);
    vec2 k = vec2(c == 1u || c == 2u || c == 4u ? 1.0 : -1.0, c == 2u || c == 4u || c == 5u ? 1.0 : -1.0);
    v_world = vec3(F.cam_pos.x + k.x * 20000.0, 140.0 + 14.0 / 16.0, F.cam_pos.z + k.y * 20000.0);
    gl_Position = F.view_proj * vec4(v_world - F.cam_pos.xyz, 1.0);
}
@end

@fs
// origin chunk (two words), then 64x64 bits: the chunks a chunk mesh covers
layout(set = 1, binding = 0) readonly buffer _Near { uint m[]; } cover;

layout(location = 0) in vec3 v_world;
layout(location = 0) out vec4 o_gb0;
layout(location = 1) out vec4 o_gb1;

void main() {
    ivec2 ch = ivec2(floor(v_world.xz / 16.0)) - ivec2(int(cover.m[0]), int(cover.m[1]));
    if (all(greaterThanEqual(ch, ivec2(0))) && all(lessThan(ch, ivec2(64)))) {
        uint bit = uint(ch.y * 64 + ch.x);
        if (((cover.m[2u + (bit >> 5u)] >> (bit & 31u)) & 1u) != 0u) discard;
    }
    if (vx_cut_away(v_world)) discard;
    if (dot(v_world.xz, v_world.xz) > VX_RING_R * VX_RING_R) discard;
    o_gb0 = vec4(VX_DEEP_WATER, 1.0);
    o_gb1 = vec4(oct_encode(vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5, gb_pack_emission(0.0, 0.0), float(VX_GB_WATER) / 3.0);
}
@end

// far terrain g-buffer pass: merged quads of the whole island, skipped in chunks whose own mesh draws
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
#include "vx_instance.glsl"
// record layout: src/voxaria/vx_far.c
layout(set = 1, binding = 0) readonly buffer _Quads { uint v[]; } quads;
// origin chunk (two words), then 64x64 bits: the chunks a chunk mesh covers
layout(set = 1, binding = 1) readonly buffer _Near { uint m[]; } cover;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out uint v_face;
layout(location = 4) flat out uint v_map;
layout(location = 5) out float v_ao;
layout(location = 6) out vec3 v_world;

void main() {
    uint vi = uint(gl_VertexIndex);
    uint o = (uint(HZ_INSTANCE_INDEX) + (vi >> 2u)) * 3u;
    uint w0 = quads.v[o];
    uint w1 = quads.v[o + 1u];
    vec3 mn = vec3(float(w0 & 1023u), float((w0 >> 20u) & 511u), float((w0 >> 10u) & 1023u)) - vec3(384.0, 0.0, 384.0);
    uint face = w0 >> 29u;
    ivec2 ch = ivec2(floor(mn.xz / 16.0)) - ivec2(int(cover.m[0]), int(cover.m[1]));
    bool covered = false;
    if (all(greaterThanEqual(ch, ivec2(0))) && all(lessThan(ch, ivec2(64)))) {
        uint bit = uint(ch.y * 64 + ch.x);
        covered = ((cover.m[2u + (bit >> 5u)] >> (bit & 31u)) & 1u) != 0u;
    }
    float ea = float((w1 & 511u) + 1u);
    float eb = float(((w1 >> 9u) & 511u) + 1u);
    uint n = face >> 1u;
    vec3 ext = n == 0u ? vec3(1.0, ea, eb) : n == 1u ? vec3(ea, 1.0, eb) : vec3(ea, eb, 1.0);
    uint cr = vx_face_corner(face, vi & 3u);
    vec3 world = mn + vec3(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u)) * ext;
    uint flags = w1 >> 28u;
    // a water surface sits where the liquid mesher puts a full block's
    if ((flags & 1u) != 0u && face == 2u) world.y -= 2.0 / 16.0;
    gl_Position = covered ? vec4(0.0, 0.0, 0.0, 1.0) : F.view_proj * vec4(world - F.cam_pos.xyz, 1.0);
    v_uv = vec2(0.0);
    v_layer = (w1 >> 18u) & 1023u;
    // far flags (1 water, 2 leaves, 4 lava) onto the g-buffer body's (8 far water, 2 leaves, 16 lava)
    v_flags = ((flags & 1u) != 0u ? 8u : 0u) | ((flags & 2u) != 0u ? 2u : 0u) | ((flags & 4u) != 0u ? 16u : 0u);
    v_face = face;
    v_map = 1u;
    v_ao = 1.0;
    v_world = world;
}
@end

@fs
#define VX_NORMAL_ROT(v) (v)
#define VX_FAR_WATER
#include "vx_gbuffer_fs.glsl"
@end

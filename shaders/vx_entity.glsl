// g-buffer pass for dynamic geometry (characters, held item, particles); camera-relative float vertices
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_uvlf;
layout(location = 2) in vec3 a_light;
layout(location = 3) in vec3 a_tint;
layout(location = 4) in vec3 a_normal;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out uint v_face;
layout(location = 4) flat out uint v_map;
layout(location = 5) out float v_ao;
layout(location = 6) out vec3 v_world;
layout(location = 7) out vec3 v_tint;
layout(location = 8) out vec3 v_normal;

void main() {
    gl_Position = F.view_proj * vec4(a_pos, 1.0);
    v_uv = a_uvlf.xy;
    v_layer = uint(a_uvlf.z + 0.5);
    v_face = uint(a_uvlf.w + 0.5);
    // entity flags (1 cutout, 4 self-lit, 128 held) onto the g-buffer body's (1, 64, 128)
    uint f = uint(a_light.z + 0.5);
    v_flags = (f & 1u) | (f & 8u) | ((f & 4u) != 0u ? 64u : 0u) | (f & 128u);
    v_map = 0u;
    v_ao = 1.0;
    v_world = a_pos + F.cam_pos.xyz;
    v_tint = a_tint;
    v_normal = a_normal;
}
@end

@fs
layout(location = 8) in vec3 v_normal;
#define VX_NORMAL_ROT(v) normalize(v_normal)
#define VX_HAS_TINT
#define VX_ITEM_TEX
#include "vx_gbuffer_fs.glsl"
@end

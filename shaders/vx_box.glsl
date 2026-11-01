// instanced boxes into the g-buffer (puppets, creatures): one record per box, 36 vertices each
#include "vx_common.glsl"
#include "vx_frame.glsl"
#include "vx_box_common.glsl"

@vs
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
    VxBoxVertex b = vx_box_vertex(uint(gl_VertexIndex));
    gl_Position = F.view_proj * vec4(b.rel, 1.0);
    v_uv = b.uv;
    v_layer = b.misc & 4095u;
    // box flags (4 self-lit, 8 near fade, 16 hot glow) onto the g-buffer body's (64, 32, 256)
    uint f = (b.misc >> 12u) & 255u;
    v_flags = ((f & 4u) != 0u ? 64u : 0u) | ((f & 8u) != 0u ? 32u : 0u) | ((f & 16u) != 0u ? 256u : 0u);
    v_face = b.face;
    v_map = 0u;
    v_ao = 1.0;
    v_world = b.rel + F.cam_pos.xyz;
    vec4 t = unpackUnorm4x8(b.tint);
    // srgb tint to linear, pushed toward white by the hit flash in alpha, keeping the creature readable
    v_tint = mix(pow(t.rgb, vec3(2.2)), vec3(2.0), t.a * 0.7);
    v_normal = b.normal;
}
@end

@fs
layout(location = 8) in vec3 v_normal;
#define VX_NORMAL_ROT(v) normalize(v_normal)
#define VX_HAS_TINT
#define VX_NEAR_FADE
#include "vx_gbuffer_fs.glsl"
@end

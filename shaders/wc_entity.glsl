// g-buffer pass for dynamic geometry (held item, particles, fireflies); camera-relative float vertices
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _EntityParams {
    mat4 normal_rot;
} P;

@vs
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_uvlf;
layout(location = 2) in vec3 a_light;
layout(location = 3) in vec3 a_tint;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out uint v_face;
layout(location = 4) out vec3 v_tint;
layout(location = 5) out vec3 v_light_ao;
layout(location = 6) out vec3 v_world;

void main() {
    gl_Position = F.view_proj * vec4(a_pos, 1.0);
    v_uv = a_uvlf.xy;
    v_layer = uint(a_uvlf.z + 0.5);
    v_face = uint(a_uvlf.w + 0.5);
    v_flags = uint(a_light.z + 0.5);
    v_light_ao = vec3(a_light.x, a_light.y, 1.0);
    v_tint = pow(a_tint, vec3(2.2));
    v_world = a_pos + F.cam_pos.xyz;
}
@end

@fs
#define WC_NORMAL_ROT(v) (mat3(P.normal_rot) * (v))
#include "wc_gbuffer_fs.glsl"
@end

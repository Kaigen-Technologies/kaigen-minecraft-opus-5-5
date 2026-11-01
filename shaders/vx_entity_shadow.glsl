// entity shadow depth into the per-frame cascades; same vertex layout as vx_entity
#include "vx_common.glsl"
#include "vx_frame.glsl"

layout(binding = 1) uniform _EntityShadowParams {
    mat4 light_vp;
} P;

@vs
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_uvlf;
layout(location = 2) in vec3 a_light;
layout(location = 3) in vec3 a_tint;
layout(location = 4) in vec3 a_normal;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_cut;

void main() {
    gl_Position = P.light_vp * vec4(a_pos, 1.0);
    v_uv = a_uvlf.xy;
    v_layer = uint(a_uvlf.z + 0.5);
    v_cut = uint(a_light.z + 0.5) & 9u;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 2) uniform texture2DArray item_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_cut;
void main() {
    if (v_cut != 0u) {
        // item sprites span their 16x16 layer; block tiles hold one block per quarter
        float a = (v_cut & 8u) != 0u ? textureLod(sampler2DArray(item_tex, smp_linear), vec3(v_uv, float(v_layer)), 0.0).a
                                     : textureLod(sampler2DArray(albedo_tex, smp_linear), vec3(v_uv * 0.5, float(v_layer)), 2.0).a;
        if (a < 0.5) discard;
    }
}
@end

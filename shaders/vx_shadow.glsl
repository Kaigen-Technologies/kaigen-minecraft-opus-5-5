// terrain shadow depth pass (atlas cascade or water surface map)
#include "vx_common.glsl"
#include "vx_frame.glsl"

layout(binding = 1) uniform _ShadowParams {
    mat4 light_vp;
} P;

@vs
#include "vx_quad.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_cut;
layout(location = 3) flat out uint v_face;
layout(location = 4) out vec3 v_world;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    VxDraw dr = draws.d[ent.y];
    uint k = vi & 3u;
    uint o = ent.x * VX_QUAD_U32;
    uint w0 = quads.v[o];
    uint w1 = quads.v[o + 1u];
    uint w2 = quads.v[o + 2u];
    uint flags = (w1 >> 10u) & 63u;
    // depth needs no diagonal flip: both triangulations cover the same planar quad
    vec3 rel = dr.origin + vx_quad_local(w0, w2, k, k, dr.origin);
    vec3 world = rel + F.cam_pos.xyz;
    uint wave = vx_quad_wave(w0, w1, k);
    if (wave == VX_WAVE_LEAVES) rel += wave_leaves(world, F.cam_pos.w, F.weather.z);
    else if (wave == VX_WAVE_PLANT) rel += wave_plant(world, F.cam_pos.w, F.weather.z);
    gl_Position = P.light_vp * vec4(rel, 1.0);
    v_uv = vx_quad_uv(w0, w2, k, k);
    v_layer = w1 & 1023u;
    v_cut = flags & 1u;
    uint shape = (w0 >> 19u) & 3u;
    // faces 0..5 of cubes and models sample their tile in world space, like the g-buffer; 7 marks a sprite
    v_face = shape == VX_QUAD_CUBE || shape == VX_QUAD_BOX ? (w0 >> 16u) & 7u : 7u;
    v_world = world;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_cut;
layout(location = 3) flat in uint v_face;
layout(location = 4) in vec3 v_world;
void main() {
    if (v_cut != 0u) {
        vec2 uv = v_uv * 0.5;
        if (v_face < 6u) {
            vec3 fn, ft, fb;
            face_basis(v_face, fn, ft, fb);
            uv = fract(vec2(dot(v_world, ft), dot(v_world, fb)) * 0.5);
        }
        if (textureLod(sampler2DArray(albedo_tex, smp_linear), vec3(uv, float(v_layer)), 2.0).a < 0.5) discard;
    }
}
@end

// terrain shadow depth pass (atlas cascade or water surface map)
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _ShadowParams {
    mat4 light_vp;
} P;

@vs
#include "wc_quad.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_cut;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    WcDraw dr = draws.d[ent.y];
    uint k = vi & 3u;
    uint o = ent.x * WC_QUAD_U32;
    uint w0 = quads.v[o];
    uint w1 = quads.v[o + 1u];
    uint flags = (w1 >> 10u) & 63u;
    // depth needs no diagonal flip: both triangulations cover the same planar quad
    vec3 rel = dr.origin + wc_quad_local(o, w0, k, k);
    vec3 world = rel + F.cam_pos.xyz;
    uint wave = wc_quad_wave(w0, w1, k);
    if (wave == WC_WAVE_LEAVES) rel += wave_leaves(world, F.cam_pos.w, F.weather.z);
    else if (wave == WC_WAVE_PLANT) rel += wave_plant(world, F.cam_pos.w, F.weather.z);
    gl_Position = P.light_vp * vec4(rel, 1.0);
    v_uv = wc_quad_uv(w0, k, k);
    v_layer = w1 & 1023u;
    v_cut = flags & 1u;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_cut;
void main() {
    if (v_cut != 0u) {
        ivec3 tc = ivec3(clamp(ivec2(floor(v_uv * 16.0)), ivec2(0), ivec2(15)), int(v_layer));
        if (texelFetch(sampler2DArray(albedo_tex, smp_linear), tc, 0).a < 0.5) discard;
    }
}
@end

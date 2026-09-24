// terrain g-buffer pass: quads pulled from a mesh pool, one draw record per column
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
#include "wc_quad.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out uint v_face;
layout(location = 4) out vec3 v_tint;
layout(location = 5) out vec3 v_light_ao;
layout(location = 6) out vec3 v_world;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    WcDraw dr = draws.d[ent.y];
    WcQuadVertex q = wc_quad_vertex(ent.x, vi & 3u);
    vec3 rel = dr.origin + q.local;
    vec3 world = rel + F.cam_pos.xyz;
    if (q.wave == WC_WAVE_LEAVES) rel += wave_leaves(world, F.cam_pos.w, F.weather.z);
    else if (q.wave == WC_WAVE_PLANT) rel += wave_plant(world, F.cam_pos.w, F.weather.z);
    gl_Position = F.view_proj * vec4(rel, 1.0);
    v_uv = q.uv;
    v_layer = q.tex_flags & 1023u;
    v_flags = q.tex_flags >> 10u;
    v_face = q.face;
    v_light_ao = vec3(q.sky, q.block, q.ao);
    v_tint = pow(q.tint, vec3(2.2));
    v_world = world;
}
@end

@fs
#define WC_NORMAL_ROT(v) (v)
#include "wc_gbuffer_fs.glsl"
@end

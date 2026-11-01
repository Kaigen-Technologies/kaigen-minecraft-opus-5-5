// terrain g-buffer pass: quads pulled from a mesh pool, one draw record per column
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
#include "vx_quad.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_layer;
layout(location = 2) flat out uint v_flags;
layout(location = 3) flat out uint v_face;
layout(location = 4) flat out uint v_map;
layout(location = 5) out float v_ao;
layout(location = 6) out vec3 v_world;

void main() {
    uint vi = uint(gl_VertexIndex);
    uvec2 ent = visible.v[uint(HZ_INSTANCE_INDEX) + (vi >> 2u)];
    VxDraw dr = draws.d[ent.y];
    VxQuadVertex q = vx_quad_vertex(ent.x, vi & 3u, dr.origin);
    vec3 rel = dr.origin + q.local;
    vec3 world = rel + F.cam_pos.xyz;
    if (q.wave == VX_WAVE_LEAVES) rel += wave_leaves(world, F.cam_pos.w, F.weather.z);
    else if (q.wave == VX_WAVE_PLANT) rel += wave_plant(world, F.cam_pos.w, F.weather.z);
    gl_Position = F.view_proj * vec4(rel, 1.0);
    v_uv = q.uv;
    v_layer = q.layer;
    v_flags = q.flags;
    v_face = q.face;
    v_map = q.map;
    v_ao = q.ao;
    v_world = world;
}
@end

@fs
#define VX_NORMAL_ROT(v) (v)
#include "vx_gbuffer_fs.glsl"
@end

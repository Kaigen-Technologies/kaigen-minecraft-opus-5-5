// instanced box depth into the per-frame shadow cascades
#include "vx_common.glsl"
#include "vx_frame.glsl"
#include "vx_box_common.glsl"

layout(binding = 1) uniform _BoxShadowParams {
    mat4 light_vp;
} P;

@vs
void main() {
    VxBoxVertex b = vx_box_vertex(uint(gl_VertexIndex));
    gl_Position = P.light_vp * vec4(b.rel, 1.0);
}
@end

@fs
void main() {}
@end

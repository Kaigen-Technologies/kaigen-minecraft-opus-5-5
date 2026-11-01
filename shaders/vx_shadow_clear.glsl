// clears the current viewport of a depth target to the far plane (reverse-z 0)
#include "vx_common.glsl"

@vs
void main() {
    vec2 uv;
    vec4 p = vx_fullscreen(gl_VertexIndex, uv);
    gl_Position = vec4(p.xy, 0.0, 1.0);
}
@end

@fs
void main() {}
@end

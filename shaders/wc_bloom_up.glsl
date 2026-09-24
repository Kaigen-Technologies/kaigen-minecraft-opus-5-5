// bloom 3x3 tent upsample, additively blended into the next larger level
#include "wc_common.glsl"

layout(binding = 0) uniform _BloomUpParams {
    vec4 params; // xy source texel size, z radius
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D src_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 tap(vec2 o) { return textureLod(sampler2D(src_tex, smp_clamp), v_uv + o, 0.0).rgb; }

void main() {
    vec2 t = P.params.xy * P.params.z;
    vec3 s = tap(vec2(-t.x, t.y));
    s += tap(vec2(0.0, t.y)) * 2.0;
    s += tap(vec2(t.x, t.y));
    s += tap(vec2(-t.x, 0.0)) * 2.0;
    s += tap(vec2(0.0)) * 4.0;
    s += tap(vec2(t.x, 0.0)) * 2.0;
    s += tap(vec2(-t.x, -t.y));
    s += tap(vec2(0.0, -t.y)) * 2.0;
    s += tap(vec2(t.x, -t.y));
    frag_color = vec4(s / 16.0, 1.0);
}
@end

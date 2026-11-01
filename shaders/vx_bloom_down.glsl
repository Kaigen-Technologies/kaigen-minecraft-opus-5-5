// bloom 13-tap downsample (Jimenez 2014)
#include "vx_common.glsl"

layout(binding = 0) uniform _BloomParams {
    vec4 params; // xy source texel size, z first level, w threshold
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D src_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 tap(vec2 o) { return textureLod(sampler2D(src_tex, smp_clamp), v_uv + P.params.xy * o, 0.0).rgb; }

void main() {
    vec3 a = tap(vec2(-2.0, 2.0));
    vec3 b = tap(vec2(0.0, 2.0));
    vec3 c = tap(vec2(2.0, 2.0));
    vec3 d = tap(vec2(-2.0, 0.0));
    vec3 e = tap(vec2(0.0, 0.0));
    vec3 f = tap(vec2(2.0, 0.0));
    vec3 g = tap(vec2(-2.0, -2.0));
    vec3 h = tap(vec2(0.0, -2.0));
    vec3 i = tap(vec2(2.0, -2.0));
    vec3 j = tap(vec2(-1.0, 1.0));
    vec3 k = tap(vec2(1.0, 1.0));
    vec3 l = tap(vec2(-1.0, -1.0));
    vec3 m = tap(vec2(1.0, -1.0));
    vec3 r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    // first level: only light above the threshold blooms (emission, lava, the sun), with a soft knee
    if (P.params.z > 0.5) {
        r = min(r, vec3(60.0));
        float l = max(r.r, max(r.g, r.b));
        float knee = P.params.w * 0.5;
        float soft = clamp(l - P.params.w + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-5);
        r *= max(soft, l - P.params.w) / max(l, 1e-5);
    }
    frag_color = vec4(max(r, vec3(0.0)), 1.0);
}
@end

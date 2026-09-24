// auto exposure: centre-weighted average log luminance, adapted over time (1x1 output)
#include "wc_common.glsl"

layout(binding = 0) uniform _ExposureParams {
    vec4 params; // x dt, y min exposure, z max exposure, w compensation
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D src_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D prev_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    float sum = 0.0;
    float wsum = 0.0;
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 16; x++) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / vec2(16.0, 12.0);
            float l = luminance(textureLod(sampler2D(src_tex, smp_clamp), uv, 0.0).rgb);
            vec2 c = uv - 0.5;
            float w = exp(-dot(c, c) * 3.0);
            sum += log2(max(l, 1e-5)) * w;
            wsum += w;
        }
    }
    float avg = exp2(sum / wsum);
    float target = clamp(0.14 * exp2(P.params.w) / max(avg, 1e-5), P.params.y, P.params.z);
    float prev = texelFetch(sampler2D(prev_tex, smp_clamp), ivec2(0), 0).r;
    if (!(prev > 0.0) || prev > 1e6) prev = target;
    float speed = target > prev ? 1.1 : 2.2;
    float e = exp2(mix(log2(prev), log2(target), 1.0 - exp(-P.params.x * speed)));
    frag_color = vec4(e, avg, 0.0, 1.0);
}
@end

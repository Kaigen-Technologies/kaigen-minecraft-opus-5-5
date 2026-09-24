// temporal accumulation of the cloud raymarch: one texel per 2x2 block is fresh each frame, the rest reproject
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _CloudTemporalParams {
    vec4 low_res; // march target w, h, reset, unused
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D current_tex;
layout(set = 1, binding = 1) uniform texture2D history_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 q = px >> 1;
    ivec2 full = ivec2(textureSize(sampler2D(history_tex, smp_clamp), 0));
    bool fresh = all(equal(min(q * 2 + half_res_offset(), full - 1), px));
    vec2 quv = (vec2(q) + 0.5) / P.low_res.xy;
    vec4 cur = fresh ? texelFetch(sampler2D(current_tex, smp_clamp), q, 0) : textureLod(sampler2D(current_tex, smp_clamp), v_uv, 0.0);
    if (P.low_res.z > 0.5) {
        frag_color = cur;
        return;
    }
    vec3 dir = view_ray(v_uv);
    vec4 pc = F.prev_view_proj * vec4(dir * 5000.0, 1.0);
    vec2 puv = clip_to_uv(pc);
    if (pc.w <= 0.0 || any(lessThan(puv, vec2(0.0))) || any(greaterThan(puv, vec2(1.0)))) {
        frag_color = cur;
        return;
    }
    vec4 hist = textureLod(sampler2D(history_tex, smp_clamp), puv, 0.0);
    vec2 texel = 1.0 / P.low_res.xy;
    vec4 mn = cur;
    vec4 mx = cur;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec4 s = textureLod(sampler2D(current_tex, smp_clamp), quv + vec2(float(x), float(y)) * texel, 0.0);
            mn = min(mn, s);
            mx = max(mx, s);
        }
    }
    vec4 range = mx - mn;
    hist = clamp(hist, mn - range * 0.4, mx + range * 0.4);
    frag_color = fresh ? mix(cur, hist, 0.9) : hist;
}
@end

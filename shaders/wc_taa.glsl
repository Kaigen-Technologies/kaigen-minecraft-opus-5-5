// temporal anti-aliasing and upscaling: closest-depth reprojection, ycocg variance clipping, catmull-rom history
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _TaaParams {
    vec4 params;  // x reset, y upscaling
    vec4 out_res; // history w, h, 1/w, 1/h
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D color_tex;
layout(set = 1, binding = 1) uniform texture2D history_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D depth_tex;
layout(set = 1, binding = 3) uniform texture2D glight_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 to_ycocg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 from_ycocg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
vec3 tm(vec3 c) { return c / (1.0 + luminance(c)); }
vec3 itm(vec3 c) { return c / max(1e-4, 1.0 - luminance(c)); }

vec3 hist_at(vec2 uv) { return textureLod(sampler2D(history_tex, smp_clamp), uv, 0.0).rgb; }

// 5-tap catmull-rom
vec3 sample_history(vec2 uv) {
    vec2 sample_pos = uv * P.out_res.xy;
    vec2 tex_pos1 = floor(sample_pos - 0.5) + 0.5;
    vec2 f = sample_pos - tex_pos1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 tp0 = (tex_pos1 - 1.0) * P.out_res.zw;
    vec2 tp3 = (tex_pos1 + 2.0) * P.out_res.zw;
    vec2 tp12 = (tex_pos1 + offset12) * P.out_res.zw;
    vec3 result = hist_at(vec2(tp12.x, tp0.y)) * w12.x * w0.y;
    result += hist_at(vec2(tp0.x, tp12.y)) * w0.x * w12.y;
    result += hist_at(vec2(tp12.x, tp12.y)) * w12.x * w12.y;
    result += hist_at(vec2(tp3.x, tp12.y)) * w3.x * w12.y;
    result += hist_at(vec2(tp12.x, tp3.y)) * w12.x * w3.y;
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(result / wsum, vec3(0.0));
}

void main() {
    // upscaling: the frame's nearest jittered sample counts by how close it lands to this output pixel's centre
    bool up = P.params.y > 0.5;
    vec2 joff = vec2(0.5 * F.jitter.x, -0.5 * F.jitter.y);
    ivec2 max_p = ivec2(F.res.xy) - 1;
    ivec2 ip = up ? clamp(ivec2((v_uv + joff) * F.res.xy), ivec2(0), max_p) : ivec2(gl_FragCoord.xy);
    vec3 cur = texelFetch(sampler2D(color_tex, smp_clamp), ip, 0).rgb;
    vec2 d_px = ((vec2(ip) + 0.5) * F.res.zw - joff - v_uv) * P.out_res.xy;
    float sample_w = up ? exp(-2.29 * dot(d_px, d_px)) : 1.0;
    if (P.params.x > 0.5) {
        frag_color = vec4(up ? textureLod(sampler2D(color_tex, smp_clamp), v_uv + joff, 0.0).rgb : cur, 1.0);
        return;
    }
    // neighbourhood statistics (tonemapped ycocg)
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    float closest = 0.0;
    ivec2 closest_p = ip;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            ivec2 q = clamp(ip + ivec2(x, y), ivec2(0), max_p);
            vec3 c = to_ycocg(tm(texelFetch(sampler2D(color_tex, smp_clamp), q, 0).rgb));
            m1 += c;
            m2 += c * c;
            float d = texelFetch(sampler2D(depth_tex, smp_clamp), q, 0).r;
            if (d > closest) {
                closest = d;
                closest_p = q;
            }
        }
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    // reproject the closest depth (reduces edge ghosting); homogeneous so the sky reprojects as a direction
    vec2 uv = (vec2(closest_p) + 0.5) * F.res.zw;
    vec4 h = F.inv_view_proj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, closest, 1.0);
    vec2 prev_uv = clip_to_uv(F.prev_view_proj * vec4(h.xyz + F.cam_delta.xyz * h.w, h.w));
    vec2 motion = prev_uv - uv;
    // the held item moves with the camera: no reprojection
    if ((int(texelFetch(sampler2D(glight_tex, smp_clamp), closest_p, 0).a * 255.0 + 0.5) & 128) != 0) motion = vec2(0.0);
    vec2 hist_uv = v_uv + motion;
    bool offscreen = any(lessThan(hist_uv, vec2(0.0))) || any(greaterThan(hist_uv, vec2(1.0)));
    vec3 hist = sample_history(hist_uv);
    vec3 hist_y = to_ycocg(tm(hist));
    float motion_px = length(motion * P.out_res.xy);
    float gamma = mix(1.25, 0.9, saturate(motion_px / 8.0));
    vec3 bmin = mean - gamma * sigma;
    vec3 bmax = mean + gamma * sigma;
    // clip towards the mean
    vec3 center = 0.5 * (bmax + bmin);
    vec3 ext = 0.5 * (bmax - bmin) + 1e-5;
    vec3 v = hist_y - center;
    vec3 a = abs(v / ext);
    float ma = max(a.x, max(a.y, a.z));
    if (ma > 1.0) hist_y = center + v / ma;
    hist = itm(from_ycocg(hist_y));
    float w = offscreen ? 0.0 : mix(0.92, 0.82, saturate(motion_px / 12.0));
    // an output pixel takes the frame only as far as its sample lands on it
    if (up) w = offscreen ? 0.0 : 1.0 - saturate((1.0 - w) * 2.0 * sample_w);
    if (up && offscreen) cur = textureLod(sampler2D(color_tex, smp_clamp), v_uv + joff, 0.0).rgb;
    vec3 res = mix(tm(cur), tm(hist), w);
    frag_color = vec4(max(itm(res), vec3(0.0)), 1.0);
}
@end

// final composite: heat/underwater distortion, sharpen, bloom, clamped exposure, hue-keeping filmic, grade, outline, vignette
#include "vx_common.glsl"
#include "vx_frame.glsl"

layout(binding = 1) uniform _FinalParams {
    vec4 grade;     // bloom strength, sharpen, saturation, hue shift (radians)
    vec4 lift;      // rgb lift, w vignette
    vec4 gain;      // rgb gain, w gamma
    vec4 sel_min;   // xyz camera-relative selection min, w active
    vec4 sel_max;   // xyz camera-relative selection max, w fixed exposure (> 0 for debug views)
    vec4 crosshair; // xy output size px, z device pixel ratio, w enabled
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D color_tex;
layout(set = 1, binding = 1) uniform texture2D bloom_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D exposure_tex;
@unfilterable
layout(set = 1, binding = 3) uniform texture2D depth_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 linear_to_srgb(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
vec3 srgb_to_linear(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

// filmic shoulder on the brightest channel keeps hue and saturation; a little per-channel roll-off lets hot emission go white
vec3 tonemap(vec3 c) {
    const float W = 7.0;
    float peak = max(c.r, max(c.g, c.b));
    float mapped = peak * (1.0 + peak / (W * W)) / (1.0 + peak);
    vec3 keep = c * (mapped / max(peak, 1e-5));
    vec3 per = c * (1.0 + c / (W * W)) / (1.0 + c);
    return mix(keep, per, 0.3);
}

vec3 hue_rotate(vec3 c, float a) {
    const vec3 k = vec3(0.57735);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

vec3 color_at(vec2 uv) { return textureLod(sampler2D(color_tex, smp_clamp), uv, 0.0).rgb; }

void main() {
    vec2 uv = v_uv;
    float t = F.cam_pos.w;
    if (F.world.x > 0.5) uv += vec2(sin(uv.y * 24.0 + t * 2.2), cos(uv.x * 19.0 + t * 1.7)) * 0.0022;
    // heat shimmer over the underworld, strongest low on the screen
    float heat = F.under_color.w;
    if (heat > 0.0) uv += vec2(sin(uv.y * 60.0 + t * 5.0) * 0.0016, cos(uv.x * 40.0 + t * 4.0) * 0.001) * heat * (0.4 + uv.y);
    vec3 col = color_at(uv);
    float sharpen = P.grade.y;
    if (sharpen > 0.0) {
        // the resolved image is at the output size when taa upscales
        vec2 tx = 1.0 / vec2(textureSize(sampler2D(color_tex, smp_clamp), 0));
        vec3 n = color_at(uv + vec2(0.0, tx.y)) + color_at(uv - vec2(0.0, tx.y)) +
                 color_at(uv + vec2(tx.x, 0.0)) + color_at(uv - vec2(tx.x, 0.0));
        vec3 sharp = col + (col * 4.0 - n) * sharpen * 0.25;
        col = max(mix(col, sharp, step(luminance(col), 30.0)), vec3(0.0));
    }
    col += textureLod(sampler2D(bloom_tex, smp_clamp), uv, 0.0).rgb * P.grade.x;
    vec4 ex = texelFetch(sampler2D(exposure_tex, smp_clamp), ivec2(0), 0);
    col *= P.sel_max.w > 0.0 ? P.sel_max.w : ex.r;

    vec3 g = tonemap(col);
    // grade: lift, gain, gamma, saturation, hue
    g = g * P.gain.rgb + P.lift.rgb * (1.0 - g);
    g = pow(max(g, vec3(0.0)), vec3(1.0 / max(P.gain.w, 0.01)));
    float l = luminance(g);
    g = max(mix(vec3(l), g, P.grade.z), vec3(0.0));
    if (P.grade.w != 0.0) g = max(hue_rotate(g, P.grade.w), vec3(0.0));

    // block selection outline (screen-space, depth aware)
    if (P.sel_min.w > 0.5) {
        float d = texelFetch(sampler2D(depth_tex, smp_clamp), uv_to_texel(v_uv), 0).r;
        if (d > 0.0) {
            vec3 p = reconstruct_rel(v_uv, d);
            float w = max(0.008, length(p) * 0.0045);
            vec3 lo = P.sel_min.xyz - 0.004;
            vec3 hi = P.sel_max.xyz + 0.004;
            if (all(greaterThan(p, lo - w)) && all(lessThan(p, hi + w))) {
                vec3 e = min(abs(p - lo), abs(p - hi));
                int edges = int(e.x < w) + int(e.y < w) + int(e.z < w);
                // a warm edge, and the block's visible faces lifted a little
                if (edges >= 2) g = mix(g, vec3(1.0, 0.84, 0.36), 0.85);
                else g = g * 1.15 + 0.025;
            }
        }
    }

    vec2 c = v_uv - 0.5;
    g *= mix(1.0, smoothstep(0.95, 0.25, length(c * vec2(1.1, 1.0))), P.lift.w);
    // dither in display space; the swapchain view re-encodes to srgb
    vec3 outc = linear_to_srgb(g) + (hash12(gl_FragCoord.xy + fract(t) * 91.0) - 0.5) / 255.0;
    // crosshair: 18px arms, 2px thick, difference-blended with white
    if (P.crosshair.w > 0.5) {
        vec2 d = abs(gl_FragCoord.xy - P.crosshair.xy * 0.5);
        float dpr = P.crosshair.z;
        if ((d.x < dpr && d.y < 9.0 * dpr) || (d.y < dpr && d.x < 9.0 * dpr)) outc = 1.0 - saturate(outc);
    }
    frag_color = vec4(srgb_to_linear(outc), 1.0);
}
@end

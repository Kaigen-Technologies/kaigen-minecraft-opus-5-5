// final composite: sharpen, bloom, exposure, purkinje shift, agx, selection outline, vignette, dither
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _FinalParams {
    vec4 grade;   // bloom strength, sharpen, saturation, contrast
    vec4 sel_min; // xyz camera-relative selection min, w active
    vec4 sel_max; // xyz camera-relative selection max, w fixed exposure (> 0 for debug views)
    vec4 crosshair; // xy output size px, z device pixel ratio, w enabled
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
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

vec3 agx_contrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 agx(vec3 val) {
    mat3 agx_mat = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                        0.0784335999999992, 0.878468636469772, 0.0784336,
                        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    float min_ev = -12.47393;
    float max_ev = 4.026069;
    val = agx_mat * val;
    val = clamp(log2(max(val, vec3(1e-10))), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
    return agx_contrast(val);
}
vec3 agx_eotf(vec3 val) {
    mat3 agx_mat_inv = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                            -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                            -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    val = agx_mat_inv * val;
    return pow(max(val, vec3(0.0)), vec3(2.2));
}
vec3 agx_look(vec3 val, float sat, float power) {
    float l = dot(val, vec3(0.2126, 0.7152, 0.0722));
    val = pow(max(val, vec3(0.0)), vec3(power));
    return l + sat * (val - l);
}
vec3 linear_to_srgb(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
vec3 srgb_to_linear(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 color_at(vec2 uv) { return textureLod(sampler2D(color_tex, smp_clamp), uv, 0.0).rgb; }

void main() {
    vec2 uv = v_uv;
    if (F.world.x > 0.5) {
        float t = F.cam_pos.w;
        uv += vec2(sin(uv.y * 24.0 + t * 2.2), cos(uv.x * 19.0 + t * 1.7)) * 0.0022;
    }
    vec3 col = color_at(uv);
    float sharpen = P.grade.y;
    if (sharpen > 0.0) {
        // the resolved image is at the output size when taa upscales
        vec2 tx = 1.0 / vec2(textureSize(sampler2D(color_tex, smp_clamp), 0));
        vec3 n = color_at(uv + vec2(0.0, tx.y)) + color_at(uv - vec2(0.0, tx.y)) +
                 color_at(uv + vec2(tx.x, 0.0)) + color_at(uv - vec2(tx.x, 0.0));
        vec3 sharp = col + (col * 4.0 - n) * sharpen * 0.25;
        col = max(mix(col, sharp, step(luminance(col), 50.0)), vec3(0.0));
    }
    vec3 bloom = textureLod(sampler2D(bloom_tex, smp_clamp), uv, 0.0).rgb;
    col = mix(col, bloom, P.grade.x);
    vec4 ex = texelFetch(sampler2D(exposure_tex, smp_clamp), ivec2(0), 0);
    col *= P.sel_max.w > 0.0 ? P.sel_max.w : ex.r;

    // purkinje-like night shift: desaturate and tint blue in very dark scenes
    float night = saturate(1.0 - log2(max(ex.g, 1e-5) * 400.0) / 4.0) * 0.55;
    float l = luminance(col);
    col = mix(col, vec3(l) * vec3(0.72, 0.85, 1.25), night * saturate(1.0 - l * 2.0));

    vec3 lin = agx_eotf(agx_look(agx(col), P.grade.z, P.grade.w));

    // block selection outline (screen-space, depth aware)
    if (P.sel_min.w > 0.5) {
        float d = texelFetch(sampler2D(depth_tex, smp_clamp), uv_to_texel(v_uv), 0).r;
        if (d > 0.0) {
            vec3 p = reconstruct_rel(v_uv, d);
            float w = max(0.004, length(p) * 0.0022);
            vec3 lo = P.sel_min.xyz - 0.004;
            vec3 hi = P.sel_max.xyz + 0.004;
            if (all(greaterThan(p, lo - w)) && all(lessThan(p, hi + w))) {
                vec3 e = min(abs(p - lo), abs(p - hi));
                int edges = int(e.x < w) + int(e.y < w) + int(e.z < w);
                if (edges >= 2) lin = mix(lin, vec3(0.02), 0.75);
            }
        }
    }

    // vignette
    vec2 c = v_uv - 0.5;
    lin *= mix(1.0, smoothstep(0.95, 0.25, length(c * vec2(1.1, 1.0))), 0.35);
    // dither in display space; the swapchain view re-encodes to srgb
    vec3 outc = linear_to_srgb(lin) + (hash12(gl_FragCoord.xy + fract(F.cam_pos.w) * 91.0) - 0.5) / 255.0;
    // crosshair: 18px arms, 2px thick, difference-blended with white
    if (P.crosshair.w > 0.5) {
        vec2 d = abs(gl_FragCoord.xy - P.crosshair.xy * 0.5);
        float dpr = P.crosshair.z;
        if ((d.x < dpr && d.y < 9.0 * dpr) || (d.y < dpr && d.x < 9.0 * dpr)) outc = 1.0 - saturate(outc);
    }
    frag_color = vec4(srgb_to_linear(outc), 1.0);
}
@end

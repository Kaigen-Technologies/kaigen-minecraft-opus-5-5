// screen-space ambient occlusion at half res (hemisphere samples against the depth buffer)
// r = ao, g = linear depth, ba = oct geometric normal of the full-res texel it sampled
#include "wc_common.glsl"
#include "wc_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
@unfilterable
layout(set = 1, binding = 0) uniform texture2D depth_tex;
layout(set = 1, binding = 1) uniform texture2D gnormal_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float depth_at(vec2 uv) { return texelFetch(sampler2D(depth_tex, smp_clamp), uv_to_texel(uv), 0).r; }

void main() {
    ivec2 px = half_res_texel(ivec2(gl_FragCoord.xy));
    vec2 uv = (vec2(px) + 0.5) * F.res.zw;
    float d = texelFetch(sampler2D(depth_tex, smp_clamp), px, 0).r;
    if (d <= 0.0) {
        frag_color = vec4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    vec3 p = reconstruct_rel(uv, d);
    vec2 n_oct = texelFetch(sampler2D(gnormal_tex, smp_clamp), px, 0).zw;
    vec3 n = oct_decode(n_oct);
    float dist = length(p);
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    float j = ign_t(vec2(px));
    float j2 = fract(j * 7.31 + 0.37);
    float radius = 0.9;
    float occ = 0.0;
    p += n * 0.02;
    for (int i = 0; i < 10; i++) {
        float fi = (float(i) + j2) / 10.0;
        float ang = float(i) * 2.39996 + j * 6.2831853;
        float r = sqrt(fi);
        float z = sqrt(max(0.0, 1.0 - fi));
        vec3 dir = t * (cos(ang) * r) + b * (sin(ang) * r) + n * z;
        float scale = mix(0.15, 1.0, fract(fi * 3.7 + j));
        vec4 clip = F.view_proj * vec4(p + dir * radius * scale, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 suv = clip_to_uv(clip);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float delta = clip.w - linear_depth(depth_at(suv));
        if (delta > 0.03) occ += smoothstep(0.0, 1.0, radius * 1.2 / delta);
    }
    float ao = 1.0 - occ / 10.0;
    ao = mix(1.0, ao, smoothstep(160.0, 60.0, dist));
    frag_color = vec4(pow(saturate(ao), 1.4), linear_depth(d), n_oct);
}
@end

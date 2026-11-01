// half-res shadowed march of the near fog: r = shadowed in-scatter weight, g = transmittance, b = march length
#include "vx_common.glsl"
#include "vx_frame.glsl"

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
@unfilterable
layout(set = 1, binding = 0) uniform texture2D depth_tex;
@depth
layout(set = 1, binding = 1) uniform texture2D shadow_cmp_tex;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#include "vx_lighting.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float fog_density(float y) { return F.fog.x * exp(-max(y - 140.0, 0.0) * F.fog.y); }

void main() {
    ivec2 p = half_res_texel(ivec2(gl_FragCoord.xy));
    vec2 uv = (vec2(p) + 0.5) * F.res.zw;
    float depth = texelFetch(sampler2D(depth_tex, smp_clamp), p, 0).r;
    vec3 dir = view_ray(uv);
    float dist = depth <= 0.0 ? 1e5 : length(reconstruct_rel(uv, depth));
    float t0 = min(vx_cut_start(dir), dist);
    float jitter = ign_t(vec2(p));
    bool lit = F.light_color.w > 0.0;

    if (F.world.x > 0.5) {
        float max_d = min(dist - t0, 32.0);
        float shafts = 0.0;
        if (lit)
            for (int i = 0; i < 10; i++) {
                float t = t0 + (float(i) + jitter) / 10.0 * max_d;
                shafts += shadow_cheap(dir * t, t) * exp(-0.1 * t);
            }
        frag_color = vec4(shafts / 10.0, 1.0, max_d, 0.0);
        return;
    }

    float max_d = min(dist - t0, 96.0);
    float dt = max_d / 14.0;
    float lit_w = 0.0;
    float trans = 1.0;
    for (int i = 0; i < 14; i++) {
        float t = t0 + (float(i) + jitter) * dt;
        vec3 q = dir * t;
        float Ts = exp(-fog_density(q.y + F.cam_pos.y) * dt);
        if (lit) lit_w += trans * (1.0 - Ts) * shadow_cheap(q, t);
        trans *= Ts;
    }
    frag_color = vec4(lit_w, trans, max_d, 0.0);
}
@end

// g-buffer fragment body shared by terrain and entities; the stage defines WC_NORMAL_ROT(v)
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 1) uniform texture2DArray normal_tex;
layout(set = 1, binding = 2) uniform texture2DArray spec_tex;
@aniso
layout(set = 1, binding = 16) uniform sampler smp_block;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_flags;
layout(location = 3) flat in uint v_face;
layout(location = 4) in vec3 v_tint;
layout(location = 5) in vec3 v_light_ao;
layout(location = 6) in vec3 v_world;

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_light;
layout(location = 3) out vec4 o_spec;

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec3 uvl = vec3(v_uv, float(v_layer));
    // pixel-exact texels when magnified (crisp pixel art), trilinear when minified
    vec2 gx = dFdx(v_uv);
    vec2 gy = dFdy(v_uv);
    float lod = 0.5 * log2(max(dot(gx, gx), dot(gy, gy)) * 256.0);
    bool magnified = lod < -0.25;
    ivec3 tc = ivec3(clamp(ivec2(floor(v_uv * 16.0)), ivec2(0), ivec2(15)), int(v_layer));
    vec4 alb = magnified ? texelFetch(sampler2DArray(albedo_tex, smp_block), tc, 0)
                         : textureGrad(sampler2DArray(albedo_tex, smp_block), uvl, gx, gy);
    bool cutout = (v_flags & 1u) != 0u;
    if (cutout && alb.a < 0.5) discard;
    vec3 albedo = cutout ? alb.rgb * v_tint : alb.rgb * mix(vec3(1.0), v_tint, alb.a);
    vec4 nt = magnified ? texelFetch(sampler2DArray(normal_tex, smp_block), tc, 0)
                        : textureGrad(sampler2DArray(normal_tex, smp_block), uvl, gx, gy);
    vec4 sp = magnified ? texelFetch(sampler2DArray(spec_tex, smp_block), tc, 0)
                        : textureGrad(sampler2DArray(spec_tex, smp_block), uvl, gx, gy);
    vec3 fn, ft, fb;
    face_basis(v_face, fn, ft, fb);
    vec3 ng = WC_NORMAL_ROT(fn);
    vec3 t = WC_NORMAL_ROT(ft);
    vec3 b = WC_NORMAL_ROT(fb);
    vec3 tn = nt.xyz * 2.0 - 1.0;
    vec3 n = normalize(t * tn.x + b * tn.y + ng * tn.z);
    uint mat_flags = v_flags;
    if (v_face == 6u) {
        n = normalize(vec3(0.0, 1.0, 0.0) + (t * tn.x + b * tn.y) * 0.25);
        mat_flags |= 64u;
    }
    if ((v_flags & 16u) != 0u) {
        // flowing lava
        float time = F.cam_pos.w;
        vec2 p = (v_face == 2u || v_face == 3u) ? v_world.xz : vec2(v_world.x + v_world.z, v_world.y);
        p = floor(p * 16.0) / 16.0;
        float nz = vnoise(p * 1.3 + vec2(time * 0.25, time * 0.18)) * 0.6 + vnoise(p * 3.1 - vec2(time * 0.4, -time * 0.3)) * 0.4;
        float hot = smoothstep(0.35, 0.85, nz);
        albedo = mix(vec3(0.62, 0.09, 0.01), vec3(1.0, 0.62, 0.12), hot);
        sp = vec4(0.45, 0.0, 0.0, 0.55 + 0.45 * hot);
        n = ng;
    }
    o_albedo = vec4(albedo, 1.0);
    o_normal = vec4(oct_encode(n), oct_encode(ng));
    o_light = vec4(v_light_ao, float(mat_flags) / 255.0);
    o_spec = sp;
}

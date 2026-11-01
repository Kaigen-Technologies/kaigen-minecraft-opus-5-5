// g-buffer fragment body shared by terrain and entities; the stage defines VX_NORMAL_ROT(v)
// flags: 1 cutout, 2 leaves, 16 lava (terrain); 8 item sprite, 64 self-lit, 128 held item, 256 hot glow (entities)
layout(set = 1, binding = 0) uniform texture2DArray albedo_tex;
layout(set = 1, binding = 1) uniform texture2DArray emissive_tex;
#ifdef VX_ITEM_TEX
layout(set = 1, binding = 2) uniform texture2DArray item_tex;
#endif
@aniso
layout(set = 1, binding = 16) uniform sampler smp_block;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_layer;
layout(location = 2) flat in uint v_flags;
layout(location = 3) flat in uint v_face;
layout(location = 4) flat in uint v_map; // 1: a cube or model face, sampled in world space; 2: one block's quarter of the tile
layout(location = 5) in float v_ao;
layout(location = 6) in vec3 v_world;
#ifdef VX_HAS_TINT
layout(location = 7) in vec3 v_tint;
#endif

layout(location = 0) out vec4 o_gb0;
layout(location = 1) out vec4 o_gb1;

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
    // derivatives first: wgsl needs them in uniform control flow
    vec3 fn, ft, fb;
    face_basis(min(v_face, 6u), fn, ft, fb);
    // a tile layer spans 2x2 blocks: block faces continue one painting across neighbours, sprites use one block of it
    vec2 buv = v_map != 0u ? vec2(dot(v_world, ft), dot(v_world, fb)) * 0.5 : v_uv * 0.5;
    vec2 gx = dFdx(buv);
    vec2 gy = dFdy(buv);
    vec3 uvl = vec3(v_map == 2u ? fract(buv * 2.0) * 0.5 : v_map != 0u ? fract(buv) : buv, float(v_layer));
    vec4 alb = textureGrad(sampler2DArray(albedo_tex, smp_block), uvl, gx, gy);
    vec2 em = textureGrad(sampler2DArray(emissive_tex, smp_block), uvl, gx, gy).rg;
#ifdef VX_ITEM_TEX
    // item sprites are 16x16 pixel art: texel-AA keeps magnified pixels hard with a one-pixel antialiased seam
    vec2 tx = v_uv * 16.0;
    vec2 fw_tx = fwidth(tx);
    vec2 seam = floor(tx + 0.5);
    vec2 aa = clamp(seam + clamp((tx - seam) / max(fw_tx, vec2(1e-5)), vec2(-0.5), vec2(0.5)), vec2(0.5), vec2(15.5));
    vec4 ialb = textureGrad(sampler2DArray(item_tex, smp_block), vec3(aa / 16.0, float(v_layer)), dFdx(v_uv), dFdy(v_uv));
    if ((v_flags & 8u) != 0u) {
        alb = ialb;
        em = vec2(0.0);
    }
#endif
    bool cutout = (v_flags & 1u) != 0u;
    if (cutout && alb.a < 0.5) discard;
    if ((v_flags & 128u) == 0u && vx_cut_away(v_world)) discard;
#ifdef VX_NEAR_FADE
    // a body crossing the camera dissolves in an ordered dither instead of filling the view
    float near_k = smoothstep(1.2, 3.5, length(v_world - F.cam_pos.xyz));
    if ((v_flags & 32u) != 0u && near_k < 1.0) {
        uvec2 bp = uvec2(gl_FragCoord.xy) & 3u;
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        if (near_k * 16.0 <= bayer[bp.y * 4u + bp.x] + 0.5) discard;
    }
#endif
    vec3 albedo = alb.rgb;
#ifdef VX_HAS_TINT
    albedo *= v_tint;
#endif

    vec3 n = VX_NORMAL_ROT(fn);
    int gflags = VX_GB_SOLID;
    if (v_face == 6u || (v_flags & 2u) != 0u) gflags = VX_GB_FOLIAGE;
    if ((v_flags & 128u) != 0u) gflags = VX_GB_HAND;
#ifdef VX_FAR_WATER
    if ((v_flags & 8u) != 0u) {
        gflags = VX_GB_WATER;
        albedo = VX_DEEP_WATER;
    }
#endif

    // emission: painted glow; glinting texels twinkle with whatever light reaches them
    float t = F.cam_pos.w;
    vec3 cell = floor(v_world * 8.0 + 0.001);
    float tw = pow(max(0.0, sin(t * 2.6 + hash13(cell) * 37.0)), 18.0);
    float emission = em.r * 1.6;
    float glint = em.g * tw;
    if ((v_flags & 64u) != 0u) emission = 2.5;
    if ((v_flags & 256u) != 0u) emission = VX_GB_EMISSION_RANGE;
    if ((v_flags & 16u) != 0u) {
        // flowing lava on the art grid
        vec2 p = (v_face == 2u || v_face == 3u) ? v_world.xz : vec2(v_world.x + v_world.z, v_world.y);
        p = floor(p * 8.0) / 8.0;
        float nz = vnoise(p * 1.3 + vec2(t * 0.25, t * 0.18)) * 0.6 + vnoise(p * 3.1 - vec2(t * 0.4, -t * 0.3)) * 0.4;
        float hot = smoothstep(0.35, 0.85, nz);
        albedo = mix(vec3(0.55, 0.08, 0.02), vec3(1.0, 0.66, 0.16), hot);
        emission = 1.2 + 1.6 * hot;
        if (v_face == 2u) {
            // drifting plates of cooled crust on the surface, cracked open along their edges
            vec2 q = p * 0.55 + vec2(t * 0.03, t * 0.02);
            vec2 c = floor(q);
            vec2 f = fract(q);
            float d1 = 8.0, d2 = 8.0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    vec2 o = vec2(float(i), float(j));
                    vec2 r = o + vec2(hash12(c + o), hash12(c + o + 17.0)) * 0.8 + 0.1 - f;
                    float d = dot(r, r);
                    if (d < d1) d2 = d1, d1 = d;
                    else if (d < d2) d2 = d;
                }
            float edge = sqrt(d2) - sqrt(d1);
            float crust = smoothstep(0.1, 0.22, edge) * smoothstep(0.25, 0.6, vnoise(p * 0.7 - vec2(t * 0.05, 0.0)));
            albedo = mix(albedo, vec3(0.05, 0.028, 0.026) + vec3(0.08, 0.015, 0.0) * hot, crust);
            emission = mix(emission, 0.04 + 0.2 * hot, crust);
        }
        n = fn;
    }
    o_gb0 = vec4(albedo, v_ao);
    o_gb1 = vec4(oct_encode(n) * 0.5 + 0.5, gb_pack_emission(emission, glint), float(gflags) / 3.0);
}

// stylized deferred lighting: toon-ramped sun/moon with tinted shade, sky ambient, coloured voxel light, emission
#include "vx_common.glsl"
#include "vx_frame.glsl"

#define VX_POINT_LIGHTS 64
layout(binding = 1) uniform _Lights {
    vec4 pos_radius[VX_POINT_LIGHTS]; // camera-relative position, radius
    vec4 color[VX_POINT_LIGHTS];      // rgb, w unused
    vec4 count;                       // x lights in use
} PL;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D gb0_tex;
layout(set = 1, binding = 1) uniform texture2D gb1_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D depth_tex;
layout(set = 1, binding = 3) uniform texture2D ssao_tex;
layout(set = 1, binding = 4) uniform texture2D clouds_tex;
layout(set = 1, binding = 5) uniform texture2D weather_tex;
layout(set = 1, binding = 6) uniform texture3D vol0_tex;
layout(set = 1, binding = 7) uniform texture3D vol1_tex;
layout(set = 1, binding = 8) uniform texture3D dyn_tex;
@depth
layout(set = 1, binding = 9) uniform texture2D shadow_cmp_tex;
@unfilterable
layout(set = 1, binding = 10) uniform texture2D shadow_raw_tex;
layout(set = 1, binding = 11) uniform texture2D cut_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#define VX_SHADOW_FULL
#include "vx_sky.glsl"
#include "vx_lighting.glsl"
#include "vx_volume.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

float cloud_shadow(vec3 world) {
    if (F.cloud.x <= 0.0) return 1.0;
    vec3 L = F.light_dir.xyz;
    if (L.y <= 0.02) return 1.0;
    float h = F.cloud.y + F.cloud.z * 0.4;
    vec3 p = world + L * ((h - world.y) / L.y);
    // the clouds pass's occupancy at its base layer
    vec2 g = p.xz - vec2(F.cloud.w * 4.0, F.cloud.w * 1.5);
    vec2 wuv = (floor(g / 6.0) * 6.0 + 3.0) / 1400.0;
    vec4 w = textureLod(sampler2D(weather_tex, smp_linear), wuv, 0.0);
    float ph = w.b * smoothstep(0.25, 0.6, w.r + (F.cloud.x - 0.5) * 0.9) - (1.0 - F.cloud.x) * 0.25;
    return mix(1.0, 0.5, step(1.0, ph * 8.0 + 0.35));
}

// half-res ao at this pixel: bilinear across its neighbours, the best depth and normal match where one disagrees
float ssao_at(ivec2 q, float lin, vec3 n) {
    ivec2 o = half_res_offset();
    ivec2 hmax = (ivec2(F.res.xy) + 1) / 2 - 1;
    ivec2 d = q - o;
    ivec2 base = ivec2(floor(vec2(d) * 0.5));
    vec2 fr = vec2(d - base * 2) * 0.5;
    float acc = 0.0;
    float nearest = 1.0;
    float best = 1e30;
    bool edge = false;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            vec4 s = texelFetch(sampler2D(ssao_tex, smp_clamp), clamp(base + ivec2(i, j), ivec2(0), hmax), 0);
            float w = (i == 0 ? 1.0 - fr.x : fr.x) * (j == 0 ? 1.0 - fr.y : fr.y);
            float err = abs(s.g - lin) / lin * 10.0 + (1.0 - dot(oct_decode(s.ba), n));
            if (err < best) {
                best = err;
                nearest = s.r;
            }
            if (w > 0.0 && err > 0.1) edge = true;
            acc += s.r * w;
        }
    return edge ? nearest : acc;
}

float outline_z(ivec2 q) {
    float d = texelFetch(sampler2D(depth_tex, smp_clamp), clamp(q, ivec2(0), ivec2(F.res.xy) - 1), 0).r;
    return d <= 0.0 ? 1e9 : linear_depth(d);
}

// silhouette outline strength on the near side of a depth step or a convex crease; about 1.5 px per 720 lines
float outline_at(ivec2 px, float zc, vec3 N, vec3 rel) {
    // the reference line is about 3 px at 720p
    int r = clamp(int(F.res.y / 270.0 + 0.5), 1, 8);
    float sil = 0.0;
    float crease = 0.0;
    for (int a = 0; a < 4; a++) {
        ivec2 dir = a == 0 ? ivec2(1, 0) : a == 1 ? ivec2(-1, 0) : a == 2 ? ivec2(0, 1) : ivec2(0, -1);
        // the opposite neighbour gives this surface's own depth slope, so grazing planes do not outline
        float slope = abs(zc - outline_z(px - dir));
        for (int s = 1; s <= 8; s++) {
            if (s > r) break;
            ivec2 q = px + dir * s;
            float zq = outline_z(q);
            if (zq - zc > max(0.12, zc * 0.035) + slope * float(s) * 2.5) sil = 1.0;
            if (crease == 0.0 && abs(zq - zc) < zc * 0.2) {
                vec3 nq = oct_decode(texelFetch(sampler2D(gb1_tex, smp_clamp), q, 0).xy * 2.0 - 1.0);
                vec3 pq = reconstruct_rel((vec2(q) + 0.5) * F.res.zw, F.world.z / zq);
                if (dot(N, nq) < 0.5 && dot(pq - rel, N) < -0.002 * zc) crease = 1.0;
            }
        }
    }
    return max(sil, crease * 0.7) * (1.0 - smoothstep(48.0, 160.0, zc));
}

vec3 hue_rotate(vec3 c, float a) {
    const vec3 k = vec3(0.57735);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

void main() {
    vec2 uv = v_uv;
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(sampler2D(depth_tex, smp_clamp), px, 0).r;
    if (depth <= 0.0) {
        vec4 cl = textureLod(sampler2D(clouds_tex, smp_clamp), uv, 0.0);
        vec3 dir = view_ray(uv);
        vec4 ring = sky_ring(dir);
        // alpha 0.5 marks the ring: it carries its own haze, so atmospherics leaves it unfogged
        frag_color = vec4(mix(sky_color(dir), ring.rgb, ring.a) * cl.a + cl.rgb, ring.a > 0.0 ? 0.5 : 1.0);
        return;
    }
    vec3 rel = reconstruct_rel(uv, depth);
    float dist = length(rel);
    vec3 world = rel + F.cam_pos.xyz;

    vec4 g0 = texelFetch(sampler2D(gb0_tex, smp_clamp), px, 0);
    vec4 g1 = texelFetch(sampler2D(gb1_tex, smp_clamp), px, 0);
    vec3 albedo = g0.rgb;
    float vao = g0.a;
    vec3 N = oct_decode(g1.xy * 2.0 - 1.0);
    vec2 em = gb_unpack_emission(g1.z);
    float emission = em.x;
    int flags = gb_flags(g1.w);
    bool foliage = flags == VX_GB_FOLIAGE;
    bool hand = flags == VX_GB_HAND;
    int mode = int(F.amb_ground.w + 0.5);

    // the voxel light half a block out from the surface: smooth coloured light, darker in corners
    vec3 vp = (hand ? F.cam_pos.xyz : world) + N * 0.5;
    vec4 vl = vol_sample(vp, mode == 2);
    vec3 blk = max(vl.rgb, dyn_sample(vp));
    // a face on the cutaway plane is lit as Terraria lights a tile: from the slab's own light map, seeping into solids
    bool cut_face = F.cut.y > 0.5 && N.z > 0.9 && abs(world.z - F.cut.x) < 0.01;
    if (cut_face) {
        vec2 cuv = vec2((world.x - F.cut.z) / 128.0, world.y / 256.0);
        vl = mode == 2 ? floor(texelFetch(sampler2D(cut_tex, smp_clamp), ivec2(floor(world.x - F.cut.z), floor(world.y)), 0) * 6.0 + 0.5) / 6.0
                       : textureLod(sampler2D(cut_tex, smp_clamp), cuv, 0.0);
        blk = vl.rgb;
    }
    float sky = vl.a;
    if (mode == 1) blk = vec3(max(blk.r, max(blk.g, blk.b))) * vec3(1.0, 0.95, 0.86);
    if (mode == 3) blk = max(hue_rotate(blk, F.cam_pos.w * 1.3 + dot(world, vec3(0.11, 0.07, 0.13))), vec3(0.0));

    // sun or moon through the toon ramp; foliage wraps light round its edges
    vec3 L = F.light_dir.xyz;
    float wrap = foliage ? 0.6 : 0.0;
    float diff = saturate((dot(N, L) + wrap) / (1.0 + wrap));
    float shadow = 0.0;
    if (F.light_color.w > 0.0 && diff > 0.0) {
        // leaves bias along their own normal; cross plants (no single side) lift toward the light
        vec3 ng = dot(N, L) < 0.0 && foliage ? L : N;
        float ngol = dot(ng, L);
        float thick;
        shadow = F.settings.w > 0.5 ? shadow_full(rel, ng, dist, ngol, gl_FragCoord.xy, thick)
                                    : shadow_pcf(rel, ng, dist, ngol, gl_FragCoord.xy);
        // beyond the cascades: outdoors is lit, anything roofed is not
        if (shadow < 0.0) shadow = smoothstep(0.75, 0.95, sky);
        shadow *= cloud_shadow(world);
    }
    // shade keeps a share of the sun where the sky is open, so shadows read as shapes, not holes; none reaches caves
    float open_sky = smoothstep(0.05, 0.4, sky);
    shadow = mix(0.32 * smoothstep(0.6, 0.95, sky), 1.0, shadow) * open_sky;
    // the cut is lit as a tile: full daylight scaled by its seeped sky light, never sun or shadow on a wall nobody built
    float lit = cut_face ? sky_curve(sky) : toon_ramp(diff) * shadow;
    vec3 direct = F.light_color.rgb * lit;

    // sky ambient: hemisphere colours, tinted toward the shade colour where the sun does not reach
    float ssao = F.settings.y > 0.5 && !hand ? ssao_at(px, linear_depth(depth), N) : 1.0;
    float occ = ao_curve(vao) * ssao;
    vec3 hemi = cut_face ? F.amb_sky.rgb : mix(F.amb_ground.rgb, F.amb_sky.rgb, N.y * 0.5 + 0.5);
    vec3 ambient = hemi * sky_curve(sky) * occ * mix(F.shade.rgb, vec3(1.0), lit);
    // the underworld's lava sea lights what faces it, fading with height above the lava
    ambient += F.under_glow.rgb * exp(-max(world.y - 14.0, 0.0) / 22.0) * saturate(0.65 - 0.2 * N.y) * occ;

    // coloured block light, a touch more saturated near the source
    vec3 bl = block_curve(blk);
    float bl_l = luminance(bl);
    bl = max(mix(vec3(bl_l), bl, 1.0 + 0.25 * saturate(bl_l)), vec3(0.0)) * occ;

    // short-lived point lights (sparks, spells): unshadowed
    vec3 pts = vec3(0.0);
    int np = int(PL.count.x + 0.5);
    for (int i = 0; i < VX_POINT_LIGHTS; i++) {
        if (i >= np) break;
        vec3 d = PL.pos_radius[i].xyz - rel;
        float r = PL.pos_radius[i].w;
        float k = saturate(1.0 - length(d) / r);
        if (k <= 0.0) continue;
        pts += PL.color[i].rgb * k * k * (0.35 + 0.65 * saturate(dot(N, normalize(d))));
    }

    vec3 incoming = direct + ambient + bl + pts;
    vec3 color = albedo * incoming + albedo * emission;
    if (flags == VX_GB_WATER) {
        // open water: the deep body under the water pass's light, the sky by fresnel, the sun's glint
        vec3 V = -rel / dist;
        vec3 body = albedo * (F.amb_sky.rgb * sky_curve(sky) + F.light_color.rgb * shadow * 0.5 + bl);
        vec3 R = reflect(-V, N);
        R.y = abs(R.y) + 0.02;
        vec3 refl = sky_gradient(normalize(R)) * mix(0.08, 1.0, sky_curve(sky));
        float fres = 0.02 + 0.98 * pow(1.0 - saturate(dot(N, V)), 5.0);
        color = mix(body, refl, fres) + F.light_color.rgb * shadow * pow(saturate(dot(reflect(-L, N), V)), 400.0) * 2.0;
    }
    // glints catch the light: a bright, light-coloured sparkle where any light arrives
    color += em.y * 2.4 * incoming * mix(albedo, vec3(1.0), 0.5);
    // outlines darken the surface's own colour toward a cool shade, never to black
    float ol = outline_at(px, linear_depth(depth), N, rel);
    color = mix(color, color * vec3(0.26, 0.24, 0.32), ol);
    int debug_view = int(F.extra.w + 0.5);
    if (debug_view > 0) {
        if (debug_view == 1) color = albedo;
        else if (debug_view == 2) color = N * 0.5 + 0.5;
        else if (debug_view == 3) color = vec3(shadow);
        else if (debug_view == 4) color = vec3(occ);
        else if (debug_view == 5) color = blk + vec3(0.0, 0.0, sky * 0.25);
        else if (debug_view == 6) color = vec3(ssao);
        else if (debug_view == 7) color = direct;
        else if (debug_view == 8) color = ambient;
        else if (debug_view == 9) color = vec3(ol);
    }
    frag_color = vec4(color, 1.0);
}
@end

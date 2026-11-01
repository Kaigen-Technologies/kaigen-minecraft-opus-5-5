// forward jelly (slimes): exact thickness through the box, coloured absorption of the refracted scene, sky rim, sun glint
#include "vx_common.glsl"
#include "vx_frame.glsl"
#include "vx_box_common.glsl"

@vs
layout(location = 0) out vec3 v_rel;
layout(location = 1) out vec3 v_local;
layout(location = 2) flat out vec3 v_cam_local;
layout(location = 3) flat out vec3 v_tint;
layout(location = 4) flat out uint v_flags;
layout(location = 5) out vec3 v_normal;
layout(location = 6) flat out uint v_face;

void main() {
    uint vi = uint(gl_VertexIndex);
    VxBoxVertex b = vx_box_vertex(vi);
    gl_Position = F.view_proj * vec4(b.rel, 1.0);
    v_rel = b.rel;
    uint bi = vi / 36u;
    vec4 r0 = uintBitsToFloat(boxes.v[bi * 4u]);
    vec4 r1 = uintBitsToFloat(boxes.v[bi * 4u + 1u]);
    vec4 r2 = uintBitsToFloat(boxes.v[bi * 4u + 2u]);
    vec3 cx = vec3(r0.x, r1.x, r2.x), cy = vec3(r0.y, r1.y, r2.y), cz = vec3(r0.z, r1.z, r2.z);
    vec3 o = vec3(r0.w, r1.w, r2.w);
    // the camera (the origin) in the cube's own [0,1]^3 frame, by the cofactor inverse of the box's axes
    vec3 i0 = cross(cy, cz), i1 = cross(cz, cx), i2 = cross(cx, cy);
    float det = dot(cx, i0);
    v_cam_local = vec3(dot(i0, -o), dot(i1, -o), dot(i2, -o)) / det;
    v_local = b.local;
    vec4 t = unpackUnorm4x8(b.tint);
    v_tint = mix(pow(t.rgb, vec3(2.2)), vec3(1.6), t.a * 0.7);
    v_flags = (b.misc >> 12u) & 255u;
    v_normal = b.normal;
    v_face = b.face;
}
@end

@fs
layout(set = 1, binding = 0) uniform texture2D scene_color_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D scene_depth_tex;
layout(set = 1, binding = 2) uniform texture3D vol0_tex;
layout(set = 1, binding = 3) uniform texture3D vol1_tex;
layout(set = 1, binding = 5) uniform texture3D dyn_tex;
@depth
layout(set = 1, binding = 6) uniform texture2D shadow_cmp_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
@clamp
layout(set = 1, binding = 17) uniform sampler smp_clamp;
layout(set = 1, binding = 18) uniform samplerShadow smp_shadow;

#include "vx_sky.glsl"
#include "vx_lighting.glsl"
#include "vx_volume.glsl"

layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_local;
layout(location = 2) flat in vec3 v_cam_local;
layout(location = 3) flat in vec3 v_tint;
layout(location = 4) flat in uint v_flags;
layout(location = 5) in vec3 v_normal;
layout(location = 6) flat in uint v_face;
layout(location = 0) out vec4 frag_color;

void main() {
    float dist = length(v_rel);
    vec3 V = -v_rel / dist;
    vec3 N = normalize(v_normal);
    vec2 suv = gl_FragCoord.xy * F.res.zw;

    // the ray's exit from the cube in its own frame; the path length scales with the fragment's distance
    vec3 d = v_local - v_cam_local;
    vec3 tx = mix(v_local, 1.0 - v_local, step(0.0, d)) / max(abs(d), vec3(1e-6));
    float thick = max(0.0, min(tx.x, min(tx.y, tx.z))) * dist;

    // the scene behind, bent by the surface, never pulling in what is in front of the jelly
    vec3 Nv = (F.view * vec4(N, 0.0)).xyz;
    vec2 off = vec2(Nv.x, -Nv.y) * -0.045 * saturate(thick) / max(1.0, dist * 0.3);
    vec2 ruv = clamp(suv + off * vec2(F.res.y * F.res.z, 1.0), vec2(0.001), vec2(0.999));
    float rd = texelFetch(sampler2D(scene_depth_tex, smp_clamp), uv_to_texel(ruv), 0).r;
    if (rd > 0.0 && length(reconstruct_rel(ruv, rd)) < dist) ruv = suv;
    vec3 refr = textureLod(sampler2D(scene_color_tex, smp_clamp), ruv, 0.0).rgb;

    // light at the surface, as the water takes it: sun through the shadow map, sky and block light from the volume
    vec3 world = v_rel + F.cam_pos.xyz;
    vec4 vl = vol_sample(world + N * 0.3, false);
    float sky = vl.a;
    vec3 blk = block_curve(max(vl.rgb, dyn_sample(world + N * 0.3)));
    float sh = F.light_color.w > 0.0 ? shadow_pcf(v_rel, N, dist, dot(N, F.light_dir.xyz), gl_FragCoord.xy) : 0.0;
    if (sh < 0.0) sh = smoothstep(0.75, 0.95, sky);
    sh *= smoothstep(0.05, 0.4, sky);
    // light wraps through the gel, so the shaded side is never dead
    float wrap = saturate(dot(N, F.light_dir.xyz) * 0.5 + 0.5);
    vec3 light = F.amb_sky.rgb * sky_curve(sky) + F.light_color.rgb * sh * wrap * 0.6 + blk;

    bool glow = (v_flags & 4u) != 0u;
    vec3 body = v_tint;
    // coloured absorption: what the body colour lacks is soaked up with depth
    vec3 sigma = (vec3(1.0) - body) * 1.8 + 0.18;
    vec3 T = exp(-sigma * thick);
    vec3 scatter = body * (glow ? vec3(2.4) : light) * 0.85;
    vec3 color = refr * T + scatter * (1.0 - T);

    // a sky rim and a hard pixel-art sun glint
    float NoV = saturate(dot(N, V));
    float fres = 0.04 + 0.96 * pow(1.0 - NoV, 4.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y) + 0.02;
    color = mix(color, sky_gradient(normalize(R)) * mix(0.1, 1.0, sky_curve(sky)) + body * light * 0.2, fres * 0.55);
    float g = pow(saturate(dot(reflect(-F.light_dir.xyz, N), V)), 48.0);
    color += F.light_color.rgb * sh * step(0.35, g) * 0.9;
    // a square glint on the upper front of the crown, as Terraria's slimes carry one
    vec2 fp = v_face < 2u ? v_local.zy : v_local.xy;
    if (v_face == 2u) fp = v_local.xz;
    float spot = step(0.12, fp.x) * step(fp.x, 0.3) * step(0.62, fp.y) * step(fp.y, 0.82);
    color = mix(color, light * 1.1 + 0.08, spot * 0.65 * (glow ? 0.0 : 1.0));
    frag_color = vec4(color, 1.0);
}
@end

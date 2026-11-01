// shadows (4-cascade atlas, reverse-z) and the stylized light curves; needs shadow_cmp_tex, smp_shadow, smp_clamp (+ shadow_raw_tex)

const vec2 VX_POISSON[12] = vec2[12](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

int select_cascade(float d, float dither) {
    for (int i = 0; i < 3; i++) {
        float split = F.cascade_splits[i];
        float blend_start = split * 0.85;
        if (d < blend_start) return i;
        if (d < split) return dither < (d - blend_start) / (split - blend_start) ? i + 1 : i;
    }
    return d < F.cascade_splits[3] ? 3 : 4;
}

// cascade-local uv (top-left origin) and reverse-z depth
vec3 shadow_coord(int c, vec3 p) {
    vec4 sc = F.shadow_vp[c] * vec4(p, 1.0);
    return vec3(sc.x * 0.5 + 0.5, 0.5 - sc.y * 0.5, sc.z);
}

// cascade c occupies quadrant (c & 1, c >> 1) of the atlas; taps stay inside it
vec2 cascade_atlas_uv(int c, vec2 uv) {
    float border = 0.75 / F.settings.z;
    return clamp(uv, vec2(border), vec2(1.0 - border)) * 0.5 + vec2(float(c & 1), float(c >> 1)) * 0.5;
}

// bilinear pcf from a gather: identical on every backend whatever the comparison sampler filters
float shadow_cmp(int c, vec2 uv, float z) {
    float size = 2.0 * F.settings.z;
    vec2 st = cascade_atlas_uv(c, uv) * size - 0.5;
    vec2 f = fract(st);
    vec4 g = textureGather(sampler2DShadow(shadow_cmp_tex, smp_shadow), (floor(st) + 1.0) / size, z);
    return mix(mix(g.w, g.z, f.x), mix(g.x, g.y, f.x), f.y);
}

#ifdef VX_SHADOW_FULL
float shadow_raw(int c, vec2 uv) {
    ivec2 p = ivec2(cascade_atlas_uv(c, uv) * (2.0 * F.settings.z));
    return texelFetch(sampler2D(shadow_raw_tex, smp_clamp), p, 0).r;
}

// visibility 0..1 (-1 beyond the last cascade); thickness = light path through occluders
float shadow_full(vec3 rel, vec3 ng, float dist, float ngol, vec2 frag_coord, out float thickness) {
    thickness = 0.0;
    if (F.settings.x < 0.5) return 1.0;
    float dither = ign_t(frag_coord);
    int c = select_cascade(dist, dither);
    if (c >= 4) return -1.0;
    float texel = F.cascade_texel[c];
    float slope = saturate(1.0 - abs(ngol));
    vec3 p = rel + ng * texel * (1.2 + 2.0 * slope) + F.light_dir.xyz * texel * 0.6;
    vec3 s = shadow_coord(c, p);
    if (s.z <= 0.0) return 1.0;
    float angle = dither * 6.2831853;
    float ca = cos(angle);
    float sa = sin(angle);
    mat2 rot = mat2(ca, sa, -sa, ca);
    float tex_size = F.settings.z;
    // blocker search (soft penumbra + transmission thickness)
    float search_r = clamp(0.35 / texel, 1.5, 12.0) / tex_size;
    float blockers = 0.0;
    float blocker_sum = 0.0;
    for (int i = 0; i < 6; i++) {
        vec2 o = rot * VX_POISSON[i * 2] * search_r;
        float bd = shadow_raw(c, s.xy + o);
        if (bd > s.z) {
            blockers += 1.0;
            blocker_sum += bd;
        }
    }
    if (blockers < 0.5) return 1.0;
    float avg_blocker = blocker_sum / blockers;
    float d_block = max(0.0, avg_blocker - s.z) * F.cascade_depth[c];
    thickness = d_block;
    // penumbra grows with blocker distance
    float penumbra_world = clamp(d_block * 0.018, 0.015, 0.6);
    float radius = clamp(penumbra_world / texel, 0.8, 10.0) / tex_size;
    float sum = 0.0;
    for (int i = 0; i < 12; i++) {
        vec2 o = rot * VX_POISSON[i] * radius;
        sum += shadow_cmp(c, s.xy + o, s.z);
    }
    return sum / 12.0;
}
#endif

// single-tap shadow for volumetrics
float shadow_cheap(vec3 rel, float dist) {
    if (F.settings.x < 0.5) return 1.0;
    int c = dist < F.cascade_splits[1] ? 1 : dist < F.cascade_splits[2] ? 2 : dist < F.cascade_splits[3] ? 3 : 4;
    if (c >= 4) return 1.0;
    vec3 s = shadow_coord(c, rel + F.light_dir.xyz * F.cascade_texel[c] * 2.0);
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 1.0;
    return shadow_cmp(c, s.xy, s.z);
}

// soft fixed-radius pcf: 8 rotated poisson taps (pcss is the soft-shadows option)
float shadow_pcf(vec3 rel, vec3 ng, float dist, float ngol, vec2 frag_coord) {
    if (F.settings.x < 0.5) return 1.0;
    float dither = ign_t(frag_coord);
    int c = select_cascade(dist, dither);
    if (c >= 4) return -1.0;
    float texel = F.cascade_texel[c];
    float slope = saturate(1.0 - abs(ngol));
    vec3 p = rel + ng * texel * (1.2 + 2.0 * slope) + F.light_dir.xyz * texel * 0.6;
    vec3 s = shadow_coord(c, p);
    if (s.z <= 0.0) return 1.0;
    float angle = dither * 6.2831853;
    mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float radius = 1.6 / F.settings.z;
    float sum = 0.0;
    for (int i = 0; i < 8; i++) sum += shadow_cmp(c, s.xy + rot * VX_POISSON[i] * radius, s.z);
    return sum / 8.0;
}

// sky light falls off fast into caves; vertex ao keeps a floor so corners never go flat black
float sky_curve(float s) { return s * s; }
float ao_curve(float v) { return 0.32 + 0.68 * v; }
// block light levels are logarithmic: each level is x0.86, so the flood fill's -1 per block keeps a light's hue
vec3 block_curve(vec3 b) {
    const float floor_v = 0.1043; // 0.86^15, the value of level 0
    vec3 v = (exp2((1.0 - b) * (15.0 * -0.2176)) - floor_v) / (1.0 - floor_v);
    // one fade for all channels, from the brightest, so the tail does not change hue
    return max(v, vec3(0.0)) * 1.7 * smoothstep(0.0, 0.3, max(b.r, max(b.g, b.b)));
}
// three soft bands of direct light: shade, half-lit, lit
float toon_ramp(float x) {
    return smoothstep(0.0, 0.06, x) * 0.42 + smoothstep(0.3, 0.38, x) * 0.33 + smoothstep(0.68, 0.76, x) * 0.25;
}

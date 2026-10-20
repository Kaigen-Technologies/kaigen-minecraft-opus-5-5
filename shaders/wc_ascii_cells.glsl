// ascii cells: one fragment per cell classifies it (sky, liquid, material, edge) and picks its glyph; the matcher is integer math, exact everywhere
#include "wc_common.glsl"
#include "wc_frame.glsl"

layout(binding = 1) uniform _CellTables {
    uvec4 site_range[4];  // site s: first entry | count << 16
    uvec4 entries[128];   // texel dx + 16 | (dy + 16) << 6 | weight << 12, from the cell's top-left texel
    uvec4 glyph_a[96];    // shape vector of vocabulary slot g, sites 0..3
    uvec4 glyph_b[96];    // sites 4, 5, glyph index, lit font px
    uvec4 vocab;          // edge glyph count (slots from 0), first cloud slot, cloud glyph count
    uvec4 layer_info[96]; // per texture layer: z flags
    uvec4 ramp[24];       // fill ramp position -> glyph index, lightest first
    uvec4 rank[24];       // glyph index -> fill ramp position
    uvec4 ramp_lut[64];   // mean display luma (0..255) -> fill ramp position
} T;

layout(binding = 2) uniform _CellParams {
    uvec4 grid;    // cols, rows, previous cells valid, translucent surfaces drawn
    vec4 ink;      // sky ink brightest channel target, sky ink gain ceiling, surface ink gain, background tint
    vec4 sky;      // sky background tint, surface ink floor, glow strength, fill ramp hysteresis (steps)
    vec4 sel_min;  // xyz camera-relative selection min, w active
    vec4 sel_max;  // xyz camera-relative selection max
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
@unfilterable
layout(set = 1, binding = 0) uniform texture2D ldr_tex;
@unfilterable
layout(set = 1, binding = 1) uniform texture2D prev_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D mat_tex;
@unfilterable
layout(set = 1, binding = 3) uniform texture2D depth_tex; // opaque surfaces, plus water and ice when translucent
@unfilterable
layout(set = 1, binding = 4) uniform texture2D liquid_tex;
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_glyph; // display-encoded ink colour, glyph index / 255
layout(location = 1) out vec4 o_back;  // display-encoded background colour, flags / 255
layout(location = 2) out vec4 o_glow;  // display-encoded halo colour this cell casts

// flags: kind in bits 0-1 (edge stroke, ramp fill, sky, water fill), held item, cloud matcher, overlay, selected (compose
// swaps ink and background); defines must not carry comments
#define KIND_EDGE 0u
#define KIND_FILL 1u
#define KIND_SKY 2u
#define KIND_WATER 3u
#define FLAG_HAND 4u
#define FLAG_CLOUD 8u
#define FLAG_OVERLAY 16u
#define FLAG_SELECTED 32u
#define LAYER_PLANT 1u
#define LAYER_EMISSIVE 2u
#define LAYER_FOLIAGE 4u
#define MODE_EDGE 0u
#define MODE_CLOUD 1u
#define G_SPACE 0u

uint u8_of(float v) { return uint(v * 255.0 + 0.5); }
uint glyph_code(uint ch) { return ch - 32u; }

// ---- the integer shape matcher ----

uint site_entry(uint i) { return T.entries[i >> 2u][i & 3u]; }
uint site_range(uint s) { return T.site_range[s >> 2u][s & 3u]; }

// the matcher image: outlines are 255, cloud cover is 0..127 (doubled for the cloud matcher; land masks it)
uint luma_at(ivec2 p, ivec2 size, uint mode) {
    uint a = u8_of(texelFetch(sampler2D(ldr_tex, smp_clamp), clamp(p, ivec2(0), size - 1), 0).a);
    if (mode == MODE_EDGE) return a == 255u ? 255u : 0u;
    return a < 128u ? a * 2u : 0u;
}

// integer display luma of an 8-bit pixel (Rec. 709 weights / 256), mirrored by tools/ascii/reference.py
uint luma8(vec3 c) { return (54u * u8_of(c.r) + 183u * u8_of(c.g) + 19u * u8_of(c.b) + 128u) >> 8u; }

uint ramp_at(uint p) { return T.ramp[p >> 2u][p & 3u]; }

// the fill glyph for a mean luma; last frame's glyph stays while its ramp step is within the hysteresis (a blank never does)
uint ramp_glyph(uint luma, uint prev) {
    uint p = T.ramp_lut[luma >> 2u][luma & 3u];
    if (prev < 95u) {
        uint q = T.rank[prev >> 2u][prev & 3u];
        if (q != 255u && q != 0u && uint(abs(int(q) - int(p))) <= uint(P.sky.w)) return prev;
    }
    return ramp_at(p);
}

// circle average, 0..65535
uint sample_site(uint s, ivec2 origin, ivec2 size, uint mode) {
    uint r = site_range(s);
    uint first = r & 0xffffu;
    uint count = r >> 16u;
    uint sum = 0u;
    uint total = 0u;
    for (uint i = 0u; i < count; i++) {
        uint e = site_entry(first + i);
        ivec2 d = ivec2(int(e & 63u) - 16, int((e >> 6u) & 63u) - 16);
        uint w = e >> 12u;
        sum += w * luma_at(origin + d, size, mode);
        total += w;
    }
    return (sum * 257u) / total;
}

// (v / m)^n * m in 1.15 fixed point
uint contrast(uint v, uint m, uint n) {
    if (m == 0u) return 0u;
    uint t = (v << 15u) / m;
    uint r = t;
    for (uint i = 1u; i < n; i++) r = (r * t) >> 15u;
    return (r * m) >> 15u;
}

// outer circles (bit j = site 6 + j) whose light pushes inner circle i down
uint affecting(uint i) {
    return i == 0u ? 0x17u : i == 1u ? 0x2bu : i == 2u ? 0x54u : i == 3u ? 0xa8u : i == 4u ? 0x350u : 0x3a0u;
}

uint glyph_comp(uint g, uint i) { return i < 4u ? T.glyph_a[g][i] : T.glyph_b[g][i - 4u]; }

uint match_glyph(ivec2 origin, ivec2 size, uint prev, uint mode, uint first, uint count) {
    uint outer[10];
    for (uint j = 0u; j < 10u; j++) outer[j] = sample_site(6u + j, origin, size, mode);
    uint v[6];
    for (uint i = 0u; i < 6u; i++) {
        uint x = sample_site(i, origin, size, mode);
        uint m = x;
        uint mask = affecting(i);
        for (uint j = 0u; j < 10u; j++)
            if ((mask & (1u << j)) != 0u) m = max(m, outer[j]);
        v[i] = contrast(x, m, 7u);
    }
    uint vmax = 0u;
    for (uint i = 0u; i < 6u; i++) vmax = max(vmax, v[i]);
    for (uint i = 0u; i < 6u; i++) v[i] = contrast(v[i], vmax, 3u) >> 4u;

    uint best = first;
    uint best_d = 0xffffffffu;
    uint prev_slot = 0xffffffffu;
    uint prev_d = 0u;
    for (uint g = first; g < first + count; g++) {
        uint d = 0u;
        for (uint i = 0u; i < 6u; i++) {
            int e = int(v[i]) - int(glyph_comp(g, i) >> 4u);
            d += uint(e * e);
        }
        if (d < best_d) {
            best_d = d;
            best = g;
        }
        if (T.glyph_b[g].z == prev) {
            prev_slot = g;
            prev_d = d;
        }
    }
    // hysteresis: last frame's glyph stays while it is within 10% of the best
    if (prev_slot != 0xffffffffu && prev_d * 10u <= best_d * 11u) best = prev_slot;
    return T.glyph_b[best].z;
}

// ---- surfaces ----

// sample points in font px: the cell centre, then the six inner sampling circles
vec2 point_px(int i) {
    return i == 0 ? vec2(3.0, 4.5) : i == 1 ? vec2(1.8, 2.07) : i == 2 ? vec2(4.2, 1.62) : i == 3 ? vec2(1.8, 4.5)
         : i == 4 ? vec2(4.2, 4.5) : i == 5 ? vec2(1.8, 7.38) : vec2(4.2, 6.93);
}

ivec2 scene_texel(vec2 grid_px) {
    vec2 s = F.ascii.xy / vec2(6.0, 9.0);
    return clamp(ivec2(grid_px * s), ivec2(0), ivec2(F.res.xy) - 1);
}

vec3 world_of(ivec2 t, float d) { return reconstruct_rel((vec2(t) + 0.5) * F.res.zw, d) + F.cam_pos.xyz; }

// ink: the colour lifted so its brightest channel reaches the target, never more than the gain ceiling
vec3 lift(vec3 c) {
    float m = max(max(c.r, c.g), max(c.b, 1e-3));
    return saturate(c * clamp(P.ink.x / m, 1.0, P.ink.y));
}

// surface ink: the gained colour's brightness mapped onto [floor, 1] so shadows stay legible; near-black cells go grey
vec3 ink_colour(vec3 c) {
    float m = max(max(c.r, c.g), c.b);
    float bright = m * min(P.ink.z, 1.0 / max(m, 1e-3));
    vec3 hue = (c + 0.02) / (m + 0.02);
    return hue * (P.sky.y + (1.0 - P.sky.y) * bright);
}

// the background under empty and surface cells, measured on the reference
#define BACK_BASE vec3(0.035, 0.035, 0.077)
#define HAND_BACK 0.75

// the held item's luma, pushed into the dense half of the ramp so shading survives (mirrored by reference.py)
uint hand_luma(uint l) { return 96u + (l * 5u) / 8u; }

bool in_selection(vec3 rel) {
    return P.sel_min.w > 0.5 && all(greaterThan(rel, P.sel_min.xyz - 0.002)) && all(lessThan(rel, P.sel_max.xyz + 0.002));
}

// the view ray through a cell's centre hits the selected block's cube in front of the surface there
bool cell_selected(ivec2 origin) {
    vec2 c = vec2(origin) + point_px(0);
    vec3 dir = view_ray(c / vec2(textureSize(sampler2D(ldr_tex, smp_clamp), 0)));
    vec3 inv = 1.0 / dir;
    vec3 t0 = (P.sel_min.xyz - 0.002) * inv, t1 = (P.sel_max.xyz + 0.002) * inv;
    vec3 lo = min(t0, t1), hi = max(t0, t1);
    float tn = max(max(lo.x, lo.y), lo.z), tf = min(min(hi.x, hi.y), hi.z);
    if (tn > tf || tf <= 0.0) return false;
    ivec2 t = scene_texel(c);
    float d = texelFetch(sampler2D(depth_tex, smp_clamp), t, 0).r;
    return d <= 0.0 || length(world_of(t, d) - F.cam_pos.xyz) >= tn - 0.05;
}

// the selected block's outline, as in minecraft: cells on its cube's silhouette (a 4-neighbour misses the cube)
bool selection_outline(ivec2 origin) {
    if (P.sel_min.w < 0.5 || !cell_selected(origin)) return false;
    return !cell_selected(origin + ivec2(6, 0)) || !cell_selected(origin - ivec2(6, 0)) ||
           !cell_selected(origin + ivec2(0, 9)) || !cell_selected(origin - ivec2(0, 9));
}

// ---- sky ----

// a square body (sun or moon) of half-size h around direction b, in its own tangent frame
bool in_square(vec3 dir, vec3 b, float h) {
    float c = dot(dir, b);
    if (c <= 0.0) return false;
    vec3 axis = abs(b.z) < 0.95 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 u = normalize(cross(axis, b));
    vec3 v = cross(b, u);
    vec2 q = vec2(dot(dir, u), dot(dir, v)) / c;
    return max(abs(q.x), abs(q.y)) < h;
}

// stars stay fixed to the sky; a few twinkle between '.', '+' and '*'
uint star_glyph(vec3 dir) {
    if (F.sky.w < 0.5 || dir.y <= 0.0) return G_SPACE;
    vec3 q = floor((mat3(F.star_rot) * dir) * 220.0);
    float h = hash13(q);
    if (h <= 0.9965) return G_SPACE;
    uint base = h > 0.9993 ? glyph_code(42u) : h > 0.998 ? glyph_code(43u) : glyph_code(46u);
    float tw = hash13(q + floor(F.cam_pos.w * 1.7));
    return tw < 0.12 ? glyph_code(43u) : tw < 0.16 ? glyph_code(42u) : base;
}

// ---- overlays ----

// one falling glyph per column segment: rain streaks (slanting in wind), drifting snow, rising bubbles
bool falling(ivec2 cell, float rows_per_s, float spacing, float density, float slant, out float h) {
    float pos = float(cell.y) - F.cam_pos.w * rows_per_s;
    float col = float(cell.x) + floor(float(cell.y) * slant);
    float seg = floor(pos / spacing);
    h = hash12(vec2(col, seg));
    return h < density && fract(pos / spacing) < 1.0 / spacing;
}

// the lightning bolt's column at row y for this strike: a random walk from a random top column
int bolt_x(int y, float seed, out bool left) {
    int x = int(hash12(vec2(seed, 7.0)) * float(P.grid.x));
    left = false;
    for (int i = 0; i <= y; i++) {
        left = hash12(vec2(float(i), seed)) < 0.5;
        x += left ? -1 : 1;
    }
    return x;
}

void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    ivec2 size = ivec2(textureSize(sampler2D(ldr_tex, smp_clamp), 0));
    ivec2 origin = cell * ivec2(6, 9);
    vec2 cuv = (vec2(origin) + vec2(3.0, 4.5)) / vec2(size);

    // cell colour, integer mean luma (the fill ramp's input), outline pixels, cloud cover
    vec3 mean = vec3(0.0);
    uint luma_sum = 0u;
    int outline_px = 0;
    uint cloud_sum = 0u;
    for (int y = 0; y < 9; y++)
        for (int x = 0; x < 6; x++) {
            vec4 px = texelFetch(sampler2D(ldr_tex, smp_clamp), clamp(origin + ivec2(x, y), ivec2(0), size - 1), 0);
            uint a = u8_of(px.a);
            outline_px += a == 255u ? 1 : 0;
            cloud_sum += a < 128u ? a : 0u;
            luma_sum += luma8(px.rgb);
            mean += px.rgb;
        }
    mean *= 1.0 / 54.0;
    float cloud = float(cloud_sum) / (54.0 * 127.0);

    // surface at each sample point
    int keys[7];
    int nsky = 0;
    int nhand = 0;
    for (int i = 0; i < 7; i++) {
        ivec2 t = scene_texel(vec2(origin) + point_px(i));
        float d = texelFetch(sampler2D(depth_tex, smp_clamp), t, 0).r;
        vec4 m = texelFetch(sampler2D(mat_tex, smp_clamp), t, 0);
        int layer = int(u8_of(m.r)) - 1;
        bool sky = d <= 0.0 || layer < 0;
        keys[i] = sky ? -1 : layer;
        if (sky) nsky++;
        if (!sky && (u8_of(m.a) & 128u) != 0u) nhand++;
    }
    ivec2 ct = scene_texel(vec2(origin) + point_px(0));
    bool water = P.grid.w != 0u && u8_of(texelFetch(sampler2D(liquid_tex, smp_clamp), ct, 0).r) == 1u;

    uint prev = 255u;
    if (P.grid.z != 0u) prev = u8_of(texelFetch(sampler2D(prev_tex, smp_clamp), cell, 0).a);

    uint kind = water ? KIND_WATER : KIND_FILL;
    uint flags = 0u;
    uint glyph = G_SPACE;
    vec3 fg = ink_colour(mean);
    vec3 bg = BACK_BASE + fg * P.ink.w;
    vec3 glow = vec3(0.0);
    float t = F.cam_pos.w;

    if (nsky == 7 && !water) {
        kind = KIND_SKY;
        fg = lift(mean);
        bg = mean * P.sky.x;
        vec3 dir = view_ray(cuv);
        bool sun = F.sun_dir.w > 0.0 && in_square(dir, normalize(F.sun_dir.xyz), 0.075);
        bool moon = !sun && in_square(dir, normalize(F.moon_dir.xyz), 0.06);
        if ((sun || moon) && cloud < 0.5) {
            glyph = sun ? glyph_code(35u) : glyph_code(64u);
            fg = sun ? vec3(1.0, 0.94, 0.62) : vec3(0.86, 0.89, 0.96);
            glow = sun ? vec3(1.0, 0.82, 0.4) : vec3(0.3, 0.33, 0.4);
        } else if (cloud > 0.06) {
            flags |= FLAG_CLOUD;
            glyph = match_glyph(origin, size, prev, MODE_CLOUD, T.vocab.y, T.vocab.z);
        } else {
            glyph = star_glyph(dir);
            if (glyph != G_SPACE) fg = vec3(0.92, 0.94, 1.0);
        }
    } else {
        // the most common surface: cutout foliage and plants never take silhouette strokes (every leaf hole is one)
        int best_key = -1;
        int best_n = 0;
        for (int i = 0; i < 7; i++) {
            if (keys[i] < 0) continue;
            int n = 0;
            for (int j = 0; j < 7; j++) n += keys[j] == keys[i] ? 1 : 0;
            if (n > best_n) {
                best_n = n;
                best_key = keys[i];
            }
        }
        uint layer_flags = best_key >= 0 ? T.layer_info[uint(best_key)].z : 0u;
        bool cutout = (layer_flags & (LAYER_PLANT | LAYER_FOLIAGE)) != 0u;
        // the held item is the player on screen: always the brightest, densest thing, on a lit background
        bool hand = nhand >= 4;
        if (hand) {
            flags |= FLAG_HAND;
            fg = lift(mean);
            bg = mean * HAND_BACK;
            glyph = ramp_glyph(hand_luma((luma_sum + 27u) / 54u), prev);
        } else if (outline_px > 0 && !cutout && !water) {
            glyph = match_glyph(origin, size, prev, MODE_EDGE, 0u, T.vocab.x);
            if (glyph != G_SPACE) kind = KIND_EDGE;
        }
        if (!hand && glyph == G_SPACE) glyph = ramp_glyph((luma_sum + 27u) / 54u, prev);
        else if (!hand) fg = lift(mean);
        if (selection_outline(origin)) flags |= FLAG_SELECTED;
        if ((layer_flags & LAYER_EMISSIVE) != 0u) glow = fg;
    }

    // overlays: lightning over the sky, then rain or snow outdoors, bubbles underwater
    float hseg = 0.0;
    bool left = false;
    if (F.extra.z > 0.25 && kind == KIND_SKY && bolt_x(cell.y, floor(t * 0.25), left) == cell.x) {
        glyph = left ? glyph_code(47u) : glyph_code(92u);
        fg = vec3(0.95, 0.93, 1.0);
        glow = vec3(0.6, 0.6, 0.8);
        flags |= FLAG_OVERLAY;
    }
    float outdoor = smoothstep(0.3, 0.8, F.world.y);
    float rain = F.weather.x * outdoor * (F.world.x > 0.5 ? 0.0 : 1.0);
    float snow = F.extra.x;
    if ((flags & (FLAG_OVERLAY | FLAG_SELECTED)) == 0u && rain > 0.02) {
        bool windy = F.weather.z > 2.0;
        ivec2 drift = ivec2(int(floor(1.2 * sin(float(cell.y) * 0.35 + t * 0.9))), 0);
        if (snow < 0.99 && falling(cell, 34.0, 5.0, rain * (1.0 - snow) * 0.55, windy ? 0.5 : 0.0, hseg)) {
            glyph = windy ? glyph_code(47u) : glyph_code(124u);
            fg = mix(fg, vec3(0.72, 0.78, 0.9), 0.65);
            flags |= FLAG_OVERLAY;
        } else if (snow > 0.01 && falling(cell + drift, 5.0, 4.0, rain * snow * 0.5, 0.0, hseg)) {
            glyph = hseg < rain * snow * 0.2 ? glyph_code(42u) : glyph_code(46u);
            fg = vec3(0.95, 0.96, 1.0);
            flags |= FLAG_OVERLAY;
        }
    }
    if (F.world.x > 0.5 && (flags & FLAG_SELECTED) == 0u && falling(cell, -4.0, 6.0, 0.05, 0.0, hseg)) {
        glyph = hseg < 0.02 ? glyph_code(111u) : glyph_code(46u);
        fg = vec3(0.75, 0.92, 1.0);
        flags |= FLAG_OVERLAY;
    }

    o_glyph = vec4(fg, float(glyph) / 255.0);
    o_back = vec4(bg, float(kind | flags) / 255.0);
    o_glow = vec4(glow * P.sky.z, 1.0);
}
@end

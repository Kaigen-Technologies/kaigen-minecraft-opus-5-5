// ascii compose: every device pixel finds its character cell and the font pixel of the cell's glyph, then the text ui's
#include "wc_common.glsl"

layout(binding = 0) uniform _ComposeParams {
    uvec4 grid;      // k (device px per font px), cols, rows, flags (1 atlas test, 2 crosshair, 4 ui only)
    uvec4 origin;    // grid top-left in device px, palette (0 colour, 1 phosphor, 2 amber)
    uvec4 ui;        // ui cells per scene-cell side m, ui cols, ui rows (0 for no ui)
    uvec4 ui_origin; // ui grid top-left in device px
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = wc_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
@unfilterable
layout(set = 1, binding = 0) uniform texture2D glyph_tex;
layout(set = 1, binding = 1) uniform texture2D back_tex;
@unfilterable
layout(set = 1, binding = 2) uniform texture2D atlas_tex;
layout(set = 1, binding = 3) uniform texture2D glow_tex;
@unfilterable
layout(set = 1, binding = 4) uniform texture2D ui_tex; // per ui cell: (fg rgb, glyph), (bg rgb, bg opacity)
@clamp
layout(set = 1, binding = 16) uniform sampler smp_clamp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

vec3 srgb_to_linear(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

bool ink(uint g, ivec2 f) { return texelFetch(sampler2D(atlas_tex, smp_clamp), ivec2(int(g) * 6 + f.x, f.y), 0).r > 0.5; }

// the scene grid's colour at a device pixel (relative to the grid origin)
vec3 scene_at(ivec2 p) {
    int k = int(P.grid.x);
    ivec2 cell_px = ivec2(6, 9) * k;
    ivec2 cols_rows = ivec2(P.grid.yz);
    if (p.x < 0 || p.y < 0 || p.x >= cols_rows.x * cell_px.x || p.y >= cols_rows.y * cell_px.y) return vec3(0.0);
    if ((P.grid.w & 4u) != 0u) return vec3(0.0);
    ivec2 cell = p / cell_px;
    ivec2 f = (p - cell * cell_px) / k;
    vec4 a = texelFetch(sampler2D(glyph_tex, smp_clamp), cell, 0);
    vec4 b = texelFetch(sampler2D(back_tex, smp_clamp), cell, 0);
    uint g = uint(a.a * 255.0 + 0.5);
    uint flags = uint(b.a * 255.0 + 0.5);
    vec3 fg = a.rgb;
    vec2 guv = (vec2(p) + 0.5) / vec2(cols_rows * cell_px);
    // sky backgrounds are filtered between cells, so the sky is a smooth gradient rather than a mosaic
    vec3 bg = (flags & 3u) == 2u ? textureLod(sampler2D(back_tex, smp_clamp), guv, 0.0).rgb : b.rgb;
    // the targeted block's outline in inverse video
    if ((flags & 32u) != 0u) {
        vec3 ink = fg;
        fg = bg * 0.6;
        bg = ink;
    }
    // emissive halo: the cells' glow, filtered across about two cells
    vec2 o = 1.0 / vec2(cols_rows);
    vec3 glow = textureLod(sampler2D(glow_tex, smp_clamp), guv, 0.0).rgb * 0.4;
    glow += (textureLod(sampler2D(glow_tex, smp_clamp), guv + vec2(o.x, 0.0), 0.0).rgb +
             textureLod(sampler2D(glow_tex, smp_clamp), guv - vec2(o.x, 0.0), 0.0).rgb +
             textureLod(sampler2D(glow_tex, smp_clamp), guv + vec2(0.0, o.y), 0.0).rgb +
             textureLod(sampler2D(glow_tex, smp_clamp), guv - vec2(0.0, o.y), 0.0).rgb) * 0.15;
    bg = saturate(bg + glow * (1.0 - bg));
    if ((P.grid.w & 1u) != 0u) {
        uint i = uint(cell.y * cols_rows.x + cell.x);
        g = i < 95u ? i : 0u;
        fg = vec3(1.0);
        bg = vec3(0.0);
    } else if ((P.grid.w & 2u) != 0u && cell == cols_rows / 2) {
        // the crosshair takes its cell
        g = 11u;
        fg = vec3(1.0);
    }
    return ink(g, f) ? fg : bg;
}

void main() {
    ivec2 d = ivec2(gl_FragCoord.xy);
    vec3 c = scene_at(d - ivec2(P.origin.xy));
    // the text ui over it: its background blends by opacity, its glyphs are opaque
    int um = int(P.grid.x * P.ui.x);
    ivec2 up = d - ivec2(P.ui_origin.xy);
    ivec2 ucell = up / (ivec2(6, 9) * um);
    if (up.x >= 0 && up.y >= 0 && ucell.x < int(P.ui.y) && ucell.y < int(P.ui.z)) {
        vec4 a = texelFetch(sampler2D(ui_tex, smp_clamp), ivec2(ucell.x * 2, ucell.y), 0);
        vec4 b = texelFetch(sampler2D(ui_tex, smp_clamp), ivec2(ucell.x * 2 + 1, ucell.y), 0);
        c = mix(c, b.rgb, b.a);
        uint g = uint(a.a * 255.0 + 0.5);
        ivec2 f = (up - ucell * ivec2(6, 9) * um) / um;
        if (g != 0u && ink(g, f)) c = a.rgb;
    }
    if (P.origin.z == 1u) c = luminance(c) * vec3(0.42, 1.0, 0.5);
    else if (P.origin.z == 2u) c = luminance(c) * vec3(1.0, 0.7, 0.28);
    frag_color = vec4(srgb_to_linear(saturate(c)), 1.0);
}
@end

// cloud weather map, mip 0: r = coverage, g = cloud type, b = puff height
#include "vx_noise_common.glsl"

@cs
layout(set = 1, binding = 0, rgba8) writeonly uniform image2D dst;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// separate cumulus puffs: tileable voronoi domes, one per cell, each its own radius and height
#define VX_PUFF_CELLS 32
float puff_height(vec2 uv) {
    vec2 p = uv * float(VX_PUFF_CELLS);
    ivec2 c = ivec2(floor(p));
    float h = 0.0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            ivec2 q = c + ivec2(i, j);
            ivec2 w = (q % VX_PUFF_CELLS + VX_PUFF_CELLS) % VX_PUFF_CELLS;
            uint hs = vx_hash3i(w.x, w.y, 0, 0x9e3779b9u);
            vec2 o = vec2(float(hs & 1023u), float((hs >> 10u) & 1023u)) / 1023.0;
            float r = 0.3 + 0.35 * float((hs >> 20u) & 255u) / 255.0;
            float top = 0.45 + 0.55 * float((hs >> 28u) & 15u) / 15.0;
            vec2 d = (p - (vec2(q) + o)) * vec2(1.0, 1.4);
            h = max(h, top * sqrt(max(0.0, 1.0 - dot(d, d) / (r * r))));
        }
    return h;
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(dst);
    if (px.x >= size.x || px.y >= size.y) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(size);
    float cov = vx_perlin_fbm(vec3(uv * 6.0, 3.0), 6, 5) * 0.5 + 0.5;
    float cells = vx_worley_fbm(vec3(uv * 10.0, 0.3), 10);
    cov = clamp(cov * 0.75 + cells * 0.45 - 0.1, 0.0, 1.0);
    float type = vx_perlin_fbm(vec3(uv * 3.0 + 7.0, 8.5), 3, 3) * 0.5 + 0.5;
    imageStore(dst, px, vec4(cov, clamp(type, 0.0, 1.0), puff_height(uv), 1.0));
}
@end

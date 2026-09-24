// cloud weather map, mip 0: r = coverage, g = cloud type
#include "wc_noise_common.glsl"

@cs
layout(set = 1, binding = 0, rgba8) writeonly uniform image2D dst;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(dst);
    if (px.x >= size.x || px.y >= size.y) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(size);
    float cov = wc_perlin_fbm(vec3(uv * 6.0, 3.0), 6, 5) * 0.5 + 0.5;
    float cells = wc_worley_fbm(vec3(uv * 10.0, 0.3), 10);
    cov = clamp(cov * 0.75 + cells * 0.45 - 0.1, 0.0, 1.0);
    float type = wc_perlin_fbm(vec3(uv * 3.0 + 7.0, 8.5), 3, 3) * 0.5 + 0.5;
    imageStore(dst, px, vec4(cov, clamp(type, 0.0, 1.0), 0.0, 1.0));
}
@end

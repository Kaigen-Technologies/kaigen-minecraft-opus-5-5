// cloud base noise, mip 0: rgba = perlin-worley, worley fbm at 4, 8 and 16 cells
#include "wc_noise_common.glsl"

@cs
layout(set = 1, binding = 0, rgba8) writeonly uniform image3D dst;

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main() {
    ivec3 v = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(dst);
    if (any(greaterThanEqual(v, size))) return;
    vec3 p = (vec3(v) + 0.5) / vec3(size);
    float pfbm = wc_perlin_fbm(p * 4.0, 4, 5) * 0.5 + 0.5;
    vec3 w = wc_worley_octaves(p);
    // remap(pfbm, 0, 1, w0, 1)
    float pw = w.x + pfbm * (1.0 - w.x);
    imageStore(dst, v, clamp(vec4(pw, w), 0.0, 1.0));
}
@end

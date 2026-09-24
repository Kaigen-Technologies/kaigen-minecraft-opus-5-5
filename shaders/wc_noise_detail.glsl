// cloud detail noise, mip 0: rgb = worley fbm at 4, 8 and 16 cells
#include "wc_noise_common.glsl"

@cs
layout(set = 1, binding = 0, rgba8) writeonly uniform image3D dst;

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main() {
    ivec3 v = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(dst);
    if (any(greaterThanEqual(v, size))) return;
    vec3 w = wc_worley_octaves((vec3(v) + 0.5) / vec3(size));
    imageStore(dst, v, vec4(clamp(w, 0.0, 1.0), 1.0));
}
@end

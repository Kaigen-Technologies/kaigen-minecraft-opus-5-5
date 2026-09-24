// one 3d mip from the level above it (2x2x2 box); the source view stops before the level being written
@cs
layout(set = 0, binding = 0) uniform _DownParams {
    int src_mip;
    int _pad0;
    int _pad1;
    int _pad2;
} params;

layout(set = 1, binding = 0, rgba8) writeonly uniform image3D dst;
layout(set = 1, binding = 1) uniform texture3D src;
@nearest
layout(set = 1, binding = 2) uniform sampler point_sampler;

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main() {
    ivec3 v = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(dst);
    if (any(greaterThanEqual(v, size))) return;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 8; k++) {
        ivec3 s = v * 2 + ivec3(k & 1, (k >> 1) & 1, k >> 2);
        sum += texelFetch(sampler3D(src, point_sampler), s, params.src_mip);
    }
    imageStore(dst, v, sum * 0.125);
}
@end

// one 2d mip from the level above it (2x2 box); the source view stops before the level being written
@cs
layout(set = 0, binding = 0) uniform _DownParams {
    int src_mip;
    int _pad0;
    int _pad1;
    int _pad2;
} params;

layout(set = 1, binding = 0, rgba8) writeonly uniform image2D dst;
layout(set = 1, binding = 1) uniform texture2D src;
@nearest
layout(set = 1, binding = 2) uniform sampler point_sampler;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(dst);
    if (px.x >= size.x || px.y >= size.y) return;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 4; k++)
        sum += texelFetch(sampler2D(src, point_sampler), px * 2 + ivec2(k & 1, k >> 1), params.src_mip);
    imageStore(dst, px, sum * 0.25);
}
@end

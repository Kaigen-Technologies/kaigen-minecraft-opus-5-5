// expands the frame's visible quad ranges into one (quad, draw record) entry per quad; one workgroup per range
@ctype uint u32

@cs
layout(set = 0, binding = 0) uniform _ExpandParams {
    uint range_count;
    uint groups_x;
    uint _pad0;
    uint _pad1;
} params;

// src quad in its pool, quad count, draw record, first output entry
layout(set = 1, binding = 0) readonly buffer _Ranges {
    uvec4 r[];
} ranges;

layout(set = 2, binding = 0) buffer _Visible {
    uvec2 v[];
} visible;

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

void main() {
    uint ri = gl_WorkGroupID.y * params.groups_x + gl_WorkGroupID.x;
    if (ri >= params.range_count) return;
    uvec4 rg = ranges.r[ri];
    for (uint i = gl_LocalInvocationID.x; i < rg.y; i += 64u) visible.v[rg.w + i] = uvec2(rg.x + i, rg.z);
}
@end

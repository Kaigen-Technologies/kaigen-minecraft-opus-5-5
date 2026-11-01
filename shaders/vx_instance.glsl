// instanced vertex pulling (vertex shaders only): the draw's base instance and cube face corners
// the instance index is the draw's first visible entry; d3d12 excludes StartInstanceLocation, so it is pushed
#ifdef SHADER_BACKEND_SPIRV
#define HZ_INSTANCE_INDEX gl_InstanceIndex
#else
#ifdef SHADER_BACKEND_HLSL
layout(push_constant) uniform _BaseInstance {
    uint base_instance;
} _hz;
#define HZ_INSTANCE_INDEX (gl_InstanceIndex + _hz.base_instance)
#else
#define HZ_INSTANCE_INDEX gl_InstanceIndex
#endif
#endif

// corner c of cube face f as x | y<<1 | z<<2, CCW seen from outside: BL, BR, TR, TL (vx_mesh.c's corner table)
uint vx_face_corner(uint f, uint c) {
    uvec3 t = uvec3(0x5a0ecdu, 0x2254feu, 0x681decu);
    return (t[f >> 1u] >> ((f & 1u) * 12u + c * 3u)) & 7u;
}

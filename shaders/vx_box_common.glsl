#ifndef VX_BOX_COMMON_GLSL
#define VX_BOX_COMMON_GLSL
// instanced box records, shared by vx_box and vx_box_shadow
// record: 3 rows of the camera-relative affine map of the unit cube | tint srgb rgba8 (alpha: hit flash), layer 12 | flags 8 << 12 (4 self-lit, 8 near fade)
layout(set = 1, binding = 4) readonly buffer _Boxes { uvec4 v[]; } boxes;

struct VxBoxVertex {
    vec3 rel;
    vec3 local; // the corner in the unit cube
    vec3 normal;
    vec2 uv;
    uint face;
    uint tint;
    uint misc;
};

// unit-cube corners (x | y << 1 | z << 2) of each face's four corners, 3 bits each, as the entity quads order them
uint vx_box_face_corners(uint f) {
    return f == 0u ? 3789u : f == 1u ? 1440u : f == 2u ? 1278u : f == 3u ? 549u : f == 4u ? 3564u : 1665u;
}

VxBoxVertex vx_box_vertex(uint vi) {
    uint bi = vi / 36u;
    uint c = vi % 36u;
    uint f = c / 6u;
    uint t = c % 6u;
    // two triangles per face: 0 1 2, 0 2 3
    uint k = t < 3u ? t : (t == 3u ? 0u : t - 2u);
    uint cr = (vx_box_face_corners(f) >> (k * 3u)) & 7u;
    vec4 r0 = uintBitsToFloat(boxes.v[bi * 4u]);
    vec4 r1 = uintBitsToFloat(boxes.v[bi * 4u + 1u]);
    vec4 r2 = uintBitsToFloat(boxes.v[bi * 4u + 2u]);
    uvec4 m = boxes.v[bi * 4u + 3u];
    vec4 p = vec4(float(cr & 1u), float((cr >> 1u) & 1u), float(cr >> 2u), 1.0);
    VxBoxVertex o;
    o.rel = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    o.local = p.xyz;
    vec3 cx = vec3(r0.x, r1.x, r2.x), cy = vec3(r0.y, r1.y, r2.y), cz = vec3(r0.z, r1.z, r2.z);
    uint a = f >> 1u;
    vec3 n = a == 0u ? cross(cy, cz) : a == 1u ? cross(cz, cx) : cross(cx, cy);
    o.normal = normalize(n) * ((f & 1u) != 0u ? -1.0 : 1.0);
    // the tile covers one block per uv unit: a face samples as much of it as its world size
    vec3 sz = vec3(length(cx), length(cy), length(cz));
    float su = f < 2u ? sz.z : sz.x;
    float sv = f < 2u ? sz.y : f < 4u ? sz.z : sz.y;
    o.uv = vec2(k == 1u || k == 2u ? 1.0 : 0.0, k < 2u ? 1.0 : 0.0) * min(vec2(1.0), vec2(su, sv));
    o.face = f;
    o.tint = m.x;
    o.misc = m.y;
    return o;
}
#endif

#include "voxaria/vx.h"

// one VX_QUAD_U32-word record per quad (vx_quad); shaders/vx_quad.glsl derives its corners

#define VX_FACES 6
#define VX_CORNERS 4
hz_internal const i32 VX_FACE_OFF[VX_FACES] = {1, -1, 324, -324, 18, -18};
// per face corner: side1, side2, diagonal neighbour offsets in the padded volume
hz_internal const i32 VX_AO_OFF[VX_FACES][VX_CORNERS][3] = {
    {{-323, 19, -305}, {-323, -17, -341}, {325, -17, 307}, {325, 19, 343}},
    {{-325, -19, -343}, {-325, 17, -307}, {323, 17, 341}, {323, -19, 305}},
    {{342, 323, 341}, {342, 325, 343}, {306, 325, 307}, {306, 323, 305}},
    {{-306, -323, -305}, {-306, -325, -307}, {-342, -325, -343}, {-342, -323, -341}},
    {{17, -306, -307}, {19, -306, -305}, {19, 342, 343}, {17, 342, 341}},
    {{-17, -342, -341}, {-19, -342, -343}, {-19, 306, 305}, {-17, 306, 307}},
};

void vx_mesh_scratch_init(VxMeshScratch *m, Allocator *alloc) {
  m->blocks = ALLOC_ARRAY_NO_ZERO(alloc, u8, VX_PAD3);
  m->state = ALLOC_ARRAY_NO_ZERO(alloc, u8, VX_PAD3);
  m->opaque = ALLOC_ARRAY_NO_ZERO(alloc, u32, VX_MAX_SECTION_QUADS * VX_QUAD_U32);
  m->trans = ALLOC_ARRAY_NO_ZERO(alloc, u32, VX_MAX_SECTION_QUADS * VX_QUAD_U32);
  m->sort = ALLOC_ARRAY_NO_ZERO(alloc, u32, VX_MAX_SECTION_QUADS * VX_QUAD_U32);
}

typedef struct {
  u32 *data;
  u32 count; // quads
} VxVerts;

typedef enum { VX_QUAD_CUBE, VX_QUAD_CROSS, VX_QUAD_BOX, VX_QUAD_LIQUID } VxQuadShape;

#define VX_Q_CROSS_DIAG (1u << 21)
#define VX_Q_CROSS_BACK (1u << 22)
#define VX_Q_CROSS_WAVE (1u << 23)
#define VX_Q_LIQUID_FALL (1u << 21)
#define VX_Q_LIQUID_SPLASH (1u << 22)
#define VX_Q_LIQUID_SHIMMER (1u << 23)
#define VX_Q_BOX_LOCAL (1u << 21) // the face shows one block's quarter of its tile wherever it stands

// ao holds 2 bits per corner in corner order
force_inline void vx_quad(VxVerts *mb, i32 x, i32 y, i32 z, u32 face, u32 shape, u32 bits, u32 layer, u32 flags,
                          u32 ao) {
  u32 *d = mb->data + mb->count * VX_QUAD_U32;
  d[0] = (u32)x | ((u32)z << 4) | ((u32)y << 8) | (face << 16) | (shape << 19) | bits;
  d[1] = layer | (flags << 10) | (ao << 16);
  d[2] = 0;
  mb->count++;
}

// texture array layer of a block face: the painted variant is picked by position
force_inline u32 vx_layer_of(u8 id, u32 face, i32 wx, i32 wy, i32 wz) {
  return vx_tex_layer_of(vx_face_tex[id][face], vx_hash3i(wx, wy, wz, 0x7a11e5u + face));
}

// a cube face's layer: a banner block shows its painted banner on the face its state names, brick elsewhere
force_inline u32 vx_cube_layer(u8 id, u8 state, u32 face, i32 wx, i32 wy, i32 wz) {
  if (id == B_BANNER && face == VX_BANNER_FACE(state)) return vx_tex_layer[T_BANNER] + VX_BANNER_LAYER(state) % vx_tex_variants[T_BANNER];
  return vx_layer_of(id, face, wx, wy, wz);
}

force_inline u32 vx_quad_bucket(const u32 *q) {
  u32 face = (q[0] >> 16) & 7;
  if (((q[0] >> 19) & 3) != VX_QUAD_CUBE) return VX_BUCKET_OTHER;
  return ((q[1] >> 10) & 63) ? VX_BUCKET_FLAGGED + face : face;
}

// stable counting sort of n quads by bucket into tmp, which it returns
hz_internal u32 *vx_sort_buckets(u32 *data, u32 *tmp, u32 n, u16 *counts) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u32 *off = ALLOC_ARRAY_NO_ZERO(&ta.allocator, u32, VX_BUCKETS);
  for (u32 b = 0; b < VX_BUCKETS; b++) counts[b] = 0;
  for (u32 q = 0; q < n; q++) counts[vx_quad_bucket(data + q * VX_QUAD_U32)]++;
  u32 sum = 0;
  for (u32 b = 0; b < VX_BUCKETS; b++) {
    off[b] = sum;
    sum += counts[b];
  }
  for (u32 q = 0; q < n; q++) {
    const u32 *src = data + q * VX_QUAD_U32;
    u32 *dst = tmp + off[vx_quad_bucket(src)]++ * VX_QUAD_U32;
    for (u32 i = 0; i < VX_QUAD_U32; i++) dst[i] = src[i];
  }
  tctx_temp_allocator_end(ta);
  return tmp;
}

force_inline b32 vx_cull_same(u8 id) { return id == B_GLASS || vx_shape[id] == VX_SHAPE_LIQUID; }

hz_internal void vx_emit_cross(VxVerts *mb, u8 id, i32 x, i32 y, i32 z, i32 wx, i32 wy, i32 wz) {
  u32 bits = vx_wave[id] == VX_WAVE_PLANT ? VX_Q_CROSS_WAVE : 0;
  u32 layer = vx_layer_of(id, 0, wx, wy, wz);
  // bottom corners ao 2, top corners ao 3
  u32 ao = 2u | (2u << 2) | (3u << 4) | (3u << 6);
  for (u32 q = 0; q < 2; q++) {
    u32 diag = q ? VX_Q_CROSS_DIAG : 0;
    vx_quad(mb, x, y, z, VX_FACE_PLANT, VX_QUAD_CROSS, bits | diag, layer, VX_VF_CUTOUT, ao);
    vx_quad(mb, x, y, z, VX_FACE_PLANT, VX_QUAD_CROSS, bits | diag | VX_Q_CROSS_BACK, layer, VX_VF_CUTOUT, ao);
  }
}

// face f after a quarter turn about +y: +x -> +z -> -x -> -z
hz_internal const u8 VX_FACE_TURN[VX_FACES] = {VX_FACE_PZ, VX_FACE_NZ, VX_FACE_PY, VX_FACE_NY, VX_FACE_NX, VX_FACE_PX};

// a model box placed by the voxel's state: torches shift onto their wall, other models turn in quarter turns
hz_internal VxBox vx_box_placed(const VxBox *src, u8 id, u8 state) {
  VxBox b = *src;
  if (vx_shape[id] == VX_SHAPE_TORCH) {
    if (state == VX_SIDE_FLOOR || state > VX_SIDE_NZ) return b;
    i32 dx = state == VX_SIDE_PX ? 6 : state == VX_SIDE_NX ? -6 : 0;
    i32 dz = state == VX_SIDE_PZ ? 6 : state == VX_SIDE_NZ ? -6 : 0;
    b.lo[0] = (u8)(b.lo[0] + dx), b.hi[0] = (u8)(b.hi[0] + dx);
    b.lo[2] = (u8)(b.lo[2] + dz), b.hi[2] = (u8)(b.hi[2] + dz);
    b.lo[1] = (u8)(b.lo[1] + 3), b.hi[1] = (u8)(b.hi[1] + 3);
    return b;
  }
  for (u32 r = 0; r < (u32)(state & 3); r++) {
    // (x, z) -> (16 - z, x)
    VxBox t = b;
    t.lo[0] = (u8)(16 - b.hi[2]), t.hi[0] = (u8)(16 - b.lo[2]);
    t.lo[2] = b.lo[0], t.hi[2] = b.hi[0];
    for (u32 f = 0; f < VX_FACES; f++) t.tex[VX_FACE_TURN[f]] = b.tex[f];
    b = t;
  }
  return b;
}

// a model's boxes: faces on the block's boundary are culled by an occluding neighbour like a cube's
hz_internal void vx_emit_model(VxVerts *mb, const u8 *blocks, i32 p, u8 id, u8 state, i32 x, i32 y, i32 z, i32 wx, i32 wy,
                               i32 wz) {
  VxModel m = vx_model[id];
  u32 flags = vx_layer[id] == VX_LAYER_CUTOUT ? VX_VF_CUTOUT : 0;
  for (u32 bi = m.first; bi < (u32)m.first + m.count; bi++) {
    VxBox placed = vx_box_placed(&vx_boxes[bi], id, state);
    const VxBox *box = &placed;
    u32 code = (u32)box->lo[0] | ((u32)box->lo[1] << 4) | ((u32)box->lo[2] << 8) | ((u32)(box->hi[0] - 1) << 12) |
               ((u32)(box->hi[1] - 1) << 16) | ((u32)(box->hi[2] - 1) << 20);
    for (u32 f = 0; f < VX_FACES; f++) {
      u32 axis = f >> 1;
      b32 on_edge = (f & 1) ? box->lo[axis] == 0 : box->hi[axis] == 16;
      if (on_edge && vx_occludes[blocks[p + VX_FACE_OFF[f]]]) continue;
      u8 t = box->tex[f] != VX_BOX_TEX_BLOCK ? box->tex[f] : vx_face_tex[id][f];
      // a sign's lettered face shows the word its state names, mapped onto the board alone
      b32 word = t == T_SIGN;
      u32 layer = word ? vx_tex_layer[T_SIGN] + (u32)(state >> 2) % vx_tex_variants[T_SIGN]
                       : vx_tex_layer_of(t, vx_hash3i(wx, wy, wz, 0x7a11e5u + f));
      u32 *d = mb->data + mb->count * VX_QUAD_U32;
      vx_quad(mb, x, y, z, f, VX_QUAD_BOX, word ? VX_Q_BOX_LOCAL : 0, layer, flags, 0xff);
      d[2] = code;
    }
  }
}

// one cell of a furniture piece: its footprint boxes cut to the cell, faces on a cut left out, then turned
hz_internal void vx_emit_furniture(VxVerts *mb, const u8 *blocks, i32 p, u8 id, u8 state, i32 x, i32 y, i32 z, i32 wx,
                                   i32 wy, i32 wz) {
  VxFurniture fu = vx_furniture[id];
  u32 part = VX_FURN_PART(state);
  i32 cx = (i32)(part % fu.w) * 16, cy = (i32)(part / fu.w) * 16;
  // a showpiece's material: the cell its part-0 origin stands in, so a whole set shares it
  u32 mat = 0;
  b32 show = (state & VX_FURN_SHOW) != 0;
  if (show) {
    u32 rot = state & 3u;
    i32 px = (i32)(part % fu.w), ox = wx - (rot == 0 ? px : rot == 2 ? -px : 0), oz = wz - (rot == 1 ? px : rot == 3 ? -px : 0);
    i32 qx = ox >= 0 ? ox / 4 : -((-ox + 3) / 4), qz = oz >= 0 ? oz / 8 : -((-oz + 7) / 8);
    mat = vx_hash2i(qx, qz, 0x6d757365u) % vx_tex_variants[T_FURN_WOOD];
  }
  for (u32 bi = fu.first; bi < (u32)fu.first + fu.count; bi++) {
    const VxBox *src = &vx_furn_boxes[bi];
    VxBox b;
    u32 cut = 0;
    b32 empty = false;
    for (u32 a = 0; a < 3 && !empty; a++) {
      i32 o = a == 0 ? cx : a == 1 ? cy : 0;
      i32 l = MAX((i32)src->lo[a] - o, 0), h = MIN((i32)src->hi[a] - o, 16);
      empty = l >= h;
      b.lo[a] = (u8)l, b.hi[a] = (u8)h;
      // a face on the cut plane is inside the piece
      if ((i32)src->lo[a] - o < 0) cut |= 1u << (a * 2 + 1);
      if ((i32)src->hi[a] - o > 16) cut |= 1u << (a * 2);
    }
    if (empty) continue;
    for (u32 f = 0; f < VX_FACES; f++) b.tex[f] = (cut >> f) & 1 ? VX_BOX_TEX_NONE : src->tex[f];
    VxBox placed = vx_box_placed(&b, id, (u8)(state & 3));
    u32 code = (u32)placed.lo[0] | ((u32)placed.lo[1] << 4) | ((u32)placed.lo[2] << 8) |
               ((u32)(placed.hi[0] - 1) << 12) | ((u32)(placed.hi[1] - 1) << 16) | ((u32)(placed.hi[2] - 1) << 20);
    for (u32 f = 0; f < VX_FACES; f++) {
      if (placed.tex[f] == VX_BOX_TEX_NONE) continue;
      u32 axis = f >> 1;
      b32 on_edge = (f & 1) ? placed.lo[axis] == 0 : placed.hi[axis] == 16;
      if (on_edge && vx_occludes[blocks[p + VX_FACE_OFF[f]]]) continue;
      u8 t = placed.tex[f] != VX_BOX_TEX_BLOCK ? placed.tex[f] : vx_face_tex[id][f];
      if (show && t == T_PLANKS) t = T_FURN_WOOD;
      u32 layer = t == T_FURN_WOOD || t == T_FURN_CLOTH ? vx_tex_layer[t] + mat : vx_tex_layer_of(t, vx_hash3i(wx, wy, wz, 0x7a11e5u + f));
      u32 *d = mb->data + mb->count * VX_QUAD_U32;
      vx_quad(mb, x, y, z, f, VX_QUAD_BOX, 0, layer, 0, 0xff);
      d[2] = code;
    }
  }
}

// a liquid cell's surface height in 1/16 block: 16 under more liquid, else by its level; -1 for a cell that does not count
force_inline i32 vx_liq_h16(const VxMeshScratch *m, i32 p, u8 id) {
  u8 b = m->blocks[p];
  if (b == id) return m->blocks[p + VX_PAD2] == id ? 16 : 2 + (i32)(8 - (m->state[p] & 7)) * 3 / 2;
  if (vx_occludes[b] || vx_shape[b] == VX_SHAPE_LIQUID) return -1;
  return 0;
}

// the four top corners' heights (x | z << 1 order, 5 bits each): each averages the cells that share the corner
hz_internal u32 vx_liq_corners(const VxMeshScratch *m, i32 p, u8 id) {
  if (m->blocks[p + VX_PAD2] == id) return 16u | 16u << 5 | 16u << 10 | 16u << 15;
  u32 out = 0;
  for (u32 c = 0; c < 4; c++) {
    i32 dx = (i32)(c & 1), dz = (i32)(c >> 1);
    i32 sum = 0, n = 0, h = 0;
    b32 full = false;
    for (i32 k = 0; k < 4; k++) {
      i32 q = p + (dx - 1 + (k & 1)) + (dz - 1 + (k >> 1)) * VX_PAD;
      i32 v = vx_liq_h16(m, q, id);
      if (v < 0) continue;
      if (v == 16) full = true;
      sum += v;
      n++;
    }
    h = full ? 16 : n ? (sum + n / 2) / n : 14;
    out |= (u32)h << (c * 5);
  }
  return out;
}

hz_internal void vx_emit_liquid(VxMeshScratch *m, VxVerts *opaque, VxVerts *trans, u8 id, i32 x, i32 y, i32 z,
                                i32 p) {
  b32 is_lava = id == B_LAVA;
  VxVerts *mb = is_lava ? opaque : trans;
  u32 flags = is_lava ? VX_VF_LAVA : id == B_HONEY ? VX_VF_HONEY : id == B_SHIMMER ? 0 : VX_VF_WATER;
  b32 under = m->blocks[p + VX_PAD2] == id;
  u32 corners = vx_liq_corners(m, p, id);
  // the surface churns where a falling column lands beside it
  b32 splash = false;
  for (i32 k = 0; k < 4 && !splash; k++) {
    i32 q = p + VX_FACE_OFF[k == 0 ? VX_FACE_PX : k == 1 ? VX_FACE_NX : k == 2 ? VX_FACE_PZ : VX_FACE_NZ];
    splash = m->blocks[q] == id && m->blocks[q + VX_PAD2] == id;
  }
  for (i32 f = 0; f < VX_FACES; f++) {
    u8 n = m->blocks[p + VX_FACE_OFF[f]];
    // on the cutaway slab the front face is the cut, whatever is in front of it
    b32 cap = f == VX_FACE_PZ && z == m->cut_lz;
    if (cap) n = B_AIR;
    if (vx_occludes[n]) continue;
    // liquids of different kinds share one surface: only the lower id draws it
    if (vx_shape[n] == VX_SHAPE_LIQUID && n != id && n < id) continue;
    // beside more of the same liquid, a side shows only where this surface stands above the neighbour's
    if (n == id) {
      if (f == VX_FACE_PY || f == VX_FACE_NY) continue;
      if (under || vx_liq_h16(m, p + VX_FACE_OFF[f], id) >= vx_liq_h16(m, p, id)) continue;
    }
    u32 bits = 0;
    if (f != VX_FACE_PY && f != VX_FACE_NY && under && n != id) bits |= VX_Q_LIQUID_FALL;
    if (f == VX_FACE_PY && splash) bits |= VX_Q_LIQUID_SPLASH;
    if (id == B_SHIMMER) bits |= VX_Q_LIQUID_SHIMMER;
    u32 layer = vx_tex_layer[vx_face_tex[id][f]];
    u32 *d = mb->data + mb->count * VX_QUAD_U32;
    vx_quad(mb, x, y, z, (u32)f, VX_QUAD_LIQUID, bits, layer, flags, 0xff);
    d[2] = corners;
  }
}

void vx_mesh_section(VxMeshScratch *m, i32 ox, i32 oy, i32 oz) {
  VxVerts opaque = {.data = m->opaque};
  VxVerts trans = {.data = m->trans};
  const u8 *blocks = m->blocks;
  u32 ao[VX_CORNERS];

  for (i32 y = 0; y < 16; y++) {
    for (i32 z = 0; z < 16; z++) {
      i32 p = (y + 1) * VX_PAD2 + (z + 1) * VX_PAD + 1;
      for (i32 x = 0; x < 16; x++, p++) {
        u8 id = blocks[p];
        if (id == B_AIR) continue;
        u8 shape = vx_shape[id];
        if (shape == VX_SHAPE_CUBE) {
          u8 layer = vx_layer[id];
          VxVerts *mb = layer == VX_LAYER_TRANSLUCENT ? &trans : &opaque;
          b32 cut = layer == VX_LAYER_CUTOUT;
          u32 flags = 0;
          if (cut) flags |= VX_VF_CUTOUT;
          if (vx_wave[id] == VX_WAVE_LEAVES) flags |= VX_VF_WAVE_LEAF;
          for (i32 f = 0; f < VX_FACES; f++) {
            i32 no = VX_FACE_OFF[f];
            u8 n = blocks[p + no];
            // on the cutaway slab the front face is the cut: always drawn, unshaded by the cells that are cut away
            if (f == VX_FACE_PZ && z == m->cut_lz) {
              vx_quad(mb, x, y, z, (u32)f, VX_QUAD_CUBE, 0, vx_cube_layer(id, m->state[p], (u32)f, ox + x, oy + y, oz + z), flags, 0xff);
              continue;
            }
            if (vx_occludes[n]) continue;
            if (n == id && vx_cull_same(id)) continue;
            for (i32 c = 0; c < 4; c++) {
              const i32 *o = VX_AO_OFF[f][c];
              u32 s1 = vx_occludes[blocks[p + o[0]]];
              u32 s2 = vx_occludes[blocks[p + o[1]]];
              u32 cc = (s1 && s2) ? 1 : vx_occludes[blocks[p + o[2]]];
              ao[c] = (s1 && s2) ? 0 : 3 - (s1 + s2 + cc);
            }
            vx_quad(mb, x, y, z, (u32)f, VX_QUAD_CUBE, 0, vx_cube_layer(id, m->state[p], (u32)f, ox + x, oy + y, oz + z), flags,
                    ao[0] | (ao[1] << 2) | (ao[2] << 4) | (ao[3] << 6));
          }
        } else if (shape == VX_SHAPE_CROSS) {
          vx_emit_cross(&opaque, id, x, y, z, ox + x, oy + y, oz + z);
        } else if (shape == VX_SHAPE_LIQUID) {
          vx_emit_liquid(m, &opaque, &trans, id, x, y, z, p);
        } else if (shape == VX_SHAPE_MODEL && vx_is_furniture(id)) {
          vx_emit_furniture(&opaque, blocks, p, id, m->state[p], x, y, z, ox + x, oy + y, oz + z);
        } else if (shape == VX_SHAPE_TORCH || shape == VX_SHAPE_MODEL) {
          vx_emit_model(&opaque, blocks, p, id, m->state[p], x, y, z, ox + x, oy + y, oz + z);
        }
      }
    }
  }
  // positions become relative to the column base so a column draws in one call
  u32 add = (u32)oy << 8;
  for (u32 q = 0; q < opaque.count; q++) opaque.data[q * VX_QUAD_U32] += add;
  for (u32 q = 0; q < trans.count; q++) trans.data[q * VX_QUAD_U32] += add;
  m->opaque_quads = opaque.count;
  m->trans_quads = trans.count;
  u32 *sorted = vx_sort_buckets(m->opaque, m->sort, m->opaque_quads, m->buckets[0]);
  m->sort = m->opaque;
  m->opaque = sorted;
  sorted = vx_sort_buckets(m->trans, m->sort, m->trans_quads, m->buckets[1]);
  m->sort = m->trans;
  m->trans = sorted;
}

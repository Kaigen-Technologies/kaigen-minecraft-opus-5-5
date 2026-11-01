#include "voxaria/vx.h"
#include "lib/allocator_tlsf.h"

force_inline u32 vx_chunk_key(i32 cx, i32 cz) {
  return (u32)(cx + 32768) * 65536u + (u32)(cz + 32768);
}

force_inline u32 vx_key_hash(u32 k) {
  k ^= k >> 16;
  k *= 0x7feb352du;
  k ^= k >> 15;
  k *= 0x846ca68bu;
  k ^= k >> 16;
  return k;
}

void vx_edits_init(VxEditStore *s) {
  memzero_struct(s);
  s->alloc = make_tlsf_allocator(tlsf_allocator_create(GB(1), MB(1)));
  s->chunk_cap = 256;
  s->chunks = ALLOC_ARRAY(&s->alloc, VxChunkEdits, s->chunk_cap);
  s->table_cap = 1024;
  s->table = ALLOC_ARRAY(&s->alloc, u32, s->table_cap);
}

void vx_edits_clear(VxEditStore *s) {
  for (u32 i = 0; i < s->chunk_count; i++) ALLOC_FREE(&s->alloc, s->chunks[i].items);
  s->chunk_count = 0;
  mem_zero(s->table, sizeof(u32) * s->table_cap);
}

hz_internal u32 vx_edits_find(const VxEditStore *s, u32 key) {
  u32 mask = s->table_cap - 1;
  for (u32 h = vx_key_hash(key) & mask;; h = (h + 1) & mask) {
    u32 v = s->table[h];
    if (v == 0) return 0;
    const VxChunkEdits *e = &s->chunks[v - 1];
    if (vx_chunk_key(e->cx, e->cz) == key) return v;
  }
}

hz_internal void vx_edits_insert_slot(VxEditStore *s, u32 key, u32 value) {
  u32 mask = s->table_cap - 1;
  u32 h = vx_key_hash(key) & mask;
  while (s->table[h] != 0) h = (h + 1) & mask;
  s->table[h] = value;
}

hz_internal VxChunkEdits *vx_edits_get_or_add(VxEditStore *s, i32 cx, i32 cz) {
  u32 key = vx_chunk_key(cx, cz);
  u32 v = vx_edits_find(s, key);
  if (v) return &s->chunks[v - 1];
  if (s->chunk_count == s->chunk_cap) {
    s->chunk_cap *= 2;
    s->chunks = REALLOC_ARRAY(&s->alloc, s->chunks, VxChunkEdits, s->chunk_cap);
  }
  // keep the table under half full
  if ((s->chunk_count + 1) * 2 > s->table_cap) {
    ALLOC_FREE(&s->alloc, s->table);
    s->table_cap *= 2;
    s->table = ALLOC_ARRAY(&s->alloc, u32, s->table_cap);
    for (u32 i = 0; i < s->chunk_count; i++)
      vx_edits_insert_slot(s, vx_chunk_key(s->chunks[i].cx, s->chunks[i].cz), i + 1);
  }
  VxChunkEdits *e = &s->chunks[s->chunk_count++];
  *e = (VxChunkEdits){.cx = cx, .cz = cz};
  vx_edits_insert_slot(s, key, s->chunk_count);
  return e;
}

void vx_edits_record(VxEditStore *s, i32 cx, i32 cz, u16 index, u8 id, u8 state) {
  VxChunkEdits *e = vx_edits_get_or_add(s, cx, cz);
  for (u32 i = 0; i < e->count; i++) {
    if (e->items[i].index == index) {
      e->items[i].id = id;
      e->items[i].state = state;
      return;
    }
  }
  if (e->count == e->cap) {
    u32 cap = e->cap ? e->cap * 2 : 16;
    e->items = e->items ? REALLOC_ARRAY(&s->alloc, e->items, VxEdit, cap)
                        : ALLOC_ARRAY(&s->alloc, VxEdit, cap);
    e->cap = cap;
  }
  e->items[e->count++] = (VxEdit){.index = index, .id = id, .state = state};
}

const VxChunkEdits *vx_edits_for_chunk(const VxEditStore *s, i32 cx, i32 cz) {
  u32 v = vx_edits_find(s, vx_chunk_key(cx, cz));
  return v ? &s->chunks[v - 1] : NULL;
}

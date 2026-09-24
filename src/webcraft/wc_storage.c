#include "webcraft/wc.h"
#include "lib/allocator_tlsf.h"

force_inline u32 wc_chunk_key(i32 cx, i32 cz) {
  return (u32)(cx + 32768) * 65536u + (u32)(cz + 32768);
}

force_inline u32 wc_key_hash(u32 k) {
  k ^= k >> 16;
  k *= 0x7feb352du;
  k ^= k >> 15;
  k *= 0x846ca68bu;
  k ^= k >> 16;
  return k;
}

void wc_edits_init(WcEditStore *s) {
  memzero_struct(s);
  s->alloc = make_tlsf_allocator(tlsf_allocator_create(GB(1), MB(1)));
  s->chunk_cap = 256;
  s->chunks = ALLOC_ARRAY(&s->alloc, WcChunkEdits, s->chunk_cap);
  s->table_cap = 1024;
  s->table = ALLOC_ARRAY(&s->alloc, u32, s->table_cap);
}

void wc_edits_clear(WcEditStore *s) {
  for (u32 i = 0; i < s->chunk_count; i++) ALLOC_FREE(&s->alloc, s->chunks[i].items);
  s->chunk_count = 0;
  mem_zero(s->table, sizeof(u32) * s->table_cap);
}

hz_internal u32 wc_edits_find(const WcEditStore *s, u32 key) {
  u32 mask = s->table_cap - 1;
  for (u32 h = wc_key_hash(key) & mask;; h = (h + 1) & mask) {
    u32 v = s->table[h];
    if (v == 0) return 0;
    const WcChunkEdits *e = &s->chunks[v - 1];
    if (wc_chunk_key(e->cx, e->cz) == key) return v;
  }
}

hz_internal void wc_edits_insert_slot(WcEditStore *s, u32 key, u32 value) {
  u32 mask = s->table_cap - 1;
  u32 h = wc_key_hash(key) & mask;
  while (s->table[h] != 0) h = (h + 1) & mask;
  s->table[h] = value;
}

hz_internal WcChunkEdits *wc_edits_get_or_add(WcEditStore *s, i32 cx, i32 cz) {
  u32 key = wc_chunk_key(cx, cz);
  u32 v = wc_edits_find(s, key);
  if (v) return &s->chunks[v - 1];
  if (s->chunk_count == s->chunk_cap) {
    s->chunk_cap *= 2;
    s->chunks = REALLOC_ARRAY(&s->alloc, s->chunks, WcChunkEdits, s->chunk_cap);
  }
  // keep the table under half full
  if ((s->chunk_count + 1) * 2 > s->table_cap) {
    ALLOC_FREE(&s->alloc, s->table);
    s->table_cap *= 2;
    s->table = ALLOC_ARRAY(&s->alloc, u32, s->table_cap);
    for (u32 i = 0; i < s->chunk_count; i++)
      wc_edits_insert_slot(s, wc_chunk_key(s->chunks[i].cx, s->chunks[i].cz), i + 1);
  }
  WcChunkEdits *e = &s->chunks[s->chunk_count++];
  *e = (WcChunkEdits){.cx = cx, .cz = cz};
  wc_edits_insert_slot(s, key, s->chunk_count);
  return e;
}

void wc_edits_record(WcEditStore *s, i32 cx, i32 cz, u16 index, u8 id) {
  WcChunkEdits *e = wc_edits_get_or_add(s, cx, cz);
  for (u32 i = 0; i < e->count; i++) {
    if (e->items[i].index == index) {
      e->items[i].id = id;
      return;
    }
  }
  if (e->count == e->cap) {
    u32 cap = e->cap ? e->cap * 2 : 16;
    e->items = e->items ? REALLOC_ARRAY(&s->alloc, e->items, WcEdit, cap)
                        : ALLOC_ARRAY(&s->alloc, WcEdit, cap);
    e->cap = cap;
  }
  e->items[e->count++] = (WcEdit){.index = index, .id = id};
}

const WcChunkEdits *wc_edits_for_chunk(const WcEditStore *s, i32 cx, i32 cz) {
  u32 v = wc_edits_find(s, wc_chunk_key(cx, cz));
  return v ? &s->chunks[v - 1] : NULL;
}

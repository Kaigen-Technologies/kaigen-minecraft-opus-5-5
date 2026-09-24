#include "webcraft/wc.h"

#define WC_UPLOAD_SEGMENT MB(16)
#ifdef WASM
// queue.writeBuffer snapshots the bytes immediately, so one segment is reusable
#define WC_UPLOAD_SEGMENTS 1
#else
#define WC_UPLOAD_SEGMENTS GPU_FRAMES_IN_FLIGHT
#endif
#define WC_POOL_MAX_ALLOCS 8192

void wc_mesh_store_init(WcMeshStore *ms, Allocator *alloc) {
  memzero_struct(ms);
  ms->alloc = *alloc;
  ms->upload_segment = WC_UPLOAD_SEGMENT;
  ms->upload_segments = WC_UPLOAD_SEGMENTS;
  u32 size = ms->upload_segment * ms->upload_segments;
  ms->upload = gpu_make_buffer(&(GpuBufferDesc){
      .type = GPU_BUFFER_STRUCTURED,
      .usage = GPU_BUFFER_USAGE_CPU_WRITE_PERSISTENT,
      .size = size,
      .label = "wc mesh upload",
#ifdef WASM
      // a growable persistent buffer keeps one stable cpu mirror on webgpu
      .reserve_size = size,
#endif
  });
  ms->retire_cap = 8192;
  ms->retire = ALLOC_ARRAY(alloc, WcMeshRetire, ms->retire_cap);
}

void wc_mesh_store_begin_frame(WcMeshStore *ms) {
  ms->frame++;
  u32 kept = 0;
  for (u32 i = 0; i < ms->retire_count; i++) {
    WcMeshRetire *r = &ms->retire[i];
    if (ms->frame - r->frame > GPU_FRAMES_IN_FLIGHT) {
      oa_free(&ms->pools[r->pool].oa,
              (OAAllocation){.offset = r->offset, .metadata = r->token});
    } else {
      ms->retire[kept++] = *r;
    }
  }
  ms->retire_count = kept;
  u32 seg = (u32)(ms->frame % ms->upload_segments);
  ms->upload_base = seg * ms->upload_segment;
  ms->upload_used = 0;
  ms->upload_ptr = (u8 *)gpu_buffer_mapped_ptr(ms->upload);
}

u32 wc_mesh_store_upload_space(const WcMeshStore *ms) {
  return ms->upload_segment - ms->upload_used;
}

hz_internal u32 wc_pool_alloc(WcMeshStore *ms, u32 quads, u32 avoid,
                              OAAllocation *out) {
  for (u32 p = 0; p < ms->pool_count; p++) {
    if (p == avoid) continue;
    OAAllocation a = oa_allocate(&ms->pools[p].oa, quads);
    if (a.offset != OA_NO_SPACE) {
      *out = a;
      return p;
    }
  }
  // a column copies out of its old pool, so there are always at least two
  while (ms->pool_count < WC_MAX_MESH_POOLS) {
    u32 p = ms->pool_count++;
    WcMeshPool *pool = &ms->pools[p];
    pool->buf = gpu_make_buffer(&(GpuBufferDesc){
        .type = GPU_BUFFER_STRUCTURED,
        .usage = GPU_BUFFER_USAGE_STATIC,
        .size = WC_POOL_QUADS * WC_QUAD_BYTES,
        .stride = 4,
        .label = "wc mesh pool",
    });
    pool->oa = oa_create(WC_POOL_QUADS, WC_POOL_MAX_ALLOCS, &ms->alloc);
    if (p == avoid) continue;
    OAAllocation a = oa_allocate(&pool->oa, quads);
    if (a.offset != OA_NO_SPACE) {
      *out = a;
      return p;
    }
  }
  return WC_NO_POOL;
}

hz_internal void wc_retire(WcMeshStore *ms, const WcMeshPart *part) {
  if (ms->retire_count == ms->retire_cap) {
    ms->retire_cap *= 2;
    WcMeshRetire *nr = ALLOC_ARRAY(&ms->alloc, WcMeshRetire, ms->retire_cap);
    mem_cpy(nr, ms->retire, sizeof(WcMeshRetire) * ms->retire_count);
    ms->retire = nr;
  }
  ms->retire[ms->retire_count++] = (WcMeshRetire){
      .pool = part->pool, .token = part->alloc_token, .offset = part->base,
      .frame = ms->frame};
  ms->quads_used -= part->start[WC_SECTIONS];
}

void wc_mesh_store_free(WcMeshStore *ms, WcMeshPart *part) {
  if (part->pool == WC_NO_POOL) return;
  wc_retire(ms, part);
  part->pool = WC_NO_POOL;
}

b32 wc_mesh_store_rebuild(WcMeshStore *ms, WcMeshPart *part,
                          const WcSectionUpdate *sec) {
  b32 any = false;
  for (u32 s = 0; s < WC_SECTIONS; s++) any |= sec[s].changed;
  if (!any) return true;

  b32 has_old = part->pool != WC_NO_POOL;
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  u32 *start = ALLOC_ARRAY(&ta.allocator, u32, WC_SECTIONS + 1);
  u32 total = 0, upload = 0;
  for (u32 s = 0; s < WC_SECTIONS; s++) {
    start[s] = total;
    if (sec[s].changed) {
      total += sec[s].quads;
      upload += sec[s].quads * WC_QUAD_BYTES;
    } else if (has_old) {
      total += part->start[s + 1] - part->start[s];
    }
  }
  start[WC_SECTIONS] = total;
  if (upload > wc_mesh_store_upload_space(ms)) {
    tctx_temp_allocator_end(ta);
    return false;
  }
  if (total == 0) {
    wc_mesh_store_free(ms, part);
    tctx_temp_allocator_end(ta);
    return true;
  }

  OAAllocation a;
  u32 pool_idx = wc_pool_alloc(ms, total, has_old ? part->pool : WC_NO_POOL, &a);
  if (pool_idx == WC_NO_POOL) {
    log_error("wc mesh store: out of pool space for % quads", fmt_u32(total));
    tctx_temp_allocator_end(ta);
    return false;
  }
  GpuBuffer dst = ms->pools[pool_idx].buf;
  for (u32 s = 0; s < WC_SECTIONS;) {
    if (sec[s].changed) {
      u32 q = sec[s].quads;
      if (q) {
        u32 bytes = q * WC_QUAD_BYTES;
        mem_cpy(ms->upload_ptr + ms->upload_base + ms->upload_used, sec[s].data, bytes);
        gpu_copy_buffer(dst, (a.offset + start[s]) * WC_QUAD_BYTES, ms->upload,
                        ms->upload_base + ms->upload_used, bytes);
        ms->upload_used += bytes;
      }
      s++;
    } else {
      // one copy for each run of untouched sections
      u32 e = s;
      while (e < WC_SECTIONS && !sec[e].changed) e++;
      u32 run = start[e] - start[s];
      if (run && has_old) {
        gpu_copy_buffer(dst, (a.offset + start[s]) * WC_QUAD_BYTES,
                        ms->pools[part->pool].buf,
                        (part->base + part->start[s]) * WC_QUAD_BYTES,
                        run * WC_QUAD_BYTES);
      }
      s = e;
    }
  }
  if (has_old) wc_retire(ms, part);
  part->pool = pool_idx;
  part->alloc_token = a.metadata;
  part->base = a.offset;
  mem_cpy(part->start, start, sizeof(u32) * (WC_SECTIONS + 1));
  for (u32 s = 0; s < WC_SECTIONS; s++) {
    u16 *m = part->meta + s * WC_SECTION_META;
    if (sec[s].changed && sec[s].buckets) {
      u32 off = 0, mask = 0;
      for (u32 b = 0; b < WC_BUCKETS; b++) {
        m[b] = (u16)off;
        off += sec[s].buckets[b];
        if (sec[s].buckets[b]) mask |= 1u << b;
      }
      m[WC_BUCKETS] = (u16)off;
      m[WC_META_MASK] = (u16)mask;
    } else if (sec[s].changed || !has_old) {
      mem_zero(m, sizeof(u16) * WC_SECTION_META);
    }
  }
  ms->quads_used += total;
  tctx_temp_allocator_end(ta);
  return true;
}

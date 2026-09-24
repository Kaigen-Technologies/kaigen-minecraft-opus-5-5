#include "webcraft/wc_game.h"
#include "procgen/pg_audio.h"

// procedural sound: filtered noise bursts (block break/place, footsteps, splash), thunder, rain

typedef struct {
  f32 freq; // band-pass centre
  f32 q;
  f32 dur;
  f32 gain;
  f32 tone; // optional pitched body (Hz), 0 = none
} WcVoice;

hz_internal const WcVoice WC_VOICES[WC_MAT_COUNT] = {
    [WC_MAT_STONE] = {1800, 0.9f, 0.12f, 0.55f, 180}, [WC_MAT_DIRT] = {700, 0.7f, 0.14f, 0.6f, 0},
    [WC_MAT_GRASS] = {2600, 0.6f, 0.16f, 0.45f, 0},   [WC_MAT_WOOD] = {900, 2.2f, 0.13f, 0.55f, 240},
    [WC_MAT_SAND] = {3800, 0.5f, 0.18f, 0.35f, 0},    [WC_MAT_GLASS] = {5200, 3.0f, 0.22f, 0.35f, 2200},
    [WC_MAT_PLANT] = {3200, 0.8f, 0.1f, 0.3f, 0},     [WC_MAT_WATER] = {600, 1.2f, 0.3f, 0.4f, 0},
    [WC_MAT_WOOL] = {500, 0.5f, 0.12f, 0.35f, 0},     [WC_MAT_SNOW] = {2200, 0.5f, 0.16f, 0.35f, 0},
};

WcMaterial wc_material_of(u8 id) {
  switch (id) {
  case B_GRASS:
  case B_OAK_LEAVES:
  case B_BIRCH_LEAVES:
  case B_SPRUCE_LEAVES:
  case B_MOSS_BLOCK:
  case B_PODZOL: return WC_MAT_GRASS;
  case B_DIRT:
  case B_COARSE_DIRT:
  case B_CLAY:
  case B_GRAVEL: return WC_MAT_DIRT;
  case B_SAND:
  case B_RED_SAND: return WC_MAT_SAND;
  case B_OAK_LOG:
  case B_BIRCH_LOG:
  case B_SPRUCE_LOG:
  case B_OAK_PLANKS:
  case B_BIRCH_PLANKS:
  case B_SPRUCE_PLANKS:
  case B_BOOKSHELF:
  case B_PUMPKIN:
  case B_JACK_O_LANTERN:
  case B_CACTUS: return WC_MAT_WOOD;
  case B_GLASS:
  case B_ICE:
  case B_PACKED_ICE:
  case B_GLOWSTONE:
  case B_SEA_LANTERN: return WC_MAT_GLASS;
  case B_TALL_GRASS:
  case B_FERN:
  case B_DANDELION:
  case B_POPPY:
  case B_CORNFLOWER:
  case B_DAISY:
  case B_DEAD_BUSH:
  case B_SUGAR_CANE:
  case B_RED_MUSHROOM:
  case B_BROWN_MUSHROOM:
  case B_TORCH: return WC_MAT_PLANT;
  case B_WATER:
  case B_LAVA: return WC_MAT_WATER;
  case B_WHITE_WOOL:
  case B_RED_WOOL:
  case B_ORANGE_WOOL:
  case B_YELLOW_WOOL:
  case B_LIME_WOOL:
  case B_BLUE_WOOL:
  case B_BLACK_WOOL: return WC_MAT_WOOL;
  case B_SNOW:
  case B_SNOWY_GRASS: return WC_MAT_SNOW;
  default: return WC_MAT_STONE;
  }
}

// exponential ramp from a to b over t01 (web audio exponentialRampToValueAtTime)
force_inline f32 wc_exp_ramp(f32 a, f32 b, f32 t01) { return a * m_powf(b / a, m_clampf(t01, 0, 1)); }

// adds one noise burst (band-passed noise + optional falling triangle tone) into out
hz_internal void wc_burst(f32 *out, u32 n, WcVoice v, f32 pitch, f32 gain_mul, Random *rng) {
  u32 sr = PG_AUDIO_RATE;
  u32 len = (u32)((v.dur + 0.05f) * sr);
  if (len > n) len = n;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  f32 *noise = ALLOC_ARRAY(&tmp.allocator, f32, len);
  pg_noise_fill(noise, len, PG_NOISE_WHITE, rng);
  PgButter bp = {0};
  pg_butbp_set(&bp, sr, v.freq * pitch, v.freq * pitch / v.q);
  pg_butter_process(&bp, noise, len);
  f32 peak = v.gain * gain_mul;
  for (u32 i = 0; i < len; i++) {
    f32 t = (f32)i / sr;
    f32 g = t < 0.005f ? peak * (t / 0.005f) : wc_exp_ramp(peak, 0.001f, (t - 0.005f) / (v.dur - 0.005f));
    if (t > v.dur) g = 0.001f * m_expf(-(t - v.dur) * 200.0f);
    out[i] += noise[i] * g;
  }
  if (v.tone > 0) {
    PgOsc osc = {0};
    u32 tone_len = (u32)(v.dur * sr);
    for (u32 i = 0; i < tone_len && i < n; i++) {
      f32 t01 = (f32)i / tone_len;
      f32 freq = wc_exp_ramp(v.tone * pitch, v.tone * pitch * 0.6f, t01);
      f32 g = wc_exp_ramp(v.gain * 0.35f * gain_mul, 0.001f, t01 / 0.8f);
      out[i] += pg_osc_next(&osc, sr, freq, PG_OSC_TRI) * g;
    }
  }
  tctx_temp_allocator_end(tmp);
}

hz_internal HzHandleT(AudioSource) wc_make_source(WcAudio *a, const f32 *pcm, u32 n) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  i16 *s16 = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, i16, n);
  for (u32 i = 0; i < n; i++) s16[i] = (i16)(m_clampf(pcm[i], -1.0f, 1.0f) * 32767.0f);
  WavFile wav = {.format = {.audio_format = 1, .channels = 1, .sample_rate = PG_AUDIO_RATE,
                            .byte_rate = PG_AUDIO_RATE * 2, .block_align = 2, .bits_per_sample = 16},
                 .audio_data = s16, .data_size = n * 2, .total_samples = n, .is_loaded = true};
  HzHandleT(AudioSource) src = audio_source_from_wav(&a->sys, &wav, &a->alloc);
  tctx_temp_allocator_end(tmp);
  return src;
}

typedef enum { WC_SFX_BREAK, WC_SFX_PLACE, WC_SFX_STEP } WcSfxKind;

hz_internal HzHandleT(AudioSource) wc_make_sfx(WcAudio *a, WcVoice v, WcSfxKind kind) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 n = (u32)((v.dur * 1.6f + 0.1f) * PG_AUDIO_RATE);
  f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
  if (kind == WC_SFX_BREAK) {
    wc_burst(buf, n, v, 1.0f, 1.0f, &a->rng);
    WcVoice longer = v;
    longer.dur *= 1.6f;
    wc_burst(buf, n, longer, 0.7f, 0.5f, &a->rng);
  } else if (kind == WC_SFX_PLACE) {
    wc_burst(buf, n, v, 0.8f, 0.8f, &a->rng);
  } else {
    WcVoice shorter = v;
    shorter.dur *= 0.7f;
    wc_burst(buf, n, shorter, 1.05f, 0.28f, &a->rng);
  }
  HzHandleT(AudioSource) src = wc_make_source(a, buf, n);
  tctx_temp_allocator_end(tmp);
  return src;
}

// low rumble: dark noise under a falling low-pass, sharp attack then a long decay
hz_internal HzHandleT(AudioSource) wc_make_thunder(WcAudio *a, f32 dur) {
  u32 sr = PG_AUDIO_RATE;
  u32 n = (u32)((dur + 0.1f) * sr);
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
  pg_noise_fill(buf, n, PG_NOISE_WHITE, &a->rng);
  PgButter dark = {0};
  pg_butlp_set(&dark, sr, 8400);
  pg_butter_process(&dark, buf, n);
  PgButter lp = {0};
  const u32 block = 64;
  for (u32 i = 0; i < n; i += block) {
    f32 t = (f32)i / sr;
    pg_butlp_set(&lp, sr, wc_exp_ramp(900, 90, t / dur));
    u32 len = i + block <= n ? block : n - i;
    pg_butter_process(&lp, buf + i, len);
    for (u32 k = 0; k < len; k++) {
      f32 tk = (f32)(i + k) / sr;
      f32 g = tk < 0.08f   ? wc_exp_ramp(0.0001f, 0.9f, tk / 0.08f)
              : tk < 0.6f  ? wc_exp_ramp(0.9f, 0.35f, (tk - 0.08f) / 0.52f)
              : tk < dur   ? wc_exp_ramp(0.35f, 0.0001f, (tk - 0.6f) / (dur - 0.6f))
                           : 0.0f;
      buf[i + k] *= g * 2.5f;
    }
  }
  HzHandleT(AudioSource) src = wc_make_source(a, buf, n);
  tctx_temp_allocator_end(tmp);
  return src;
}

// seamless rain bed: band-limited noise with an equal-power crossfade at the loop point
hz_internal HzHandleT(AudioSource) wc_make_rain(WcAudio *a) {
  u32 sr = PG_AUDIO_RATE;
  u32 len = sr * 4, fade = sr / 4;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, len + fade);
  pg_noise_fill(buf, len + fade, PG_NOISE_WHITE, &a->rng);
  PgButter hp = {0}, lp = {0};
  pg_buthp_set(&hp, sr, 900);
  pg_butlp_set(&lp, sr, 6000);
  pg_butter_process(&hp, buf, len + fade);
  pg_butter_process(&lp, buf, len + fade);
  for (u32 i = 0; i < fade; i++) {
    f32 t = (f32)i / fade;
    buf[i] = buf[i] * m_sqrtf(t) + buf[len + i] * m_sqrtf(1 - t);
  }
  HzHandleT(AudioSource) src = wc_make_source(a, buf, len);
  tctx_temp_allocator_end(tmp);
  return src;
}

void wc_audio_init(WcAudio *a, Allocator *alloc) {
  memzero_struct(a);
  a->alloc = *alloc;
  a->rng = random_create(0x5eed5eedu);
  a->sys = audio_system_init(&a->alloc, NULL);
  a->brk = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), WC_MAT_COUNT);
  a->place = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), WC_MAT_COUNT);
  a->step = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), WC_MAT_COUNT);
  for (u32 m = 0; m < WC_MAT_COUNT; m++) {
    a->brk[m] = wc_make_sfx(a, WC_VOICES[m], WC_SFX_BREAK);
    a->place[m] = wc_make_sfx(a, WC_VOICES[m], WC_SFX_PLACE);
    a->step[m] = wc_make_sfx(a, WC_VOICES[m], WC_SFX_STEP);
  }
  WcVoice water = WC_VOICES[WC_MAT_WATER];
  {
    TempAllocator tmp = tctx_temp_allocator_begin(NULL);
    u32 n = (u32)((water.dur + 0.1f) * PG_AUDIO_RATE);
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    wc_burst(buf, n, water, 0.85f, 1.2f, &a->rng);
    a->splash = wc_make_source(a, buf, n);
    tctx_temp_allocator_end(tmp);
  }
  a->thunder = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), WC_THUNDER_VARIANTS);
  for (u32 i = 0; i < WC_THUNDER_VARIANTS; i++) a->thunder[i] = wc_make_thunder(a, 3.0f + i * 1.25f);
  a->rain = wc_make_rain(a);
  a->pending = ALLOC_ARRAY(alloc, WcPendingThunder, WC_MAX_PENDING_THUNDER);
  a->rain_voice = audio_play(&a->sys, a->rain,
                             &(AudioPlayDesc){.bus = AUDIO_BUS_SFX, .volume = 1e-4f, .loop = true, .protected_voice = true});
}

hz_internal void wc_play(WcAudio *a, HzHandleT(AudioSource) src, f32 volume, f32 pitch) {
  audio_play(&a->sys, src, &(AudioPlayDesc){.bus = AUDIO_BUS_SFX, .volume = volume, .pitch = pitch});
}

force_inline f32 wc_arand(WcAudio *a, f32 lo, f32 hi) { return lo + random_f32(&a->rng) * (hi - lo); }

void wc_audio_break(WcAudio *a, u8 id) { wc_play(a, a->brk[wc_material_of(id)], 1.0f, wc_arand(a, 0.85f, 1.15f)); }
void wc_audio_place(WcAudio *a, u8 id) { wc_play(a, a->place[wc_material_of(id)], 1.0f, wc_arand(a, 0.875f, 1.125f)); }
void wc_audio_step(WcAudio *a, u8 id) { wc_play(a, a->step[wc_material_of(id)], 1.0f, wc_arand(a, 0.857f, 1.143f)); }
void wc_audio_splash(WcAudio *a) { wc_play(a, a->splash, 1.0f, wc_arand(a, 0.82f, 1.18f)); }

void wc_audio_thunder(WcAudio *a, f32 delay, f32 strength) {
  if (a->pending_count == WC_MAX_PENDING_THUNDER) return;
  u32 variant = random_u32(&a->rng) % WC_THUNDER_VARIANTS;
  a->pending[a->pending_count++] = (WcPendingThunder){.delay = delay, .strength = strength, .variant = variant};
}

void wc_audio_update(WcAudio *a, f32 dt, f32 volume, f32 rain, f32 outdoors) {
  audio_bus_set_volume(&a->sys, AUDIO_BUS_MASTER, volume);
  for (u32 i = 0; i < a->pending_count;) {
    WcPendingThunder *p = &a->pending[i];
    p->delay -= dt;
    if (p->delay <= 0) {
      wc_play(a, a->thunder[p->variant], p->strength, 1.0f);
      *p = a->pending[--a->pending_count];
    } else {
      i++;
    }
  }
  // rain ambience follows the weather with a 0.5 s time constant
  f32 target = rain * (0.12f + 0.18f * outdoors);
  a->rain_gain += (target - a->rain_gain) * (1 - m_expf(-dt / 0.5f));
  audio_voice_set_volume(&a->sys, a->rain_voice, m_maxf(a->rain_gain, 1e-5f));
  audio_system_update(&a->sys, dt);
}

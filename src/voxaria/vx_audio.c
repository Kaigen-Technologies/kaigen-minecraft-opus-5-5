#include "voxaria/vx_game.h"

// procedural sound: filtered noise bursts (block break/place, footsteps, splash), thunder, rain

typedef struct {
  f32 freq; // band-pass centre
  f32 q;
  f32 dur;
  f32 gain;
  f32 tone; // optional pitched body (Hz), 0 = none
} VxVoice;

hz_internal const VxVoice VX_VOICES[VX_MAT_COUNT] = {
    [VX_MAT_STONE] = {1800, 0.9f, 0.12f, 0.55f, 180}, [VX_MAT_DIRT] = {700, 0.7f, 0.14f, 0.6f, 0},
    [VX_MAT_GRASS] = {2600, 0.6f, 0.16f, 0.45f, 0},   [VX_MAT_WOOD] = {900, 2.2f, 0.13f, 0.55f, 240},
    [VX_MAT_SAND] = {3800, 0.5f, 0.18f, 0.35f, 0},    [VX_MAT_GLASS] = {5200, 3.0f, 0.22f, 0.35f, 2200},
    [VX_MAT_PLANT] = {3200, 0.8f, 0.1f, 0.3f, 0},     [VX_MAT_WATER] = {600, 1.2f, 0.3f, 0.4f, 0},
    [VX_MAT_WOOL] = {500, 0.5f, 0.12f, 0.35f, 0},     [VX_MAT_SNOW] = {2200, 0.5f, 0.16f, 0.35f, 0},
};

VxMaterial vx_material_of(u8 id) {
  switch (id) {
  case B_GRASS:
  case B_JUNGLE_GRASS:
  case B_MUSHROOM_GRASS:
  case B_CORRUPT_GRASS:
  case B_HALLOW_GRASS:
  case B_LEAVES:
  case B_BOREAL_LEAVES:
  case B_JUNGLE_LEAVES:
  case B_PALM_LEAVES:
  case B_CORRUPT_LEAVES: return VX_MAT_GRASS;
  case B_DIRT:
  case B_CLAY:
  case B_MUD:
  case B_ASH: return VX_MAT_DIRT;
  case B_SAND:
  case B_PEARLSAND: return VX_MAT_SAND;
  case B_LOG:
  case B_BOREAL_LOG:
  case B_MAHOGANY_LOG:
  case B_PALM_LOG:
  case B_EBON_LOG:
  case B_PLANKS:
  case B_BOREAL_PLANKS:
  case B_MAHOGANY_PLANKS:
  case B_CACTUS:
  case B_GLOW_STEM:
  case B_PLATFORM:
  case B_DOOR:
  case B_DOOR_OPEN:
  case B_WORKBENCH:
  case B_TABLE:
  case B_CHAIR:
  case B_BED:
  case B_BOOKCASE:
  case B_SOFA:
  case B_LAMP:
  case B_CLOCK:
  case B_PIANO:
  case B_SIGN:
  case B_CHEST:
  case B_GOLD_CHEST: return VX_MAT_WOOD;
  case B_GLASS:
  case B_ICE:
  case B_AMETHYST:
  case B_SAPPHIRE:
  case B_RUBY:
  case B_EMERALD:
  case B_LIFE_CRYSTAL: return VX_MAT_GLASS;
  case B_GRASS_PLANT:
  case B_FLOWERS:
  case B_JUNGLE_PLANT:
  case B_CORRUPT_PLANT:
  case B_MUSHROOM:
  case B_GLOW_MUSHROOM:
  case B_GLOW_CAP:
  case B_VINE:
  case B_TORCH:
  case B_ICE_TORCH: return VX_MAT_PLANT;
  case B_WATER:
  case B_LAVA:
  case B_HONEY:
  case B_SHIMMER: return VX_MAT_WATER;
  case B_CLOUD: return VX_MAT_WOOL;
  case B_SNOW: return VX_MAT_SNOW;
  default: return VX_MAT_STONE;
  }
}

// exponential ramp from a to b over t01 (web audio exponentialRampToValueAtTime)
force_inline f32 vx_exp_ramp(f32 a, f32 b, f32 t01) { return a * m_powf(b / a, m_clampf(t01, 0, 1)); }

// adds one noise burst (band-passed noise + optional falling triangle tone) into out
hz_internal void vx_burst(f32 *out, u32 n, VxVoice v, f32 pitch, f32 gain_mul, Random *rng) {
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
    f32 g = t < 0.005f ? peak * (t / 0.005f) : vx_exp_ramp(peak, 0.001f, (t - 0.005f) / (v.dur - 0.005f));
    if (t > v.dur) g = 0.001f * m_expf(-(t - v.dur) * 200.0f);
    out[i] += noise[i] * g;
  }
  if (v.tone > 0) {
    PgOsc osc = {0};
    u32 tone_len = (u32)(v.dur * sr);
    for (u32 i = 0; i < tone_len && i < n; i++) {
      f32 t01 = (f32)i / tone_len;
      f32 freq = vx_exp_ramp(v.tone * pitch, v.tone * pitch * 0.6f, t01);
      f32 g = vx_exp_ramp(v.gain * 0.35f * gain_mul, 0.001f, t01 / 0.8f);
      out[i] += pg_osc_next(&osc, sr, freq, PG_OSC_TRI) * g;
    }
  }
  tctx_temp_allocator_end(tmp);
}

hz_internal HzHandleT(AudioSource) vx_make_source(VxAudio *a, const f32 *pcm, u32 n) {
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

typedef enum { VX_SFX_BREAK, VX_SFX_PLACE, VX_SFX_STEP } VxSfxKind;

hz_internal HzHandleT(AudioSource) vx_make_sfx(VxAudio *a, VxVoice v, VxSfxKind kind) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 n = (u32)((v.dur * 1.6f + 0.1f) * PG_AUDIO_RATE);
  f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
  if (kind == VX_SFX_BREAK) {
    vx_burst(buf, n, v, 1.0f, 1.0f, &a->rng);
    VxVoice longer = v;
    longer.dur *= 1.6f;
    vx_burst(buf, n, longer, 0.7f, 0.5f, &a->rng);
  } else if (kind == VX_SFX_PLACE) {
    vx_burst(buf, n, v, 0.8f, 0.8f, &a->rng);
  } else {
    VxVoice shorter = v;
    shorter.dur *= 0.7f;
    vx_burst(buf, n, shorter, 1.05f, 0.28f, &a->rng);
  }
  HzHandleT(AudioSource) src = vx_make_source(a, buf, n);
  tctx_temp_allocator_end(tmp);
  return src;
}

// low rumble: dark noise under a falling low-pass, sharp attack then a long decay
hz_internal HzHandleT(AudioSource) vx_make_thunder(VxAudio *a, f32 dur) {
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
    pg_butlp_set(&lp, sr, vx_exp_ramp(900, 90, t / dur));
    u32 len = i + block <= n ? block : n - i;
    pg_butter_process(&lp, buf + i, len);
    for (u32 k = 0; k < len; k++) {
      f32 tk = (f32)(i + k) / sr;
      f32 g = tk < 0.08f   ? vx_exp_ramp(0.0001f, 0.9f, tk / 0.08f)
              : tk < 0.6f  ? vx_exp_ramp(0.9f, 0.35f, (tk - 0.08f) / 0.52f)
              : tk < dur   ? vx_exp_ramp(0.35f, 0.0001f, (tk - 0.6f) / (dur - 0.6f))
                           : 0.0f;
      buf[i + k] *= g * 2.5f;
    }
  }
  HzHandleT(AudioSource) src = vx_make_source(a, buf, n);
  tctx_temp_allocator_end(tmp);
  return src;
}

// seamless rain bed: band-limited noise with an equal-power crossfade at the loop point
hz_internal HzHandleT(AudioSource) vx_make_rain(VxAudio *a) {
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
  HzHandleT(AudioSource) src = vx_make_source(a, buf, len);
  tctx_temp_allocator_end(tmp);
  return src;
}

// a sine whose pitch glides from f0 to f1 under an attack/decay envelope, mixed into out
hz_internal void vx_glide(f32 *out, u32 n, f32 start_s, f32 dur_s, f32 f0, f32 f1, f32 gain, f32 attack_s) {
  u32 sr = PG_AUDIO_RATE, i0 = (u32)(start_s * sr), len = (u32)(dur_s * sr);
  PgOsc osc = {0};
  for (u32 i = 0; i < len && i0 + i < n; i++) {
    f32 t = (f32)i / (f32)sr, t01 = (f32)i / (f32)len;
    f32 env = m_minf(1, t / attack_s) * (1 - t01) * (1 - t01);
    out[i0 + i] += pg_osc_next(&osc, sr, vx_exp_ramp(f0, f1, t01), PG_OSC_SINE) * env * gain;
  }
}

hz_internal HzHandleT(AudioSource) vx_source_from(VxAudio *a, PgAudioBuf b, f32 gain) {
  for (u32 i = 0; i < b.len; i++) b.samples[i] *= gain;
  return vx_make_source(a, b.samples, b.len);
}

// white noise through a band, under a hit envelope, mixed into out
hz_internal void vx_noise_hit(f32 *out, u32 n, Random *rng, Allocator *tmp, f32 lo_hz, f32 hi_hz, f32 gain, f32 decay_s) {
  f32 *w = ALLOC_ARRAY(tmp, f32, n);
  pg_noise_fill(w, n, PG_NOISE_WHITE, rng);
  PgButter f = {0};
  pg_butbp_set(&f, PG_AUDIO_RATE, (lo_hz + hi_hz) * 0.5f, hi_hz - lo_hz);
  pg_butter_process(&f, w, n);
  pg_env_hit(w, n, PG_AUDIO_RATE, gain, decay_s);
  pg_buf_mix(out, n, w, n, 1);
}

hz_internal HzHandleT(AudioSource) vx_finish(VxAudio *a, f32 *buf, u32 n, f32 peak) {
  pg_buf_normalize(buf, n, peak);
  return vx_make_source(a, buf, n);
}

// Terraria's combat set: a punchy hit, a wet one for slimes, a bony clack, the kill splat, bow, arrow and wand
hz_internal void vx_make_combat_sounds(VxAudio *a, PgSfxCtx *ctx, Allocator *tmp) {
  HzHandleT(AudioSource) *o = a->sounds;
  u32 sr = PG_AUDIO_RATE;
  {
    // hit: a dull body thump, a falling knock and a short slap of noise
    u32 n = sr * 22 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    PgAudioBuf thud = pg_sfx_impact(ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_SOFT, .size01 = 0.45f, .energy01 = 1});
    pg_buf_mix(buf, n, thud.samples, thud.len, 0.9f);
    vx_glide(buf, n, 0, 0.11f, 190, 85, 0.6f, 0.002f);
    vx_noise_hit(buf, n, &a->rng, tmp, 600, 2600, 0.5f, 0.03f);
    o[VX_SND_HIT] = vx_finish(a, buf, n, 0.8f);
  }
  {
    // slime hit: a wet squelch, two bubbles bending down under filtered noise
    u32 n = sr * 24 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    vx_glide(buf, n, 0, 0.14f, 330, 150, 0.6f, 0.004f);
    vx_glide(buf, n, 0.03f, 0.12f, 560, 260, 0.35f, 0.004f);
    vx_noise_hit(buf, n, &a->rng, tmp, 250, 1400, 0.5f, 0.07f);
    o[VX_SND_HIT_SLIME] = vx_finish(a, buf, n, 0.75f);
  }
  {
    // bone hit: two dry clacks a beat apart
    u32 n = sr * 18 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    for (u32 k = 0; k < 2; k++) {
      PgAudioBuf c = pg_sfx_impact(ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_WOOD, .size01 = 0.12f + 0.06f * k, .energy01 = 1});
      u32 at = k * sr * 45 / 1000;
      pg_buf_mix(buf + at, n - at, c.samples, c.len, k ? 0.6f : 1);
    }
    o[VX_SND_HIT_BONE] = vx_finish(a, buf, n, 0.8f);
  }
  {
    // kill: a deep splat, a falling groan of pitch and a soft body landing
    u32 n = sr * 45 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    vx_glide(buf, n, 0, 0.38f, 150, 42, 0.7f, 0.004f);
    vx_noise_hit(buf, n, &a->rng, tmp, 150, 900, 0.8f, 0.16f);
    PgAudioBuf thud = pg_sfx_impact(ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_SOFT, .size01 = 0.9f, .energy01 = 1});
    pg_buf_mix(buf, n, thud.samples, thud.len, 0.8f);
    o[VX_SND_KILL] = vx_finish(a, buf, n, 0.85f);
  }
  {
    // slime kill: the jelly bursting, a spray of bubbles over a wet splat
    u32 n = sr * 4 / 10;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    vx_noise_hit(buf, n, &a->rng, tmp, 200, 1200, 0.9f, 0.12f);
    for (u32 k = 0; k < 4; k++)
      vx_glide(buf, n, 0.02f + 0.045f * k, 0.09f, 280 + 140 * k, 140 + 60 * k, 0.4f - 0.06f * k, 0.003f);
    o[VX_SND_KILL_SLIME] = vx_finish(a, buf, n, 0.8f);
  }
  {
    // bow: the string's twang, a low plucked tone with two overtones, and the arrow leaving
    u32 n = sr * 3 / 10;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    for (u32 h = 1; h <= 3; h++) vx_glide(buf, n, 0, 0.28f, 150.0f * h, 138.0f * h, 0.6f / (f32)h, 0.002f);
    PgAudioBuf w = pg_sfx_whoosh(ctx, &(PgSfxWhooshDesc){.dur_s = 0.14f, .f_start_hz = 1400, .f_end_hz = 3400, .level = 0.5f});
    pg_buf_mix(buf, n, w.samples, w.len, 0.6f);
    o[VX_SND_BOW] = vx_finish(a, buf, n, 0.7f);
  }
  o[VX_SND_ARROW_HIT] = vx_source_from(a, pg_sfx_impact(ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_WOOD,
                                                                               .size01 = 0.3f, .energy01 = 0.8f}), 1);
  {
    // spark: a rising buzzy zap through a band, with crackle
    u32 n = sr * 26 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    PgOsc osc = {0};
    u32 len = sr * 2 / 10;
    for (u32 i = 0; i < len; i++) {
      f32 t01 = (f32)i / (f32)len;
      buf[i] = pg_osc_next(&osc, sr, vx_exp_ramp(300, 1500, t01), PG_OSC_SAW) * m_minf(1, t01 * 40) * (1 - t01) * 0.5f;
    }
    PgButter bp = {0};
    pg_butbp_set(&bp, sr, 1300, 1600);
    pg_butter_process(&bp, buf, n);
    vx_noise_hit(buf, n, &a->rng, tmp, 2500, 7000, 0.35f, 0.12f);
    o[VX_SND_SPARK] = vx_finish(a, buf, n, 0.6f);
  }
  {
    // zombie groan: a low buzzing voice sagging in pitch, shaped by two vowel formants
    u32 n = sr * 13 / 10, len = sr * 11 / 10;
    f32 *src = ALLOC_ARRAY(tmp, f32, n);
    PgOsc osc = {0};
    for (u32 i = 0; i < len; i++) {
      f32 t = (f32)i / (f32)sr, t01 = (f32)i / (f32)len;
      f32 f = 82.0f * (1 - 0.18f * t01) * (1 + 0.03f * m_sinf(2 * PI * 5.5f * t));
      f32 env = m_minf(1, t / 0.18f) * (1 - t01 * t01);
      src[i] = pg_osc_next(&osc, sr, f, PG_OSC_SAW) * env;
    }
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    f32 *f2 = ALLOC_ARRAY(tmp, f32, n);
    mem_cpy(buf, src, n * sizeof(f32));
    mem_cpy(f2, src, n * sizeof(f32));
    PgButter a1 = {0}, a2 = {0};
    pg_butbp_set(&a1, sr, 520, 180);
    pg_butbp_set(&a2, sr, 880, 260);
    pg_butter_process(&a1, buf, n);
    pg_butter_process(&a2, f2, n);
    pg_buf_mix(buf, n, f2, n, 0.6f);
    o[VX_SND_ZOMBIE] = vx_finish(a, buf, n, 0.7f);
  }
  {
    // boss roar: a deep growl swelling and sagging in pitch, rasped with noise, through two vowel formants
    u32 n = sr * 18 / 10, len = sr * 16 / 10;
    f32 *src = ALLOC_ARRAY(tmp, f32, n);
    PgOsc o1 = {0}, o2 = {0};
    for (u32 i = 0; i < len; i++) {
      f32 t = (f32)i / (f32)sr, t01 = (f32)i / (f32)len;
      f32 f = 66.0f * (1 + 0.55f * m_sinf(PI * m_powf(t01, 0.7f))) * (1 + 0.05f * m_sinf(2 * PI * 7.0f * t));
      f32 env = m_minf(1, t / 0.12f) * m_powf(1 - t01, 1.4f);
      f32 rasp = random_f32(&a->rng) * 2 - 1;
      src[i] = (pg_osc_next(&o1, sr, f, PG_OSC_SAW) + 0.6f * pg_osc_next(&o2, sr, f * 1.51f, PG_OSC_SAW) + 0.7f * rasp) * env;
    }
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    f32 *f2 = ALLOC_ARRAY(tmp, f32, n);
    mem_cpy(buf, src, n * sizeof(f32));
    mem_cpy(f2, src, n * sizeof(f32));
    PgButter a1 = {0}, a2 = {0};
    pg_butbp_set(&a1, sr, 640, 260);
    pg_butbp_set(&a2, sr, 1150, 380);
    pg_butter_process(&a1, buf, n);
    pg_butter_process(&a2, f2, n);
    pg_buf_mix(buf, n, f2, n, 0.7f);
    pg_buf_mix(buf, n, src, n, 0.12f);
    o[VX_SND_ROAR] = vx_finish(a, buf, n, 0.9f);
  }
  {
    // boss dash: a heavy rush of air under a short falling growl
    u32 n = sr * 55 / 100;
    f32 *buf = ALLOC_ARRAY(tmp, f32, n);
    PgAudioBuf w = pg_sfx_whoosh(ctx, &(PgSfxWhooshDesc){.dur_s = 0.45f, .f_start_hz = 280, .f_end_hz = 950, .level = 0.9f});
    pg_buf_mix(buf, n, w.samples, w.len, 1);
    vx_glide(buf, n, 0, 0.32f, 120, 58, 0.5f, 0.01f);
    o[VX_SND_BOSS_DASH] = vx_finish(a, buf, n, 0.8f);
  }
}

// every one-shot in VX_SOUND_LIST: the engine's procedural voices where they fit, the rest synthesized here
hz_internal void vx_make_sounds(VxAudio *a, Allocator *alloc) {
  a->sounds = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), VX_SND_COUNT);
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  PgSfxCtx ctx = {.alloc = &tmp.allocator, .rng = &a->rng};
  HzHandleT(AudioSource) *o = a->sounds;
  u32 sr = PG_AUDIO_RATE;
  o[VX_SND_SWING] = vx_source_from(a, pg_sfx_whoosh(&ctx, &(PgSfxWhooshDesc){.dur_s = 0.2f, .f_start_hz = 350,
                                                                             .f_end_hz = 1900, .level = 0.7f}), 1);
  o[VX_SND_JUMP] = vx_source_from(a, pg_sfx_jump(&ctx, &(PgSfxJumpDesc){.effort01 = 0.5f}), 1);
  o[VX_SND_LAND] = vx_source_from(a, pg_sfx_land(&ctx, &(PgSfxLandDesc){.surface = PG_SFX_SURF_SOFT,
                                                                        .impact_speed01 = 0.6f}), 1);
  o[VX_SND_DOUBLE_JUMP] = vx_source_from(a, pg_sfx_whoosh(&ctx, &(PgSfxWhooshDesc){.dur_s = 0.32f, .f_start_hz = 180,
                                                                                   .f_end_hz = 760, .level = 0.9f}), 1);
  o[VX_SND_HOOK_FIRE] = vx_source_from(a, pg_sfx_whoosh(&ctx, &(PgSfxWhooshDesc){.dur_s = 0.16f, .f_start_hz = 900,
                                                                                 .f_end_hz = 3600, .level = 0.7f}), 1);
  o[VX_SND_HOOK_LATCH] = vx_source_from(a, pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_METAL,
                                                                                  .size01 = 0.2f, .energy01 = 0.7f}), 1);
  o[VX_SND_WINGS] = vx_source_from(a, pg_sfx_whoosh(&ctx, &(PgSfxWhooshDesc){.dur_s = 0.16f, .f_start_hz = 140,
                                                                             .f_end_hz = 520, .level = 0.8f}), 1);
  o[VX_SND_HURT] = vx_source_from(a, pg_sfx_ui(&ctx, &(PgSfxUiDesc){.kind = PG_SFX_UI_DAMAGE}), 1);
  o[VX_SND_CRAFT] = vx_source_from(a, pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_METAL,
                                                                             .size01 = 0.12f, .energy01 = 0.5f}), 1);
  o[VX_SND_DOOR_CLOSE] = vx_source_from(a, pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_WOOD,
                                                                                  .size01 = 0.6f, .energy01 = 1}), 1);
  {
    // death: a long falling cry over a low thud
    u32 n = sr;
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    vx_glide(buf, n, 0, 0.9f, 420, 85, 0.5f, 0.01f);
    vx_glide(buf, n, 0, 0.9f, 630, 128, 0.18f, 0.01f);
    PgAudioBuf thud = pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_SOFT, .size01 = 0.9f});
    pg_buf_mix(buf, n, thud.samples, thud.len, 0.8f);
    o[VX_SND_DEATH] = vx_make_source(a, buf, n);
  }
  {
    // drink: three gulps, each a bubble whose pitch jumps up
    u32 n = sr * 45 / 100;
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    for (u32 k = 0; k < 3; k++) vx_glide(buf, n, 0.13f * k, 0.09f, 240 + 30 * k, 620 + 40 * k, 0.5f, 0.004f);
    o[VX_SND_DRINK] = vx_make_source(a, buf, n);
  }
  {
    // shimmer (mirror, crystals): rising bell partials under a tremolo
    u32 n = sr * 8 / 10;
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    for (u32 k = 0; k < 5; k++)
      vx_glide(buf, n, 0.06f * k, 0.55f, 880 * (1 + k * 0.5f), 1320 * (1 + k * 0.5f), 0.16f, 0.02f);
    for (u32 i = 0; i < n; i++) buf[i] *= 0.75f + 0.25f * m_sinf(2 * PI * 14 * (f32)i / (f32)sr);
    o[VX_SND_SHIMMER] = vx_make_source(a, buf, n);
  }
  {
    // creaks (chest lid, door hinge): a low buzzy saw with a wavering pitch under a low-pass, then a wooden knock
    for (u32 v = 0; v < 2; v++) {
      f32 dur = v == 0 ? 0.42f : 0.3f;
      u32 n = (u32)((dur + 0.25f) * sr);
      f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
      PgOsc osc = {0};
      u32 len = (u32)(dur * sr);
      for (u32 i = 0; i < len; i++) {
        f32 t = (f32)i / (f32)sr, t01 = (f32)i / (f32)len;
        f32 f = (v == 0 ? 95.0f : 120.0f) * (1 + 0.35f * t01) * (1 + 0.06f * m_sinf(2 * PI * 9 * t));
        buf[i] = pg_osc_next(&osc, sr, f, PG_OSC_SAW) * m_minf(1, t / 0.03f) * (1 - t01) * 0.35f;
      }
      PgButter lp = {0};
      pg_butlp_set(&lp, sr, 1400);
      pg_butter_process(&lp, buf, n);
      PgAudioBuf knock = pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_WOOD, .size01 = 0.4f,
                                                                .energy01 = 0.6f});
      pg_buf_mix(buf, n, knock.samples, knock.len, 0.7f);
      o[v == 0 ? VX_SND_CHEST : VX_SND_DOOR_OPEN] = vx_make_source(a, buf, n);
    }
  }
  {
    // a tree coming down: a big wooden crack, then a low rumble of the crown landing
    u32 n = sr * 12 / 10;
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    PgAudioBuf crack = pg_sfx_impact(&ctx, &(PgSfxImpactDesc){.surface = PG_SFX_SURF_WOOD, .size01 = 1, .energy01 = 1});
    pg_buf_mix(buf, n, crack.samples, crack.len, 0.9f);
    f32 *rum = ALLOC_ARRAY(&tmp.allocator, f32, n);
    pg_noise_fill(rum, n, PG_NOISE_WHITE, &a->rng);
    PgButter lp = {0};
    pg_butlp_set(&lp, sr, 260);
    pg_butter_process(&lp, rum, n);
    for (u32 i = 0; i < n; i++) {
      f32 t = (f32)i / (f32)sr;
      buf[i] += rum[i] * (t < 0.25f ? 0 : m_minf(1, (t - 0.25f) / 0.05f) * m_expf(-(t - 0.25f) * 4)) * 1.6f;
    }
    o[VX_SND_TREE_FALL] = vx_make_source(a, buf, n);
  }
  {
    // the item pop: a sine chirp sweeping up an octave over a short decay, with a click of noise at its start
    TempAllocator tmp = tctx_temp_allocator_begin(NULL);
    u32 sr = PG_AUDIO_RATE, n = sr * 9 / 100;
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    f32 ph = 0;
    for (u32 i = 0; i < n; i++) {
      f32 t = (f32)i / (f32)n;
      ph += 2 * PI * vx_exp_ramp(620, 1350, m_minf(1, t * 2.5f)) / (f32)sr;
      f32 env = m_minf(1, t * 60) * m_expf(-t * 5.5f);
      f32 click = i < sr / 400 ? (random_f32(&a->rng) * 2 - 1) * 0.3f : 0;
      buf[i] = (m_sinf(ph) * 0.7f + m_sinf(ph * 2) * 0.15f) * env + click;
    }
    a->sounds[VX_SND_PICKUP] = vx_make_source(a, buf, n);
    tctx_temp_allocator_end(tmp);
  }
  vx_make_combat_sounds(a, &ctx, &tmp.allocator);
  tctx_temp_allocator_end(tmp);
}

// ---- ambience: one live stream mixing the beds, rendered ahead of the mixer a quarter second at a time ----

#define VX_AMB_LATENCY 0.3f
#define VX_AMB_ECHO_S 0.23f

hz_internal const char *const VX_AMB_NAMES[VX_AMB_COUNT] = {"day", "night", "cave", "underworld", "ocean"};

hz_internal void vx_amb_init(VxAudio *a, Allocator *alloc) {
  VxAmbience *m = &a->amb;
  u32 sr = PG_AUDIO_RATE;
  m->rng = random_create(0xA3B1E7u);
  pg_ambience_init(&m->day, sr, 0x0DA7u);
  m->day.bed_level = 0.2f;
  m->day.sparkle_density = 1.4f;
  pg_ambience_init(&m->night, sr, 0x4167u);
  m->night.bed_level = 0.1f;
  m->night.sparkle_density = 0.03f;
  for (u32 k = 0; k < VX_CRICKETS; k++) {
    VxCricket *c = &m->crickets[k];
    c->freq = random_f32_range(&m->rng, 4100, 5300);
    c->period = random_f32_range(&m->rng, 0.5f, 0.95f);
    c->t = random_f32_range(&m->rng, 0, c->period);
    c->pulses = 2 + random_u32(&m->rng) % 3;
  }
  pg_butlp_set(&m->drone_lp, sr, 140);
  pg_butlp_set(&m->rumble_lp, sr, 75);
  pg_butlp_set(&m->surf_lp, sr, 750);
  m->echo_len = (u32)(VX_AMB_ECHO_S * sr);
  m->echo = ALLOC_ARRAY(alloc, f32, m->echo_len);
  m->dominant = -1;
  m->stream = audio_source_stream(&a->sys, &(AudioStreamSourceDesc){.sample_rate = sr, .channels = 1,
                                                                    .mode = AUDIO_STREAM_LIVE,
                                                                    .max_latency_s = VX_AMB_LATENCY, .alloc = &a->alloc});
  m->target_frames = (u32)(VX_AMB_LATENCY * 0.75f * (f32)sr);
}

// a grain's next sample: a sine bending from f0 by bend over its first rise_s, fading over decay_s; 0 once spent
hz_internal f32 vx_amb_grain(VxAmbGrain *g, f32 bend, f32 rise_s, f32 decay_s, f32 inv) {
  if (!g->on) return 0;
  f32 f = g->f0 * (1 + bend * m_minf(1, g->age / rise_s));
  g->phase += 2 * PI * f * inv;
  if (g->phase > 2 * PI) g->phase -= 2 * PI;
  f32 env = m_minf(1, g->age / 0.002f) * m_expf(-g->age / decay_s);
  g->age += inv;
  if (g->age > decay_s * 8) g->on = false;
  return m_sinf(g->phase) * env;
}

// renders n frames, each layer's gain ramping from where the last block left it to its current value
hz_internal void vx_amb_render(VxAmbience *m, f32 *out, u32 n, Allocator *tmp) {
  u32 sr = PG_AUDIO_RATE;
  f32 inv = 1.0f / (f32)sr, step = 1.0f / (f32)n;
  // silent layers are skipped; their generators pick up where they stopped
  u32 on = 0;
  for (u32 l = 0; l < VX_AMB_COUNT; l++) on |= (u32)(m->rendered[l] > 1e-4f || m->gain[l] > 1e-4f) << l;
  f32 *day = on & (1u << VX_AMB_DAY) ? ALLOC_ARRAY(tmp, f32, n) : NULL;
  f32 *night = on & (1u << VX_AMB_NIGHT) ? ALLOC_ARRAY(tmp, f32, n) : NULL;
  if (day) pg_ambience_render(&m->day, day, n);
  if (night) pg_ambience_render(&m->night, night, n);
  for (u32 i = 0; i < n; i++) {
    f32 k = (f32)(i + 1) * step, t = (f32)m->t;
    f32 white = random_f32(&m->rng) * 2 - 1;
    f32 v = 0;
    if (day) v += day[i] * m_lerpf(m->rendered[VX_AMB_DAY], m->gain[VX_AMB_DAY], k);
    if (night) {
      // crickets: short pulses of a high carrier, a few to a chirp, each insect on its own clock
      f32 cr = 0;
      for (u32 c = 0; c < VX_CRICKETS; c++) {
        VxCricket *q = &m->crickets[c];
        q->t += inv;
        if (q->t >= q->period) q->t -= q->period;
        q->phase += 2 * PI * q->freq * inv;
        if (q->phase > 2 * PI) q->phase -= 2 * PI;
        u32 pulse = (u32)(q->t / 0.034f);
        f32 u = (q->t - (f32)pulse * 0.034f) / 0.018f;
        if (pulse < q->pulses && u < 1) cr += m_sinf(q->phase) * m_sinf(PI * u) * (0.6f + 0.4f * (f32)(c & 1));
      }
      v += (night[i] + cr * 0.07f) * m_lerpf(m->rendered[VX_AMB_NIGHT], m->gain[VX_AMB_NIGHT], k);
    }
    if (on & (1u << VX_AMB_CAVE)) {
      // a low drone and the odd drip, plinking up in pitch, with its echo off the walls
      f32 drone = pg_butter_next(&m->drone_lp, white) * 1.3f * (0.75f + 0.25f * m_sinf(2 * PI * 0.05f * t)) +
                  m_sinf(2 * PI * 55 * t) * 0.025f;
      if (!m->drip.on && random_f32(&m->rng) < 0.45f * inv)
        m->drip = (VxAmbGrain){.f0 = random_f32_range(&m->rng, 900, 1700), .on = true};
      f32 drip = vx_amb_grain(&m->drip, 0.9f, 0.025f, 0.05f, inv) * 0.22f;
      f32 *e = &m->echo[m->echo_pos];
      f32 wet = *e;
      *e = drip + wet * 0.38f;
      m->echo_pos = (m->echo_pos + 1) % m->echo_len;
      v += (drone + drip + wet * 0.45f) * m_lerpf(m->rendered[VX_AMB_CAVE], m->gain[VX_AMB_CAVE], k);
    }
    if (on & (1u << VX_AMB_UNDERWORLD)) {
      // a deep rolling rumble, lava blorping, and crackle
      f32 rumble = pg_butter_next(&m->rumble_lp, white) * 1.9f * (0.7f + 0.3f * m_sinf(2 * PI * 0.11f * t));
      if (!m->blorp.on && random_f32(&m->rng) < 1.1f * inv)
        m->blorp = (VxAmbGrain){.f0 = random_f32_range(&m->rng, 90, 150), .on = true};
      f32 blorp = vx_amb_grain(&m->blorp, -0.5f, 0.12f, 0.07f, inv) * 0.22f;
      f32 crackle = random_f32(&m->rng) < 7.0f * inv ? white * 0.25f : 0;
      v += (rumble + blorp + crackle) * m_lerpf(m->rendered[VX_AMB_UNDERWORLD], m->gain[VX_AMB_UNDERWORLD], k);
    }
    if (on & (1u << VX_AMB_OCEAN)) {
      // surf: filtered noise swelling and breaking on a slow, wandering period
      f32 sw = 0.5f + 0.5f * m_sinf(2 * PI * 0.085f * t + 0.6f * m_sinf(2 * PI * 0.031f * t));
      f32 surf = pg_butter_next(&m->surf_lp, white) * (0.06f + 0.55f * sw * sw);
      v += surf * m_lerpf(m->rendered[VX_AMB_OCEAN], m->gain[VX_AMB_OCEAN], k);
    }
    out[i] = v;
    m->t += inv;
  }
  for (u32 l = 0; l < VX_AMB_COUNT; l++) m->rendered[l] = m->gain[l];
  pg_softclip_process(out, n);
}

// tops the stream up to its target backlog; the voice starts once the first block is in
hz_internal void vx_amb_update(VxAudio *a, f32 dt, const VxAmbienceMix *mix) {
  VxAmbience *m = &a->amb;
  if (!handle_is_valid(m->stream)) return;
  i32 dom = -1;
  for (u32 l = 0; l < VX_AMB_COUNT; l++) {
    m->want[l] = mix ? m_saturatef(mix->w[l]) : 0;
    m->gain[l] += (m->want[l] - m->gain[l]) * (1 - m_expf(-dt / 0.8f));
    if (m->want[l] > 0.3f && (dom < 0 || m->want[l] > m->want[dom])) dom = (i32)l;
  }
  if (dom != m->dominant && dom >= 0) {
    if (a->trace) log_info("audio: ambience %", fmt_cstr(VX_AMB_NAMES[dom]));
    m->dominant = dom;
  }
  u32 buffered = audio_source_stream_buffered_frames(&a->sys, m->stream);
  if (buffered < m->target_frames) {
    u32 frames = MIN(m->target_frames - buffered, audio_source_stream_free_space(&a->sys, m->stream) / (u32)sizeof(i16));
    if (frames) {
      tctx_temp_allocator_scope(ta, NULL) {
        f32 *f = ALLOC_ARRAY(&ta.allocator, f32, frames);
        i16 *pcm = ALLOC_ARRAY(&ta.allocator, i16, frames);
        vx_amb_render(m, f, frames, &ta.allocator);
        pg_buf_to_pcm16(f, frames, pcm);
        audio_source_stream_write(&a->sys, m->stream, (const u8 *)pcm, frames * (u32)sizeof(i16));
      }
    }
  }
  if (!handle_is_valid(m->voice))
    m->voice = audio_play(&a->sys, m->stream, &(AudioPlayDesc){.bus = AUDIO_BUS_SFX, .volume = 0.55f, .protected_voice = true});
}

#undef VX_AMB_LATENCY
#undef VX_AMB_ECHO_S

void vx_audio_init(VxAudio *a, Allocator *alloc) {
  memzero_struct(a);
  a->alloc = *alloc;
  a->rng = random_create(0x5eed5eedu);
  // every source made below: break, place and step per material, the one-shots, thunder, splash, rain, ambience and music
  u32 sources = 3 * VX_MAT_COUNT + VX_SND_COUNT + VX_THUNDER_VARIANTS + 4;
  a->sys = audio_system_init(&a->alloc, &(AudioSystemDesc){.source_pool = sources + 8});
  a->brk = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), VX_MAT_COUNT);
  a->place = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), VX_MAT_COUNT);
  a->step = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), VX_MAT_COUNT);
  for (u32 m = 0; m < VX_MAT_COUNT; m++) {
    a->brk[m] = vx_make_sfx(a, VX_VOICES[m], VX_SFX_BREAK);
    a->place[m] = vx_make_sfx(a, VX_VOICES[m], VX_SFX_PLACE);
    a->step[m] = vx_make_sfx(a, VX_VOICES[m], VX_SFX_STEP);
  }
  VxVoice water = VX_VOICES[VX_MAT_WATER];
  {
    TempAllocator tmp = tctx_temp_allocator_begin(NULL);
    u32 n = (u32)((water.dur + 0.1f) * PG_AUDIO_RATE);
    f32 *buf = ALLOC_ARRAY(&tmp.allocator, f32, n);
    vx_burst(buf, n, water, 0.85f, 1.2f, &a->rng);
    a->splash = vx_make_source(a, buf, n);
    tctx_temp_allocator_end(tmp);
  }
  vx_make_sounds(a, alloc);
  a->thunder = ALLOC_ARRAY(alloc, HzHandleT(AudioSource), VX_THUNDER_VARIANTS);
  for (u32 i = 0; i < VX_THUNDER_VARIANTS; i++) a->thunder[i] = vx_make_thunder(a, 3.0f + i * 1.25f);
  a->rain = vx_make_rain(a);
  a->pending = ALLOC_ARRAY(alloc, VxPendingThunder, VX_MAX_PENDING_THUNDER);
  a->rain_voice = audio_play(&a->sys, a->rain,
                             &(AudioPlayDesc){.bus = AUDIO_BUS_SFX, .volume = 1e-4f, .loop = true, .protected_voice = true});
  vx_amb_init(a, alloc);
  vx_music_init(&a->music, &a->sys, alloc);
}

hz_internal const char *const VX_MAT_NAMES[VX_MAT_COUNT] = {
    [VX_MAT_STONE] = "stone", [VX_MAT_DIRT] = "dirt", [VX_MAT_GRASS] = "grass", [VX_MAT_WOOD] = "wood",
    [VX_MAT_SAND] = "sand",   [VX_MAT_GLASS] = "glass", [VX_MAT_PLANT] = "plant", [VX_MAT_WATER] = "water",
    [VX_MAT_WOOL] = "wool",   [VX_MAT_SNOW] = "snow",
};

hz_internal const char *const VX_SOUND_NAMES[VX_SND_COUNT] = {
#define VX_X(id, name) name,
    VX_SOUND_LIST(VX_X)
#undef VX_X
};

hz_internal void vx_play(VxAudio *a, HzHandleT(AudioSource) src, f32 volume, f32 pitch, const char *what,
                         const char *mat) {
  audio_play(&a->sys, src, &(AudioPlayDesc){.bus = AUDIO_BUS_SFX, .volume = volume, .pitch = pitch});
  if (a->trace) log_info("audio: %%%", fmt_cstr(what), fmt_cstr(mat ? " " : ""), fmt_cstr(mat ? mat : ""));
}

force_inline f32 vx_arand(VxAudio *a, f32 lo, f32 hi) { return lo + random_f32(&a->rng) * (hi - lo); }

void vx_audio_break(VxAudio *a, u8 id) {
  VxMaterial m = vx_material_of(id);
  vx_play(a, a->brk[m], 1.0f, vx_arand(a, 0.85f, 1.15f), "break", VX_MAT_NAMES[m]);
}
void vx_audio_place(VxAudio *a, u8 id) {
  VxMaterial m = vx_material_of(id);
  vx_play(a, a->place[m], 1.0f, vx_arand(a, 0.875f, 1.125f), "place", VX_MAT_NAMES[m]);
}
void vx_audio_step(VxAudio *a, u8 id) {
  VxMaterial m = vx_material_of(id);
  vx_play(a, a->step[m], 1.0f, vx_arand(a, 0.857f, 1.143f), "step", VX_MAT_NAMES[m]);
}
void vx_audio_dig(VxAudio *a, u8 id) {
  VxMaterial m = vx_material_of(id);
  vx_play(a, a->brk[m], 0.45f, vx_arand(a, 1.2f, 1.4f), "dig", VX_MAT_NAMES[m]);
}
void vx_audio_splash(VxAudio *a) { vx_play(a, a->splash, 1.0f, vx_arand(a, 0.82f, 1.18f), "splash", NULL); }
void vx_audio_pickup(VxAudio *a) { vx_audio_play(a, VX_SND_PICKUP); }

void vx_audio_play(VxAudio *a, VxSound s) {
  f32 vol = s == VX_SND_PICKUP ? 0.55f : s == VX_SND_SWING || s == VX_SND_WINGS ? 0.5f : 0.8f;
  vx_play(a, a->sounds[s], vol, vx_arand(a, 0.94f, 1.06f), VX_SOUND_NAMES[s], NULL);
}

void vx_audio_play_vol(VxAudio *a, VxSound s, f32 volume) {
  if (volume < 0.02f) return;
  vx_play(a, a->sounds[s], volume, vx_arand(a, 0.9f, 1.1f), VX_SOUND_NAMES[s], NULL);
}

void vx_audio_thunder(VxAudio *a, f32 delay, f32 strength) {
  if (a->pending_count == VX_MAX_PENDING_THUNDER) return;
  u32 variant = random_u32(&a->rng) % VX_THUNDER_VARIANTS;
  a->pending[a->pending_count++] = (VxPendingThunder){.delay = delay, .strength = strength, .variant = variant};
}

void vx_audio_update(VxAudio *a, f32 dt, f32 volume, f32 rain, f32 outdoors, const VxAmbienceMix *mix, i32 music) {
  audio_bus_set_volume(&a->sys, AUDIO_BUS_MASTER, volume);
  for (u32 i = 0; i < a->pending_count;) {
    VxPendingThunder *p = &a->pending[i];
    p->delay -= dt;
    if (p->delay <= 0) {
      vx_play(a, a->thunder[p->variant], p->strength, 1.0f, "thunder", NULL);
      *p = a->pending[--a->pending_count];
    } else {
      i++;
    }
  }
  // rain ambience follows the weather with a 0.5 s time constant
  f32 target = rain * (0.12f + 0.18f * outdoors);
  a->rain_gain += (target - a->rain_gain) * (1 - m_expf(-dt / 0.5f));
  audio_voice_set_volume(&a->sys, a->rain_voice, m_maxf(a->rain_gain, 1e-5f));
  vx_amb_update(a, dt, mix);
  vx_music_update(&a->music, &a->sys, a->trace, music);
  audio_system_update(&a->sys, dt);
}

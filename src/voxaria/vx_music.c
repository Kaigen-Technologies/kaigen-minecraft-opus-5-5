#include "voxaria/vx_game.h"

// music: a pattern tracker driving a small synth band (lead, bass, pad, arp, drums) into one live stream

#define VX_MUSIC_LATENCY 0.3f
#define VX_MUSIC_FADE_S 1.6f
#define VX_MUSIC_ECHO_MAX_S 0.6f

typedef enum {
  VX_SCALE_MAJOR,
  VX_SCALE_MINOR,
  VX_SCALE_PHRYGIAN,
} VxScale;

typedef enum {
  VX_LEAD_SQUARE,
  VX_LEAD_TRI,
  VX_LEAD_SAW,
  VX_LEAD_BELL,
  VX_LEAD_HORN,
} VxLeadWave;

// one loop: a chord per bar and 16 steps per bar; pattern chars are listed with each field
typedef struct {
  const char *name;
  f32 bpm;
  i32 root; // midi note of the bass tonic
  u8 scale, lead_wave, bass_wave;
  i32 lead_base; // semitones above root of lead index 0
  f32 lead_gain, lead_cut, bass_gain, bass_cut, pad_gain, arp_gain, drum_gain, echo;
  const char *chords; // a scale degree per bar, '1'..'7'
  const char *lead;   // per step: '0'-'9','a'-'e' scale index up from lead_base, '-' hold, '.' rest
  const char *bass;   // one bar: 'x' chord root, 'o' its octave, '3' third, '5' fifth, '-' hold, '.' rest
  const char *arp;    // one bar: '0'-'3' chord tone (root, third, fifth, octave), '.' rest; NULL for none
  const char *kick, *snare, *hat; // one bar each: 'x' hit; NULL for none
} VxSong;

hz_internal const i32 VX_SCALES[3][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 1, 3, 5, 7, 8, 10}};

hz_internal const VxSong VX_SONGS[VX_SONG_COUNT] = {
    [VX_SONG_TITLE] = {"title", 84, 48, VX_SCALE_MAJOR, VX_LEAD_HORN, PG_OSC_TRI, 12, 0.30f, 2400, 0.32f, 500, 0.16f, 0.10f, 0.5f, 0.3f,
                       "15641545",
                       "2---4---7-------"
                       "6---8---4-------"
                       "7---9---5-------"
                       "5---3---0---1-2-"
                       "4-------7---9---"
                       "8-------6---4---"
                       "5---7---a---9---"
                       "8-------------..",
                       "x-------5-------", "0...1...2...3...", "x...............", NULL, NULL},
    [VX_SONG_DAY] = {"day", 120, 50, VX_SCALE_MAJOR, VX_LEAD_SQUARE, PG_OSC_TRI, 12, 0.22f, 3200, 0.34f, 700, 0.07f, 0.0f, 0.55f, 0.22f,
                     "14516425",
                     "2-4-7---6-7-9---"
                     "8-7-5---3-5-7---"
                     "6-8-b---a-9-8---"
                     "9---7---4-------"
                     "5-7-9---8-9-7---"
                     "a-9-7---5-7-a---"
                     "8-a-8-7-5-3-1---"
                     "4---6---8---6---",
                     "x..xo..x5..xo...", NULL, "x...x...x...x...", "....x.......x...", "..x...x...x...x."},
    [VX_SONG_NIGHT] = {"night", 92, 45, VX_SCALE_MINOR, VX_LEAD_TRI, PG_OSC_TRI, 24, 0.26f, 2200, 0.30f, 450, 0.12f, 0.08f, 0.35f, 0.35f,
                       "16371645",
                       "4-------2---4-3-"
                       "2-------0---2-5-"
                       "4-------6---4-2-"
                       "1-------3---1---"
                       "0---2---4---7---"
                       "5---4---2---0---"
                       "3---5---7---5---"
                       "4-------1-------",
                       "x-------5-------", "0.1.2.3.2.1.0.1.", "x.......x.......", NULL, "....x.......x..."},
    [VX_SONG_UNDERGROUND] = {"underground", 104, 38, VX_SCALE_MINOR, VX_LEAD_BELL, PG_OSC_SAW, 24, 0.30f, 4000, 0.26f, 380, 0.06f, 0.12f, 0.4f, 0.38f,
                             "11671455",
                             "4.......7...4..."
                             "5...4...2...0..."
                             "5.......7...9..."
                             "6...8...6...3..."
                             "4.......2...4..."
                             "3...5...7...5..."
                             "4...6...8...6..."
                             "4...............",
                             "x.x.x.x.x.x.x.x.", "0123012301230123", "x.......x.......", NULL, "..x...x...x...x."},
    [VX_SONG_UNDERWORLD] = {"underworld", 96, 28, VX_SCALE_PHRYGIAN, VX_LEAD_SAW, PG_OSC_SAW, 36, 0.20f, 1800, 0.40f, 320, 0.10f, 0.0f, 0.75f, 0.2f,
                            "11211671",
                            "0---1---0-------"
                            "0---3---2---1---"
                            "1-------5---3---"
                            "0---------------"
                            "4---5---4---2---"
                            "5---7---5---4---"
                            "6---8---6---5---"
                            "4---------------",
                            "xxx.xx.xx.x.xx.x", NULL, "x..x..x.x..x..x.", "....x.......x..x", NULL},
    [VX_SONG_BOSS] = {"boss", 150, 36, VX_SCALE_MINOR, VX_LEAD_SAW, PG_OSC_SAW, 24, 0.20f, 3000, 0.36f, 600, 0.08f, 0.0f, 0.7f, 0.18f,
                      "16711465",
                      "7.7.6.7.4...2.4."
                      "5.5.4.5.9...7.5."
                      "6.6.5.6.a...8.6."
                      "7-------4---7---"
                      "b.a.9.7.9.a.b..."
                      "a.9.7.5.7.9.a..."
                      "9.7.5.4.5.7.9..."
                      "8-------b-------",
                      "xxoxxxoxxxoxxxox", NULL, "x...x...x...x..x", "....x.......x...", "xxxxxxxxxxxxxxxx"},
};

hz_internal f32 vx_mu_hz(i32 midi) { return 440.0f * m_powf(2.0f, (f32)(midi - 69) / 12.0f); }

// the midi note of a scale index above base, octaves included
hz_internal i32 vx_mu_note(const VxSong *s, i32 base, i32 idx) {
  i32 oct = idx >= 0 ? idx / 7 : -((-idx + 6) / 7);
  return base + VX_SCALES[s->scale][idx - oct * 7] + 12 * oct;
}

hz_internal i32 vx_mu_index(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'e' ? c - 'a' + 10 : -1; }

hz_internal void vx_mu_on(VxMusicVoice *v, f32 hz) {
  b32 silent = !v->gate && v->rel_from * m_expf(-v->rel_t / 0.1f) < 0.01f;
  v->target = hz;
  if (silent || v->freq <= 0) v->freq = hz;
  v->gate = true;
  v->t = 0;
}

hz_internal void vx_mu_off(VxMusicVoice *v) {
  if (!v->gate) return;
  v->gate = false;
  v->rel_t = 0;
}

// attack, exponential decay to sustain while held; exponential release from wherever it was
hz_internal f32 vx_mu_env(VxMusicVoice *v, f32 att, f32 dec, f32 sus, f32 rel, f32 inv) {
  f32 a;
  if (v->gate) {
    a = v->t < att ? v->t / att : sus + (1 - sus) * m_expf(-(v->t - att) / dec);
    v->rel_from = a;
  } else {
    a = v->rel_from * m_expf(-v->rel_t / rel);
    v->rel_t += inv;
  }
  v->t += inv;
  return a;
}

hz_internal void vx_mu_start(VxMusicPlayer *p, i32 song) {
  const VxSong *s = &VX_SONGS[song];
  u32 sr = PG_AUDIO_RATE;
  f32 *echo = p->echo;
  u32 cap = (u32)(VX_MUSIC_ECHO_MAX_S * (f32)sr);
  Random rng = p->rng;
  memzero_struct(p);
  p->echo = echo;
  p->rng = rng;
  mem_zero(echo, cap * sizeof(f32));
  p->song = song;
  p->echo_len = MIN(cap, (u32)(3.0f * (f32)sr * 15.0f / s->bpm));
  pg_butlp_set(&p->lead_lp, sr, s->lead_cut);
  pg_butlp_set(&p->bass_lp, sr, s->bass_cut);
  pg_butlp_set(&p->pad_lp, sr, 1100);
  pg_butlp_set(&p->arp_lp, sr, 2600);
  pg_butbp_set(&p->snare_bp, sr, 1800, 2400);
  pg_buthp_set(&p->hat_hp, sr, 7500);
  p->kick_t = p->snare_t = p->hat_t = 10;
}

hz_internal void vx_mu_step(VxMusicPlayer *p, const VxSong *s) {
  u32 bars = cstr_len(s->chords), k = p->step % 16, bar = (p->step / 16) % bars;
  i32 deg = s->chords[bar] - '1';
  // the pad holds the bar's triad, gliding between chords
  if (k == 0)
    for (u32 v = 0; v < 3; v++) {
      f32 hz = vx_mu_hz(vx_mu_note(s, s->root + 12, deg + 2 * (i32)v));
      p->pad[v].target = hz;
      if (p->pad[v].freq <= 0) p->pad[v].freq = hz;
    }
  char c = s->lead[p->step % cstr_len(s->lead)];
  if (c == '.') vx_mu_off(&p->lead);
  else if (c != '-') vx_mu_on(&p->lead, vx_mu_hz(vx_mu_note(s, s->root + s->lead_base, vx_mu_index(c))));
  char b = s->bass[k];
  if (b == '.') vx_mu_off(&p->bass);
  else if (b != '-') {
    i32 n = vx_mu_note(s, s->root, deg + (b == '3' ? 2 : b == '5' ? 4 : 0)) + (b == 'o' ? 12 : 0);
    vx_mu_on(&p->bass, vx_mu_hz(n));
  }
  if (s->arp && s->arp[k] >= '0' && s->arp[k] <= '3') {
    i32 t = s->arp[k] - '0';
    p->arp.freq = vx_mu_hz(vx_mu_note(s, s->root + 24, deg + (t == 3 ? 7 : 2 * t)));
    p->arp.t = 0;
  }
  if (s->kick && s->kick[k] == 'x') p->kick_t = 0;
  if (s->snare && s->snare[k] == 'x') p->snare_t = 0;
  if (s->hat && s->hat[k] == 'x') p->hat_t = 0;
  p->step++;
}

hz_internal f32 vx_mu_sample(VxMusicPlayer *p, const VxSong *s, u32 sr, f32 inv) {
  // lead, with a delayed vibrato on held notes
  VxMusicVoice *l = &p->lead;
  l->freq += (l->target - l->freq) * 0.004f;
  f32 f = l->freq * (1 + 0.006f * m_sinf(2 * PI * 5.5f * l->t) * m_saturatef((l->t - 0.25f) / 0.3f)), lead;
  switch (s->lead_wave) {
  case VX_LEAD_BELL: {
    // a struck bell: partials at 1 and 2.76, ringing out whatever the gate
    f32 env = l->freq > 0 ? m_expf(-l->t / 0.9f) * m_minf(1, l->t / 0.003f) : 0;
    lead = (pg_osc_next(&l->o1, sr, f, PG_OSC_SINE) + 0.35f * pg_osc_next(&l->o2, sr, f * 2.76f, PG_OSC_SINE) * m_expf(-l->t / 0.25f)) * env;
    l->t += inv;
  } break;
  case VX_LEAD_TRI:
    lead = (pg_osc_next(&l->o1, sr, f, PG_OSC_TRI) + 0.2f * pg_osc_next(&l->o2, sr, f * 2, PG_OSC_SQUARE)) * vx_mu_env(l, 0.01f, 0.3f, 0.7f, 0.2f, inv);
    break;
  case VX_LEAD_SAW:
    lead = 0.6f * (pg_osc_next(&l->o1, sr, f, PG_OSC_SAW) + pg_osc_next(&l->o2, sr, f * 1.006f, PG_OSC_SAW)) * vx_mu_env(l, 0.01f, 0.15f, 0.6f, 0.1f, inv);
    break;
  case VX_LEAD_HORN:
    lead = (pg_osc_next(&l->o1, sr, f, PG_OSC_SAW) + 0.5f * pg_osc_next(&l->o2, sr, f * 0.998f, PG_OSC_TRI)) * vx_mu_env(l, 0.07f, 0.4f, 0.8f, 0.25f, inv);
    break;
  default:
    lead = (0.6f * pg_osc_next(&l->o1, sr, f, PG_OSC_SQUARE) + 0.4f * pg_osc_next(&l->o2, sr, f * 1.003f, PG_OSC_SAW)) *
           vx_mu_env(l, 0.008f, 0.2f, 0.55f, 0.12f, inv);
    break;
  }
  lead = pg_butter_next(&p->lead_lp, lead) * s->lead_gain;
  VxMusicVoice *b = &p->bass;
  b->freq += (b->target - b->freq) * 0.01f;
  f32 bass = pg_osc_next(&b->o1, sr, b->freq, (PgOscShape)s->bass_wave) * vx_mu_env(b, 0.005f, 0.12f, 0.7f, 0.05f, inv);
  bass = pg_butter_next(&p->bass_lp, bass) * s->bass_gain;
  f32 pad = 0;
  for (u32 v = 0; v < 3; v++) {
    VxMusicVoice *q = &p->pad[v];
    q->freq += (q->target - q->freq) * 0.0008f;
    if (q->freq > 0) pad += pg_osc_next(&q->o1, sr, q->freq, PG_OSC_SAW) + 0.5f * pg_osc_next(&q->o2, sr, q->freq * 1.005f, PG_OSC_SAW);
  }
  pad = pg_butter_next(&p->pad_lp, pad * 0.33f) * s->pad_gain;
  VxMusicVoice *a = &p->arp;
  f32 arp = a->freq > 0 ? pg_osc_next(&a->o1, sr, a->freq, PG_OSC_SQUARE) * m_expf(-a->t / 0.16f) : 0;
  a->t += inv;
  arp = pg_butter_next(&p->arp_lp, arp) * s->arp_gain;
  // drums: a pitch-dropping sine kick, a noisy snare with a tone, a short bright hat
  f32 drums = 0;
  if (p->kick_t < 0.5f) {
    drums += pg_osc_next(&p->kick_osc, sr, 45 + 85 * m_expf(-p->kick_t / 0.03f), PG_OSC_SINE) * m_expf(-p->kick_t / 0.18f);
    p->kick_t += inv;
  }
  f32 noise = random_f32(&p->rng) * 2 - 1;
  if (p->snare_t < 0.4f) {
    drums += (1.6f * pg_butter_next(&p->snare_bp, noise) * m_expf(-p->snare_t / 0.11f) +
              0.4f * pg_osc_next(&p->snare_osc, sr, 185, PG_OSC_TRI) * m_expf(-p->snare_t / 0.06f)) * 0.6f;
    p->snare_t += inv;
  }
  if (p->hat_t < 0.2f) {
    drums += pg_butter_next(&p->hat_hp, noise) * m_expf(-p->hat_t / 0.035f) * 0.35f;
    p->hat_t += inv;
  }
  drums *= s->drum_gain;
  // the lead and arp ring on in a feedback echo
  f32 *e = &p->echo[p->echo_pos];
  f32 wet = *e;
  *e = (lead + arp) * s->echo + wet * 0.35f;
  p->echo_pos = (p->echo_pos + 1) % MAX(p->echo_len, 1u);
  return lead + bass + pad + arp + drums + wet;
}

hz_internal void vx_mu_render(VxMusic *m, f32 *out, u32 n) {
  u32 sr = PG_AUDIO_RATE;
  f32 inv = 1.0f / (f32)sr, rate = inv / VX_MUSIC_FADE_S;
  for (u32 k = 0; k < 2; k++) {
    VxMusicPlayer *p = &m->pl[k];
    if (p->song < 0) continue;
    const VxSong *s = &VX_SONGS[p->song];
    f32 sps = (f32)sr * 15.0f / s->bpm;
    for (u32 i = 0; i < n; i++) {
      if (p->step_left <= 0) {
        vx_mu_step(p, s);
        p->step_left += sps;
      }
      p->step_left -= 1;
      f32 x = vx_mu_sample(p, s, sr, inv);
      p->gain += m_clampf(p->want - p->gain, -rate, rate);
      out[i] += x * p->gain;
    }
    if (p->want <= 0 && p->gain <= 0) p->song = -1;
  }
  // a gentle saturation keeps chord peaks from clipping
  for (u32 i = 0; i < n; i++) out[i] = out[i] / (1 + m_absf(out[i]) * 0.5f);
}

void vx_music_init(VxMusic *m, AudioSystem *sys, Allocator *alloc) {
  u32 sr = PG_AUDIO_RATE;
  for (u32 k = 0; k < 2; k++) {
    m->pl[k].song = -1;
    m->pl[k].echo = ALLOC_ARRAY(alloc, f32, (u32)(VX_MUSIC_ECHO_MAX_S * (f32)sr));
    m->pl[k].rng = random_create(0x5016u + k);
  }
  for (u32 i = 0; i < VX_SONG_COUNT; i++)
    if (cstr_len(VX_SONGS[i].lead) != 16 * cstr_len(VX_SONGS[i].chords) || cstr_len(VX_SONGS[i].bass) != 16)
      log_error("music: song % has a pattern of the wrong length", fmt_cstr(VX_SONGS[i].name));
  m->stream = audio_source_stream(sys, &(AudioStreamSourceDesc){.sample_rate = sr, .channels = 1, .mode = AUDIO_STREAM_LIVE,
                                                                .max_latency_s = VX_MUSIC_LATENCY, .alloc = alloc});
  m->target_frames = (u32)(VX_MUSIC_LATENCY * 0.75f * (f32)sr);
}

void vx_music_update(VxMusic *m, AudioSystem *sys, b32 trace, i32 want) {
  if (!handle_is_valid(m->stream)) return;
  VxMusicPlayer *cur = &m->pl[m->cur], *other = &m->pl[1 - m->cur];
  if (want != cur->song) {
    // a song still fading out comes back where it was; any other starts from its top in the free player
    if (want >= 0 && other->song == want) {
      m->cur = 1 - m->cur;
    } else if (want >= 0) {
      if (other->song >= 0 && other->gain > cur->gain) m->cur = 1 - m->cur;
      VxMusicPlayer *fresh = &m->pl[1 - m->cur];
      vx_mu_start(fresh, want);
      m->cur = 1 - m->cur;
    }
    if (trace) log_info("audio: music %", fmt_cstr(want >= 0 ? VX_SONGS[want].name : "off"));
    cur = &m->pl[m->cur];
    other = &m->pl[1 - m->cur];
    if (want < 0) cur->want = 0;
  }
  if (want >= 0) cur->want = 1;
  other->want = 0;
  u32 buffered = audio_source_stream_buffered_frames(sys, m->stream);
  if (buffered < m->target_frames) {
    u32 frames = MIN(m->target_frames - buffered, audio_source_stream_free_space(sys, m->stream) / (u32)sizeof(i16));
    if (frames) {
      tctx_temp_allocator_scope(ta, NULL) {
        f32 *f = ALLOC_ARRAY(&ta.allocator, f32, frames);
        i16 *pcm = ALLOC_ARRAY(&ta.allocator, i16, frames);
        vx_mu_render(m, f, frames);
        pg_buf_to_pcm16(f, frames, pcm);
        audio_source_stream_write(sys, m->stream, (const u8 *)pcm, frames * (u32)sizeof(i16));
      }
    }
  }
  if (!handle_is_valid(m->voice))
    m->voice = audio_play(sys, m->stream, &(AudioPlayDesc){.bus = AUDIO_BUS_MUSIC, .volume = 0.38f, .protected_voice = true});
}

#undef VX_MUSIC_LATENCY
#undef VX_MUSIC_FADE_S
#undef VX_MUSIC_ECHO_MAX_S

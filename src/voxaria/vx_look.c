#include "voxaria/vx_render.h"

// art direction: sky and light keyed by time of day (0 sunrise, 0.25 noon), fog and grade keyed by biome

typedef struct {
  f32 t;
  u32 zenith, horizon, glow;
  f32 glow_k;
  u32 direct;
  f32 direct_k;
  u32 amb;
  f32 amb_k;
  u32 ground;
  f32 ground_k;
  u32 shade;
} VxSkyKey;

#define VX_SKY_KEYS 11
hz_internal const VxSkyKey VX_SKY_PALETTE[VX_SKY_KEYS] = {
    {0.00f, 0x5A6FB8, 0xFFB08A, 0xFF7A3A, 0.9f, 0xFFB27A, 1.04f, 0xB0A0C0, 0.78f, 0x6A4A44, 0.44f, 0xE6DCFF},
    {0.06f, 0x2858C8, 0xC6DCF6, 0xFFC080, 0.3f, 0xFFE6C8, 1.43f, 0xB4C0DC, 0.98f, 0x6E6250, 0.50f, 0xDCE2FF},
    {0.25f, 0x1648C8, 0xB4D8FF, 0xFFF0D0, 0.12f, 0xFFF6E8, 1.65f, 0xB8C6E0, 1.08f, 0x726A52, 0.53f, 0xD8E0FF},
    {0.40f, 0x2050C4, 0xCADCF2, 0xFFC890, 0.35f, 0xFFE2BE, 1.43f, 0xBCC0D8, 0.98f, 0x70604C, 0.50f, 0xE0DEFF},
    {0.44f, 0x1A3C94, 0x8C9CC0, 0xFFC890, 0.25f, 0xFFE2BE, 0.42f, 0xBCC0D8, 0.3f, 0x70604C, 0.16f, 0xE0DEFF},
    {0.50f, 0x383870, 0xC07A5A, 0xFF6A2A, 0.7f, 0xFF9E62, 0.4f, 0xBC9CB8, 0.32f, 0x60403E, 0.18f, 0xEED6FF},
    {0.53f, 0x3E3450, 0xA8786E, 0xFF8A62, 0.35f, 0xFFC08A, 0.45f, 0x6A5C7E, 0.6f, 0x302428, 0.3f, 0xD8CCF0},
    {0.56f, 0x1A2250, 0x5A4A7A, 0xA0505A, 0.45f, 0x8AA0E8, 0.19f, 0x3E4A86, 0.55f, 0x20202E, 0.25f, 0xC4CCFF},
    {0.75f, 0x060C26, 0x182A50, 0x000000, 0.0f, 0x9AB4FF, 0.23f, 0x2A3870, 0.55f, 0x10141E, 0.25f, 0xBCC6FF},
    {0.94f, 0x18205A, 0x5A5078, 0xC07060, 0.35f, 0x9AB4FF, 0.17f, 0x3A4280, 0.55f, 0x20202E, 0.25f, 0xC4CCFF},
    {1.00f, 0x5A6FB8, 0xFFB08A, 0xFF7A3A, 0.9f, 0xFFB27A, 1.04f, 0xB0A0C0, 0.78f, 0x6A4A44, 0.44f, 0xE6DCFF},
};

typedef struct {
  u32 fog;
  f32 fog_density;
  u32 horizon_tint; // the horizon leans toward this colour
  f32 tint_k;
  u32 under; // underground fog colour
  u32 lift, gain;
  f32 gamma, saturation, hue;
} VxBiomeLook;

hz_internal const VxBiomeLook VX_BIOME_LOOK[VX_BIOME_COUNT] = {
    [VX_BIOME_FOREST] = {0xB0CCF2, 0.0021f, 0xB4D8FF, 0.0f, 0x0A0C14, 0x020306, 0xFFFFFA, 1.0f, 1.12f, 0.0f},
    [VX_BIOME_DESERT] = {0xC4D6F0, 0.0019f, 0xC8DCF6, 0.1f, 0x140E08, 0x030303, 0xFFFAF2, 1.0f, 1.0f, 0.0f},
    [VX_BIOME_SNOW] = {0xE6EEFF, 0.0028f, 0xEAF2FF, 0.5f, 0x0A1020, 0x000208, 0xF4FAFF, 1.0f, 0.95f, 0.0f},
    [VX_BIOME_JUNGLE] = {0xB4D6D0, 0.0016f, 0xC8EAB4, 0.2f, 0x081008, 0x000500, 0xF6FFEA, 0.98f, 1.2f, 0.0f},
    [VX_BIOME_CORRUPTION] = {0xA08CC0, 0.0022f, 0xC8B4E6, 0.5f, 0x100818, 0x050008, 0xF0E4FF, 1.0f, 0.88f, -0.05f},
    [VX_BIOME_OCEAN] = {0xB0DCFF, 0.0010f, 0xBEE6FF, 0.2f, 0x060C18, 0x000206, 0xFAFFFF, 1.0f, 1.12f, 0.0f},
    [VX_BIOME_MUSHROOM] = {0x5A80C8, 0.0020f, 0x6A8AD0, 0.0f, 0x0A1636, 0x00030C, 0xE8F2FF, 0.98f, 1.15f, 0.0f},
    [VX_BIOME_HALLOW] = {0xC0D6F4, 0.0019f, 0xD2E6FF, 0.1f, 0x0C0C18, 0x020306, 0xFCFAFF, 1.0f, 1.1f, 0.0f},
};

// the underworld's own look, blended in by depth
hz_internal const VxBiomeLook VX_UNDERWORLD_LOOK = {0x5A1206, 0.018f, 0xFF7A3A, 0.0f, 0x4A1408, 0x0A0204, 0xFFE4E4,
                                                     1.0f, 1.1f, 0.0f};

force_inline v3 vx_lin(u32 hex) {
  v3 c = {(f32)((hex >> 16) & 255) / 255.0f, (f32)((hex >> 8) & 255) / 255.0f, (f32)(hex & 255) / 255.0f};
  return (v3){m_powf(c.x, 2.2f), m_powf(c.y, 2.2f), m_powf(c.z, 2.2f)};
}

force_inline v3 vx_mix3(v3 a, v3 b, f32 t) { return v3_add(a, v3_scale(v3_sub(b, a), t)); }

force_inline f32 vx_smooth01(f32 e0, f32 e1, f32 x) {
  f32 t = m_clampf((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}

VxLook vx_look_compute(const VxFrameInput *in, f32 sun_y) {
  VxLook l = {0};
  f32 t = m_fmodf(in->day_time, 1.0f);
  if (t < 0) t += 1.0f;
  u32 k = 0;
  while (k + 2 < VX_SKY_KEYS && VX_SKY_PALETTE[k + 1].t <= t) k++;
  const VxSkyKey *a = &VX_SKY_PALETTE[k], *b = &VX_SKY_PALETTE[k + 1];
  f32 f = vx_smooth01(0, 1, (t - a->t) / (b->t - a->t));
  l.zenith = vx_mix3(vx_lin(a->zenith), vx_lin(b->zenith), f);
  l.horizon = vx_mix3(vx_lin(a->horizon), vx_lin(b->horizon), f);
  l.glow = vx_mix3(v3_scale(vx_lin(a->glow), a->glow_k), v3_scale(vx_lin(b->glow), b->glow_k), f);
  l.direct = vx_mix3(v3_scale(vx_lin(a->direct), a->direct_k), v3_scale(vx_lin(b->direct), b->direct_k), f);
  l.amb_sky = vx_mix3(v3_scale(vx_lin(a->amb), a->amb_k), v3_scale(vx_lin(b->amb), b->amb_k), f);
  l.amb_ground = vx_mix3(v3_scale(vx_lin(a->ground), a->ground_k), v3_scale(vx_lin(b->ground), b->ground_k), f);
  l.shade = vx_mix3(vx_lin(a->shade), vx_lin(b->shade), f);
  l.night = vx_smooth01(0.02f, -0.15f, sun_y);
  l.stars = vx_smooth01(-0.02f, -0.2f, sun_y);
  // how bright the sky is now relative to noon: biome fog follows it
  f32 day_k = m_clampf(v3_length(l.amb_sky) / v3_length(v3_scale(vx_lin(0xB8C6E0), 1.08f)), 0.05f, 1.2f);

  // biome blend: fog, horizon tint and grade
  f32 wsum = 0;
  v3 fog = {0}, tint = {0}, under = {0}, lift = {0}, gain = {0};
  f32 dens = 0, tint_k = 0, gamma = 0, sat = 0, hue = 0;
  for (u32 i = 0; i < VX_BIOME_COUNT; i++) {
    f32 w = in->biome_w[i];
    if (w <= 0) continue;
    const VxBiomeLook *bl = &VX_BIOME_LOOK[i];
    wsum += w;
    fog = v3_add(fog, v3_scale(vx_lin(bl->fog), w));
    tint = v3_add(tint, v3_scale(vx_lin(bl->horizon_tint), w * bl->tint_k));
    tint_k += bl->tint_k * w;
    under = v3_add(under, v3_scale(vx_lin(bl->under), w));
    lift = v3_add(lift, v3_scale(vx_lin(bl->lift), w));
    gain = v3_add(gain, v3_scale(vx_lin(bl->gain), w));
    dens += bl->fog_density * w;
    gamma += bl->gamma * w;
    sat += bl->saturation * w;
    hue += bl->hue * w;
  }
  if (wsum <= 0) {
    const VxBiomeLook *bl = &VX_BIOME_LOOK[VX_BIOME_FOREST];
    wsum = 1;
    fog = vx_lin(bl->fog);
    under = vx_lin(bl->under);
    lift = vx_lin(bl->lift);
    gain = vx_lin(bl->gain);
    dens = bl->fog_density;
    gamma = bl->gamma;
    sat = bl->saturation;
  }
  f32 iw = 1.0f / wsum;
  l.fog_color = v3_scale(fog, iw * day_k);
  if (tint_k > 0) l.horizon = vx_mix3(l.horizon, v3_scale(v3_scale(tint, 1.0f / tint_k), day_k), m_minf(1, tint_k * iw));
  l.under_color = v3_scale(under, iw);
  l.lift = v3_scale(lift, iw);
  l.gain = v3_scale(gain, iw);
  // the far terrain carries the whole island: the haze is aerial perspective, no longer the world's edge
  l.fog_density = dens * iw * 1.1f * (1 + in->rain * 1.6f);
  l.fog_falloff = 1.0f / 40.0f;
  l.gamma = gamma * iw;
  l.saturation = sat * iw;
  l.hue = hue * iw;
  l.under_density = 0.012f;
  l.aurora = in->biome_w[VX_BIOME_SNOW] * l.night;

  // the underworld takes over with depth: hot fog, a warm grade and shimmer
  f32 uw = m_clampf(in->underworld, 0, 1);
  if (uw > 0) {
    const VxBiomeLook *h = &VX_UNDERWORLD_LOOK;
    l.under_color = vx_mix3(l.under_color, vx_lin(h->under), uw);
    l.lift = vx_mix3(l.lift, vx_lin(h->lift), uw);
    l.gain = vx_mix3(l.gain, vx_lin(h->gain), uw);
    l.saturation = m_lerpf(l.saturation, h->saturation, uw);
    l.under_density = m_lerpf(l.under_density, h->fog_density, uw);
    l.heat = uw;
    l.under_glow = v3_scale(vx_lin(0xFFA888), 0.7f * uw);
  }
  // open lava nearby shimmers the air too
  l.heat = m_maxf(l.heat, m_clampf(in->lava_heat, 0, 1) * 0.7f);
  return l;
}

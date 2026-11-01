// art-directed sky: time-of-day gradient, pixel-art sun and phased moon, twinkling stars, falling stars, aurora

float sky_vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

// zenith-to-horizon gradient with a warm glow on the sun's side of the horizon
vec3 sky_gradient(vec3 dir) {
    float h = dir.y;
    // a thin pale band at the horizon, deep blue above it
    vec3 c = mix(F.sky_horizon.rgb, F.sky_zenith.rgb, pow(saturate(h), 0.28));
    float toward = saturate(dot(dir, F.sun_dir.xyz) * 0.5 + 0.5);
    c += F.sky_glow.rgb * pow(toward, 5.0) * exp(-abs(h) * 4.5);
    // below the horizon the haze darkens a little toward the ground
    return mix(c, F.sky_horizon.rgb * 0.8, smoothstep(0.0, -0.25, h));
}

// a body's tangent frame: pixel-art sprites are drawn on a grid in this plane
void sky_frame(vec3 body, out vec3 tx, out vec3 ty) {
    vec3 up = abs(body.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tx = normalize(cross(up, body));
    ty = cross(body, tx);
}

// 16-pixel sun with a pixel rim and eight slowly turning rays
vec3 sky_sun(vec3 dir) {
    vec3 s = F.sun_dir.xyz;
    if (dot(dir, s) < 0.9) return vec3(0.0);
    vec3 tx, ty;
    sky_frame(s, tx, ty);
    const float R = 0.045;
    vec2 uv = vec2(dot(dir, tx), dot(dir, ty)) / R;
    vec2 q = (floor(uv * 8.0) + 0.5) / 8.0;
    float r = length(q);
    vec3 col = vec3(0.0);
    if (r < 0.8) col = vec3(1.0, 0.97, 0.78) * 9.0;
    else if (r < 1.0) col = vec3(1.0, 0.82, 0.32) * 6.0;
    else if (r < 2.1) {
        float a = atan(q.y, q.x) / 6.2831853 * 8.0 + F.cam_pos.w * 0.02;
        float ray = abs(fract(a) - 0.5);
        float len = r < 1.5 ? 0.16 : 0.08;
        if (ray < len) col = vec3(1.0, 0.86, 0.4) * mix(3.5, 1.2, saturate((r - 1.0) / 1.1));
    }
    return col * smoothstep(-0.05, 0.02, s.y + 0.04);
}

// 12-pixel moon: the lit part follows the phase, darker pixel craters
vec3 sky_moon(vec3 dir) {
    vec3 m = F.moon_dir.xyz;
    if (dot(dir, m) < 0.9) return vec3(0.0);
    vec3 tx, ty;
    sky_frame(m, tx, ty);
    const float R = 0.045;
    vec2 uv = vec2(dot(dir, tx), dot(dir, ty)) / R;
    vec2 q = (floor(uv * 6.0) + 0.5) / 6.0;
    float r2 = dot(q, q);
    if (r2 > 1.0) return vec3(0.0);
    float z = sqrt(1.0 - r2);
    float ph = F.moon_dir.w * 6.2831853;
    float lit = dot(vec3(q, z), vec3(sin(ph), 0.0, cos(ph)));
    float crater = step(0.72, hash12(floor(uv * 6.0) + 7.0)) * 0.25;
    vec3 base = vec3(0.9, 0.93, 1.0) * (1.0 - crater);
    vec3 dark = vec3(0.06, 0.07, 0.12);
    return (lit > 0.0 ? base * 2.2 : dark) * smoothstep(-0.05, 0.03, m.y + 0.03);
}

// a moon low over the horizon: a soft halo and rays fanning up through the haze
vec3 sky_moon_glow(vec3 dir) {
    vec3 m = F.moon_dir.xyz;
    float low = smoothstep(-0.06, 0.03, m.y) * smoothstep(0.5, 0.15, m.y);
    if (low <= 0.0) return vec3(0.0);
    float ang = acos(clamp(dot(dir, m), -1.0, 1.0));
    vec3 tx, ty;
    sky_frame(m, tx, ty);
    float a = atan(dot(dir, ty), dot(dir, tx));
    float rays = pow(sky_vnoise(vec2(a * 7.0, 1.7)), 2.0) * (0.55 + 0.45 * sky_vnoise(vec2(a * 21.0, F.cam_pos.w * 0.04)));
    float fan = rays * smoothstep(-0.35, 0.5, sin(a)) * exp(-ang * 2.0) * 0.7;
    float halo = exp(-ang * 16.0) * 1.4 + exp(-ang * 4.5) * 0.22;
    // the horizon beneath it warms to peach
    float band = exp(-abs(dir.y) * 9.0) * exp(-ang * 1.6) * 0.55;
    return (vec3(1.0, 0.9, 0.88) * (halo + fan) * smoothstep(-0.03, 0.03, dir.y) + vec3(1.0, 0.62, 0.45) * band) * low;
}

// pixel stars on a rotating hashed grid; a few big ones get a cross
vec3 sky_stars(vec3 dir) {
    vec3 d = mat3(F.star_rot) * dir;
    vec3 col = vec3(0.0);
    for (int layer = 0; layer < 2; layer++) {
        float scale = layer == 0 ? 90.0 : 190.0;
        vec3 p = d * scale;
        vec3 cell = floor(p);
        vec3 f = fract(p) - 0.5;
        vec3 h = hash33(cell + float(layer) * 17.0);
        if (h.x < (layer == 0 ? 0.955 : 0.975)) continue;
        vec3 o = (hash33(cell * 1.7 + 3.0) - 0.5) * 0.4;
        vec3 e = abs(f - o);
        float sz = layer == 0 ? 0.11 : 0.08;
        float core = step(max(e.x, max(e.y, e.z)), sz);
        float cross_arm = layer == 0 && h.y > 0.8 ? step(min(e.x, min(e.y, e.z)), sz * 0.6) * step(max(e.x, max(e.y, e.z)), sz * 3.0) : 0.0;
        float tw = 0.6 + 0.4 * sin(F.cam_pos.w * (1.5 + h.z * 4.0) + h.y * 40.0);
        vec3 tint = mix(vec3(1.0, 0.86, 0.66), vec3(0.75, 0.86, 1.0), h.z);
        col += tint * (core + cross_arm * 0.5) * tw * (layer == 0 ? 2.2 : 1.1);
    }
    return col;
}

// the game's falling star: a bright head with a fading tail along its heading
vec3 sky_falling(vec3 dir) {
    float travel = F.star_a.w;
    if (travel < 0.0) return vec3(0.0);
    vec3 start = F.star_a.xyz;
    vec3 head = F.star_b.xyz;
    vec3 tip = normalize(start + head * travel * 0.45);
    vec3 tail = normalize(start + head * max(0.0, travel * 0.45 - 0.14));
    vec3 ab = tip - tail;
    float s = saturate(dot(dir - tail, ab) / max(dot(ab, ab), 1e-6));
    float d = length(dir - (tail + ab * s));
    float w = smoothstep(0.004, 0.0, d) * s * s * sin(travel * 3.14159);
    float glow = smoothstep(0.02, 0.0, length(dir - tip)) * sin(travel * 3.14159);
    return vec3(1.0, 0.92, 0.6) * (w * 7.0 + glow * 1.5);
}

// aurora curtains over the snow biome
vec3 sky_aurora(vec3 dir) {
    if (F.sky_glow.w <= 0.0 || dir.y < 0.05) return vec3(0.0);
    float az = atan(dir.z, dir.x);
    float t = F.cam_pos.w * 0.05;
    float band = sky_vnoise(vec2(az * 3.0 + t, t * 0.7)) * 0.6 + sky_vnoise(vec2(az * 9.0 - t * 2.0, 3.0)) * 0.4;
    float curtain = smoothstep(0.45, 0.8, band);
    float h = (dir.y - 0.12) / 0.5;
    float vert = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.3, h);
    vec3 col = mix(vec3(0.15, 1.0, 0.55), vec3(0.6, 0.3, 1.0), saturate(h * 1.3));
    float rays = 0.7 + 0.3 * sin(az * 140.0 + t * 20.0);
    return col * curtain * vert * rays * F.sky_glow.w * 1.4;
}

// everything in the sky except clouds
vec3 sky_color(vec3 dir) {
    vec3 c = sky_gradient(dir);
    float night = F.sky_zenith.w;
    float above = smoothstep(-0.02, 0.06, dir.y);
    if (F.sky_horizon.w > 0.0) {
        c += sky_stars(dir) * F.sky_horizon.w * above;
        c += sky_falling(dir) * night * above;
    }
    c += sky_aurora(dir) * night;
    c += sky_moon_glow(dir);
    c += sky_moon(dir);
    c += sky_sun(dir);
    // overcast: a grey, flatter sky while it rains
    c = mix(c, vec3(luminance(c)) * vec3(0.66, 0.7, 0.78), F.weather.x * 0.7);
    // lightning flash
    return c + F.extra.z * vec3(0.75, 0.8, 1.0) * (0.6 + 0.4 * saturate(dir.y + 0.3));
}

// haze colour for fog toward dir: the gradient at the horizon band
vec3 sky_haze(vec3 dir) {
    vec3 d = normalize(vec3(dir.x, clamp(dir.y, -0.05, 0.25), dir.z));
    return mix(sky_gradient(d), vec3(luminance(sky_gradient(d))) * vec3(0.66, 0.7, 0.78), F.weather.x * 0.7);
}

// ---- background ring: biome silhouettes on 4 cylinders round the island, nearest first ----

// periodic value noise over n cells round the circle (t in turns)
float ring_noise(float t, float n) {
    float x = t * n;
    float i = floor(x);
    float f = x - i;
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(vec2(mod(i, n), n));
    float b = hash12(vec2(mod(i + 1.0, n), n));
    return mix(a, b, f);
}

// one biome's skyline, 0..1, at t turns round layer l (0 near .. 3 far)
float ring_shape(int b, float t, float l) {
    float s = l * 7.31;
    float hills = ring_noise(t + s, 9.0) * 0.6 + ring_noise(t + s, 31.0) * 0.3 + ring_noise(t + s, 97.0) * 0.1;
    if (b == 0 || b == 6 || b == 7) {
        // rolling hills under a row of round tree crowns
        float cell = fract(t * 420.0 + s);
        float crown = sqrt(saturate(1.0 - pow(cell * 2.0 - 1.0, 2.0))) * step(0.3, ring_noise(t + s * 2.0, 140.0));
        return hills * 0.5 + crown * 0.16 * (1.0 - l * 0.25);
    }
    if (b == 1) {
        // dunes with sharp crests
        float d = 1.0 - abs(fract(t * 46.0 + ring_noise(t + s, 13.0) * 1.7) * 2.0 - 1.0);
        return hills * 0.25 + pow(d, 1.6) * 0.3;
    }
    if (b == 2) {
        // jagged peaks
        float r = 1.0 - abs(ring_noise(t + s, 23.0) * 2.0 - 1.0);
        float r2 = 1.0 - abs(ring_noise(t + s, 71.0) * 2.0 - 1.0);
        return pow(r, 2.2) * 0.85 + r2 * 0.15;
    }
    if (b == 3) {
        // a tall broken canopy
        float cell = fract(t * 300.0 + s);
        float crown = sqrt(saturate(1.0 - pow(cell * 2.0 - 1.0, 2.0)));
        return 0.12 + hills * 0.3 + crown * 0.18 * step(0.2, ring_noise(t + s * 3.0, 100.0));
    }
    if (b == 4) {
        // thorny spires
        float sp = pow(1.0 - abs(fract(t * 160.0 + s) * 2.0 - 1.0), 6.0) * step(0.45, ring_noise(t + s, 160.0));
        return hills * 0.4 + sp * 0.55;
    }
    // ocean: a low far shore
    return hills * 0.08;
}

vec3 ring_color(int b) {
    if (b == 1) return vec3(0.78, 0.6, 0.34);
    if (b == 2) return vec3(0.24, 0.3, 0.44);
    if (b == 3) return vec3(0.1, 0.28, 0.1);
    if (b == 4) return vec3(0.32, 0.2, 0.46);
    if (b == 5) return vec3(0.3, 0.46, 0.6);
    if (b == 7) return vec3(0.2, 0.55, 0.55);
    return vec3(0.16, 0.4, 0.16);
}

// the ring toward dir: rgb, a 1 where a skyline covers the sky; skylines are stepped to read as pixel art
vec4 sky_ring(vec3 dir) {
    if (dir.y > 0.45) return vec4(0.0);
    float hl = length(dir.xz);
    if (hl < 1e-3) return vec4(0.0);
    vec2 dh = dir.xz / hl;
    vec2 cp = F.cam_pos.xz;
    float w[8] = float[8](F.biome_a.x, F.biome_a.y, F.biome_a.z, F.biome_a.w, F.biome_b.x, F.biome_b.y, F.biome_b.z, F.biome_b.w);
    float wsum = max(1e-4, w[0] + w[1] + w[2] + w[3] + w[4] + w[5] + w[6] + w[7]);
    vec3 haze = sky_haze(dir);
    float lum = luminance(haze);
    // the far field and the open sea fill the nearer rings' distances: only the mountain range remains
    for (int l = 3; l < 4; l++) {
        float fl = float(l);
        float R = VX_RING_R;
        // horizontal distance to the cylinder of radius R round the world origin
        float b = dot(cp, dh);
        float t = -b + sqrt(max(0.0, b * b - dot(cp, cp) + R * R));
        vec2 p = cp + dh * t;
        float turn = atan(p.y, p.x) * (0.5 / 3.14159265) + 0.5;
        // 1600 steps round the circle, heights on the same angular grid
        float step_turn = 1.0 / 1600.0;
        float tq = (floor(turn / step_turn) + 0.5) * step_turn;
        float h = 0.0;
        vec3 col = vec3(0.0);
        float snow = 0.0;
        for (int k = 0; k < 8; k++) {
            if (w[k] < 0.01) continue;
            h += w[k] * ring_shape(k, tq, fl);
            col += w[k] * ring_color(k);
        }
        h /= wsum;
        col /= wsum;
        // the farthest ring is a blue-grey mountain range behind every biome
        if (l == 3) {
            float r = 1.0 - abs(ring_noise(tq + 3.7, 17.0) * 2.0 - 1.0);
            h = max(h * 0.6, pow(r, 1.8) * 0.8 + ring_noise(tq, 53.0) * 0.2);
            col = mix(col, vec3(0.22, 0.27, 0.4), 0.85);
        }
        float amp = R * (l == 3 ? 0.06 : 0.045 + 0.025 * fl);
        float top = 140.0 + R * 0.01 + h * amp;
        float hstep = R * step_turn * 6.2832;
        top = floor(top / hstep) * hstep;
        float y = F.cam_pos.y + dir.y / hl * t;
        if (y > top) continue;
        // a snowline: peaks above half the ring's height are white
        float cap = w[2] / wsum * step(140.0 + R * 0.01 + amp * 0.5, y);
        col = mix(col, vec3(0.9, 0.94, 1.0), cap);
        // aerial perspective: farther rings fade into the haze; lit by how bright the haze is
        vec3 lit = col * (lum * 1.2 + 0.015);
        float fade = 0.38;
        return vec4(mix(lit, haze, mix(fade, 1.0, F.weather.x * 0.5)), 1.0);
    }
    return vec4(0.0);
}

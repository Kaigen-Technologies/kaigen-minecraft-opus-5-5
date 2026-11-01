// voxel clouds: a dda through a drifting grid of cloud cells, toon lit; one texel per 2x2 block of the cloud target
#include "vx_common.glsl"
#include "vx_frame.glsl"

layout(binding = 1) uniform _CloudParams {
    vec4 low_res; // cloud target w, h
} P;

@vs
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vx_fullscreen(gl_VertexIndex, v_uv); }
@end

@fs
layout(set = 1, binding = 0) uniform texture2D weather_tex;
layout(set = 1, binding = 16) uniform sampler smp_linear;
#include "vx_sky.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

// cell size in blocks and the drift of the whole grid
#define VX_CELL vec3(6.0, 4.0, 6.0)
#define VX_CLOUD_LAYERS 8
#define VX_CLOUD_FAR 1500.0
// enough column steps to always reach VX_CLOUD_FAR, so the fade and not the step budget ends the clouds
#define VX_CLOUD_STEPS 520

vec2 wind() { return vec2(F.cloud.w * 4.0, F.cloud.w * 1.5); }

// stacked cells per column on a flat base: a puff's stepped dome where the weather map's coverage lets puffs form
ivec2 column_range(vec2 cell_xz) {
    vec2 uv = (cell_xz * VX_CELL.xz + VX_CELL.xz * 0.5) / 1400.0;
    vec4 w = textureLod(sampler2D(weather_tex, smp_linear), uv, 0.0);
    float gate = smoothstep(0.25, 0.6, w.r + (F.cloud.x - 0.5) * 0.9);
    float h = w.b * gate - (1.0 - F.cloud.x) * 0.25;
    int top = int(clamp(floor(h * float(VX_CLOUD_LAYERS) + 0.35), 0.0, float(VX_CLOUD_LAYERS)));
    return ivec2(0, top);
}

bool occupied(ivec3 c) {
    if (c.y < 0 || c.y >= VX_CLOUD_LAYERS) return false;
    ivec2 r = column_range(vec2(c.xz));
    return c.y >= r.x && c.y < r.y;
}

void main() {
    ivec2 ct = min(ivec2(gl_FragCoord.xy) * 2 + half_res_offset(), ivec2(P.low_res.xy) - 1);
    vec3 dir = view_ray((vec2(ct) + 0.5) / P.low_res.xy);
    frag_color = vec4(0.0, 0.0, 0.0, 1.0);
    if (F.cloud.x <= 0.0) return;
    float base = F.cloud.y;
    float top = base + VX_CELL.y * float(VX_CLOUD_LAYERS);
    float cy = F.cam_pos.y;
    // the slab the ray crosses
    float t0, t1;
    if (abs(dir.y) < 1e-4) {
        if (cy < base || cy > top) return;
        t0 = 0.0;
        t1 = VX_CLOUD_FAR;
    } else {
        float ta = (base - cy) / dir.y, tb = (top - cy) / dir.y;
        t0 = max(min(ta, tb), 0.0);
        t1 = min(max(ta, tb), VX_CLOUD_FAR);
    }
    if (t1 <= t0) return;
    // grid space: the camera's xz relative to the drifting grid, y from the cloud base
    vec3 ro = vec3(F.cam_pos.x - wind().x, cy - base, F.cam_pos.z - wind().y) / VX_CELL;
    vec3 rd = dir / VX_CELL;
    vec3 stp = sign(rd);
    // a 2d dda over columns: each column's cells are one contiguous span, so the ray meets it at a side or its top or belly
    vec2 p2 = ro.xz + rd.xz * t0;
    ivec2 cc = ivec2(floor(p2));
    vec2 inv = 1.0 / max(abs(rd.xz), vec2(1e-6));
    vec2 tmax = vec2(t0) + ((floor(p2) + max(stp.xz, vec2(0.0))) - p2) * stp.xz * inv;
    int axis = 1;
    float tin = t0, t = t0;
    bool hit = false;
    for (int i = 0; i < VX_CLOUD_STEPS; i++) {
        float tout = min(min(tmax.x, tmax.y), t1);
        ivec2 r = column_range(vec2(cc));
        if (r.y > r.x) {
            float lo = float(r.x), hi = float(r.y);
            float yin = ro.y + rd.y * tin, yout = ro.y + rd.y * tout;
            if (yin >= lo && yin <= hi) {
                t = tin;
                hit = true;
                break;
            }
            if ((yin > hi && yout <= hi) || (yin < lo && yout >= lo)) {
                t = ((yin > hi ? hi : lo) - ro.y) / rd.y;
                axis = 1;
                hit = true;
                break;
            }
        }
        if (tout >= t1) break;
        if (tmax.x < tmax.y) {
            tin = tmax.x;
            tmax.x += inv.x;
            cc.x += int(stp.x);
            axis = 0;
        } else {
            tin = tmax.y;
            tmax.y += inv.y;
            cc.y += int(stp.z);
            axis = 2;
        }
    }
    if (!hit) return;
    ivec3 cell = ivec3(cc.x, int(floor(ro.y + rd.y * t + stp.y * 1e-3)), cc.y);
    vec3 n = vec3(0.0);
    n[axis] = -stp[axis];
    // toon light: lit faces toward the sun, the rest in the sky's shade; cells above toward the sun cast onto it
    vec3 L = F.sun_dir.y > -0.05 ? F.sun_dir.xyz : F.moon_dir.xyz;
    float ndl = dot(n, L);
    float lit = ndl > 0.25 ? 1.0 : ndl > -0.1 ? 0.6 : 0.3;
    ivec3 sc = cell + ivec3(n);
    ivec3 toward = ivec3(sign(L.x) * step(0.35, abs(L.x)), 1, sign(L.z) * step(0.35, abs(L.z)));
    if (occupied(sc + toward) || occupied(sc + toward * 2)) lit *= 0.55;
    vec3 sun_c = F.light_color.rgb * 0.22 + F.sky_horizon.rgb * 0.2;
    // shaded sides and undersides: a lavender grey clearly darker than the haze, as in the reference
    vec3 shade_c = F.amb_sky.rgb * F.shade.rgb * 0.55 + F.sky_zenith.rgb * 0.12;
    vec3 col = mix(shade_c, vec3(0.95, 0.96, 1.0) * (F.amb_sky.rgb * 0.6 + sun_c * 2.2), lit);
    col *= n.y < -0.5 ? 0.72 : 1.0;
    // distant cloud melts into the haze
    float fade = exp(-t / 1600.0) * (1.0 - smoothstep(VX_CLOUD_FAR * 0.6, VX_CLOUD_FAR, t)) * smoothstep(-0.01, 0.06, dir.y + (cy > base ? 1.0 : 0.0));
    vec3 haze = sky_haze(dir);
    col = mix(haze, col, fade);
    float a = smoothstep(0.0, 0.25, fade + 0.05);
    frag_color = vec4(col * a, 1.0 - a);
}
@end

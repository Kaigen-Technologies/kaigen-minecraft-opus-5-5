// physically based sky (Hillaire 2020), distances in megameters
#define GROUND_RADIUS_MM 6.360
#define ATMO_RADIUS_MM 6.460
#define GROUND_ALBEDO vec3(0.3)
#define RAYLEIGH_SCATTER_BASE vec3(5.802, 13.558, 33.1)
#define MIE_SCATTER_BASE 3.996
#define MIE_ABSORB_BASE 4.4
#define OZONE_ABSORB_BASE vec3(0.650, 1.881, 0.085)

float safeacos(float x) { return acos(clamp(x, -1.0, 1.0)); }

void get_scattering_values(vec3 pos, out vec3 rayleigh_scattering, out float mie_scattering, out vec3 extinction) {
    float altitude_km = (length(pos) - GROUND_RADIUS_MM) * 1000.0;
    float rayleigh_density = exp(-altitude_km / 8.0);
    float mie_density = exp(-altitude_km / 1.2);
    rayleigh_scattering = RAYLEIGH_SCATTER_BASE * rayleigh_density;
    mie_scattering = MIE_SCATTER_BASE * mie_density;
    float mie_absorption = MIE_ABSORB_BASE * mie_density;
    vec3 ozone_absorption = OZONE_ABSORB_BASE * max(0.0, 1.0 - abs(altitude_km - 25.0) / 15.0);
    extinction = rayleigh_scattering + mie_scattering + mie_absorption + ozone_absorption;
}

float ray_intersect_sphere(vec3 ro, vec3 rd, float rad) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - rad * rad;
    if (c > 0.0 && b > 0.0) return -1.0;
    float discr = b * b - c;
    if (discr < 0.0) return -1.0;
    if (discr > b * b) return (-b + sqrt(discr));
    return -b - sqrt(discr);
}

float mie_phase(float cos_theta) {
    float g = 0.8;
    float scale = 3.0 / (8.0 * PI);
    float num = (1.0 - g * g) * (1.0 + cos_theta * cos_theta);
    float denom = (2.0 + g * g) * pow((1.0 + g * g - 2.0 * g * cos_theta), 1.5);
    return scale * num / denom;
}
float rayleigh_phase(float cos_theta) {
    return 3.0 / (16.0 * PI) * (1.0 + cos_theta * cos_theta);
}

// uv into the transmittance / multiscatter luts
vec2 lut_uv(vec3 pos, vec3 sun_dir) {
    float height = length(pos);
    vec3 up = pos / height;
    float sun_cos_zenith = dot(sun_dir, up);
    return vec2(clamp(0.5 + 0.5 * sun_cos_zenith, 0.0, 1.0),
                clamp((height - GROUND_RADIUS_MM) / (ATMO_RADIUS_MM - GROUND_RADIUS_MM), 0.0, 1.0));
}

#ifdef WC_ATMO_VIEW
vec3 atmo_view_pos() { return vec3(0.0, GROUND_RADIUS_MM + 0.0003 + clamp(F.cam_pos.y - 62.0, 0.0, 3000.0) * 1e-6, 0.0); }

// uv into a sky-view lut for a world ray and the lut's body direction
vec2 sky_lut_uv(vec3 ray_dir, vec3 sun_dir) {
    vec3 view_pos = atmo_view_pos();
    float height = length(view_pos);
    vec3 up = view_pos / height;
    float horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height);
    float altitude_angle = horizon_angle - acos(clamp(dot(ray_dir, up), -1.0, 1.0));
    float azimuth_angle;
    if (abs(altitude_angle) > (0.5 * PI - 0.0001)) {
        azimuth_angle = 0.0;
    } else {
        vec3 right = cross(sun_dir, up);
        vec3 forward = cross(up, right);
        vec3 projected = normalize(ray_dir - up * (dot(ray_dir, up)) + vec3(1e-6, 0.0, 0.0));
        float sin_theta = dot(projected, right);
        float cos_theta = dot(projected, forward);
        azimuth_angle = atan(sin_theta, cos_theta) + PI;
    }
    float v = 0.5 + 0.5 * sign(altitude_angle) * sqrt(abs(altitude_angle) * 2.0 / PI);
    return vec2(azimuth_angle / (2.0 * PI), v);
}
#endif

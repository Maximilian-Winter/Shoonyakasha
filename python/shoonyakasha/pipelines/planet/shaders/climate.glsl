// The planet's climate at a point, before the climate settings' offsets
// (biome.glsl adds those): climateAt(dir, height) is
//
//     x  temperature (degrees C): 30 at the equator falling to -14 at the
//        poles (30 - 44 sin^3 of the latitude), the latitude wavered by a
//        noise, plus a noise of about 3 degrees either way, less
//        CLIMATE_LAPSE per metre above sea level
//     y  moisture (0 dry .. 1 wet, not clamped): wet at the equator and
//        near 60 degrees, dry near 30 degrees and at the poles
//        (0.5 + 0.28 cos(6 latitude)), drier inland (by the continent
//        value), a little wetter high up, plus a noise
//     z  cover (0 .. 1): a noise about 400 m across, which thins forests
//        into clearings and gathers trees into groves on open ground
//
// chunk_build.comp stores it in each vertex's horizon.yzw. Include after
// terrain.glsl (for the noise, terrainContinent and `terrain`).

const float CLIMATE_EQUATOR = 30.0;          // degrees C at sea level on the equator
const float CLIMATE_POLE_DROP = 44.0;        // degrees C colder at the poles
const float CLIMATE_LAPSE = 0.0085;          // degrees C colder per metre above sea level
const float CLIMATE_COVER_WAVELENGTH = 400.0; // metres

vec3 climateAt(vec3 dir, float height) {
    vec3 p = dir + terrain.seed.xyz;
    float latitude = clamp(abs(dir.y) + 0.06 * fbm3(p * 3.0 + vec3(7.1, 2.3, 9.4), 3, 2.0, 0.5), 0.0, 1.0);
    float above = max(height, 0.0);

    float temperature = CLIMATE_EQUATOR - CLIMATE_POLE_DROP * latitude * latitude * latitude
                      + 7.0 * fbm3(p * 5.0 + vec3(4.4, 8.8, 1.2), 3, 2.0, 0.5)
                      - CLIMATE_LAPSE * above;

    float bands = 0.5 + 0.28 * cos(6.0 * asin(latitude));
    float inland = smoothstep(0.05, 0.5, terrainContinent(dir));
    float moisture = bands - 0.3 * inland + 0.08 * smoothstep(400.0, 2000.0, above)
                   + 0.7 * fbm3(p * 9.0 + vec3(3.1, 0.7, 5.3), 4, 2.0, 0.5);

    vec3 coverInput = dir * (terrain.siteShape.z / CLIMATE_COVER_WAVELENGTH) + fract(terrain.seed.xyz * 0.0517) * 16.0;
    float cover = clamp(0.5 + 1.6 * fbm3(coverInput, 3, 2.0, 0.5), 0.0, 1.0);
    return vec3(temperature, moisture, cover);
}

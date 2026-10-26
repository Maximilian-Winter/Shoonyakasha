// The planet's ground as layers: sand (and the sea floor), dry ground,
// grass, forest, rock and snow, and the moon's ground. groundLayers gives
// each layer's weight and colour at a point; the terrain's colours
// (terrain.frag, ground.glsl) and the vegetation (vegetation_scatter.comp)
// both read them. Include after terrain.glsl (for fbm3 and `terrain`).

const int GROUND_SAND = 0;
const int GROUND_DRY = 1;
const int GROUND_GRASS = 2;
const int GROUND_FOREST = 3;
const int GROUND_ROCK = 4;
const int GROUND_SNOW = 5;
const int GROUND_MOON = 6;
const int GROUND_LAYERS = 7;

const vec3 SEABED_DEEP = vec3(0.02, 0.05, 0.12);
const vec3 SEABED_SHALLOW = vec3(0.10, 0.22, 0.30);
const vec3 SAND = vec3(0.62, 0.55, 0.38);
const vec3 GRASS = vec3(0.16, 0.30, 0.08);
const vec3 FOREST = vec3(0.06, 0.16, 0.05);
const vec3 DRY = vec3(0.45, 0.38, 0.22);
const vec3 ROCK = vec3(0.30, 0.27, 0.25);
const vec3 SNOW = vec3(0.85, 0.87, 0.90);

// The planet's ground as weights of the layers (ground.glsl) and their
// colours: the sea floor (on the sand's texture) darker with depth; sand
// below 40 m; dry ground, grass or forest by a moisture noise; rock where
// the slope is steep; snow above a snow line that falls from 2,500 m at the
// equator to 200 m at the poles.
void groundLayers(vec3 radial, float height, vec3 normal, out float weights[GROUND_LAYERS],
                  out vec3 colours[GROUND_LAYERS]) {
    for (int i = 0; i < GROUND_LAYERS; ++i) weights[i] = 0.0;
    colours = vec3[GROUND_LAYERS](SAND, DRY, GRASS, FOREST, ROCK, SNOW, vec3(0.0));
    if (height < 0.0) {
        weights[GROUND_SAND] = 1.0;
        colours[GROUND_SAND] = mix(SEABED_SHALLOW, SEABED_DEEP, smoothstep(0.0, 800.0, -height));
        return;
    }
    float slope = 1.0 - dot(normal, radial);                  // 0 flat, 1 vertical
    float latitude = abs(radial.y);
    float moisture = fbm3(radial * 9.0 + terrain.seed.xyz + vec3(3.1, 0.7, 5.3), 4, 2.0, 0.5) * 2.0 + 0.5;
    float green = smoothstep(0.3, 0.5, moisture);
    float forest = smoothstep(0.55, 0.8, moisture);
    float land = smoothstep(10.0, 40.0, height);
    float rock = smoothstep(0.12, 0.22, slope);
    float snowLine = mix(2500.0, 200.0, latitude * latitude);
    float snow = smoothstep(snowLine - 150.0, snowLine + 150.0, height) * (1.0 - smoothstep(0.35, 0.5, slope));
    float open = (1.0 - rock) * (1.0 - snow);
    weights[GROUND_DRY] = (1.0 - green) * land * open;
    weights[GROUND_GRASS] = green * (1.0 - forest) * land * open;
    weights[GROUND_FOREST] = green * forest * land * open;
    weights[GROUND_SAND] = (1.0 - land) * open;
    weights[GROUND_ROCK] = rock * (1.0 - snow);
    weights[GROUND_SNOW] = snow;
}

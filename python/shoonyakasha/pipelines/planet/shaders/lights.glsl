// The local lights (PlanetWorld.set_lights): up to MAX_LIGHTS points and
// spots in atmosphere.lightCount / lightPosition / lightDirection /
// lightColour (camera.glsl), positions relative to the camera. Each lights
// a surface by its colour (the illuminance at 1 m) over the distance
// squared, faded to 0 at its range by (1 - (d / range)^4)^2, and for a spot
// by its cone, from 1 inside the inner cosine to 0 at the outer. They cast
// no shadows. Include after camera.glsl and atmosphere.glsl.

const int MAX_LIGHTS = 8;

// The radiance the local lights give a diffuse surface with `albedo` at
// `relativePos` (relative to the camera) facing `normal`; a two-sided
// surface (leaves) takes light from either side.
vec3 localLights(vec3 albedo, vec3 relativePos, vec3 normal, bool twoSided) {
    vec3 sum = vec3(0.0);
    int count = min(int(atmosphere.lightCount.x), MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        vec3 toLight = atmosphere.lightPosition[i].xyz - relativePos;
        float d2 = max(dot(toLight, toLight), 0.01);
        float d = sqrt(d2);
        vec3 l = toLight / d;
        float reach = clamp(1.0 - pow(d / atmosphere.lightPosition[i].w, 4.0), 0.0, 1.0);
        vec4 spot = atmosphere.lightDirection[i];
        float cone = dot(spot.xyz, spot.xyz) > 0.5
                   ? smoothstep(spot.w, atmosphere.lightColour[i].w, dot(-l, spot.xyz)) : 1.0;
        float facing = twoSided ? abs(dot(normal, l)) : max(dot(normal, l), 0.0);
        sum += atmosphere.lightColour[i].rgb * (facing * cone * reach * reach / d2);
    }
    return albedo / ATMOSPHERE_PI * sum;
}

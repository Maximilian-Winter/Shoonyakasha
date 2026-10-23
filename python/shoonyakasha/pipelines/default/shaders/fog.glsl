// fog.glsl - exponential height fog: density settings.fogDensity per metre
// at height settings.fogHeight, falling by a factor e every
// 1 / settings.fogHeightFalloff metres above it and growing below it (the
// same everywhere with falloff 0). Its light is the environment's, blurred
// as far as prefilterMap goes, times settings.iblIntensity and the tint
// settings.fogColor. The including shader declares the Settings block as
// `settings` and `samplerCube prefilterMap`.

#ifndef DEFAULT_FOG_GLSL
#define DEFAULT_FOG_GLSL

// The share of the light along a ray `distance` metres long from `origin`
// in unit direction `dir` that the fog replaces with its own: 1 - e^-(the
// density integrated along the ray).
float fogAmount(vec3 origin, vec3 dir, float distance) {
    float density = settings.fogDensity;
    if (density <= 0.0) return 0.0;
    float falloff = max(settings.fogHeightFalloff, 0.0);
    float atOrigin = density * exp(min(-falloff * (origin.y - settings.fogHeight), 80.0));
    float k = falloff * dir.y * distance;
    float average = abs(k) > 1e-4 ? (1.0 - exp(min(-k, 80.0))) / k : 1.0 - 0.5 * k;
    return 1.0 - exp(-atOrigin * distance * average);
}

// The fog's light towards the camera along `dir`.
vec3 fogLight(vec3 dir) {
    float maxLod = float(textureQueryLevels(prefilterMap) - 1);
    return textureLod(prefilterMap, dir, maxLod).rgb * settings.iblIntensity * settings.fogColor;
}

#endif

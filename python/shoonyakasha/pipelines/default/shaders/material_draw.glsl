// material_draw.glsl - the per-draw push constants of every G-buffer and
// forward pass (pipeline.json "MaterialDraw").

layout(push_constant) uniform MaterialDraw {
    mat4 model;
    vec4 baseColorFactor;
    vec4 emissiveFactor;     // rgb, a unused
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    float hasNormalMap;      // 1 when the material has its own texture
    float hasMetalRoughMap;
} draw;

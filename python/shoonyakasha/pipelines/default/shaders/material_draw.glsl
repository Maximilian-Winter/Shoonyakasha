// material_draw.glsl - the per-draw push constants of every G-buffer and
// forward pass (pipeline.json "MaterialDraw").

layout(push_constant) uniform MaterialDraw {
    mat4 model;
    mat4 prevModel;          // model last frame, for motion vectors
    vec4 baseColorFactor;
    vec4 emissiveFactor;     // rgb, a unused
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    float hasNormalMap;      // 1 when the material has its own texture
    float hasMetalRoughMap;
    float exposureCompensation;  // 0..1: how much emission is divided by the exposure
    float normalScale;       // scales the normal map's x and y (glTF normalTexture.scale)
    float occlusionStrength; // 0: the occlusion map has no effect, 1: its full effect
    float clearcoatFactor;   // KHR_materials_clearcoat: coat amount, 0 for none
    float clearcoatRoughness;
    float transmissionFactor;   // KHR_materials_transmission: light let through, 0 for none
    float ior;                  // KHR_materials_ior
    vec4 attenuationColor;      // KHR_materials_volume: rgb, a unused
    float thicknessFactor;      // thickness in mesh units, 0 for a thin surface
    float attenuationDistance;  // distance light travels before it is attenuationColor, 0 for none
    uint alphaMode;             // 0 opaque, 1 mask, 2 blend
} draw;

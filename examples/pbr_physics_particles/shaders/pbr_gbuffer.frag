#version 450
//
// PBR GBuffer Fragment Shader — Multiple Render Targets
//
// 青龍司生  生發而有序
// Four channels of geometry data, separated like the four directions
//
// Output layout:
//   RT0 (R16G16B16A16_SFLOAT) — World position
//   RT1 (R16G16B16A16_SFLOAT) — World normal (packed to [0,1])
//   RT2 (R8G8B8A8_SRGB)       — Albedo color
//   RT3 (R8G8_UNORM)          — Metallic + Roughness
//

// Material textures (set 1)
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;

// Material factors push constant (104 bytes total)
layout(push_constant) uniform MaterialFactors {
    mat4 model;            // offset 0
    vec4 baseColorFactor;  // offset 64
    float metallicFactor;  // offset 80
    float roughnessFactor; // offset 84
    float hasNormalMap;    // offset 88 (1.0 = has normal map, 0.0 = no)
    float hasMetalRoughMap; // offset 92
    float alphaCutoff;     // offset 96 (threshold for alpha test)
    float padding1;        // offset 100 (alignment)
} material;

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragColor;

// MRT outputs
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec2 gMetallicRoughness;

// Compute TBN matrix for normal mapping (robust version)
// Handles edge cases where UV derivatives are degenerate
mat3 computeTBN(vec3 worldNormal, vec3 worldPos, vec2 uv) {
    // Compute tangent and bitangent from screen-space derivatives
    vec3 Q1 = dFdx(worldPos);
    vec3 Q2 = dFdy(worldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 N = normalize(worldNormal);

    // Check for degenerate UV derivatives (would cause division by zero or NaN)
    float det = st1.s * st2.t - st2.s * st1.t;
    if (abs(det) < 0.0001) {
        // Fallback: construct arbitrary tangent frame
        vec3 T = abs(N.y) < 0.999 ? normalize(cross(N, vec3(0.0, 1.0, 0.0))) : normalize(cross(N, vec3(1.0, 0.0, 0.0)));
        vec3 B = normalize(cross(N, T));
        return mat3(T, B, N);
    }

    // Standard tangent/bitangent calculation
    float invDet = 1.0 / det;
    vec3 T = (Q1 * st2.t - Q2 * st1.t) * invDet;
    vec3 B = (Q2 * st1.s - Q1 * st2.s) * invDet;

    // Gram-Schmidt orthonormalize
    T = normalize(T - N * dot(N, T));
    B = cross(N, T);

    return mat3(T, B, N);
}

void main() {
    // Sample albedo texture
    vec4 albedo = texture(albedoMap, fragTexCoord);

    // Apply base color factor and vertex color
    albedo.rgb *= material.baseColorFactor.rgb * fragColor;
    albedo.a *= material.baseColorFactor.a;

    // Alpha test - discard fragments below alpha cutoff threshold
    // For OPAQUE mode, alphaCutoff should be 0.0 (nothing discarded except true 0 alpha)
    // For MASK mode, alphaCutoff is the threshold (default 0.5)
    // For BLEND mode, this shader shouldn't be used (handled by forward pass)
    if (albedo.a < material.alphaCutoff) {
        discard;
    }

    // Normal calculation
    vec3 N = normalize(fragWorldNormal);

    // DEBUG: Check if vertex normal is bad BEFORE any processing
    float normalLength = length(fragWorldNormal);
    bool badVertexNormal = (normalLength < 0.001) || isnan(normalLength) || isinf(normalLength);

    // Apply normal map if available
    if (material.hasNormalMap > 0.5) {
        vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;

        // Note: glTF specifies OpenGL-style normal maps (Y+ is up)
        // If your normal maps are in DirectX format (Y- is up), uncomment:
        // tangentNormal.y = -tangentNormal.y;

        mat3 TBN = computeTBN(fragWorldNormal, fragWorldPos, fragTexCoord);
        vec3 mappedNormal = TBN * tangentNormal;

        // Safety: ensure we have a valid normal (avoid NaN from bad TBN)
        if (dot(mappedNormal, mappedNormal) > 0.001) {
            N = normalize(mappedNormal);
        }
        // If invalid, fall back to vertex normal (N is already set)
    }

    // Two-sided lighting: flip normal if viewing back face
    // This is essential for indoor scenes like Sponza where you view walls from inside
    // gl_FrontFacing is true for front faces, false for back faces
    if (!gl_FrontFacing) {
        N = -N;
    }

    // Sample metallic/roughness or use factors
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    bool usedTexture = false;

    if (material.hasMetalRoughMap > 0.5) {
        vec4 mr = texture(metallicRoughnessMap, fragTexCoord);
        // glTF packing: Blue = Metallic, Green = Roughness
        metallic = mr.b * material.metallicFactor;
        roughness = mr.g * material.roughnessFactor;
        usedTexture = true;
    }

    // Clamp roughness to avoid divide-by-zero in lighting
    roughness = clamp(roughness, 0.04, 1.0);

    // Output to GBuffer
    gPosition = vec4(fragWorldPos, 1.0);
    gNormal = vec4(N * 0.5 + 0.5, 1.0);  // Pack normal to [0,1]
    gAlbedo = vec4(albedo.rgb, albedo.a);
    gMetallicRoughness = vec2(metallic, roughness);

    // ══════════════════════════════════════════════════════════════════
    // DEBUG MODE: Set to 0 for NORMAL rendering, 1 for debug visualization
    // ══════════════════════════════════════════════════════════════════
    #define DEBUG_MODE 0

    #if DEBUG_MODE == 1
    // Visualization key:
    //   GREEN = non-metallic (metallic ≈ 0) - stone, wood, etc. should be green
    //   RED = fully metallic (metallic ≈ 1) - metal objects should be red
    //   BLUE = hasMetalRoughMap flag is FALSE (no texture, using factor directly)
    //   YELLOW = hasMetalRoughMap TRUE but metallic still high (texture issue?)
    if (!usedTexture) {
        // No texture - show BLUE tint to indicate fallback to factor
        gAlbedo = vec4(metallic * 0.5, 1.0 - metallic, 1.0, 1.0);  // Blue tint
    } else if (metallic > 0.9) {
        // Texture used but metallic is high - potential texture/sampling issue
        gAlbedo = vec4(1.0, 1.0, 0.0, 1.0);  // YELLOW = warning
    } else {
        // Normal case: show metallic as red, non-metallic as green
        gAlbedo = vec4(metallic, 1.0 - metallic, 0.0, 1.0);
    }
    #endif
}

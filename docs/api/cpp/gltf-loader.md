# GltfSceneLoader

Include [Resources/GltfSceneLoader.h](../../../include/Resources/GltfSceneLoader.h); namespace `Shoonyakasha`. The loader imports glTF/GLB through cgltf, producing GPU geometry/textures and optional ECS entities, skins, and animation clips.

Most applications should call [EngineAPI::loadGltfScene](engine-api.md). The native result is richer than the facade result: `GltfLoadResult` includes primitives, renderable entities, `nodeEntities`, `rootEntities`, skeletons, animation clips, statistics, and success/error fields.

Default `flattenHierarchy=false` preserves node entities and shares mesh-space geometry for repeated mesh references within a load. Renderable primitives are children of node entities. Flattening bakes world transforms into vertices and loses this sharing.

Options include texture/material/entity/skin/animation loading, hierarchy flattening, texture settings, and a name prefix. `maxTextureSize` currently warns instead of resizing; `srgbAlbedo` is not consulted by the current loader's slot-based format selection. Inspect [GltfSceneLoader.cpp](../../../src/Resources/GltfSceneLoader.cpp) when depending on import behavior beyond the examples.

References: [loading guide](../../guides/loading-scenes.md), [instancing](../../guides/instancing.md), [result/options](../python/gltf-result.md), [animation](../../guides/animation.md).

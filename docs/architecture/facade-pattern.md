# Facade pattern

The `Shoonyakasha::Facade` layer gives native applications and Cython a concrete interface without exposing Vulkan/EnTT internals. Headers use standard-library/GLM types and hide implementation through PIMPL.

| Facade | Role |
|---|---|
| EngineAPI | Configuration, lifecycle, convenience creation, custom values, capture |
| SceneAPI | Entity lifecycle and typed native component operations |
| InputAPI | Keyboard/mouse polling and event callbacks |
| PhysicsAPI | World settings and force/velocity/body operations |
| EcsAPI | Named opaque script components and callback systems |

EngineAPI's private CallbackApp subclasses ApplicationBase. Input/physics facades exist immediately, but native wiring occurs during init. Scene/Ecs require the live Scene and throw if requested too early. In particular, early physics setters do not preserve configuration despite a public-header comment suggesting otherwise.

A facade method is not necessarily the whole native feature: the C++ loader exposes root/node collections omitted from GltfResult, and raw registry/controller/graph APIs are not Python bindings. Document this distinction whenever adding features.

Source: [EngineAPI.cpp](../../src/Facade/EngineAPI.cpp), [facade headers](../../include/Facade). References: [C++ EngineAPI](../api/cpp/engine-api.md), [Python Engine](../api/python/engine.md).

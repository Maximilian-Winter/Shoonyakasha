# distutils: language = c++
#
# _facade_types.pxd — C++ declarations for Facade/FacadeTypes.h
#
# Declares: EntityHandle, NullEntity, enums, structs (EngineConfig, GltfOptions, GltfResult)
#

from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.pair cimport pair
from libcpp cimport bool as cbool
from libc.stdint cimport uint8_t, uint32_t


cdef extern from "Facade/FacadeTypes.h" namespace "Shoonyakasha::Facade":

    # ── Entity Handle ──────────────────────────────────────────
    ctypedef uint32_t EntityHandle
    cdef EntityHandle NullEntity

    # ── Enums ──────────────────────────────────────────────────
    cpdef enum CameraType "Shoonyakasha::Facade::CameraType":
        CameraType_Perspective "Shoonyakasha::Facade::CameraType::Perspective"
        CameraType_Orthographic "Shoonyakasha::Facade::CameraType::Orthographic"

    cpdef enum LightType "Shoonyakasha::Facade::LightType":
        LightType_Directional "Shoonyakasha::Facade::LightType::Directional"
        LightType_Point "Shoonyakasha::Facade::LightType::Point"
        LightType_Spot "Shoonyakasha::Facade::LightType::Spot"

    cpdef enum RigidBodyType "Shoonyakasha::Facade::RigidBodyType":
        RigidBodyType_Static "Shoonyakasha::Facade::RigidBodyType::Static"
        RigidBodyType_Kinematic "Shoonyakasha::Facade::RigidBodyType::Kinematic"
        RigidBodyType_Dynamic "Shoonyakasha::Facade::RigidBodyType::Dynamic"

    cpdef enum ColliderShape "Shoonyakasha::Facade::ColliderShape":
        ColliderShape_Box "Shoonyakasha::Facade::ColliderShape::Box"
        ColliderShape_Sphere "Shoonyakasha::Facade::ColliderShape::Sphere"
        ColliderShape_Capsule "Shoonyakasha::Facade::ColliderShape::Capsule"
        ColliderShape_Mesh "Shoonyakasha::Facade::ColliderShape::Mesh"
        ColliderShape_Plane "Shoonyakasha::Facade::ColliderShape::Plane"

    cpdef enum UIAnchor "Shoonyakasha::Facade::UIAnchor":
        UIAnchor_TopLeft "Shoonyakasha::Facade::UIAnchor::TopLeft"
        UIAnchor_TopCenter "Shoonyakasha::Facade::UIAnchor::TopCenter"
        UIAnchor_TopRight "Shoonyakasha::Facade::UIAnchor::TopRight"
        UIAnchor_MiddleLeft "Shoonyakasha::Facade::UIAnchor::MiddleLeft"
        UIAnchor_MiddleCenter "Shoonyakasha::Facade::UIAnchor::MiddleCenter"
        UIAnchor_MiddleRight "Shoonyakasha::Facade::UIAnchor::MiddleRight"
        UIAnchor_BottomLeft "Shoonyakasha::Facade::UIAnchor::BottomLeft"
        UIAnchor_BottomCenter "Shoonyakasha::Facade::UIAnchor::BottomCenter"
        UIAnchor_BottomRight "Shoonyakasha::Facade::UIAnchor::BottomRight"

    cpdef enum TextHAlign "Shoonyakasha::Facade::TextHAlign":
        TextHAlign_Left "Shoonyakasha::Facade::TextHAlign::Left"
        TextHAlign_Center "Shoonyakasha::Facade::TextHAlign::Center"
        TextHAlign_Right "Shoonyakasha::Facade::TextHAlign::Right"

    # ── Structs ────────────────────────────────────────────────
    cdef cppclass EngineConfig:
        int width
        int height
        string title
        string logFile
        int logLevel
        string hdrEnvironmentPath
        string pipelineJsonPath
        uint32_t maxFramesInFlight
        vector[pair[string, uint32_t]] renderGraphParameters

    cdef cppclass GltfOptions:
        cbool loadTextures
        cbool loadMaterials
        cbool createEntities
        cbool loadSkins
        cbool loadAnimations
        cbool flattenHierarchy
        int maxTextureSize
        cbool generateMipmaps
        cbool srgbAlbedo
        string namePrefix

    cdef cppclass ClipInfo "Shoonyakasha::Facade::GltfResult::ClipInfo":
        string name
        float duration

    cdef cppclass GltfResult:
        cbool success
        string error
        vector[EntityHandle] entities
        size_t totalVertices
        size_t totalIndices
        size_t totalTextures
        size_t totalMaterials
        vector[ClipInfo] animationClips
        size_t skeletonCount

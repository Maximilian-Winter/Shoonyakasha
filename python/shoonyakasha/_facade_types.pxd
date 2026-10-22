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
from libc.stdint cimport uint8_t, uint32_t, uint64_t


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
        TextHAlign_Start "Shoonyakasha::Facade::TextHAlign::Start"
        TextHAlign_End "Shoonyakasha::Facade::TextHAlign::End"

    cpdef enum TextVAlign "Shoonyakasha::Facade::TextVAlign":
        TextVAlign_Top "Shoonyakasha::Facade::TextVAlign::Top"
        TextVAlign_Middle "Shoonyakasha::Facade::TextVAlign::Middle"
        TextVAlign_Bottom "Shoonyakasha::Facade::TextVAlign::Bottom"

    cpdef enum TextDirection "Shoonyakasha::Facade::TextDirection":
        TextDirection_Auto "Shoonyakasha::Facade::TextDirection::Auto"
        TextDirection_LeftToRight "Shoonyakasha::Facade::TextDirection::LeftToRight"
        TextDirection_RightToLeft "Shoonyakasha::Facade::TextDirection::RightToLeft"
        TextDirection_FromLayout "Shoonyakasha::Facade::TextDirection::FromLayout"

    cpdef enum AccessibleRole "Shoonyakasha::Facade::AccessibleRole":
        AccessibleRole_Default "Shoonyakasha::Facade::AccessibleRole::Default"
        AccessibleRole_Group "Shoonyakasha::Facade::AccessibleRole::Group"
        AccessibleRole_Button "Shoonyakasha::Facade::AccessibleRole::Button"
        AccessibleRole_CheckBox "Shoonyakasha::Facade::AccessibleRole::CheckBox"
        AccessibleRole_Slider "Shoonyakasha::Facade::AccessibleRole::Slider"
        AccessibleRole_Edit "Shoonyakasha::Facade::AccessibleRole::Edit"
        AccessibleRole_Pane "Shoonyakasha::Facade::AccessibleRole::Pane"
        AccessibleRole_Text "Shoonyakasha::Facade::AccessibleRole::Text"

    cpdef enum TextOrientation "Shoonyakasha::Facade::TextOrientation":
        TextOrientation_Horizontal "Shoonyakasha::Facade::TextOrientation::Horizontal"
        TextOrientation_VerticalRightToLeft "Shoonyakasha::Facade::TextOrientation::VerticalRightToLeft"
        TextOrientation_VerticalLeftToRight "Shoonyakasha::Facade::TextOrientation::VerticalLeftToRight"

    cpdef enum LayoutDirection "Shoonyakasha::Facade::LayoutDirection":
        LayoutDirection_Inherit "Shoonyakasha::Facade::LayoutDirection::Inherit"
        LayoutDirection_LeftToRight "Shoonyakasha::Facade::LayoutDirection::LeftToRight"
        LayoutDirection_RightToLeft "Shoonyakasha::Facade::LayoutDirection::RightToLeft"

    cpdef enum SizeFit "Shoonyakasha::Facade::SizeFit":
        SizeFit_Unconstrained "Shoonyakasha::Facade::SizeFit::Unconstrained"
        SizeFit_Min "Shoonyakasha::Facade::SizeFit::Min"
        SizeFit_Preferred "Shoonyakasha::Facade::SizeFit::Preferred"

    cpdef enum CanvasScaleMode "Shoonyakasha::Facade::CanvasScaleMode":
        CanvasScaleMode_ConstantPixel "Shoonyakasha::Facade::CanvasScaleMode::ConstantPixel"
        CanvasScaleMode_ScaleWithScreen "Shoonyakasha::Facade::CanvasScaleMode::ScaleWithScreen"

    # ── Structs ────────────────────────────────────────────────
    cdef cppclass EngineConfig:
        int width
        int height
        string title
        string logFile
        int logLevel
        string hdrEnvironmentPath
        float uniformEnvironmentColor[3]
        cbool iblCache
        string pipelineJsonPath
        uint32_t maxFramesInFlight
        cbool enableValidation
        vector[pair[string, uint32_t]] renderGraphParameters

    cdef cppclass RecordingOptions:
        RecordingOptions()
        int fps
        int quality
        string codec
        string ffmpegPath
        cbool fixedStep

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
        cbool compressTextures
        string namePrefix

    cdef cppclass ClipInfo "Shoonyakasha::Facade::GltfResult::ClipInfo":
        string name
        float duration

    cdef cppclass RenderPassStats:
        string name
        double cpuMs
        double gpuMs
        cbool gpuValid
        uint32_t drawCalls
        uint32_t dispatches
        uint64_t vertices

    cdef cppclass RenderStatsSnapshot:
        cbool enabled
        double fps
        double frameTimeMs
        double frameTimeMaxMs
        double cpuRecordMs
        double gpuMs
        cbool gpuValid
        uint32_t drawCalls
        uint32_t dispatches
        uint64_t vertices
        cbool validationLayers
        vector[RenderPassStats] passes
        string summary

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

//
// ECS/CameraController.h - The Eye That Perceives Motion
//
// 攝影機控制器 - Camera Controller Components and System
//
// Like Vajrayogini's all-seeing wisdom eye, the camera perceives
// the mandala of the digital realm from any vantage point.
//
// Four modes of perception:
//   Free        — Unbound flight through space (developer/spectator)
//   Orbit       — Circumambulation around sacred center (inspection)
//   FirstPerson — Direct embodiment (immersion)
//   ThirdPerson — Witness consciousness (gameplay)
//

#pragma once

#include "Core.h"
#include <GLFW/glfw3.h>
#include <array>
#include <algorithm>

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// Input State Component - The Sensory Aggregate
// ═══════════════════════════════════════════════════════════════
//
// Tracks the continuous state of all input devices.
// Events are ephemeral; state persists across frames.
//

struct InputStateComponent {
    // Keyboard state (indexed by GLFW key codes)
    std::array<bool, GLFW_KEY_LAST + 1> keys{};

    // Mouse state
    glm::vec2 mousePosition{0.0f};
    glm::vec2 lastMousePosition{0.0f};
    glm::vec2 mouseDelta{0.0f};
    glm::vec2 scrollDelta{0.0f};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseButtons{};

    // Mouse capture state (for FPS-style control)
    bool mouseCaptured = false;
    bool firstMouseCapture = true;

    // ─────────────────────────────────────────────────────────────
    // Query Methods - The Questions We Ask of Reality
    // ─────────────────────────────────────────────────────────────

    bool isKeyDown(int key) const {
        return key >= 0 && key <= GLFW_KEY_LAST && keys[key];
    }

    bool isMouseButtonDown(int button) const {
        return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && mouseButtons[button];
    }

    // ─────────────────────────────────────────────────────────────
    // Frame Update - Called at start of each frame
    // ─────────────────────────────────────────────────────────────

    void beginFrame() {
        // Calculate mouse delta from last frame
        // On first capture (or first frame ever), zero out delta to prevent huge jumps
        if (firstMouseCapture) {
            mouseDelta = glm::vec2(0.0f);
            lastMousePosition = mousePosition;  // Sync positions
            firstMouseCapture = false;
        } else {
            mouseDelta = mousePosition - lastMousePosition;
            lastMousePosition = mousePosition;
        }

        // Reset scroll (scroll is per-frame, not continuous)
        // Note: scrollDelta is accumulated via events, reset here after reading
    }

    // Reset scroll delta after it's been consumed
    void endFrame() {
        scrollDelta = glm::vec2(0.0f);
    }
};

// ═══════════════════════════════════════════════════════════════
// Camera Controller Component - The Will That Moves
// ═══════════════════════════════════════════════════════════════
//
// Configuration for how the camera responds to input.
// Each mode offers a different relationship with space.
//

struct CameraControllerComponent {
    // ─────────────────────────────────────────────────────────────
    // Control Modes - Ways of Perceiving
    // ─────────────────────────────────────────────────────────────

    enum class Mode {
        Free,           // Fly camera - WASD + mouse look (developer mode)
        Orbit,          // Orbit around target - drag to rotate, scroll to zoom
        FirstPerson,    // FPS camera - locked to ground plane
        ThirdPerson     // Following camera - behind target entity
    };

    Mode mode = Mode::Free;
    bool enabled = true;

    // ─────────────────────────────────────────────────────────────
    // Movement Parameters - The Speed of Thought
    // ─────────────────────────────────────────────────────────────

    float moveSpeed = 5.0f;           // Units per second
    float sprintMultiplier = 2.5f;    // Speed multiplier when sprinting
    float rotationSpeed = 2.0f;       // Radians per second (keyboard rotation)
    float mouseSensitivity = 0.003f;  // Radians per pixel (higher = faster turning)
    float scrollSensitivity = 2.0f;   // Units per scroll tick
    float smoothing = 10.0f;          // Interpolation factor (higher = snappier)

    // ─────────────────────────────────────────────────────────────
    // Orbit Mode Parameters - Circumambulation
    // ─────────────────────────────────────────────────────────────

    glm::vec3 orbitTarget{0.0f};      // Point to orbit around
    float orbitDistance = 10.0f;       // Distance from target
    float orbitYaw = 0.0f;            // Horizontal angle (radians)
    float orbitPitch = 0.3f;          // Vertical angle (radians)
    float orbitMinDistance = 1.0f;    // Minimum zoom distance
    float orbitMaxDistance = 100.0f;  // Maximum zoom distance
    float orbitMinPitch = -1.5f;      // Minimum vertical angle (radians)
    float orbitMaxPitch = 1.5f;       // Maximum vertical angle (radians)
    bool orbitAutoRotate = false;     // Auto-rotate when idle
    float orbitAutoRotateSpeed = 0.3f;// Radians per second

    // ─────────────────────────────────────────────────────────────
    // Free Camera Parameters - Flight Options
    // ─────────────────────────────────────────────────────────────

    bool horizonLocked = true;        // Keep horizon level (no roll) - recommended for most uses
                                      // When false, allows full 6DOF flight (can roll)

    // ─────────────────────────────────────────────────────────────
    // First Person Parameters - Embodiment
    // ─────────────────────────────────────────────────────────────

    float eyeHeight = 1.7f;           // Camera height from ground
    float groundY = 0.0f;             // Ground plane Y coordinate
    bool constrainPitch = true;       // Prevent looking straight up/down
    float maxPitch = 1.5f;            // Maximum vertical look angle

    // ─────────────────────────────────────────────────────────────
    // Third Person Parameters - Witness
    // ─────────────────────────────────────────────────────────────

    entt::entity followTarget = entt::null;  // Entity to follow
    glm::vec3 followOffset{0.0f, 2.0f, 5.0f}; // Offset from target
    float followLag = 5.0f;           // How quickly camera catches up

    // ─────────────────────────────────────────────────────────────
    // Internal State - The Memory of Motion
    // ─────────────────────────────────────────────────────────────

    glm::vec3 velocity{0.0f};         // Current movement velocity
    glm::vec2 angularVelocity{0.0f};  // Current rotation velocity (yaw, pitch)
    float currentYaw = 0.0f;          // Current horizontal rotation
    float currentPitch = 0.0f;        // Current vertical rotation
    bool initialized = false;         // Whether yaw/pitch have been initialized from transform

    // ─────────────────────────────────────────────────────────────
    // Key Bindings - The Map of Intention
    // ─────────────────────────────────────────────────────────────

    int keyForward = GLFW_KEY_W;
    int keyBackward = GLFW_KEY_S;
    int keyLeft = GLFW_KEY_A;
    int keyRight = GLFW_KEY_D;
    int keyUp = GLFW_KEY_E;           // Fly up / jump
    int keyDown = GLFW_KEY_Q;         // Fly down / crouch
    int keySprint = GLFW_KEY_LEFT_SHIFT;
    int keyCaptureMouse = GLFW_KEY_ESCAPE;  // Toggle mouse capture
    int mouseButtonLook = GLFW_MOUSE_BUTTON_RIGHT;  // Hold to look (orbit mode)
    int mouseButtonPan = GLFW_MOUSE_BUTTON_MIDDLE;  // Hold to pan (orbit mode)

    // ─────────────────────────────────────────────────────────────
    // Convenience Factories - Quick Creation
    // ─────────────────────────────────────────────────────────────

    static CameraControllerComponent createFreeCamera(float speed = 5.0f) {
        CameraControllerComponent ctrl;
        ctrl.mode = Mode::Free;
        ctrl.moveSpeed = speed;
        return ctrl;
    }

    static CameraControllerComponent createOrbitCamera(
        const glm::vec3& target = glm::vec3(0.0f),
        float distance = 10.0f,
        bool autoRotate = false
    ) {
        CameraControllerComponent ctrl;
        ctrl.mode = Mode::Orbit;
        ctrl.orbitTarget = target;
        ctrl.orbitDistance = distance;
        ctrl.orbitAutoRotate = autoRotate;
        return ctrl;
    }

    static CameraControllerComponent createFirstPersonCamera(float eyeHeight = 1.7f) {
        CameraControllerComponent ctrl;
        ctrl.mode = Mode::FirstPerson;
        ctrl.eyeHeight = eyeHeight;
        return ctrl;
    }

    static CameraControllerComponent createThirdPersonCamera(
        entt::entity target,
        const glm::vec3& offset = glm::vec3(0.0f, 2.0f, 5.0f)
    ) {
        CameraControllerComponent ctrl;
        ctrl.mode = Mode::ThirdPerson;
        ctrl.followTarget = target;
        ctrl.followOffset = offset;
        return ctrl;
    }
};

// ═══════════════════════════════════════════════════════════════
// Camera Controller System - The Dance of Perception
// ═══════════════════════════════════════════════════════════════
//
// Processes input state and updates camera transforms.
// Runs at priority 5 (before CameraSystem at 10).
//

class CameraControllerSystem : public ISystem {
public:
    CameraControllerSystem() {
        priority = -10;  // Run BEFORE TransformSystem (0) so worldMatrix reflects our changes
    }

    void update(entt::registry& registry, float deltaTime) override {
        if (!enabled) return;

        // Get the input state (singleton pattern - only one input state per scene)
        InputStateComponent* inputState = nullptr;
        auto inputView = registry.view<InputStateComponent>();
        for (auto entity : inputView) {
            inputState = &inputView.get<InputStateComponent>(entity);
            break;
        }

        if (!inputState) return;

        // Update all camera controllers
        auto view = registry.view<CameraControllerComponent, TransformComponent>();
        for (auto entity : view) {
            auto& controller = view.get<CameraControllerComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            if (!controller.enabled) continue;

            switch (controller.mode) {
                case CameraControllerComponent::Mode::Free:
                    updateFreeCamera(controller, transform, *inputState, deltaTime);
                    break;
                case CameraControllerComponent::Mode::Orbit:
                    updateOrbitCamera(controller, transform, *inputState, deltaTime);
                    break;
                case CameraControllerComponent::Mode::FirstPerson:
                    updateFirstPersonCamera(controller, transform, *inputState, deltaTime);
                    break;
                case CameraControllerComponent::Mode::ThirdPerson:
                    updateThirdPersonCamera(registry, controller, transform, *inputState, deltaTime);
                    break;
            }
        }

        // NOTE: Do NOT call inputState->beginFrame() here!
        // The app is responsible for managing input state lifecycle.
    }

private:
    // ─────────────────────────────────────────────────────────────
    // Free Camera - Unbound Flight
    // ─────────────────────────────────────────────────────────────

    void updateFreeCamera(
        CameraControllerComponent& ctrl,
        TransformComponent& transform,
        const InputStateComponent& input,
        float dt
    ) {
        // Initialize yaw/pitch from current transform on first update
        if (!ctrl.initialized) {
            ctrl.currentYaw = transform.rotation.y;
            ctrl.currentPitch = transform.rotation.x;
            ctrl.initialized = true;
        }

        // Mouse look (when captured or holding right mouse button)
        if (input.mouseCaptured || input.isMouseButtonDown(ctrl.mouseButtonLook)) {
            ctrl.currentYaw -= input.mouseDelta.x * ctrl.mouseSensitivity;
            ctrl.currentPitch -= input.mouseDelta.y * ctrl.mouseSensitivity;

            // Clamp pitch to prevent flipping
            ctrl.currentPitch = glm::clamp(ctrl.currentPitch, -1.5f, 1.5f);
        }

        // Calculate direction vectors from CURRENT yaw/pitch
        // In Vulkan: -Z is forward, +X is right, +Y is up
        float cosYaw = cos(ctrl.currentYaw);
        float sinYaw = sin(ctrl.currentYaw);
        float cosPitch = cos(ctrl.currentPitch);
        float sinPitch = sin(ctrl.currentPitch);

        glm::vec3 forward;
        glm::vec3 right;
        glm::vec3 up;

        if (ctrl.horizonLocked) {
            // Horizon-locked mode (default): standard FPS controls
            // WASD moves on horizontal plane, Q/E moves along world Y
            // Looking up/down doesn't affect movement direction
            forward = glm::vec3(-sinYaw, 0.0f, -cosYaw);  // Horizontal forward
            right = glm::vec3(cosYaw, 0.0f, -sinYaw);     // Horizontal right
            up = glm::vec3(0.0f, 1.0f, 0.0f);             // World up
        } else {
            // Flight mode: full 6DOF like Minecraft creative/spectator
            // W/S moves toward where you're looking (fly up when looking up)
            // Q/E moves along your local up axis
            forward = glm::vec3(
                -sinYaw * cosPitch,   // X
                sinPitch,             // Y
                -cosYaw * cosPitch    // Z
            );
            right = glm::vec3(cosYaw, 0.0f, -sinYaw);
            // Local up is perpendicular to forward and right
            up = glm::normalize(glm::cross(right, forward));
        }

        // Calculate target velocity from input
        glm::vec3 targetVelocity{0.0f};
        float speed = ctrl.moveSpeed;
        if (input.isKeyDown(ctrl.keySprint)) {
            speed *= ctrl.sprintMultiplier;
        }

        if (input.isKeyDown(ctrl.keyForward))  targetVelocity += forward * speed;
        if (input.isKeyDown(ctrl.keyBackward)) targetVelocity -= forward * speed;
        if (input.isKeyDown(ctrl.keyRight))    targetVelocity += right * speed;
        if (input.isKeyDown(ctrl.keyLeft))     targetVelocity -= right * speed;
        if (input.isKeyDown(ctrl.keyUp))       targetVelocity += up * speed;
        if (input.isKeyDown(ctrl.keyDown))     targetVelocity -= up * speed;

        // Smooth velocity
        ctrl.velocity = glm::mix(ctrl.velocity, targetVelocity,
                                 glm::clamp(ctrl.smoothing * dt, 0.0f, 1.0f));

        // Apply movement
        transform.position += ctrl.velocity * dt;
        transform.rotation = glm::vec3(ctrl.currentPitch, ctrl.currentYaw, 0.0f);
        transform.isDirty = true;
    }

    // ─────────────────────────────────────────────────────────────
    // Orbit Camera - Sacred Circumambulation
    // ─────────────────────────────────────────────────────────────

    void updateOrbitCamera(
        CameraControllerComponent& ctrl,
        TransformComponent& transform,
        const InputStateComponent& input,
        float dt
    ) {
        // On first frame, snap to orbit position immediately (no smoothing)
        // This prevents the "tilting" effect as camera settles into position
        bool firstFrame = !ctrl.initialized;
        if (!ctrl.initialized) {
            ctrl.initialized = true;
        }

        // Auto-rotate when enabled and no input
        bool hasInput = input.isMouseButtonDown(ctrl.mouseButtonLook) ||
                       input.isMouseButtonDown(ctrl.mouseButtonPan);

        if (ctrl.orbitAutoRotate && !hasInput) {
            ctrl.orbitYaw += ctrl.orbitAutoRotateSpeed * dt;
        }

        // Mouse rotation (right button drag)
        if (input.isMouseButtonDown(ctrl.mouseButtonLook)) {
            ctrl.orbitYaw -= input.mouseDelta.x * ctrl.mouseSensitivity;
            ctrl.orbitPitch -= input.mouseDelta.y * ctrl.mouseSensitivity;

            // Clamp pitch
            ctrl.orbitPitch = glm::clamp(ctrl.orbitPitch, ctrl.orbitMinPitch, ctrl.orbitMaxPitch);
        }

        // Mouse panning (middle button drag)
        if (input.isMouseButtonDown(ctrl.mouseButtonPan)) {
            glm::vec3 right = glm::normalize(glm::cross(
                glm::vec3(0.0f, 1.0f, 0.0f),
                transform.position - ctrl.orbitTarget
            ));
            glm::vec3 up{0.0f, 1.0f, 0.0f};

            float panSpeed = ctrl.orbitDistance * 0.001f;
            ctrl.orbitTarget += right * input.mouseDelta.x * panSpeed;
            ctrl.orbitTarget += up * input.mouseDelta.y * panSpeed;
        }

        // Scroll zoom
        ctrl.orbitDistance -= input.scrollDelta.y * ctrl.scrollSensitivity;
        ctrl.orbitDistance = glm::clamp(ctrl.orbitDistance, ctrl.orbitMinDistance, ctrl.orbitMaxDistance);

        // Keyboard zoom fallback
        if (input.isKeyDown(GLFW_KEY_UP))   ctrl.orbitDistance -= ctrl.scrollSensitivity * dt * 5.0f;
        if (input.isKeyDown(GLFW_KEY_DOWN)) ctrl.orbitDistance += ctrl.scrollSensitivity * dt * 5.0f;
        ctrl.orbitDistance = glm::clamp(ctrl.orbitDistance, ctrl.orbitMinDistance, ctrl.orbitMaxDistance);

        // Keyboard rotation fallback
        if (input.isKeyDown(GLFW_KEY_LEFT))  ctrl.orbitYaw += ctrl.rotationSpeed * dt;
        if (input.isKeyDown(GLFW_KEY_RIGHT)) ctrl.orbitYaw -= ctrl.rotationSpeed * dt;
        if (input.isKeyDown(GLFW_KEY_PAGE_UP))   ctrl.orbitPitch += ctrl.rotationSpeed * dt;
        if (input.isKeyDown(GLFW_KEY_PAGE_DOWN)) ctrl.orbitPitch -= ctrl.rotationSpeed * dt;
        ctrl.orbitPitch = glm::clamp(ctrl.orbitPitch, ctrl.orbitMinPitch, ctrl.orbitMaxPitch);

        // Calculate camera position on sphere
        float x = ctrl.orbitDistance * cos(ctrl.orbitPitch) * sin(ctrl.orbitYaw);
        float y = ctrl.orbitDistance * sin(ctrl.orbitPitch);
        float z = ctrl.orbitDistance * cos(ctrl.orbitPitch) * cos(ctrl.orbitYaw);

        glm::vec3 targetPosition = ctrl.orbitTarget + glm::vec3(x, y, z);

        // On first frame or if too far, snap to position immediately (no smoothing)
        // This prevents the "tilting" effect as camera settles into position
        float distToTarget = glm::length(targetPosition - transform.position);
        if (firstFrame || distToTarget > ctrl.orbitDistance * 0.5f) {
            // Snap to position immediately
            transform.position = targetPosition;
        } else {
            // Normal smooth interpolation
            transform.position = glm::mix(transform.position, targetPosition,
                                          glm::clamp(ctrl.smoothing * dt, 0.0f, 1.0f));
        }

        // Look at target - compute rotation to face the target
        // In Vulkan, -Z is forward, so we need to rotate to align -Z with direction
        glm::vec3 direction = glm::normalize(ctrl.orbitTarget - transform.position);

        // Yaw: rotation around Y axis to face horizontal direction
        // We want -Z to point toward (direction.x, 0, direction.z)
        transform.rotation.y = atan2(-direction.x, -direction.z);

        // Pitch: rotation around X axis to look up/down
        transform.rotation.x = asin(direction.y);

        transform.rotation.z = 0.0f;
        transform.isDirty = true;
    }

    // ─────────────────────────────────────────────────────────────
    // First Person Camera - Direct Embodiment
    // ─────────────────────────────────────────────────────────────

    void updateFirstPersonCamera(
        CameraControllerComponent& ctrl,
        TransformComponent& transform,
        const InputStateComponent& input,
        float dt
    ) {
        // Initialize yaw/pitch from current transform on first update
        if (!ctrl.initialized) {
            ctrl.currentYaw = transform.rotation.y;
            ctrl.currentPitch = transform.rotation.x;
            ctrl.initialized = true;
        }

        // Mouse look (always active when captured or holding right mouse)
        if (input.mouseCaptured || input.isMouseButtonDown(ctrl.mouseButtonLook)) {
            ctrl.currentYaw -= input.mouseDelta.x * ctrl.mouseSensitivity;
            ctrl.currentPitch -= input.mouseDelta.y * ctrl.mouseSensitivity;

            // Clamp pitch
            if (ctrl.constrainPitch) {
                ctrl.currentPitch = glm::clamp(ctrl.currentPitch, -ctrl.maxPitch, ctrl.maxPitch);
            }
        }

        // Calculate horizontal movement vectors (no vertical component)
        // In Vulkan: -Z is forward, so forward on XZ plane is (-sinYaw, 0, -cosYaw)
        float cosYaw = cos(ctrl.currentYaw);
        float sinYaw = sin(ctrl.currentYaw);

        glm::vec3 forward{-sinYaw, 0.0f, -cosYaw};
        glm::vec3 right{cosYaw, 0.0f, -sinYaw};

        // Target velocity
        glm::vec3 targetVelocity{0.0f};
        float speed = ctrl.moveSpeed;
        if (input.isKeyDown(ctrl.keySprint)) {
            speed *= ctrl.sprintMultiplier;
        }

        if (input.isKeyDown(ctrl.keyForward))  targetVelocity += forward * speed;
        if (input.isKeyDown(ctrl.keyBackward)) targetVelocity -= forward * speed;
        if (input.isKeyDown(ctrl.keyRight))    targetVelocity += right * speed;
        if (input.isKeyDown(ctrl.keyLeft))     targetVelocity -= right * speed;

        // Smooth velocity (only horizontal)
        glm::vec2 vel2D = glm::mix(
            glm::vec2(ctrl.velocity.x, ctrl.velocity.z),
            glm::vec2(targetVelocity.x, targetVelocity.z),
            glm::clamp(ctrl.smoothing * dt, 0.0f, 1.0f)
        );
        ctrl.velocity = glm::vec3(vel2D.x, 0.0f, vel2D.y);

        // Apply movement
        transform.position.x += ctrl.velocity.x * dt;
        transform.position.z += ctrl.velocity.z * dt;
        transform.position.y = ctrl.groundY + ctrl.eyeHeight;

        transform.rotation = glm::vec3(ctrl.currentPitch, ctrl.currentYaw, 0.0f);
        transform.isDirty = true;
    }

    // ─────────────────────────────────────────────────────────────
    // Third Person Camera - Witness Consciousness
    // ─────────────────────────────────────────────────────────────

    void updateThirdPersonCamera(
        entt::registry& registry,
        CameraControllerComponent& ctrl,
        TransformComponent& transform,
        const InputStateComponent& input,
        float dt
    ) {
        if (ctrl.followTarget == entt::null || !registry.valid(ctrl.followTarget)) {
            // Fallback to orbit mode if no valid target
            updateOrbitCamera(ctrl, transform, input, dt);
            return;
        }

        auto* targetTransform = registry.try_get<TransformComponent>(ctrl.followTarget);
        if (!targetTransform) return;

        // Initialize yaw/pitch from current transform on first update
        if (!ctrl.initialized) {
            ctrl.currentYaw = transform.rotation.y;
            ctrl.currentPitch = transform.rotation.x;
            ctrl.initialized = true;
        }

        // Mouse rotation around target
        if (input.isMouseButtonDown(ctrl.mouseButtonLook)) {
            ctrl.currentYaw -= input.mouseDelta.x * ctrl.mouseSensitivity;
            ctrl.currentPitch -= input.mouseDelta.y * ctrl.mouseSensitivity;
            ctrl.currentPitch = glm::clamp(ctrl.currentPitch, -0.5f, 1.2f);
        }

        // Calculate camera position behind target
        glm::vec3 targetPos = glm::vec3(targetTransform->worldMatrix[3]);

        // Rotate offset by yaw
        float offsetDist = glm::length(ctrl.followOffset);
        glm::vec3 rotatedOffset{
            sin(ctrl.currentYaw) * offsetDist * cos(ctrl.currentPitch),
            ctrl.followOffset.y + sin(ctrl.currentPitch) * offsetDist,
            cos(ctrl.currentYaw) * offsetDist * cos(ctrl.currentPitch)
        };

        glm::vec3 desiredPosition = targetPos + rotatedOffset;

        // Smooth follow
        transform.position = glm::mix(transform.position, desiredPosition,
                                      glm::clamp(ctrl.followLag * dt, 0.0f, 1.0f));

        // Look at target - compute rotation to face the target
        glm::vec3 lookTarget = targetPos + glm::vec3(0.0f, ctrl.followOffset.y * 0.5f, 0.0f);
        glm::vec3 direction = glm::normalize(lookTarget - transform.position);

        // Same fix as orbit camera: -Z is forward in Vulkan
        transform.rotation.y = atan2(-direction.x, -direction.z);
        transform.rotation.x = asin(direction.y);

        transform.rotation.z = 0.0f;
        transform.isDirty = true;
    }
};

// ═══════════════════════════════════════════════════════════════
// Component Registration Extension
// ═══════════════════════════════════════════════════════════════

inline void registerCameraControllerComponents(ComponentRegistry& registry) {
    registry.registerComponent<InputStateComponent>("InputState");
    registry.registerComponent<CameraControllerComponent>("CameraController");
}

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;

#include "PhysicsTestApp.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ═══════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════

PhysicsTestApp::PhysicsTestApp(const ApplicationConfig& config)
    : ApplicationBase(config)
{
}

// ═══════════════════════════════════════════════════════════════
// ApplicationBase Hooks
// ═══════════════════════════════════════════════════════════════

void PhysicsTestApp::registerSystems() {
    // Base systems (Transform, Camera, CameraController)
    ApplicationBase::registerSystems();

    // Physics — the star of the show
    m_physicsSystem = getScene().addSystem<ECS::PhysicsSystem>();
    m_physicsSystem->enabled = false;  // Start paused — press P to begin
}

void PhysicsTestApp::onInit() {
    // Camera — elevated position looking slightly down at the action
    createCamera(glm::vec3(0.0f, 8.0f, 20.0f), 60.f, 10.f, 0.1f, 500.f);

    auto& cameraTransform = getRegistry().get<ECS::TransformComponent>(getCameraEntity());
    cameraTransform.rotation = glm::vec3(-0.3f, 0.0f, 0.0f);  // Slight downward pitch
    cameraTransform.isDirty = true;

    // Physics scene
    createPhysicsScene();

    // Lights — warm sun + fill
    createDirectionalLight(
        glm::vec3(-0.5f, -1.0f, -0.3f),
        glm::vec3(1.0f, 0.975f, 0.95f),
        3.0f);
    createPointLight(
        glm::vec3(0.0f, 10.0f, 5.0f),
        glm::vec3(1.0f, 0.8f, 0.6f),
        3.0f, 25.0f);
}

void PhysicsTestApp::onPostInit() {
    getLogger().log(LogLevel::Info, "Physics bodies: %u", m_physicsSystem->getBodyCount());
}

void PhysicsTestApp::onKeyPressed(int keyCode) {
    switch (keyCode) {
        case GLFW_KEY_SPACE:
            spawnRandomObject();
            break;
        case GLFW_KEY_P:
            m_physicsSystem->enabled = !m_physicsSystem->enabled;
            getLogger().log(LogLevel::Info, "Physics: %s",
                            m_physicsSystem->enabled ? "PLAYING" : "PAUSED");
            break;
        case GLFW_KEY_R:
            resetScene();
            break;
    }
}

// ═══════════════════════════════════════════════════════════════
// Physics Scene Creation
// ═══════════════════════════════════════════════════════════════

void PhysicsTestApp::createPhysicsScene() {
    getLogger().log(LogLevel::Info, "Creating physics scene...");

    // Ground plane
    createGroundPlane(0.0f, 50.0f);

    // Falling boxes — stacked tower
    createBox(glm::vec3(0.0f, 2.0f, 0.0f),  glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.8f, 0.2f, 0.2f), 1.0f);
    createBox(glm::vec3(0.0f, 4.0f, 0.0f),  glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.2f, 0.8f, 0.2f), 1.0f);
    createBox(glm::vec3(0.0f, 6.0f, 0.0f),  glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.2f, 0.2f, 0.8f), 1.0f);
    createBox(glm::vec3(0.1f, 8.0f, 0.1f),  glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.8f, 0.8f, 0.2f), 1.0f);
    createBox(glm::vec3(-0.1f, 10.0f, -0.1f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.8f, 0.2f, 0.8f), 1.0f);

    // Falling spheres — off to the side
    createSphere(glm::vec3(5.0f, 5.0f, 0.0f), 0.5f, glm::vec3(0.9f, 0.9f, 0.1f), 1.0f);
    createSphere(glm::vec3(5.0f, 8.0f, 1.0f), 0.4f, glm::vec3(0.1f, 0.9f, 0.9f), 1.0f);
    createSphere(glm::vec3(5.0f, 12.0f, -1.0f), 0.6f, glm::vec3(0.9f, 0.5f, 0.1f), 2.0f);

    // Scattered boxes
    createBox(glm::vec3(-4.0f, 3.0f, 3.0f),  glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.6f, 0.3f, 0.1f), 0.5f);
    createBox(glm::vec3(-3.0f, 7.0f, -2.0f), glm::vec3(1.5f, 0.5f, 1.0f), glm::vec3(0.3f, 0.6f, 0.3f), 1.5f);

    getLogger().log(LogLevel::Info, "Physics scene created with %u bodies", m_physicsSystem->getBodyCount());
}

// ═══════════════════════════════════════════════════════════════
// Procedural Mesh Generation
// ═══════════════════════════════════════════════════════════════

Shoonyakasha::MeshComponent PhysicsTestApp::uploadMesh(
    const std::vector<StandardVertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    Shoonyakasha::MeshComponent mesh;
    auto& device = getDevice();

    // Upload vertex buffer
    VkDeviceSize vertexSize = sizeof(StandardVertex) * vertices.size();
    mesh.vertexBuffer = Shoonyakasha::GPUResourceFactory::createBuffer(
        device.getAllocator().getHandle(),
        vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    Shoonyakasha::GPUResourceFactory::uploadBuffer(
        device.getAllocator().getHandle(),
        device.getLogicalDevice(),
        device.getGraphicsQueue(),
        device.getCommandPool(),
        mesh.vertexBuffer,
        vertices.data(),
        vertexSize);

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.vertexStride = sizeof(StandardVertex);

    // Upload index buffer
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
    mesh.indexBuffer = Shoonyakasha::GPUResourceFactory::createBuffer(
        device.getAllocator().getHandle(),
        indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    Shoonyakasha::GPUResourceFactory::uploadBuffer(
        device.getAllocator().getHandle(),
        device.getLogicalDevice(),
        device.getGraphicsQueue(),
        device.getCommandPool(),
        mesh.indexBuffer,
        indices.data(),
        indexSize);

    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.indexType = Shoonyakasha::IndexType::UInt32;

    return mesh;
}

entt::entity PhysicsTestApp::createBox(const glm::vec3& pos, const glm::vec3& size,
                                        const glm::vec3& color, float mass)
{
    glm::vec3 h = size * 0.5f;  // half-extents

    // 24 vertices (4 per face, proper normals)
    std::vector<StandardVertex> vertices = {
        // Front face (+Z)
        {{ -h.x, -h.y,  h.z }, color, { 0, 0 }, {  0,  0,  1 }},
        {{  h.x, -h.y,  h.z }, color, { 1, 0 }, {  0,  0,  1 }},
        {{  h.x,  h.y,  h.z }, color, { 1, 1 }, {  0,  0,  1 }},
        {{ -h.x,  h.y,  h.z }, color, { 0, 1 }, {  0,  0,  1 }},
        // Back face (-Z)
        {{  h.x, -h.y, -h.z }, color, { 0, 0 }, {  0,  0, -1 }},
        {{ -h.x, -h.y, -h.z }, color, { 1, 0 }, {  0,  0, -1 }},
        {{ -h.x,  h.y, -h.z }, color, { 1, 1 }, {  0,  0, -1 }},
        {{  h.x,  h.y, -h.z }, color, { 0, 1 }, {  0,  0, -1 }},
        // Top face (+Y)
        {{ -h.x,  h.y,  h.z }, color, { 0, 0 }, {  0,  1,  0 }},
        {{  h.x,  h.y,  h.z }, color, { 1, 0 }, {  0,  1,  0 }},
        {{  h.x,  h.y, -h.z }, color, { 1, 1 }, {  0,  1,  0 }},
        {{ -h.x,  h.y, -h.z }, color, { 0, 1 }, {  0,  1,  0 }},
        // Bottom face (-Y)
        {{ -h.x, -h.y, -h.z }, color, { 0, 0 }, {  0, -1,  0 }},
        {{  h.x, -h.y, -h.z }, color, { 1, 0 }, {  0, -1,  0 }},
        {{  h.x, -h.y,  h.z }, color, { 1, 1 }, {  0, -1,  0 }},
        {{ -h.x, -h.y,  h.z }, color, { 0, 1 }, {  0, -1,  0 }},
        // Right face (+X)
        {{  h.x, -h.y,  h.z }, color, { 0, 0 }, {  1,  0,  0 }},
        {{  h.x, -h.y, -h.z }, color, { 1, 0 }, {  1,  0,  0 }},
        {{  h.x,  h.y, -h.z }, color, { 1, 1 }, {  1,  0,  0 }},
        {{  h.x,  h.y,  h.z }, color, { 0, 1 }, {  1,  0,  0 }},
        // Left face (-X)
        {{ -h.x, -h.y, -h.z }, color, { 0, 0 }, { -1,  0,  0 }},
        {{ -h.x, -h.y,  h.z }, color, { 1, 0 }, { -1,  0,  0 }},
        {{ -h.x,  h.y,  h.z }, color, { 1, 1 }, { -1,  0,  0 }},
        {{ -h.x,  h.y, -h.z }, color, { 0, 1 }, { -1,  0,  0 }},
    };

    // 36 indices (2 triangles per face)
    std::vector<uint32_t> indices;
    for (uint32_t face = 0; face < 6; face++) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    auto meshComp = uploadMesh(vertices, indices);
    auto& registry = getRegistry();
    auto entity = getScene().createEntity("Box_" + std::to_string(m_spawnCount++))
        .withTransform(pos)
        .build();

    registry.emplace<Shoonyakasha::MeshComponent>(entity, std::move(meshComp));

    auto& material = registry.emplace<Shoonyakasha::MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", glm::vec4(color, 1.0f));
    material.setParam("metallicFactor", 0.1f);
    material.setParam("roughnessFactor", 0.7f);

    auto& tag = registry.emplace<Shoonyakasha::RenderableTagComponent>(entity);
    tag.visible = true;

    // Physics — RigidBody + Collider + rebuild
    auto& rb = registry.emplace<ECS::RigidBodyComponent>(entity);
    rb.type = (mass > 0.0f) ? ECS::RigidBodyComponent::Dynamic : ECS::RigidBodyComponent::Static;
    rb.mass = mass;
    rb.drag = 0.05f;
    rb.angularDrag = 0.05f;

    auto& collider = registry.emplace<ECS::ColliderComponent>(entity);
    collider.shape = ECS::ColliderComponent::Box;
    collider.size = size;
    collider.friction = 0.6f;
    collider.restitution = 0.3f;

    m_physicsSystem->rebuildBody(registry, entity);

    m_physicsEntities.push_back(entity);
    return entity;
}

entt::entity PhysicsTestApp::createSphere(const glm::vec3& pos, float radius,
                                           const glm::vec3& color, float mass)
{
    const int stacks = 16;
    const int slices = 24;

    std::vector<StandardVertex> vertices;
    std::vector<uint32_t> indices;

    // Generate UV sphere vertices
    for (int i = 0; i <= stacks; i++) {
        float phi = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * glm::pi<float>() * static_cast<float>(j) / static_cast<float>(slices);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            glm::vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
            glm::vec3 position = normal * radius;
            glm::vec2 texCoord(
                static_cast<float>(j) / static_cast<float>(slices),
                static_cast<float>(i) / static_cast<float>(stacks));

            vertices.push_back({position, color, texCoord, normal});
        }
    }

    // Generate indices
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            uint32_t topLeft = i * (slices + 1) + j;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (i + 1) * (slices + 1) + j;
            uint32_t bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    auto meshComp = uploadMesh(vertices, indices);
    auto& registry = getRegistry();
    auto entity = getScene().createEntity("Sphere_" + std::to_string(m_spawnCount++))
        .withTransform(pos)
        .build();

    registry.emplace<Shoonyakasha::MeshComponent>(entity, std::move(meshComp));

    auto& material = registry.emplace<Shoonyakasha::MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", glm::vec4(color, 1.0f));
    material.setParam("metallicFactor", 0.3f);
    material.setParam("roughnessFactor", 0.4f);

    auto& tag = registry.emplace<Shoonyakasha::RenderableTagComponent>(entity);
    tag.visible = true;

    // Physics
    auto& rb = registry.emplace<ECS::RigidBodyComponent>(entity);
    rb.type = (mass > 0.0f) ? ECS::RigidBodyComponent::Dynamic : ECS::RigidBodyComponent::Static;
    rb.mass = mass;
    rb.drag = 0.05f;
    rb.angularDrag = 0.05f;

    auto& collider = registry.emplace<ECS::ColliderComponent>(entity);
    collider.shape = ECS::ColliderComponent::Sphere;
    collider.size = glm::vec3(radius);  // size.x = radius for sphere
    collider.friction = 0.5f;
    collider.restitution = 0.5f;

    m_physicsSystem->rebuildBody(registry, entity);

    m_physicsEntities.push_back(entity);
    return entity;
}

entt::entity PhysicsTestApp::createGroundPlane(float y, float size)
{
    glm::vec3 color(0.4f, 0.45f, 0.4f);  // Neutral gray-green

    // A large flat quad as visual representation
    std::vector<StandardVertex> vertices = {
        {{ -size, 0,  size }, color, { 0, 0 }, { 0, 1, 0 }},
        {{  size, 0,  size }, color, { size, 0 }, { 0, 1, 0 }},
        {{  size, 0, -size }, color, { size, size }, { 0, 1, 0 }},
        {{ -size, 0, -size }, color, { 0, size }, { 0, 1, 0 }},
    };

    std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };

    auto meshComp = uploadMesh(vertices, indices);
    auto& registry = getRegistry();
    auto entity = getScene().createEntity("GroundPlane")
        .withTransform(glm::vec3(0.0f, y, 0.0f))
        .build();

    registry.emplace<Shoonyakasha::MeshComponent>(entity, std::move(meshComp));

    auto& material = registry.emplace<Shoonyakasha::MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", glm::vec4(color, 1.0f));
    material.setParam("metallicFactor", 0.0f);
    material.setParam("roughnessFactor", 0.9f);

    auto& tag = registry.emplace<Shoonyakasha::RenderableTagComponent>(entity);
    tag.visible = true;

    // Static rigid body with plane collider
    auto& rb = registry.emplace<ECS::RigidBodyComponent>(entity);
    rb.type = ECS::RigidBodyComponent::Static;
    rb.mass = 0.0f;

    auto& collider = registry.emplace<ECS::ColliderComponent>(entity);
    collider.shape = ECS::ColliderComponent::Plane;
    collider.friction = 0.8f;
    collider.restitution = 0.1f;

    m_physicsSystem->rebuildBody(registry, entity);

    m_physicsEntities.push_back(entity);
    getLogger().log(LogLevel::Info, "Created ground plane at y=%.1f (%.0f x %.0f)", y, size * 2, size * 2);
    return entity;
}

// ═══════════════════════════════════════════════════════════════
// Spawning & Reset
// ═══════════════════════════════════════════════════════════════

void PhysicsTestApp::spawnRandomObject() {
    auto& registry = getRegistry();
    auto& camTransform = registry.get<ECS::TransformComponent>(getCameraEntity());

    // Spawn in front of camera
    glm::vec3 forward = camTransform.getForward();
    glm::vec3 spawnPos = camTransform.position + forward * 3.0f;
    glm::vec3 color = randomColor();

    entt::entity entity;
    if (m_spawnCount % 2 == 0) {
        entity = createBox(spawnPos, glm::vec3(0.6f), color, 1.0f);
    } else {
        entity = createSphere(spawnPos, 0.3f, color, 1.0f);
    }

    // Launch forward with impulse
    m_physicsSystem->addImpulse(registry, entity, forward * 12.0f);

    getLogger().log(LogLevel::Info, "Spawned object #%d (total bodies: %u)",
                    m_spawnCount, m_physicsSystem->getBodyCount());
}

void PhysicsTestApp::resetScene() {
    auto& registry = getRegistry();

    // Destroy all physics entities (EnTT on_destroy signal cleans up Bullet bodies)
    for (auto entity : m_physicsEntities) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
    m_physicsEntities.clear();
    m_spawnCount = 0;

    // Recreate the scene from scratch, paused
    m_physicsSystem->enabled = false;
    createPhysicsScene();

    getLogger().log(LogLevel::Info, "Scene reset (PAUSED) — %u bodies", m_physicsSystem->getBodyCount());
}

glm::vec3 PhysicsTestApp::randomColor() {
    std::uniform_real_distribution<float> dist(0.2f, 0.9f);
    return glm::vec3(dist(m_rng), dist(m_rng), dist(m_rng));
}

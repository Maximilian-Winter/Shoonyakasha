# PhysicsAPI

Access: `engine.getPhysics()`. [Other language reference](../python/physics.md).

Include `Facade/PhysicsAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/PhysicsAPI.h).

Controls the Bullet-backed ECS physics system. Obtain it through the engine and configure it in init or later. The facade exists before initialization, but setters do not buffer pending configuration.

## World and body contract

Defaults are disabled, gravity `(0, -9.81, 0)`, fixed step `1/60` seconds, and maximum 10 substeps. The `enabled` switch controls stepping; do not assume it prevents every body mutation API from being called.

Forces, impulses, and velocity operations require an entity with a live physics body. Missing bodies produce no effect or zero-valued velocity results. Rebuild after changing native collider/body settings; body count is the number tracked by the native system.

C++ native ECS access is needed to configure body mass/type and collider dimensions. Python name-based component addition creates defaults but does not offer those typed fields. There is no facade raycast, constraint, or collision-event API. Native Mesh colliders currently fall back to a box.

See the [physics guide](../../guides/physics.md) for setup, supported shapes, and the native C++ example.

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
PhysicsAPI();
~PhysicsAPI();
bool isEnabled() const;
void setEnabled(bool enabled);
void setGravity(const glm::vec3& gravity);
glm::vec3 getGravity() const;
void setFixedTimeStep(float timeStep);
float getFixedTimeStep() const;
void setMaxSubSteps(int maxSubSteps);
int getMaxSubSteps() const;
void addForce(EntityHandle entity, const glm::vec3& force);
void addImpulse(EntityHandle entity, const glm::vec3& impulse);
void addTorqueImpulse(EntityHandle entity, const glm::vec3& torque);
void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity);
glm::vec3 getLinearVelocity(EntityHandle entity) const;
void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity);
glm::vec3 getAngularVelocity(EntityHandle entity) const;
void rebuildBody(EntityHandle entity);
uint32_t getBodyCount() const;
```

<!-- END SOURCE API -->

#pragma once

// ComponentDeserializer provides a factory function for creating
// ECS components from JSON configuration. This enables entities
// to be defined in JSON scene files (e.g., config/*.jsonc).

#include <iostream>
#include "../ecs/entity.hpp"
#include "camera.hpp"
#include "mesh-renderer.hpp"
#include "free-camera-controller.hpp"
#include "movement.hpp"
#include "light.hpp"

namespace our
{

    // Factory function that creates the appropriate ECS component
    // based on the "type" field in the JSON data.
    //
    // Supported component types in JSON:
    //   - "Camera": CameraComponent for viewpoint
    //   - "MeshRenderer": MeshRendererComponent for renderable geometry
    //   - "FreeCameraController": FreeCameraControllerComponent for input-driven camera movement
    //   - "Movement": MovementComponent for entity movement/animation
    //   - "Light": LightComponent for scene lighting
    //
    // After creating the component, deserialize() is called to parse
    // type-specific properties from the JSON.
    inline void deserializeComponent(const nlohmann::json &data, Entity *entity)
    {
        std::string type = data.value("type", "");
        std::cout << "Loading component: '" << type << "'\n";

        Component *component = nullptr;

        // Factory: instantiate the correct component type
        // Each component class provides getID() returning its type string
        if (type == CameraComponent::getID())
        {
            component = entity->addComponent<CameraComponent>();
        }
        else if (type == FreeCameraControllerComponent::getID())
        {
            component = entity->addComponent<FreeCameraControllerComponent>();
        }
        else if (type == MovementComponent::getID())
        {
            component = entity->addComponent<MovementComponent>();
        }
        else if (type == MeshRendererComponent::getID())
        {
            component = entity->addComponent<MeshRendererComponent>();
        }
        else if (type == LightComponent::getID())
        {
            component = entity->addComponent<LightComponent>();
        }

        // Parse component-specific properties from JSON
        // Each component knows how to deserialize its own fields
        if (component)
            component->deserialize(data);
    }

}
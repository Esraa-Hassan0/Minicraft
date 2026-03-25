#include "entity.hpp"
#include "../deserialize-utils.hpp"
#include "../components/component-deserializer.hpp"

#include <glm/gtx/euler_angles.hpp>

namespace our
{

    // This function returns the transformation matrix from the entity's local space to the world space
    // Remember that you can get the transformation matrix from this entity to its parent from "localTransform"
    // To get the local to world matrix, you need to combine this entities matrix with its parent's matrix and
    // its parent's parent's matrix and so on till you reach the root.
    glm::mat4 Entity::getLocalToWorldMatrix() const
    {
        // TODO: (Req 8) Write this function
        // Start with this entity's own local transform matrix
        // This converts from local space → parent's local space
        glm::mat4 localToWorld = localTransform.toMat4();

        // Walk up the parent chain until we reach the root
        const Entity *curr = parent;
        while (curr != nullptr)
        {
            // Left-multiply by the parent's transform so that:
            // World = Root × ... × Grandparent × Parent × Local
            // This ensures parent transforms are applied AFTER child transforms,
            localToWorld = curr->localTransform.toMat4() * localToWorld; // column major 
            curr = curr->parent; // Move one level up the hierarchy
        }
        return localToWorld;
    }

    // Deserializes the entity data and components from a json object
    void Entity::deserialize(const nlohmann::json &data)
    {
        if (!data.is_object())
            return;
        name = data.value("name", name);
        localTransform.deserialize(data);
        if (data.contains("components"))
        {
            if (const auto &components = data["components"]; components.is_array())
            {
                for (auto &component : components)
                {
                    deserializeComponent(component, this);
                }
            }
        }
    }

}
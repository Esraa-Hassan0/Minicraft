#pragma once

#include "../ecs/component.hpp"
#include <glm/glm.hpp>
#include <string>

namespace our {

    // Axis-Aligned Bounding Box (AABB) component for collision detection
    class AABBColliderComponent : public Component {
    public:
        glm::vec3 center = glm::vec3(0.0f);      // Center of the bounding box (relative to entity)
        glm::vec3 halfSize = glm::vec3(0.5f);    // Half extents (width/2, height/2, depth/2)
        
        // Whether this collider causes physical collisions (blocks movement)
        bool isPhysical = true;
        
        // Whether this is a trigger (detects overlaps but doesn't block)
        bool isTrigger = false;
        
        // Layer for collision filtering
        int collisionLayer = 0;
        int collisionMask = -1; // -1 means collide with everything

        // Get the minimum corner of the AABB in world space
        glm::vec3 getMinCorner(const glm::vec3& entityPosition) const {
            return entityPosition + center - halfSize;
        }

        // Get the maximum corner of the AABB in world space
        glm::vec3 getMaxCorner(const glm::vec3& entityPosition) const {
            return entityPosition + center + halfSize;
        }

        // Check if this AABB overlaps with another AABB
        bool overlaps(const glm::vec3& thisPos, const AABBColliderComponent& other, const glm::vec3& otherPos) const {
            glm::vec3 thisMin = getMinCorner(thisPos);
            glm::vec3 thisMax = getMaxCorner(thisPos);
            glm::vec3 otherMin = other.getMinCorner(otherPos);
            glm::vec3 otherMax = other.getMaxCorner(otherPos);

            return (thisMin.x < otherMax.x && thisMax.x > otherMin.x) &&
                   (thisMin.y < otherMax.y && thisMax.y > otherMin.y) &&
                   (thisMin.z < otherMax.z && thisMax.z > otherMin.z);
        }

        // Get the penetration depth when overlapping
        glm::vec3 getPenetration(const glm::vec3& thisPos, const AABBColliderComponent& other, const glm::vec3& otherPos) const {
            glm::vec3 thisMin = getMinCorner(thisPos);
            glm::vec3 thisMax = getMaxCorner(thisPos);
            glm::vec3 otherMin = other.getMinCorner(otherPos);
            glm::vec3 otherMax = other.getMaxCorner(otherPos);

            glm::vec3 penetration(0.0f);

            // X-axis penetration
            float leftPenetration = thisMax.x - otherMin.x;
            float rightPenetration = otherMax.x - thisMin.x;
            penetration.x = (leftPenetration < rightPenetration) ? -leftPenetration : rightPenetration;

            // Y-axis penetration
            float bottomPenetration = thisMax.y - otherMin.y;
            float topPenetration = otherMax.y - thisMin.y;
            penetration.y = (bottomPenetration < topPenetration) ? -bottomPenetration : topPenetration;

            // Z-axis penetration
            float backPenetration = thisMax.z - otherMin.z;
            float frontPenetration = otherMax.z - thisMin.z;
            penetration.z = (backPenetration < frontPenetration) ? -backPenetration : frontPenetration;

            return penetration;
        }

        static std::string getID() { return "AABBCollider"; }

        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) return;

            if (data.contains("center") && data["center"].is_array() && data["center"].size() >= 3) {
                center = glm::vec3(data["center"][0], data["center"][1], data["center"][2]);
            }

            if (data.contains("halfSize") && data["halfSize"].is_array() && data["halfSize"].size() >= 3) {
                halfSize = glm::vec3(data["halfSize"][0], data["halfSize"][1], data["halfSize"][2]);
            }

            isPhysical = data.value("isPhysical", isPhysical);
            isTrigger = data.value("isTrigger", isTrigger);
            collisionLayer = data.value("collisionLayer", collisionLayer);
            collisionMask = data.value("collisionMask", collisionMask);
        }
    };

}

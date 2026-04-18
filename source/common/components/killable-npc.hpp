#pragma once

#include "../ecs/component.hpp"
#include <glm/glm.hpp>

namespace our {

    // Marks an NPC that can be removed by the player with the primary attack ray.
    // Simple wander on the XZ plane; Y is snapped to terrain each frame.
    class KillableNpcComponent : public Component {
    public:
        float moveSpeed = 1.05f;
        /// Time until the next random direction change (seconds).
        float wanderTimerSec = 1.0f;
        /// Per-entity retarget interval range, read from config.
        float wanderRetargetMinSec = 1.2f;
        float wanderRetargetMaxSec = 3.0f;
        /// Horizontal velocity direction (x, z), not necessarily normalized between updates.
        glm::vec2 wanderDirXZ{1.0f, 0.0f};

        static std::string getID() { return "KillableNpc"; }

        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) {
                return;
            }
            moveSpeed = data.value("moveSpeed", moveSpeed);
            wanderTimerSec = data.value("wanderTimerSec", wanderTimerSec);
            wanderRetargetMinSec = data.value("wanderRetargetMinSec", wanderRetargetMinSec);
            wanderRetargetMaxSec = data.value("wanderRetargetMaxSec", wanderRetargetMaxSec);
        }
    };

} // namespace our

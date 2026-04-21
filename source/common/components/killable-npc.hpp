#pragma once
#include "../ecs/component.hpp"
#include <string>

namespace our {
    class KillableNPCComponent : public Component {
    public:
        int foodReward = 1;
        std::string npcType = "default";

        static std::string getID() { return "KillableNPCComponent"; }

        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) return;
            foodReward = data.value("foodReward", foodReward);
            npcType = data.value("npcType", npcType);
        }
    };
}
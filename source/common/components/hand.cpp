#include "hand.hpp"

namespace our {
    
    // Deserializes HandComponent from JSON configuration
    void HandComponent::deserialize(const nlohmann::json& data) {
        if (!data.is_object())
            return;
        
        // Parse base position
        if (data.contains("basePosition") && data["basePosition"].is_array() && data["basePosition"].size() == 3) {
            basePosition = glm::vec3(
                data["basePosition"][0].get<float>(),
                data["basePosition"][1].get<float>(),
                data["basePosition"][2].get<float>()
            );
        }
        
        // Parse base rotation (in degrees, convert to radians)
        if (data.contains("baseRotation") && data["baseRotation"].is_array() && data["baseRotation"].size() == 3) {
            baseRotation = glm::vec3(
                glm::radians(data["baseRotation"][0].get<float>()),
                glm::radians(data["baseRotation"][1].get<float>()),
                glm::radians(data["baseRotation"][2].get<float>())
            );
        }
        
        // Parse interaction parameters
        interactionRange = data.value("interactionRange", 10.0f);
        idleSwayAmplitude = data.value("idleSwayAmplitude", 0.01f);
        hitForwardThrust = data.value("hitForwardThrust", 0.15f);
        
        // Initialize current state to base state
        currentPosition = basePosition;
        currentRotation = baseRotation;
    }
}

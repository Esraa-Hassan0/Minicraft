#include "light.hpp"
#include <glm/glm.hpp>

namespace our {

    // Deserializes LightComponent from JSON configuration.
    // Expected JSON fields:
    //   - lightType: "directional", "point", or "spot" (default: "point")
    //   - color: [r, g, b] RGB values (default: white)
    //   - ambient: [r, g, b] ambient contribution (default: dark gray)
    //   - attenConstant, attenLinear, attenQuadratic: distance attenuation
    //   - innerCutoff, outerCutoff: spot light angles in degrees
    void LightComponent::deserialize(const nlohmann::json& data) {
        // Ensure we have valid JSON object
        if (!data.is_object())
            return;

        // Parse light type from string
        // "directional" = sun-like parallel rays
        // "point" = omni-directional from a position
        // "spot" = cone-shaped directed light
        std::string t = data.value("lightType", "point");
        if (t == "directional") type = LightType::DIRECTIONAL;
        else if (t == "spot") type = LightType::SPOT;
        else type = LightType::POINT;

        // Parse RGB color array [r, g, b]
        if (data.contains("color") && data["color"].is_array()) {
            color = glm::vec3(data["color"][0].get<float>(), data["color"][1].get<float>(), data["color"][2].get<float>());
        } else {
            color = glm::vec3{1.0f, 1.0f, 1.0f};
        }

        // Parse ambient contribution (minimum illumination level)
        if (data.contains("ambient") && data["ambient"].is_array()) {
            ambient = glm::vec3(data["ambient"][0].get<float>(), data["ambient"][1].get<float>(), data["ambient"][2].get<float>());
        } else {
            ambient = glm::vec3{0.1f, 0.1f, 0.1f};
        }

        // Parse distance attenuation coefficients (used for point/spot lights)
        // Default values approximate physically-based falloff
        attenConstant  = data.value("attenConstant",  1.0f);
        attenLinear    = data.value("attenLinear",    0.09f);
        attenQuadratic = data.value("attenQuadratic", 0.032f);

        // Parse spot light cutoff angles in degrees (only used for spot lights)
        innerCutoffDeg = data.value("innerCutoff", 12.5f);
        outerCutoffDeg = data.value("outerCutoff", 17.5f);
    }
}
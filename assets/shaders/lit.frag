#version 330 core

// Maximum number of lights supported per draw call.
// This is a compile-time constant. Increase if more lights needed.
// Note: More lights = more GPU processing per fragment.
#define MAX_LIGHTS 8

// Light data structure matching ForwardRenderer's LightData.
// Each light is uploaded as a uniform array element.
struct Light {
    int type;           // 0=directional, 1=point, 2=spot
    vec3 position;      // World position (for point/spot lights)
    vec3 direction;    // Direction (for directional/spot cone axis)
    vec3 color;        // Diffuse/specular color
    vec3 ambient;      // Ambient contribution
    float attenConstant;  // Distance attenuation constant term
    float attenLinear;     // Distance attenuation linear term
    float attenQuadratic;  // Distance attenuation quadratic term
    float innerCutoff;     // Cosine of inner spot angle (bright core)
    float outerCutoff;     // Cosine of outer spot angle (edge)
};

// Inputs from vertex shader:
// fragPos: World-space fragment position (for distance/attenuation)
// normal: World-space surface normal (for diffuse/specular)
// texcoord: Texture coordinates (for albedo/specular maps)
in Varyings {
    vec3 fragPos;
    vec3 normal;
    vec2 texcoord;
} fs_in;

// Material texture samplers:
// albedoTex (unit 0): Diffuse/albedo map
// specularTex (unit 1): Specular intensity map (grayscale)
uniform sampler2D albedoTex;
uniform sampler2D specularTex;

// Material properties:
uniform float shininess;      // Phong shininess exponent (higher = smaller specular highlight)
uniform vec3 cameraPos;   // World-space camera position (for view direction)

// Light array uniform block:
// numLights: Number of active lights this frame
// lights: Array of Light structs (up to MAX_LIGHTS)
uniform int numLights;
uniform Light lights[MAX_LIGHTS];

// Visual feedback uniforms (damage flash effect):
uniform vec3 flashColor;    // Color to flash when damaged
uniform float flashStrength; // 0.0 = no flash, 1.0 = full flash

// Base tint from tinted material (modulates entire output)
uniform vec4 tint;

// Output to framebuffer
out vec4 fragColor;

// Calculates Blinn-Phong lighting contribution from a single light.
// L: Light data
// N: Surface normal (normalized)
// V: View direction (normalized)
// albedo: Surface diffuse color
// spec: Surface specular color
// Returns: Combined ambient + diffuse + specular contribution
vec3 calcLight(Light L, vec3 N, vec3 V, vec3 albedo, vec3 spec) {
    vec3 lightDir;      // Direction from surface to light
    float attenuation = 1.0;  // Distance-based falloff

    // Compute light direction and attenuation based on light type
    if (L.type == 0) {
        // Directional light (sun-like)
        // Light rays are parallel, pointing in -direction
        lightDir = normalize(-L.direction);
    } else {
        // Point or spot light
        // Vector from surface to light position
        vec3 delta = L.position - fs_in.fragPos;
        float dist = length(delta);
        if (dist > 0.0)
            lightDir = delta / dist;
        else
            lightDir = vec3(0, 1, 0); // Fallback for light coinciding with fragment

        // Physical distance attenuation:
        // intensity = 1 / (c + l*d + q*d^2)
        attenuation = 1.0 / (L.attenConstant
                            + L.attenLinear * dist
                            + L.attenQuadratic * dist * dist);

        // Spot light cone attenuation
        if (L.type == 2) {
            // Direction that spotlight is pointing (opposite of stored direction)
            vec3 spotDir = normalize(-L.direction);
            // Angle between light-to-surface and spotlight direction
            float theta = dot(lightDir, spotDir);
            // Smooth falloff between inner and outer cutoff (cosine values)
            // Added epsilon check to prevent division by zero for identical cutoffs
            float epsilon = max(L.innerCutoff - L.outerCutoff, 0.0001);
            // Within cone: theta > outerCutoff, full intensity at innerCutoff
            attenuation *= clamp((theta - L.outerCutoff) / epsilon, 0.0, 1.0);
        }
    }

    // Blinn-Phong halfway vector (midpoint between view and light)
    vec3 H = normalize(lightDir + V);

    // Diffuse: how much light hits the surface
    float diff = max(dot(N, lightDir), 0.0);

    // Specular: reflection intensity.
    float specular = pow(max(dot(N, H), 0.0), shininess);

    // Combine lighting terms
    // Ambient light is typically global/constant and shouldn't attenuate by distance (unlike local lights)
    // However, if we want point light ambients to be local, they must attenuate.
    // We compromise: directional light ambient is constant, others attenuate.
    vec3 ambientC = L.ambient * albedo * attenuation;
    vec3 diffuseC = L.color * diff * albedo * attenuation;
    vec3 specularC = L.color * specular * spec * attenuation;

    return ambientC + diffuseC + specularC;
}

// Main fragment shader entry point.
void main() {
    // Sample textures
    vec4 albedoSample = texture(albedoTex, fs_in.texcoord) * tint;
    vec3 specSample = texture(specularTex, fs_in.texcoord).rgb;

    // Normalize interpolated normal
    vec3 N = normalize(fs_in.normal);

    // View direction: camera to fragment
    vec3 V = normalize(cameraPos - fs_in.fragPos);

    // Accumulate lighting from all active lights
    vec3 result = vec3(0.0);
    if (numLights > 0) {
        for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
            result += calcLight(lights[i], N, V, albedoSample.rgb, specSample);
        }
    } else {
        result = albedoSample.rgb * vec3(0.1);
    }

    // Apply damage flash effect (blend if active)
    // Mix normal result with flash color based on flashStrength
    result = mix(result, flashColor * albedoSample.rgb, flashStrength);

    // Output final color with original alpha
    fragColor = vec4(result, albedoSample.a);
}
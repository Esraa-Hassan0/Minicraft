#version 330

// The texture holding the scene pixels
uniform sampler2D tex;
// The depth texture for fog calculation
uniform sampler2D depthTex;

// Camera near and far planes for linear depth conversion
uniform float cameraNear;
uniform float cameraFar;

// Fog parameters
uniform vec3 fogColorDay = vec3(0.7, 0.8, 0.9);
uniform vec3 fogColorNight = vec3(0.2, 0.25, 0.25);
uniform float fogDensity = 0.0008;
uniform bool enableFog = true;
uniform bool isNight = false;

in vec2 tex_coord;

out vec4 frag_color;

// Convert non-linear depth buffer value to linear depth
float linearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0;
    return (2.0 * cameraNear * cameraFar) / (cameraFar + cameraNear - z * (cameraFar - cameraNear));
}

void main()
{
    // Get scene color
    vec4 color = texture(tex, tex_coord);

    // Get depth value from depth texture
    float depth = texture(depthTex, tex_coord).r;

    // Convert to linear depth (distance from camera)
    float linearDepth = linearizeDepth(depth);

    // Calculate exponential fog factor
    float fogFactor = enableFog ? (1.0 - exp(-fogDensity * linearDepth)) : 0.0;
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    // Choose fog color based on day/night
    vec3 currentFogColor = isNight ? fogColorNight : fogColorDay;

    // Mix scene color with fog color based on fog factor
    vec3 finalColor = mix(color.rgb, currentFogColor, fogFactor);

    frag_color = vec4(finalColor, color.a);
}
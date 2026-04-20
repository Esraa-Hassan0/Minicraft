#version 330 core

// Vertex attributes matching our Vertex struct layout:
// Location 0: position (vec3) - local space vertex position
// Location 1: color (vec4) - vertex color (unused in lit shader, kept for compatibility)
// Location 2: tex_coord (vec2) - texture coordinates
// Location 3: normal (vec3) - surface normal for lighting calculations
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 tex_coord;
layout(location = 3) in vec3 normal;

// Outputs to fragment shader:
// fragPos: World-space position of fragment (for light distance calculations)
// normal: Transformed normal in world space (for diffuse/specular)
// texcoord: Texture coordinates for albedo and specular maps
out Varyings {
    vec3 fragPos;    // World-space position
    vec3 normal;     // World-space normal
    vec2 texcoord;   // Texture coordinates
    vec4 fragPosLightSpace; // Position in directional light clip space
} vs_out;

// Uniforms:
// transform: Model-View-Projection matrix (for clip-space position)
// model: Model matrix only (for world-space position)
// normalMatrix: transpose(inverse(mat3(model))) for proper normal transformation
uniform mat4 transform;
uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 lightSpaceMatrix;

void main() {
    // Transform position to world space (for light calculations)
    vec4 worldPos = model * vec4(position, 1.0);
    vs_out.fragPos = worldPos.xyz;

    // Transform normal to world space using normal matrix
    // Normal matrix is inverse-transpose of model to handle non-uniform scaling
    vs_out.normal = normalMatrix * normal;

    // Pass texture coordinates to fragment shader
    vs_out.texcoord = tex_coord;

    // Used by the fragment shader to sample the directional shadow map
    vs_out.fragPosLightSpace = lightSpaceMatrix * worldPos;

    // Transform to clip space for rendering
    gl_Position = transform * vec4(position, 1.0);
}
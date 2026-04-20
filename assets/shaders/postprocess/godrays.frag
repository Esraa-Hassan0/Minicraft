#version 330

uniform sampler2D tex;
uniform sampler2D depthTex;

uniform vec3 sunScreenPos;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float sunDensity;
uniform float sunWeight;
uniform float sunDecay;
uniform float cameraNear;
uniform float cameraFar;
uniform int isUnderwater;

in vec2 tex_coord;
out vec4 frag_color;

#define NUM_SAMPLES 64

float linearizeDepth(float depthValue)
{
    float z = depthValue * 2.0 - 1.0;
    return (2.0 * cameraNear * cameraFar) / (cameraFar + cameraNear - z * (cameraFar - cameraNear));
}

void main()
{
    vec2 deltaTexCoord = tex_coord - sunScreenPos.xy;
    deltaTexCoord *= 1.0 / float(NUM_SAMPLES) * sunDensity;

    vec2 coord = tex_coord;
    float illuminationDecay = 1.0;
    vec3 color = vec3(0.0);

    for(int i = 0; i < NUM_SAMPLES; i++)
    {
        coord -= deltaTexCoord;
        vec3 sampleColor = texture(tex, coord).rgb;

        sampleColor *= illuminationDecay * sunWeight;
        color += sampleColor;
        illuminationDecay *= sunDecay;
    }

    vec4 originalColor = texture(tex, tex_coord);
    color *= sunColor * sunIntensity;

    vec3 finalColor = color + originalColor.rgb;

    if (isUnderwater != 0)
    {
        float depthValue = texture(depthTex, tex_coord).r;
        float linearDepth = linearizeDepth(depthValue);

        // Daytime underwater look: bright blue haze + aggressive distance extinction.
        float depthFog = 1.0 - exp(-linearDepth * 0.22);
        float upwardHaze = smoothstep(0.35, 1.0, tex_coord.y) * 0.28;
        float fogAmount = clamp(depthFog + upwardHaze, 0.0, 0.97);

        vec3 waterFogColor = vec3(0.18, 0.50, 0.72);
        vec3 attenuatedScene = finalColor * vec3(0.72, 0.86, 0.96);
        finalColor = mix(attenuatedScene, waterFogColor, fogAmount);

        // Keep some sunlight influence while avoiding harsh god rays underwater.
        finalColor = mix(finalColor, finalColor + color * 0.08, 0.55);
    }

    frag_color = vec4(finalColor, 1.0);
}
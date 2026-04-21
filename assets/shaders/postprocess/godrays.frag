#version 330

uniform sampler2D tex;

uniform vec3 sunScreenPos;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float sunDensity;
uniform float sunWeight;
uniform float sunDecay;
uniform bool isNight;

in vec2 tex_coord;
out vec4 frag_color;

#define NUM_SAMPLES 64

void main()
{
    vec4 originalColor = texture(tex, tex_coord);
    if (isNight || sunIntensity <= 0.0) {
        frag_color = originalColor;
        return;
    }

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

    color *= sunColor * sunIntensity;

    frag_color = vec4(color + originalColor.rgb, 1.0);
}
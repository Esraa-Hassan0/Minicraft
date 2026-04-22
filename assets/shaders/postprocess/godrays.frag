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
        
        // FIX 1: Prevent sampling outside the screen.
        // Without this, the shader wraps around to the other side of the screen and causes full-screen flashes.
        if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
            continue; 
        }

        vec3 sampleColor = texture(tex, coord).rgb;

        // FIX 2: Brightness Threshold
        // Calculate how bright the pixel is using standard luminance math.
        float brightness = dot(sampleColor, vec3(0.299, 0.587, 0.114));
        
        // Only allow very bright pixels (like the sky or sun) to cast rays. 
        // Anything darker than 0.7 gets zeroed out, preventing colored terrain from smearing.
        if (brightness < 0.7) {
            sampleColor = vec3(0.0);
        }

        sampleColor *= illuminationDecay * sunWeight;
        color += sampleColor;
        illuminationDecay *= sunDecay;
    }
    float godrayMasterVolume = 0.3;

    color *= sunColor * sunIntensity * godrayMasterVolume;

    frag_color = vec4(color + originalColor.rgb, 1.0);
}
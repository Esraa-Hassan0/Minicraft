#version 330 core

// render horizontal pass first (horizontal=true) into FBO A,
// then render vertical pass (horizontal=false) into FBO B.
// Repeat N times for a stronger blur.

in  vec2 tex_coord;
out vec4 frag_color;

uniform sampler2D tex;
uniform vec2      texelSize;   // vec2(1.0/width, 1.0/height)
uniform bool      horizontal;

// 9-tap Gaussian kernel (sum = 1)
const float weight[5] = float[](
    0.227027,   // centre
    0.194595,   // ±1
    0.121622,   // ±2
    0.054054,   // ±3
    0.016216    // ±4
);

void main() {
    vec2 step = horizontal
        ? vec2(texelSize.x, 0.0)
        : vec2(0.0, texelSize.y);

    vec3 result = texture(tex, tex_coord).rgb * weight[0];

    for (int i = 1; i < 5; ++i) {
        vec2 off = step * float(i);
        result += texture(tex, tex_coord + off).rgb * weight[i];
        result += texture(tex, tex_coord - off).rgb * weight[i];
    }

    frag_color = vec4(result, 1.0);
}

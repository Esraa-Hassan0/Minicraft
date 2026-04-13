#version 330

// The texture holding the scene pixels
uniform sampler2D tex;

// Read "assets/shaders/fullscreen.vert" to know what "tex_coord" holds;
in vec2 tex_coord;

out vec4 frag_color;

// Vignette is a postprocessing effect that darkens the corners of the screen
// to grab the attention of the viewer towards the center of the screen
// Req 11: this shader runs in the fullscreen postprocess pass.

void main(){
    vec2 ndc = tex_coord * 2.0 - 1.0;
    // Squared radius from center (cheap and good enough for vignette falloff).
    float l2 = dot(ndc, ndc);

    vec4 color = texture(tex, tex_coord);
    // Corners get darker because l2 is larger away from screen center.
    color.rgb /= (1.0 + l2);

    frag_color = color;
}
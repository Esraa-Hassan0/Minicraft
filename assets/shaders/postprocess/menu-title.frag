#version 330 core

in Varyings {
    vec4 color;
    vec2 tex_coord;
} fs_in;

out vec4 frag_color;

uniform sampler2D tex;

void main() {
    vec4 col = texture(tex, fs_in.tex_coord);

    // Keep transparency
    if (col.a < 0.04) discard;

    frag_color = col;
}
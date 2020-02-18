#version 320 es

precision mediump float;
in vec2 text_pos;

uniform sampler2D tex;

out vec4 color;

void main() {
    color = texture(tex, text_pos);
}

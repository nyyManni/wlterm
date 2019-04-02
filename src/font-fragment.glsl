#version 320 es

precision highp float;
in vec2 text_pos;
in vec4 text_color;
out vec4 color;

uniform sampler2D font_texture;
uniform mat4 font_projection;

void main() {
    vec2 coords = (font_projection * vec4(text_pos, 0.0, 1.0)).xy;
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(font_texture, coords).r);
    color = text_color * sampled;
}

#version 320 es

precision mediump float;
in vec2 text_pos;
out vec4 color;

uniform sampler2D font_texture;
uniform mat4 font_projection;

uniform vec3 font_color;


void main() {
    // color = vec4(1.0, 1.0, 1.0, 1.0);
    // color =  vec4(text_pos, 1.0, 1.0);
    vec2 coords = (font_projection * vec4(text_pos, 0.0, 1.0)).xy;
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(font_texture, coords).r);
    // float r = texture(font_texture, text_pos).r;
    // color = vec4(r, r, r, 1.0);
    // vec4 sampled = vec4(1.0, 1.0, 1.0, texture(font_texture, text_pos).r);
    color = vec4(1.0, 1.0, 1.0, 1.0) * sampled;
}

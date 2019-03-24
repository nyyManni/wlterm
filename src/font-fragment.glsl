#version 320 es

precision mediump float;
in vec2 TexCoords;
out vec4 color;

uniform sampler2D text;
uniform vec3 textColor;
uniform mat4 textProjection;


void main() {
    // vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, (textProjection * vec4(TexCoords, 0.0, 1.0)).rg).r);
    vec4 sampled = vec4(1.0, 1.0, 1.0, 1.0 - texture(text, TexCoords).r);
    color = vec4(textColor, 1.0) * sampled;
}

#version 320 es

precision mediump float;

out vec4 color;
uniform vec3 accentColor;

void main() {
    
    color = vec4(accentColor, 1.0);
}

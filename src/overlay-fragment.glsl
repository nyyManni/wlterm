#version 320 es

precision mediump float;
in vec4 overlay_color;
out vec4 color;

void main() {
    // color = vec4(1.0, 0.0, 0.0, 1.0);
    color = overlay_color;
}

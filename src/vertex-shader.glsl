#version 320 es

uniform mat4 projection;
uniform vec4 offset;
uniform mat4 rotation;

layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 color;
out vec4 v_color;

void main() {
    gl_Position = projection * ((rotation * pos) + offset);
    v_color = color;
}

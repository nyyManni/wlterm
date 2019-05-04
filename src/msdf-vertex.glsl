#version 320 es

layout (location = 0) in vec2 vertex;

precision mediump float;
uniform mat4 projection;
uniform vec2 offset;

void main() {
    
    gl_Position = projection * vec4(vertex.xy + offset, 1.0, 1.0);
    // gl_Position.y = -gl_Position.y;
    // gl_Position = vec4(vertex.xy, 0.0, 1.0);
    // gl_Position = vec4(0.5, 0.5, 1.0, 1.0);
}

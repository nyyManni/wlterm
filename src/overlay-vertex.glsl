#version 320 es

layout (location = 0) in vec2 vertex;
layout (location = 1) in uvec4 color;

uniform mat4 projection;
uniform vec2 offset;

out VS_OUT {
    vec4 color;
} vs_out;

void main() {
    gl_Position = vec4(vertex.xy + offset, 0.0, 1.0);
    uvec4 c = color;
    vs_out.color = vec4(float(c.a) / 255.0, float(c.b) / 255.0,
                        float(c.g) / 255.0, float(c.r) / 255.0);
}

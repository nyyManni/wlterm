#version 320 es

layout (location = 0) in vec2 vertex;
layout (location = 1) in uvec4 glyph_color;
layout (location = 2) in int glyph_index;

uniform mat4 projection;
uniform vec2 offset;

out VS_OUT {
    int glyph;
    vec4 color;
} vs_out;

void main() {
    gl_Position = vec4(vertex.xy + offset, 0.0, 1.0);
    vs_out.glyph = glyph_index;
    uvec4 c = glyph_color;
    vs_out.color = vec4(float(c.a) / 255.0, float(c.b) / 255.0,
                        float(c.g) / 255.0, float(c.r) / 255.0);
}

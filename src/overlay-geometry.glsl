#version 320 es

layout (lines) in;
layout (line_strip, max_vertices = 256) out;

in VS_OUT {
    vec4 color;
} gs_in[];

out vec4 overlay_color;

uniform mat4 projection;


void main() {
    vec4 start = gl_in[0].gl_Position;
    vec4 end = gl_in[1].gl_Position;
    float o = 2.0;
    
    vec4 p = start;
    while (p.x < end.x) {
        p.x += 5.0;
        p.y += o;
        o = -1.0 * o;
        gl_Position = projection * p;
        overlay_color = gs_in[0].color;
        EmitVertex();
    }

    EndPrimitive();
}

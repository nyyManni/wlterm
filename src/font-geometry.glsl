#version 320 es

layout (points) in;
layout (triangle_strip, max_vertices = 6) out;

in VS_OUT {
    int glyph;
} gs_in[];

out vec2 text_pos;

uniform mat4 projection;

precision mediump samplerBuffer;
uniform samplerBuffer font_vertices;

void main() {
    int _offset = 6 * gs_in[0].glyph;
    vec2 text_offset = vec2(texelFetch(font_vertices, _offset + 0).r,
                            texelFetch(font_vertices, _offset + 1).r);
    vec2 text_size = vec2(texelFetch(font_vertices, _offset + 2).r,
                          texelFetch(font_vertices, _offset + 3).r);
    vec4 bearing = vec4(texelFetch(font_vertices, _offset + 4).r,
                        texelFetch(font_vertices, _offset + 5).r, 0.0, 0.0);

    // Top left triangle
    // TL
    gl_Position = projection * (gl_in[0].gl_Position + bearing);
    text_pos = text_offset;
    EmitVertex();

    // TR
    // gl_Position = projection * (gl_in[0].gl_Position + vec4(9 * 10, 0, 0, 0));
    gl_Position = projection * (gl_in[0].gl_Position + bearing + vec4(text_size.x , 0.0, 0.0, 0.0));
    text_pos = text_offset + vec2(text_size.x, 0.0);
    EmitVertex();

    // BL
    // gl_Position = projection * (gl_in[0].gl_Position + vec4(0, 18 , 0, 0));
    gl_Position = projection * (gl_in[0].gl_Position + bearing + vec4(0.0, text_size.y , 0.0, 0.0));
    text_pos = text_offset + vec2(0.0, text_size.y);
    EmitVertex();
    

    // Bottom right triangle
    // TR
    // gl_Position = projection * (gl_in[0].gl_Position +  vec4(0, 18 , 0, 0));
    gl_Position = projection * (gl_in[0].gl_Position + bearing + vec4(0.0, text_size.y , 0.0, 0.0));
    text_pos = text_offset + vec2(0.0, text_size.y);
    EmitVertex();

    // BL
    // gl_Position = projection * (gl_in[0].gl_Position + vec4(9 , 0, 0, 0));
    gl_Position = projection * (gl_in[0].gl_Position + bearing + vec4(text_size.x , 0.0, 0.0, 0.0));
    text_pos = text_offset + vec2(text_size.x, 0.0);
    EmitVertex();

    // BR
    // gl_Position = projection * (gl_in[0].gl_Position + vec4(9  , 18 , 0, 0));
    gl_Position = projection * (gl_in[0].gl_Position + bearing + vec4(text_size.x, text_size.y , 0.0, 0.0));
    text_pos = text_offset + text_size;
    EmitVertex();

    EndPrimitive();
}

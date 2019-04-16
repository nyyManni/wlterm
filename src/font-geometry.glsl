#version 320 es

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

in VS_OUT {
    int glyph;
    vec4 color;
    // int angle;
    // int size;
    // int y_offset;
} gs_in[];

out vec2 text_pos;
out vec4 text_color;

uniform mat4 projection;

precision highp samplerBuffer;
uniform samplerBuffer font_vertices;

uniform float font_scale;
// uniform float italics;
float padding = 2.0;

float italics = 0.1;


void main() {
    text_color = gs_in[0].color;
    int _offset = 8 * gs_in[0].glyph;
    vec2 text_offset = vec2(texelFetch(font_vertices, _offset + 0).r,
                            texelFetch(font_vertices, _offset + 1).r);
    vec2 glyph_texture_width = vec2(texelFetch(font_vertices, _offset + 2).r, 0.0 );
    vec2 glyph_texture_height = vec2(0.0, texelFetch(font_vertices, _offset + 3).r);

    vec4 bearing = vec4(texelFetch(font_vertices, _offset + 4).r,
                        texelFetch(font_vertices, _offset + 5).r, 0.0, 0.0) * 1.0;


    vec4 glyph_width = vec4(texelFetch(font_vertices, _offset + 6).r, 0.0, 0.0, 0.0);
    vec4 glyph_height = vec4(0.0, texelFetch(font_vertices, _offset + 7).r, 0.0, 0.0);

    vec4 padding_x = vec4(padding, 0.0, 0.0, 0.0);
    vec4 padding_y = vec4(0.0, padding, 0.0, 0.0);

    vec4 p = gl_in[0].gl_Position;
    vec4 _p = p;

    // BL
    _p = p + bearing + glyph_height - padding_x + padding_y;
    _p.x += italics * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset;
    EmitVertex();

    // BR
    _p = p + bearing + glyph_height + glyph_width + padding_x + padding_y;
    _p.x += italics * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_width;
    EmitVertex();

    // TL
    _p = p + bearing - padding_x - padding_y;
    _p.x += italics * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_height;
    EmitVertex();

    // TR
    _p = p + bearing + glyph_width + padding_x - padding_y;
    _p.x += italics * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_width + glyph_texture_height;
    EmitVertex();

    EndPrimitive();

}

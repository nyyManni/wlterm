#version 320 es

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

in VS_OUT {
    int glyph;
    vec4 color;
    float size;
    float y_offset;
    float skewness;
    float strength;
} gs_in[];

out vec2 text_pos;
out vec4 text_color;
out float strength;

uniform mat4 projection;
uniform float padding;

precision mediump samplerBuffer;
uniform samplerBuffer font_vertices;


void main() {
    text_color = gs_in[0].color;
    strength = gs_in[0].strength;
    float font_size = gs_in[0].size;
    int _offset = 8 * gs_in[0].glyph;
    vec2 text_offset = vec2(texelFetch(font_vertices, _offset + 0).r,
                            texelFetch(font_vertices, _offset + 1).r);
    vec2 glyph_texture_width = vec2(texelFetch(font_vertices, _offset + 2).r, 0.0 );
    vec2 glyph_texture_height = vec2(0.0, texelFetch(font_vertices, _offset + 3).r);

    vec4 bearing = vec4(texelFetch(font_vertices, _offset + 4).r,
                        -texelFetch(font_vertices, _offset + 5).r, 0.0, 0.0) * font_size;


    vec4 glyph_width = vec4(texelFetch(font_vertices, _offset + 6).r, 0.0, 0.0, 0.0) * font_size;
    vec4 glyph_height = vec4(0.0, texelFetch(font_vertices, _offset + 7).r, 0.0, 0.0) * font_size;

    vec4 padding_x = vec4(padding, 0.0, 0.0, 0.0) * font_size;
    vec4 padding_y = vec4(0.0, padding, 0.0, 0.0) * font_size;
    float skewness = gs_in[0].skewness;

    vec4 p = gl_in[0].gl_Position + vec4(0.0, gs_in[0].y_offset, 0.0, 0.0);
    vec4 _p = p;
    
    // text_offset.x = 666.9688;
    // text_offset.y = 196.0312;
    glyph_texture_width.x = 36.9375;
    glyph_texture_height.y = 44.7500;
    bearing.x = 2.0781 * font_size;
    bearing.y = -17.9219 * font_size;
    glyph_width.x = 14.4688 * font_size;
    glyph_height.y = 18.3750 * font_size;
    

    // BL
    _p = p + bearing + glyph_height - padding_x + padding_y;
    _p.x += skewness * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_height;
    // text_pos = vec2(0.0, 1024.0);
    EmitVertex();

    // BR
    _p = p + bearing + glyph_height + glyph_width + padding_x + padding_y;
    _p.x += skewness * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_width + glyph_texture_height;
    // text_pos = vec2(1024.0, 1024.0);
    EmitVertex();

    // TL
    _p = p + bearing - padding_x - padding_y;
    _p.x += skewness * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset;
    // text_pos = vec2(0.0, 0.0);
    EmitVertex();

    // TR
    _p = p + bearing + glyph_width + padding_x - padding_y;
    _p.x += skewness * (p.y - _p.y);
    gl_Position = projection * _p;
    text_pos = text_offset + glyph_texture_width;
    // text_pos = vec2(1024.0, 0.0);
    EmitVertex();

    EndPrimitive();
}

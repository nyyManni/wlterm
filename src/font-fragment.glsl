#version 320 es

precision highp float;
in vec2 text_pos;
in vec4 text_color;
out vec4 color;

uniform sampler2D font_texture;
uniform mat4 font_projection;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}
float pxRange = 4.0;

void main() {
    vec2 coords = (font_projection * vec4(text_pos, 0.0, 1.0)).xy;

    vec2 msdfUnit = pxRange/vec2(textureSize(font_texture, 0));
    vec3 s = texture(font_texture, coords).rgb;
    float sigDist = median(s.r, s.g, s.b) - 0.5;
    sigDist *= dot(msdfUnit, 0.5/fwidth(coords));
    float opacity = clamp(sigDist + 0.5, 0.0, 1.0);
    color = mix(vec4(0.0, 0.0, 0.0, 0.0), text_color, opacity);
}

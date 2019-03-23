uniform mat4 projection;
uniform vec4 offset;
uniform mat4 rotation;
attribute vec4 pos;
attribute vec4 color;
varying vec4 v_color;

void main() {
    gl_Position = projection * ((rotation * pos) + offset);
    // gl_Position = projection * pos + offset;
    v_color = color;
}

uniform mat4 rotation;
attribute vec4 pos;
attribute vec4 color;
varying vec4 v_color;

void main() {
    gl_Position = rotation * pos;
    v_color = color;
}

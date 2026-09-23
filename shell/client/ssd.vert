#version 330
layout(location = 0) in vec2 position;
out vec4 pos;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    pos = gl_Position;
}

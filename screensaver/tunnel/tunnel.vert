#version 110

// Positions arrive relative to the camera, so only the camera's rotation
// (view) and the perspective (projection) are left to apply.
attribute vec3 position;
attribute vec3 color;

uniform mat4 view;
uniform mat4 projection;

varying vec3 v_color;
varying vec3 v_offset;

void main() {
    v_color = color;
    v_offset = position;
    gl_Position = projection * view * vec4(position, 1.0);
}

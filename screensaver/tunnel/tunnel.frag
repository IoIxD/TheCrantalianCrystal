#version 110

uniform float fog_end;

varying vec3 v_color;
varying vec3 v_offset;

void main() {
    // Darken with straight-line distance from the camera, reaching black at
    // fog_end. The view matrix is a pure rotation, so the camera-relative
    // offset's length is already that distance.
    float fade = clamp(1.0 - length(v_offset) / fog_end, 0.0, 1.0);
    gl_FragColor = vec4(v_color * fade, 1.0);
}

#version 120
uniform sampler2D tex;
uniform vec4 src; // x, y, width, height, as texture coordinates
uniform vec2 dir; // one step along the blur, as texture coordinates
varying vec2 uv;
void main() {
    vec2 p = src.xy + uv * src.zw;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = -12; i <= 12; i++) {
        float w = exp(-float(i * i) / 32.0); // sigma of 4 steps
        sum += texture2D(tex, p + dir * float(i)).rgb * w;
        total += w;
    }
    gl_FragColor = vec4(sum / total, 1.0);
}

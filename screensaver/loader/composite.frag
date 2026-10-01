#version 120
uniform sampler2D tex;
uniform vec4 src;
uniform vec2 size;
uniform float radius;
varying vec2 uv;
void main() {
    vec2 q = abs(uv * size - size * 0.5) - size * 0.5 + radius;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float alpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    gl_FragColor = vec4(texture2D(tex, src.xy + uv * src.zw).rgb, alpha);
}

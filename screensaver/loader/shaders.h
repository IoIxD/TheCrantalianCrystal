#define BLUR_FRAG_SOURCE R"__DELIM__(#version 120
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
)__DELIM__"
#define COMPOSITE_FRAG_SOURCE R"__DELIM__(#version 120
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
)__DELIM__"
#define QUAD_VERT_SOURCE R"__DELIM__(#version 120
attribute vec2 position;
varying vec2 uv;
void main() {
    uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)__DELIM__"

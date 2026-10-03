#version 120
// Frosted glass over what was behind the clock: light scattered across a
// disc and a milky tint, scaled by strength, cut to the window's rounded
// corners.
uniform sampler2D backdrop;
uniform vec2 backdrop_size; // in logical pixels
uniform vec4 window;        // x, y, width, height within the backdrop, likewise
uniform float radius;       // of the window's corners, likewise
uniform vec2 spread;    // the disc's radius at full strength, as texture coordinates
uniform vec3 tint;
uniform float strength; // 0 for clear glass, 1 for fully frosted

void main() {
    vec2 p = gl_TexCoord[0].st;
    vec2 pos = p * backdrop_size;

    vec3 sum = vec3(0.0);
    for (int i = 0; i < 48; i++) {
        // (a sunflower spiral, so the samples cover the disc evenly)
        float r = sqrt((float(i) + 0.5) / 48.0);
        float a = float(i) * 2.3999632;
        sum += texture2D(backdrop, p + vec2(cos(a), sin(a)) * r * spread * strength).rgb;
    }
    vec3 color = mix(sum / 48.0, tint, 0.45 * strength);

    // How far outside the window's rounded rectangle this is.
    vec2 q = abs(pos - window.xy - window.zw * 0.5) -
             window.zw * 0.5 + radius;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float inside = 1.0 - smoothstep(-0.5, 0.5, dist);
    // (transparent past them, with alpha premultiplied as Wayland wants it)
    gl_FragColor = vec4(color, 1.0) * inside;
}

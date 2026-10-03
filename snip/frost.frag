#version 120
// Frosted glass over the whole screen, the same as the clock's: light
// scattered across a disc, a milky tint and a little grain, all scaled by
// strength. What's being selected is left clear, with a thin border around it.
uniform sampler2D backdrop;
uniform vec2 backdrop_size; // in logical pixels
uniform vec4 selection;     // x, y, width, height, likewise (empty for none)
uniform vec2 spread;    // the disc's radius at full strength, as texture coordinates
uniform vec3 tint;
uniform vec3 border;
uniform float strength; // 0 for clear glass, 1 for fully frosted

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 p = gl_TexCoord[0].st;
    vec3 clear = texture2D(backdrop, p).rgb;

    // How far outside the selection this is, and whether it's on its border.
    vec2 q = abs(p * backdrop_size - selection.xy - selection.zw * 0.5) -
             selection.zw * 0.5;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
    if (selection.z > 0.0 && selection.w > 0.0 && dist <= 0.0) {
        gl_FragColor = vec4(clear, 1.0);
        return;
    }

    // Every pixel turns the disc by its own amount, which is what gives the
    // glass its grain rather than a smooth blur.
    float turn = hash(gl_FragCoord.xy) * 6.2831853;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 48; i++) {
        // (a sunflower spiral, so the samples cover the disc evenly)
        float r = sqrt((float(i) + 0.5) / 48.0);
        float a = float(i) * 2.3999632 + turn;
        sum += texture2D(backdrop, p + vec2(cos(a), sin(a)) * r * spread * strength).rgb;
    }
    vec3 color = mix(sum / 48.0, tint, 0.45 * strength);
    color += (hash(gl_FragCoord.xy + 17.0) - 0.5) * 0.05 * strength;

    if (selection.z > 0.0 && selection.w > 0.0) {
        float edge = 1.0 - smoothstep(1.0, 2.0, dist);
        color = mix(color, border, edge * strength);
    }
    gl_FragColor = vec4(color, 1.0);
}

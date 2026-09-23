#version 330
in vec4 pos;
uniform vec2 resolution;
uniform int ssd_border_size;
uniform int ssd_border_size_top;

float rounded_box_sdf(vec2 p, vec2 half_size, float radius) {
    vec2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    vec3 baseColor = vec3(.416, .196, .576);
    vec3 lowColor = vec3(1.0, .612, .404);
    float mixBy = (1.0 - pos.y);
    if (mixBy > 0.75) mixBy = 0.75;

    vec3 mixedColor = mix(baseColor, lowColor, mixBy);

    vec2 frag_coord = (pos.xy * 0.5 + 0.5) * resolution;
    float dist = rounded_box_sdf(frag_coord - resolution * 0.5, resolution * 0.5, 7.0);
    float alpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    if (alpha <= 0.0) {
        discard;
    }

    float border_dist = (dist + 2.0);
    float black_dist = (dist + ssd_border_size);
    float darken = 0.15 * (1.0 - smoothstep(0.0, 1.0, border_dist));
    mixedColor -= darken;
    if (resolution.y - frag_coord.y >= ssd_border_size_top) {
        float black = 0.15 * (1.0 - smoothstep(0.0, 1.0, black_dist));
        mixedColor -= black;
    }

    gl_FragColor = vec4(mixedColor.rgb, alpha);
}

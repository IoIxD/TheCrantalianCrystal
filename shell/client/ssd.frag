#version 330

/* as somebody who has struggled with math for 24 fucking years of his life i will shamefully admit i used AI for some of this code */

#define BUTTON_TYPE_CLOSE 0
#define BUTTON_TYPE_MINIMIZE 1
#define BUTTON_TYPE_MAXIMIZE 2

in vec4 pos;
uniform vec2 resolution;
uniform int ssd_border_size;
uniform int ssd_border_size_top;

uniform bool close_held;
uniform bool minimize_held;
uniform bool maximize_held;
uniform bool close_hover;
uniform bool minimize_hover;
uniform bool maximize_hover;
uniform bool show_maxmin;

vec3 baseColorBackground = vec3(.416, .196, .576); /* #6a3293 */
vec3 lowColorBackground = vec3(1.0, .612, .404); /* #ff9c67 */
vec3 mixedColorBackground = vec3(0, 0, 0);

vec3 baseColorButton = vec3(0.9, 0.9, 0.9);
vec3 lowColorButton = vec3(0.6, 0.6, 0.6);
vec3 mixedColorButton = vec3(0, 0, 0);

float mixBy = 1.0;
vec2 frag_coord = vec2(0, 0);

float rounded_box_sdf(vec2 p, vec2 half_size, float radius) {
    vec2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void draw_backing_border(float alpha) {
    float dist = rounded_box_sdf(frag_coord - resolution * 0.5, resolution * 0.5, 7.0);
    if (alpha == -1.0) alpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    if (alpha <= 0.0) {
        discard;
    }

    float border_dist = (dist + 2.0);
    float black_dist = (dist + ssd_border_size);
    float darken = 0.15 * (1.0 - smoothstep(0.0, 1.0, border_dist));
    mixedColorBackground -= darken;
    if (resolution.y - frag_coord.y >= ssd_border_size_top) {
        float black = 0.15 * (1.0 - smoothstep(0.0, 1.0, black_dist));
        mixedColorBackground -= black;
    }
    gl_FragColor = vec4(mixedColorBackground.rgb, alpha);
}

vec3 blendMultiply(vec3 base, vec3 blend) {
    return base * blend;
}

vec3 blendMultiply(vec3 base, vec3 blend, float opacity) {
    return (blendMultiply(base, blend) * opacity + base * (1.0 - opacity));
}

void draw_button(int type, vec2 lo, vec2 hi) {
    vec2 half_size = (hi - lo) * 0.5;
    float radius = min(4.0, min(half_size.x, half_size.y));
    float dist = rounded_box_sdf(frag_coord - (lo + half_size), half_size, radius);
    float alpha = 1.0 - smoothstep(-0.1, 0.1, dist);

    if (alpha < 0.01) {
        draw_backing_border(-1);
        return;
    }

    vec3 backingGradient = vec3(0, 0, 0);

    float fade = mixBy * (resolution.y / 32.0);

    mixedColorButton = mix(baseColorButton, lowColorButton, fade);
    mixedColorButton = mix(mixedColorButton, mixedColorBackground, 0.3);
    vec3 finalBackground = vec3(0, 0, 0);
    #define INSET(x, by) x = (x) - vec3(by,by,by);

    #define HELDHOVERSWITCH(x) if (!x##_held && !x##_hover) {finalBackground = blendMultiply(mixedColorBackground, mixedColorButton, alpha);}else {finalBackground = mix(mixedColorBackground, mixedColorButton, alpha); if(x##_held) {INSET(finalBackground, 0.5);} else {INSET(finalBackground, 0.4);}}

    switch (type) {
        case BUTTON_TYPE_CLOSE:
        {
            HELDHOVERSWITCH(close);
            vec2 p = frag_coord - (lo + half_size);
            float extent = min(half_size.x, half_size.y) - 5;
            if (abs(p.x) <= extent && abs(p.y) <= extent && abs(abs(p.x) - abs(p.y)) <= 1.5) {
                finalBackground = vec3(1, 1, 1);
                // INSET(finalBackground, 0.3);
            }
        }
        break;
        case BUTTON_TYPE_MINIMIZE:
        {
            HELDHOVERSWITCH(minimize);
            if (frag_coord.x <= hi.x - 5 && frag_coord.x >= lo.x + 5 && frag_coord.y <= hi.y - 9 && frag_coord.y >= lo.y + 9) {
                finalBackground = vec3(1, 1, 1);
                // INSET(finalBackground, 0.3);
            }
        }
        break;
        case BUTTON_TYPE_MAXIMIZE:
        {
            HELDHOVERSWITCH(maximize);
            if (frag_coord.x <= hi.x - 5 && frag_coord.x >= lo.x + 5 && frag_coord.y <= hi.y - 5 && frag_coord.y >= lo.y + 5) {
                if (frag_coord.x <= hi.x - 7 && frag_coord.x >= lo.x + 7 && frag_coord.y <= hi.y - 7 && frag_coord.y >= lo.y + 7) {} else {
                    finalBackground = vec3(1, 1, 1);
                    // INSET(finalBackground, 0.3);
                }
            }
        }
        break;
        default:
        break;
    }
    gl_FragColor = vec4(finalBackground.rgb, 1.0);
}

void main() {
    mixBy = (1.0 - pos.y);
    if (mixBy > 0.75) mixBy = 0.75;

    mixedColorBackground = mix(baseColorBackground, lowColorBackground, mixBy);

    frag_coord = (pos.xy * 0.5 + 0.5) * resolution;

    float button_lo_y = resolution.y - (ssd_border_size_top - 4);
    float button_hi_y = resolution.y - 6;
    if (frag_coord.y <= button_hi_y && frag_coord.y >= button_lo_y) {
        if (frag_coord.x <= resolution.x - 10 && frag_coord.x >= resolution.x - 32) {
            draw_button(BUTTON_TYPE_CLOSE, vec2(resolution.x - 32, button_lo_y), vec2(resolution.x - 10, button_hi_y));
        } else if (show_maxmin) {
            if (frag_coord.x <= resolution.x - 35 && frag_coord.x >= resolution.x - 57) {
                draw_button(BUTTON_TYPE_MAXIMIZE, vec2(resolution.x - 57, button_lo_y), vec2(resolution.x - 35, button_hi_y));
            } else if (frag_coord.x <= resolution.x - 60 && frag_coord.x >= resolution.x - 82) {
                draw_button(BUTTON_TYPE_MINIMIZE, vec2(resolution.x - 82, button_lo_y), vec2(resolution.x - 60, button_hi_y));
            } else {
                draw_backing_border(-1.0);
            }
        } else {
            draw_backing_border(-1.0);
        }
    } else {
        draw_backing_border(-1.0);
    }
}

#version 120
// A gaussian blur along one direction over a layer (with premultiplied
// alpha), faded by opacity. Run once across and once down for the full blur.
uniform sampler2D layer;
uniform vec2 dir;       // one pixel along the blur, as texture coordinates
uniform float sigma;    // in pixels
uniform float opacity;

void main() {
    vec2 p = gl_TexCoord[0].st;
    if (sigma < 0.05) {
        gl_FragColor = texture2D(layer, p) * opacity;
        return;
    }
    // 25 samples, out to 3 sigma either side.
    float step = sigma / 4.0;
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int i = -12; i <= 12; i++) {
        float w = exp(-float(i * i) / 32.0);
        sum += texture2D(layer, p + dir * float(i) * step) * w;
        total += w;
    }
    gl_FragColor = sum / total * opacity;
}

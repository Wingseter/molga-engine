#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform vec2 uTexelSize;
uniform bool uPrefilter;
uniform float uThreshold;
uniform float uSoftKnee;

vec3 sampleBox(vec2 uv) {
    // Cover every source texel in the 2x2 footprint. The HDR scene target is
    // intentionally nearest-filtered, so half-texel offsets are required to
    // keep a single bright pixel from falling between downsample taps.
    vec2 offset = uTexelSize * 0.5;
    vec3 color = texture(uSource, uv + vec2(-offset.x, -offset.y)).rgb;
    color += texture(uSource, uv + vec2(offset.x, -offset.y)).rgb;
    color += texture(uSource, uv + vec2(-offset.x, offset.y)).rgb;
    color += texture(uSource, uv + vec2(offset.x, offset.y)).rgb;
    return color * 0.25;
}

void main() {
    vec3 color = sampleBox(vUV);
    if (uPrefilter) {
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float knee = max(uThreshold * uSoftKnee, 0.00001);
        float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 0.00001);
        float contribution = max(brightness - uThreshold, soft) /
                             max(brightness, 0.00001);
        color *= contribution;
    }
    FragColor = vec4(color, 1.0);
}

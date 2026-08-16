#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uHigh;
uniform sampler2D uLow;
uniform vec2 uLowTexelSize;
uniform float uScatter;

void main() {
    vec3 low = texture(uLow, vUV).rgb * 4.0;
    low += texture(uLow, vUV + uLowTexelSize * vec2(-1.0, -1.0)).rgb;
    low += texture(uLow, vUV + uLowTexelSize * vec2(1.0, -1.0)).rgb;
    low += texture(uLow, vUV + uLowTexelSize * vec2(-1.0, 1.0)).rgb;
    low += texture(uLow, vUV + uLowTexelSize * vec2(1.0, 1.0)).rgb;
    low *= 0.125;
    FragColor = vec4(texture(uHigh, vUV).rgb + low * uScatter, 1.0);
}

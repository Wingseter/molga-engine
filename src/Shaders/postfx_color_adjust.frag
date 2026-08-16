#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform float uExposureEV;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uTint;

void main() {
    vec4 source = texture(uSource, vUV);
    vec3 color = source.rgb * exp2(uExposureEV);
    color = (color - vec3(0.5)) * (1.0 + uContrast) + vec3(0.5);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, uSaturation);
    color *= uTint;
    FragColor = vec4(color, source.a);
}

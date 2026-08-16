#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform float uIntensity;
uniform float uSmoothness;
uniform float uAspect;
uniform vec3 uColor;

void main() {
    vec4 source = texture(uSource, vUV);
    vec2 position = (vUV - vec2(0.5)) * 2.0;
    position.x *= uAspect;
    float maximumRadius = length(vec2(uAspect, 1.0));
    float radius = length(position) / max(maximumRadius, 0.00001);
    float mask = smoothstep(1.0 - uSmoothness, 1.0, radius) * uIntensity;
    FragColor = vec4(mix(source.rgb, uColor, mask), source.a);
}

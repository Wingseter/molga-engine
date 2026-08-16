#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uIntensity;

void main() {
    vec4 scene = texture(uScene, vUV);
    scene.rgb += texture(uBloom, vUV).rgb * uIntensity;
    FragColor = scene;
}

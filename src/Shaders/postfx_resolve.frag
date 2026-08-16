#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;

void main() {
    FragColor = clamp(texture(uSource, vUV), 0.0, 1.0);
}

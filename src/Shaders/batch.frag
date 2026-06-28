#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec4 Color;

uniform sampler2D uTexture;
uniform bool useTexture;

void main() {
    if (useTexture) {
        FragColor = texture(uTexture, TexCoord) * Color;
    } else {
        FragColor = Color;
    }
}

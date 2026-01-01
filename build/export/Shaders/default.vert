#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 projection;
uniform vec4 uUV;  // u0, v0, u1, v1

void main() {
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
    // Map texture coordinates to UV region
    TexCoord = vec2(
        mix(uUV.x, uUV.z, aTexCoord.x),
        mix(uUV.y, uUV.w, aTexCoord.y)
    );
}

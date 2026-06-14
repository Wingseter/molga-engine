#version 330 core
layout (location = 0) in vec2 aPos;

// 화면 좌표를 월드 좌표로 변환하기 위해 inverse(projection * view) 를 전달받음
uniform mat4 invProjView;
uniform vec2 screenSize;

out vec2 worldPos;

void main() {
    // -1..1 NDC 쿼드를 그대로 출력하고
    // fragment 에서 세계 좌표를 재구성
    gl_Position = vec4(aPos, 0.0, 1.0);

    // NDC -> world
    vec4 wp = invProjView * vec4(aPos, 0.0, 1.0);
    worldPos = wp.xy;
}

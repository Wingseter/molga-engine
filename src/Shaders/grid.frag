#version 330 core
out vec4 FragColor;

in vec2 worldPos;

uniform float gridSpacing;   // 그리드 한 칸 크기 (월드 단위)
uniform vec4  gridColor;     // 보조선 색 (RGBA)
uniform vec4  originColor;   // 원점 축 색
uniform float lineWidth;     // 선 굵기 (월드 단위)

float gridLine(float coord, float spacing, float lw) {
    float halfCell = spacing * 0.5;
    float val = mod(abs(coord) + halfCell, spacing) - halfCell;
    float fw = fwidth(coord);
    return 1.0 - smoothstep(lw * fw, (lw + 1.0) * fw, abs(val));
}

void main() {
    float lw = lineWidth;

    // 보조 그리드
    float gx = gridLine(worldPos.x, gridSpacing, lw);
    float gy = gridLine(worldPos.y, gridSpacing, lw);
    float grid = max(gx, gy);

    // 원점 축 (더 굵게)
    float axisLW = lw * 3.0;
    float axisX = 1.0 - smoothstep(axisLW * fwidth(worldPos.y), (axisLW + 1.0) * fwidth(worldPos.y), abs(worldPos.y));
    float axisY = 1.0 - smoothstep(axisLW * fwidth(worldPos.x), (axisLW + 1.0) * fwidth(worldPos.x), abs(worldPos.x));

    vec4 color = vec4(0.0);
    if (grid > 0.01) {
        color = mix(color, gridColor, grid * gridColor.a);
    }
    // X축 = 빨강, Y축 = 초록
    if (axisX > 0.01) {
        color = mix(color, vec4(0.85, 0.25, 0.25, 1.0), axisX);
    }
    if (axisY > 0.01) {
        color = mix(color, vec4(0.25, 0.85, 0.25, 1.0), axisY);
    }

    if (color.a < 0.01) discard;
    FragColor = color;
}

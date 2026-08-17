#include "molga_bindings.hlsl"

struct VertexInput {
    float2 position : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 worldPosition : TEXCOORD0;
};

cbuffer GridVertexConstants MOLGA_VS_UNIFORM0 {
    float4x4 inverseProjectionView;
};

cbuffer GridFragmentConstants MOLGA_PS_UNIFORM0 {
    float4 gridColor;
    float4 originColor;
    float gridSpacing;
    float lineWidth;
    float2 gridPadding;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.worldPosition = mul(inverseProjectionView,
                               float4(input.position, 0.0, 1.0)).xy;
    return output;
}

float GridLine(float coordinate, float spacing, float width) {
    float halfCell = spacing * 0.5;
    float value = fmod(abs(coordinate) + halfCell, spacing) - halfCell;
    float derivative = max(fwidth(coordinate), 1.0e-6);
    return 1.0 - smoothstep(width * derivative,
                            (width + 1.0) * derivative, abs(value));
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float xLine = GridLine(input.worldPosition.x, gridSpacing, lineWidth);
    float yLine = GridLine(input.worldPosition.y, gridSpacing, lineWidth);
    float grid = max(xLine, yLine);
    float axisWidth = lineWidth * 3.0;
    float xAxis = 1.0 - smoothstep(
        axisWidth * fwidth(input.worldPosition.y),
        (axisWidth + 1.0) * fwidth(input.worldPosition.y),
        abs(input.worldPosition.y));
    float yAxis = 1.0 - smoothstep(
        axisWidth * fwidth(input.worldPosition.x),
        (axisWidth + 1.0) * fwidth(input.worldPosition.x),
        abs(input.worldPosition.x));
    float4 color = 0.0;
    color = lerp(color, gridColor, grid * gridColor.a);
    color = lerp(color, float4(0.85, 0.25, 0.25, 1.0), xAxis);
    color = lerp(color, float4(0.25, 0.85, 0.25, 1.0), yAxis);
    if (color.a < 0.01) discard;
    return color;
}

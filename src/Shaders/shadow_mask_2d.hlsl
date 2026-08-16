#include "molga_bindings.hlsl"

struct VertexInput {
    float2 position : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
};

cbuffer ShadowVertexConstants MOLGA_VS_UNIFORM0 {
    float4x4 projection;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    return float4(1.0, 0.0, 0.0, 1.0);
}

#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer BloomUpConstants MOLGA_PS_UNIFORM0 {
    float2 lowTexelSize;
    float scatter;
    float bloomUpPadding;
};

Texture2D<float4> uHigh : MOLGA_PS_TEXTURE_uHigh;
SamplerState uHighSampler : MOLGA_PS_SAMPLER_uHigh;
Texture2D<float4> uLow : MOLGA_PS_TEXTURE_uLow;
SamplerState uLowSampler : MOLGA_PS_SAMPLER_uLow;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.position * 0.5 + 0.5;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float3 low = uLow.Sample(uLowSampler, input.uv).rgb * 4.0;
    low += uLow.Sample(uLowSampler, input.uv + lowTexelSize * float2(-1.0, -1.0)).rgb;
    low += uLow.Sample(uLowSampler, input.uv + lowTexelSize * float2(1.0, -1.0)).rgb;
    low += uLow.Sample(uLowSampler, input.uv + lowTexelSize * float2(-1.0, 1.0)).rgb;
    low += uLow.Sample(uLowSampler, input.uv + lowTexelSize * float2(1.0, 1.0)).rgb;
    low *= 0.125;
    return float4(uHigh.Sample(uHighSampler, input.uv).rgb + low * scatter, 1.0);
}

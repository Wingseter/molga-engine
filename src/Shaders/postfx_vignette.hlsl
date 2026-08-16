#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer VignetteConstants MOLGA_PS_UNIFORM0 {
    float4 vignetteColor;
    float intensity;
    float smoothness;
    float aspect;
    float vignettePadding;
};

Texture2D<float4> uSource : MOLGA_PS_TEXTURE_uSource;
SamplerState uSourceSampler : MOLGA_PS_SAMPLER_uSource;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.position * 0.5 + 0.5;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float4 source = uSource.Sample(uSourceSampler, input.uv);
    float2 position = (input.uv - 0.5) * 2.0;
    position.x *= aspect;
    float maximumRadius = length(float2(aspect, 1.0));
    float radius = length(position) / max(maximumRadius, 0.00001);
    float mask = smoothstep(1.0 - smoothness, 1.0, radius) * intensity;
    return float4(lerp(source.rgb, vignetteColor.rgb, mask), source.a);
}

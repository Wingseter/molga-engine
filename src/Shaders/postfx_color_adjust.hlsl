#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer ColorAdjustConstants MOLGA_PS_UNIFORM0 {
    float4 tint;
    float exposureEV;
    float contrast;
    float saturation;
    float colorAdjustPadding;
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
    float3 color = source.rgb * exp2(exposureEV);
    color = (color - 0.5) * (1.0 + contrast) + 0.5;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luminance.xxx, color, saturation) * tint.rgb;
    return float4(color, source.a);
}

#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
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
    return saturate(uSource.Sample(uSourceSampler, input.uv));
}

#include "molga_bindings.hlsl"

struct VertexInput {
    float2 position : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float4 color : TEXCOORD2;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer BatchVertexConstants MOLGA_VS_UNIFORM0 {
    float4x4 projection;
};

cbuffer BatchFragmentConstants MOLGA_PS_UNIFORM0 {
    uint useTexture;
    float3 batchFragmentPadding;
};

Texture2D<float4> uTexture : MOLGA_PS_TEXTURE_uTexture;
SamplerState uTextureSampler : MOLGA_PS_SAMPLER_uTexture;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    return useTexture != 0
        ? uTexture.Sample(uTextureSampler, input.texCoord) * input.color
        : input.color;
}

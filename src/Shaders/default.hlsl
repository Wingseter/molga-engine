#include "molga_bindings.hlsl"

struct VertexInput {
    float2 position : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

cbuffer DefaultVertexConstants MOLGA_VS_UNIFORM0 {
    float4x4 model;
    float4x4 projection;
    float4 uvRegion;
};

cbuffer DefaultFragmentConstants MOLGA_PS_UNIFORM0 {
    float4 tint;
    uint useTexture;
    float3 defaultFragmentPadding;
};

Texture2D<float4> uTexture : MOLGA_PS_TEXTURE_uTexture;
SamplerState uTextureSampler : MOLGA_PS_SAMPLER_uTexture;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(projection, mul(model, float4(input.position, 0.0, 1.0)));
    output.texCoord = lerp(uvRegion.xy, uvRegion.zw, input.texCoord);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    return useTexture != 0
        ? uTexture.Sample(uTextureSampler, input.texCoord) * tint
        : tint;
}

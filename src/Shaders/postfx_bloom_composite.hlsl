#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer BloomCompositeConstants MOLGA_PS_UNIFORM0 {
    float intensity;
    float3 bloomCompositePadding;
};

Texture2D<float4> uScene : MOLGA_PS_TEXTURE_uScene;
SamplerState uSceneSampler : MOLGA_PS_SAMPLER_uScene;
Texture2D<float4> uBloom : MOLGA_PS_TEXTURE_uBloom;
SamplerState uBloomSampler : MOLGA_PS_SAMPLER_uBloom;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.position * 0.5 + 0.5;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float4 scene = uScene.Sample(uSceneSampler, input.uv);
    scene.rgb += uBloom.Sample(uBloomSampler, input.uv).rgb * intensity;
    return scene;
}

#include "molga_bindings.hlsl"

struct VertexInput { float2 position : TEXCOORD0; };
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer BloomDownConstants MOLGA_PS_UNIFORM0 {
    float2 texelSize;
    float threshold;
    float softKnee;
    uint prefilter;
    float3 bloomDownPadding;
};

Texture2D<float4> uSource : MOLGA_PS_TEXTURE_uSource;
SamplerState uSourceSampler : MOLGA_PS_SAMPLER_uSource;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.position * 0.5 + 0.5;
    return output;
}

float3 SampleBox(float2 uv) {
    float2 offset = texelSize * 0.5;
    float3 color = uSource.Sample(uSourceSampler, uv + float2(-offset.x, -offset.y)).rgb;
    color += uSource.Sample(uSourceSampler, uv + float2(offset.x, -offset.y)).rgb;
    color += uSource.Sample(uSourceSampler, uv + float2(-offset.x, offset.y)).rgb;
    color += uSource.Sample(uSourceSampler, uv + float2(offset.x, offset.y)).rgb;
    return color * 0.25;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float3 color = SampleBox(input.uv);
    if (prefilter != 0) {
        float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
        float knee = max(threshold * softKnee, 0.00001);
        float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 0.00001);
        float contribution = max(brightness - threshold, soft) /
                             max(brightness, 0.00001);
        color *= contribution;
    }
    return float4(color, 1.0);
}

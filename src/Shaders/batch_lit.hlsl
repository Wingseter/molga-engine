#include "molga_bindings.hlsl"

#define MOLGA_MAX_LIGHTS 8

struct VertexInput {
    float2 position : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float4 color : TEXCOORD2;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 worldPosition : TEXCOORD2;
};

cbuffer LitVertexConstants MOLGA_VS_UNIFORM0 {
    float4x4 projection;
};

cbuffer LitFragmentConstants MOLGA_PS_UNIFORM0 {
    float4 ambientColor;
    float4 shadowViewport;
    float ambientIntensity;
    uint lightCount;
    uint receiverLayer;
    uint useTexture;
    uint useNormalTexture;
    float normalStrength;
    float2 litPadding;
    float4 lightPositionRadiusHeight[MOLGA_MAX_LIGHTS];
    float4 lightColorIntensity[MOLGA_MAX_LIGHTS];
    float4 lightFalloffPadding[MOLGA_MAX_LIGHTS];
    uint4 lightMetadata[MOLGA_MAX_LIGHTS];
};

Texture2D<float4> uTexture : MOLGA_PS_TEXTURE_uTexture;
SamplerState uTextureSampler : MOLGA_PS_SAMPLER_uTexture;
Texture2D<float4> uNormalTexture : MOLGA_PS_TEXTURE_uNormalTexture;
SamplerState uNormalTextureSampler : MOLGA_PS_SAMPLER_uNormalTexture;
Texture2DArray<float4> uShadowMasks : MOLGA_PS_TEXTURE_uShadowMasks;
SamplerState uShadowMasksSampler : MOLGA_PS_SAMPLER_uShadowMasks;

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    output.color = input.color;
    output.worldPosition = input.position;
    return output;
}

float3 TangentNormal(VertexOutput input) {
    if (useNormalTexture == 0) return float3(0.0, 0.0, 1.0);
    float3 sampled =
        uNormalTexture.Sample(uNormalTextureSampler, input.texCoord).rgb * 2.0 - 1.0;
    sampled.xy *= normalStrength;
    sampled = normalize(sampled);
    float2 planePosition = float2(input.worldPosition.x, -input.worldPosition.y);
    float2 dpdx = ddx(planePosition);
    float2 dpdy = ddy(planePosition);
    float2 duvdx = ddx(input.texCoord);
    float2 duvdy = ddy(input.texCoord);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(determinant) < 1.0e-8) return float3(0.0, 0.0, 1.0);
    float2 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
    float2 bitangent = normalize((-dpdx * duvdy.x + dpdy * duvdx.x) / determinant);
    return normalize(float3(tangent * sampled.x + bitangent * sampled.y,
                            sampled.z));
}

float ShadowForLight(VertexOutput input, uint lightIndex) {
    int layer = asint(lightMetadata[lightIndex].y);
    if (layer < 0 || shadowViewport.z <= 0.0 || shadowViewport.w <= 0.0) {
        return 0.0;
    }
    float2 uv = (input.position.xy - shadowViewport.xy) / shadowViewport.zw;
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    return uShadowMasks.Sample(uShadowMasksSampler,
                               float3(uv, float(layer))).r;
}

float4 PSMain(VertexOutput input) : SV_Target0 {
    float4 diffuse = useTexture != 0
        ? uTexture.Sample(uTextureSampler, input.texCoord) * input.color
        : input.color;
    float3 normal = TangentNormal(input);
    float3 lighting = ambientColor.rgb * max(ambientIntensity, 0.0);
    uint receiverBit = 1U << min(receiverLayer, 31U);
    [loop]
    for (uint index = 0U; index < MOLGA_MAX_LIGHTS; ++index) {
        if (index >= lightCount) break;
        if ((lightMetadata[index].x & receiverBit) == 0U) continue;
        float2 delta = lightPositionRadiusHeight[index].xy - input.worldPosition;
        float distanceToLight = length(delta);
        float radius = max(lightPositionRadiusHeight[index].z, 0.01);
        float radial = saturate(1.0 - distanceToLight / radius);
        if (radial <= 0.0) continue;
        float3 lightVector = float3(
            delta.x, -delta.y, lightPositionRadiusHeight[index].w);
        float vectorLength = length(lightVector);
        float3 lightDirection = vectorLength > 1.0e-8
            ? lightVector / vectorLength : float3(0.0, 0.0, 1.0);
        float lambert = max(dot(normal, lightDirection), 0.0);
        float attenuation = pow(radial, max(lightFalloffPadding[index].x, 0.1)) *
                            max(lightColorIntensity[index].a, 0.0);
        float visible = 1.0 - saturate(ShadowForLight(input, index));
        lighting += lightColorIntensity[index].rgb * lambert * attenuation * visible;
    }
    return float4(diffuse.rgb * lighting, diffuse.a);
}

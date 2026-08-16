#version 330 core

const int MAX_LIGHTS = 8;

out vec4 FragColor;

in vec2 TexCoord;
in vec4 Color;
in vec2 WorldPos;

uniform sampler2D uTexture;
uniform sampler2D uNormalTexture;
uniform sampler2DArray uShadowMasks;
uniform bool useTexture;
uniform bool useNormalTexture;
uniform float uNormalStrength;
uniform uint uReceiverLayer;

uniform vec4 uAmbientColor;
uniform float uAmbientIntensity;
uniform int uLightCount;
uniform vec2 uLightPosition[MAX_LIGHTS];
uniform vec4 uLightColor[MAX_LIGHTS];
uniform float uLightIntensity[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightHeight[MAX_LIGHTS];
uniform float uLightFalloff[MAX_LIGHTS];
uniform uint uLightAffectMask[MAX_LIGHTS];
uniform int uLightShadowLayer[MAX_LIGHTS];
uniform vec2 uShadowViewportOrigin;
uniform vec2 uShadowViewportSize;

vec3 tangentNormal() {
    if (!useNormalTexture) return vec3(0.0, 0.0, 1.0);

    vec3 sampled = texture(uNormalTexture, TexCoord).rgb * 2.0 - 1.0;
    sampled.xy *= uNormalStrength;
    sampled = normalize(sampled);

    // Molga world coordinates are +Y down. Work in a +Y-up plane for the
    // published normal-map convention, while derivatives preserve rotation,
    // UV flips and negative/non-uniform scale.
    vec2 planePosition = vec2(WorldPos.x, -WorldPos.y);
    vec2 dpdx = dFdx(planePosition);
    vec2 dpdy = dFdy(planePosition);
    vec2 duvdx = dFdx(TexCoord);
    vec2 duvdy = dFdy(TexCoord);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(determinant) < 1.0e-8) return vec3(0.0, 0.0, 1.0);

    vec2 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
    vec2 bitangent = normalize((-dpdx * duvdy.x + dpdy * duvdx.x) / determinant);
    return normalize(vec3(tangent * sampled.x + bitangent * sampled.y,
                          sampled.z));
}

float shadowForLight(int lightIndex) {
    int layer = uLightShadowLayer[lightIndex];
    if (layer < 0 || uShadowViewportSize.x <= 0.0 ||
        uShadowViewportSize.y <= 0.0) {
        return 0.0;
    }
    vec2 uv = (gl_FragCoord.xy - uShadowViewportOrigin) /
              uShadowViewportSize;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        return 0.0;
    }
    return texture(uShadowMasks, vec3(uv, float(layer))).r;
}

void main() {
    vec4 diffuse = useTexture ? texture(uTexture, TexCoord) * Color : Color;
    vec3 normal = tangentNormal();
    vec3 lighting = uAmbientColor.rgb * max(uAmbientIntensity, 0.0);
    uint receiverBit = 1u << min(uReceiverLayer, 31u);

    for (int index = 0; index < MAX_LIGHTS; ++index) {
        if (index >= uLightCount) break;
        if ((uLightAffectMask[index] & receiverBit) == 0u) continue;

        vec2 delta = uLightPosition[index] - WorldPos;
        float distanceToLight = length(delta);
        float radius = max(uLightRadius[index], 0.01);
        float radial = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
        if (radial <= 0.0) continue;

        vec3 lightVector =
            vec3(delta.x, -delta.y, uLightHeight[index]);
        float lightVectorLength = length(lightVector);
        vec3 lightDirection = lightVectorLength > 1.0e-8
            ? lightVector / lightVectorLength
            : vec3(0.0, 0.0, 1.0);
        float lambert = max(dot(normal, lightDirection), 0.0);
        float attenuation = pow(radial, max(uLightFalloff[index], 0.1)) *
                            max(uLightIntensity[index], 0.0);
        float visible = 1.0 - clamp(shadowForLight(index), 0.0, 1.0);
        lighting += uLightColor[index].rgb * lambert * attenuation * visible;
    }

    FragColor = vec4(diffuse.rgb * lighting, diffuse.a);
}

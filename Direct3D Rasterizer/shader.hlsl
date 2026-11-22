struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 viewPosition : TEXCOORD0;
    float3 normal : NORMAL;
    float4 lightPosition : TEXCOORD1;
    float3 lightDirectionView : TEXCOORD2;
    float2 uv : TEXCOORD3;
};

cbuffer cb0 : register(b0)
{
    uint angle;
    float roughness;
    float metallic;
    uint textureFlags;
    
    float3 baseColor;
    float paddingColor;
    
    float3 objectScale;
    float paddingScale;

    float3 objectPosition;
    float paddingPosition;
    
    row_major float4x4 lightViewProjection;
    float3 lightDirectionWorld;
    float paddingLight;

    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
};

Texture2D<float> shadowMap : register(t0);
SamplerComparisonState shadowSampler : register(s0);

Texture2D<float4> baseColorMap : register(t1);
Texture2D<float4> roughnessMap : register(t2);
Texture2D<float4> normalMap : register(t3);
Texture2D<float4> metallicMap : register(t4);

SamplerState materialSampler : register(s1);

float3 schlick(float VdotH, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
}

float ggxDistribution(float NdotH , float alpha)
{
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (alpha2 - 1.0f) + 1.0f);
    denom = 3.14159265359f * denom * denom;
    return alpha2 / denom;
}

float G(float NdotX, float alpha)
{
    float k = alpha + 1.0f;
    k = k * k / 8.0f;

    return NdotX /
        max(NdotX * (1.0f - k) + k, 0.000001f);
}

float3 getWorldPosition(float3 position)
{
    float rotation = angle * -0.01f;
    float c = cos(rotation);
    float s = sin(rotation);

    float3 p = position * objectScale;

    return float3(
        p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c
    ) + objectPosition;
}

float4 ShadowVS(VSInput input) : SV_POSITION
{
    float3 worldPosition = getWorldPosition(input.position);

    return mul(
        float4(worldPosition, 1.0f),
        lightViewProjection
    );
}

// Vertex Shader
PSInput VSMain(VSInput input)
{
    PSInput output;
    // Compute the rotation matrix
    float rotation = angle * -0.01f;
    float c = cos(rotation);
    float s = sin(rotation);

    float3 worldPosition = getWorldPosition(input.position);

    float3 scaledNormal = input.normal / objectScale;

    float3 worldNormal = normalize(float3(
        scaledNormal.x * c - scaledNormal.z * s,
        scaledNormal.y,
        scaledNormal.x * s + scaledNormal.z * c
    ));
    
    float3 forward = cameraForward.xyz;
    float3 right = cameraRight.xyz;
    float3 up = cameraUp.xyz;
    
     // 4. Express position relative to the camera.
    float3 relativePosition = worldPosition - cameraPosition.xyz;

    float3 viewPosition = float3(
        dot(relativePosition, right),
        dot(relativePosition, up),
        dot(relativePosition, forward)
    );
    
    // Express the normal in the same coordinate system.
    output.normal = float3(
        dot(worldNormal, right),
        dot(worldNormal, up),
        dot(worldNormal, forward)
    );
    
    float fovY = 60.0f * 3.14159265f / 180.0f;
    float aspectRatio = 1.0f; // Width / height; square viewport.
    float nearPlane = 0.1f;
    float farPlane = 200.0f;

    float yScale = 1.0f / tan(fovY * 0.5f);
    float xScale = yScale / aspectRatio;

    float depthScale = farPlane / (farPlane - nearPlane);
    float depthOffset = -nearPlane * depthScale;

    // View space uses positive Z in front of the camera.
    output.position = float4(
        viewPosition.x * xScale,
        viewPosition.y * yScale,
        viewPosition.z * depthScale + depthOffset,
        viewPosition.z
    );
    
    output.lightPosition = mul(
        float4(worldPosition, 1.0f),
        lightViewProjection
    );

    output.lightDirectionView = float3(
        dot(lightDirectionWorld, right),
        dot(lightDirectionWorld, up),
        dot(lightDirectionWorld, forward)
    );
    
    output.viewPosition = viewPosition;
    output.uv = input.uv;
    
    return output;
}

float getShadowVisibility(float4 lightPosition)
{
    if (lightPosition.w <= 0.0f)
        return 1.0f;

    float3 projected =
        lightPosition.xyz / lightPosition.w;

    float2 uv = float2(
        projected.x * 0.5f + 0.5f,
        -projected.y * 0.5f + 0.5f
    );

    if (any(uv < 0.0f) || any(uv > 1.0f) ||
        projected.z < 0.0f || projected.z > 1.0f)
    {
        return 1.0f;
    }
    
    // Screen-space changes in shadow UV and light-space depth.
    
    float3 dx = ddx(float3(uv, projected.z));
    float3 dy = ddy(float3(uv, projected.z));

// Convert those into depth change per unit of shadow UV.
    float determinant = dx.x * dy.y - dx.y * dy.x;

    float2 depthGradient = float2(0.0f, 0.0f);

    if (abs(determinant) > 0.0000000001f)
    {
        depthGradient = float2(
        dy.y * dx.z - dx.y * dy.z,
        dx.x * dy.z - dy.x * dx.z
    ) / determinant;
    }

    uint width, height;
    shadowMap.GetDimensions(width, height);

    float2 texelSize = 1.0f / float2(width, height);

    float bias = 0.0003f;
    float visibility = 0.0f;

    // Radius 1 means a 3×3 grid: nine comparison samples.
        const int radius = 6;

    [unroll]
        for (int y = -radius; y <= radius; ++y)
        {
        [unroll]
            for (int x = -radius; x <= radius; ++x)
            {
            float2 offset = float2(x, y) * texelSize;

                // Expected depth of this surface at the offset location.
                            float referenceDepth =
                    projected.z + dot(depthGradient, offset);

                            visibility += shadowMap.SampleCmpLevelZero(
                    shadowSampler,
                    uv + offset,
                    referenceDepth - bias
                );
            }
        }

    const int diameter = radius * 2 + 1;
    return visibility / float(diameter * diameter);
}

float3 applyNormalMap(
    float3 viewPosition,
    float3 interpolatedNormal,
    float2 uv,
    float3 tangentNormal)
{
    float3 N = normalize(interpolatedNormal);

    float3 dpdx = ddx(viewPosition);
    float3 dpdy = ddy(viewPosition);

    float2 duvdx = ddx(uv);
    float2 duvdy = ddy(uv);

    float determinant =
        duvdx.x * duvdy.y -
        duvdx.y * duvdy.x;

    if (abs(determinant) < 0.00000001f)
        return N;

    float3 T =
        (dpdx * duvdy.y - dpdy * duvdx.y)
        / determinant;

    float3 B =
        (dpdy * duvdx.x - dpdx * duvdy.x)
        / determinant;

    T -= N * dot(N, T);

    if (dot(T, T) < 0.00000001f)
        return N;

    T = normalize(T);

    float handedness =
        dot(cross(N, T), B) < 0.0f ? -1.0f : 1.0f;

    B = cross(N, T) * handedness;

    return normalize(
        tangentNormal.x * T +
        tangentNormal.y * B +
        tangentNormal.z * N
    );
}

float3 linearToSRGB(float3 color)
{
    color = max(color, 0.0f);

    float3 low = color * 12.92f;
    float3 high =
        1.055f * pow(color, 1.0f / 2.4f) - 0.055f;

    return float3(
        color.r <= 0.0031308f ? low.r : high.r,
        color.g <= 0.0031308f ? low.g : high.g,
        color.b <= 0.0031308f ? low.b : high.b
    );
}

// Pixel Shader
float4 PSMain(PSInput input) : SV_TARGET
{
    
    float3 N = normalize(input.normal);

    if ((textureFlags & 4u) != 0u)
    {
            float3 tangentNormal = normalMap.Sample(
            materialSampler,
            input.uv
        ).xyz * 2.0f - 1.0f;

            N = applyNormalMap(
            input.viewPosition,
            input.normal,
            input.uv,
            tangentNormal
        );
    }
    
    float3 L = normalize(input.lightDirectionView);
    float3 V = normalize(-input.viewPosition);
    float3 H = normalize(L + V);
    
    float NdotL = max(dot(N, L), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    float NDotV = max(dot(N, V), 0.0f);
    
    float3 surfaceColor = baseColor;
    float surfaceRoughness = roughness;
    float surfaceMetallic = metallic;

    if ((textureFlags & 1u) != 0u)
    {
        surfaceColor *= baseColorMap.Sample(
        materialSampler, input.uv
    ).rgb;
    }

    if ((textureFlags & 2u) != 0u)
    {
        surfaceRoughness *= roughnessMap.Sample(
        materialSampler, input.uv
    ).r;
    }

    if ((textureFlags & 8u) != 0u)
    {
        surfaceMetallic *= metallicMap.Sample(
        materialSampler, input.uv
    ).r;
    }

    float r = clamp(surfaceRoughness, 0.05f, 1.0f);
    float m = saturate(surfaceMetallic);
    
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f),surfaceColor,m);
    
    float3 F = schlick(VdotH,F0);
    
    float3 Ks = F;
    float3 Kd = (1.0 - F) * (1.0f - m);
    
    float3 SpecularBRDF = (ggxDistribution(NdotH,r * r) * F * G(NdotL,r) * G(NDotV,r)) / (4.0f * NdotL * NDotV + 0.001f);
    
    float3 LambertianBRDF = surfaceColor / 3.14159265359f;
    float3 DiffuseBRDF = Kd * LambertianBRDF;
    
    float shadowVisibility = getShadowVisibility(input.lightPosition);
    float visibility = lerp(0.05f, 1.0f, shadowVisibility);

    float3 finalColor = (DiffuseBRDF + SpecularBRDF) * NdotL * visibility * 2.5f;

    return float4(
    linearToSRGB(saturate(finalColor)),
    1.0f
);


}

struct SkyInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

SkyInput SkyVS(uint vertexID : SV_VertexID)
{
    // One large triangle covering the whole viewport.
    const float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    SkyInput output;

    float2 position = positions[vertexID];

    output.position = float4(position, 0.0f, 1.0f);
    output.uv = position * 0.5f + 0.5f;

    return output;
}

float4 SkyPS(SkyInput input) : SV_TARGET
{
    float3 bottomColor = float3(0.95f, 0.97f, 1.0f);
    float3 topColor = float3(0.55f, 0.75f, 0.95f);

    float amount = saturate(input.uv.y);

    float3 color = lerp(
        bottomColor,
        topColor,
        amount
    );

    return float4(color, 1.0f);
}



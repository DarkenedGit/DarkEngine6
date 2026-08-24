// Shared CSM receiver. Define SHADOW_T before including (t1 mesh, t5 terrain).
#ifndef DE_SHADOW_HLSLI
#define DE_SHADOW_HLSLI

#ifndef SHADOW_T
#define SHADOW_T t5
#endif

#pragma pack_matrix(row_major)

cbuffer ShadowConstants : register(b1)
{
    float4x4 cascadeViewProj[3];
    float4   cascadeSplits; // xyz = cascade far in view-Z, w = map size
    float4   shadowParams;  // x=world-space bias (m), y=strength, z=count, w=array slice offset
    float3   shadowLook;
    float    _shadowPad;
    float4   cascadeInvZ;   // xyz = 1 / light-space Z range per cascade
};

Texture2DArray           gShadowMap  : register(SHADOW_T);
SamplerComparisonState   gShadowSamp : register(s1);

int SelectCascade(float viewZ)
{
    int c = 0;
    if (viewZ > cascadeSplits.x)
        c = 1;
    if (viewZ > cascadeSplits.y)
        c = 2;
    int maxC = (int)shadowParams.z - 1;
    if (maxC < 0)
        maxC = 0;
    return min(c, maxC);
}

float SampleCascadePCF(float3 worldPos, int cascade)
{
    float4 lp = mul(float4(worldPos, 1.0f), cascadeViewProj[cascade]);
    float  w  = max(abs(lp.w), 1e-5f);
    float3 uvz = lp.xyz / w;
    uvz.xy = uvz.xy * float2(0.5f, -0.5f) + 0.5f;
    // World-space bias -> NDC using this cascade's ortho Z range so a 1m
    // caster still wins against the receiver on a large terrain.
    uvz.z -= shadowParams.x * cascadeInvZ[cascade];

    if (uvz.x < 0.0f || uvz.x > 1.0f || uvz.y < 0.0f || uvz.y > 1.0f || uvz.z < 0.0f || uvz.z > 1.0f)
        return 1.0f;

    float  mapSize = max(cascadeSplits.w, 1.0f);
    float2 texel   = 1.0f / mapSize;
    float  sum     = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 uv = float3(uvz.xy + float2(x, y) * texel, (float)cascade + shadowParams.w);
            sum += gShadowMap.SampleCmpLevelZero(gShadowSamp, uv, uvz.z);
        }
    }
    return sum / 9.0f;
}

float ComputeShadow(float3 worldPos, float3 cameraPos)
{
    float strength = saturate(shadowParams.y);
    if (strength <= 0.0f)
        return 1.0f;

    float viewZ = dot(worldPos - cameraPos, shadowLook);
    int   cas   = SelectCascade(viewZ);
    float s     = SampleCascadePCF(worldPos, cas);
    return lerp(1.0f, s, strength);
}

#endif

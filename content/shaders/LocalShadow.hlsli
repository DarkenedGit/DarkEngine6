// Local point/spot shadow sample. Included only from LocalLightVolume.hlsl.
// GpuLocalLight is defined by that file. Do not include Shadow.hlsli here.
#ifndef DE_LOCAL_SHADOW_HLSLI
#define DE_LOCAL_SHADOW_HLSLI

struct GpuLocalShadowFace
{
    float4x4 viewProj;
    float    zn;
    float    zf;
    float    tanHalfFov;
    float    slice;
};

struct GpuLocalShadowRecord
{
    float faceCount;
    float mapSize;
    float depthBias;
    float strength;
    GpuLocalShadowFace faces[6];
};

Texture2DArray<float>                  gLocalShadow        : register(t7);
StructuredBuffer<GpuLocalShadowRecord> gLocalShadowRecords : register(t8);
SamplerComparisonState                 gLocalShadowSamp    : register(s1);

int SelectPointShadowFace(float3 fromLight)
{
    float3 ad = abs(fromLight);
    if (ad.x >= ad.y && ad.x >= ad.z)
        return fromLight.x >= 0.0f ? 0 : 1;
    if (ad.y >= ad.z)
        return fromLight.y >= 0.0f ? 2 : 3;
    return fromLight.z >= 0.0f ? 4 : 5;
}

bool ProjectLocalShadow(float3 shadowPos, GpuLocalShadowFace face, float depthBias, float mapSize, out float3 uvz)
{
    float4 clip = mul(float4(shadowPos, 1.0f), face.viewProj);
    if (clip.w <= face.zn * 0.5f)
        return false;
    uvz = clip.xyz / max(abs(clip.w), 1e-5f);
    uvz.xy = uvz.xy * float2(0.5f, -0.5f) + 0.5f;
    float viewZ = max(clip.w, face.zn);
    float m43   = face.zn * face.zf / max(face.zf - face.zn, 1e-4f);
    float dz    = depthBias * m43 / max(viewZ * viewZ, 1e-4f);
    dz          = min(dz, 4.0f / max(mapSize, 1.0f));
    uvz.z       = min(uvz.z + dz, 1.0f);
    if (uvz.x < 0.0f || uvz.x > 1.0f || uvz.y < 0.0f || uvz.y > 1.0f || uvz.z < 0.0f || uvz.z > 1.0f)
        return false;
    return true;
}

float SampleLocalShadowTap(GpuLocalLight light, float3 worldPos)
{
    if (light.pad < 0.0f)
        return 1.0f;
    GpuLocalShadowRecord rec = gLocalShadowRecords[(uint)light.pad];
    int face = 0;
    if (light.type < 0.5f)
        face = SelectPointShadowFace(worldPos - light.pos);
    float3 uvz;
    if (!ProjectLocalShadow(worldPos, rec.faces[face], rec.depthBias, rec.mapSize, uvz))
        return 1.0f;
    float s = gLocalShadow.SampleCmpLevelZero(gLocalShadowSamp, float3(uvz.xy, rec.faces[face].slice), uvz.z);
    return lerp(1.0f, s, saturate(rec.strength));
}

float SampleLocalShadow(GpuLocalLight light, float3 worldPos, float3 n, out int faceId)
{
    faceId = 0;
    if (light.pad < 0.0f)
        return 1.0f;
    GpuLocalShadowRecord rec = gLocalShadowRecords[(uint)light.pad];
    int face = 0;
    if (light.type < 0.5f)
        face = SelectPointShadowFace(worldPos - light.pos);
    faceId = face;
    GpuLocalShadowFace f = rec.faces[face];

    float4 clip = mul(float4(worldPos, 1.0f), f.viewProj);
    float viewZ = max(clip.w, f.zn);
    float3 toLight = light.pos - worldPos;
    float ndotl = saturate(dot(n, toLight / max(length(toLight), 1e-4f)));
    float texelWorld = (2.0f * viewZ * f.tanHalfFov) / max(rec.mapSize, 1.0f);
    float recvOffset = texelWorld * (1.0f + 3.0f * (1.0f - ndotl) * (1.0f - ndotl));
    float3 shadowPos = worldPos + n * recvOffset;

    float3 uvz;
    if (!ProjectLocalShadow(shadowPos, f, rec.depthBias, rec.mapSize, uvz))
        return 1.0f;

    float mapSize = max(rec.mapSize, 1.0f);
    float2 texel = 1.0f / mapSize;
    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 uv = uvz.xy + float2(x, y) * texel;
            sum += gLocalShadow.SampleCmpLevelZero(gLocalShadowSamp, float3(uv, f.slice), uvz.z);
        }
    }
    return lerp(1.0f, sum / 9.0f, saturate(rec.strength));
}

#endif

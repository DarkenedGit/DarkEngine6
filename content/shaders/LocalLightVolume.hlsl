// Additive deferred point/spot volumes. Depth is a G-buffer SRV; coverage is analytic in the PS.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "PbrLighting.hlsli"
#include "Fog.hlsli"

cbuffer LocalLightPassConstants : register(b0)
{
    float4x4 invViewProj;
    float4x4 viewProj;
    float3   cameraPos;
    float    fogDensity;
    float3   fogColor;
    float    lighting;
    uint     baseIndex;
    uint     lightIndex;
    float    viewportW;
    float    viewportH;
    float    heightFogDensity;
    float    heightFogFalloff;
    float    heightFogHeight;
    float    volumetricFogDensity;
    float    waterLevel;
    float    volumetricHeight;
    float    heightOriginX;
    float    heightOriginZ;
    float    heightCellSize;
    float    heightWorldSizeX;
    float    heightWorldSizeZ;
    float    _padFog;
};

Texture2D    gAlbedo     : register(t0);
Texture2D    gAttrib     : register(t1);
Texture2D    gDepth      : register(t2);
Texture2D    gHeightMap  : register(t6);
SamplerState gHeightSamp : register(s0);

struct GpuLocalLight
{
    float3 pos;
    float  range;
    float3 color;
    float  invRange2;
    float3 dir;
    float  type;
    float  innerCos;
    float  outerCos;
    float  sourceRadius;
    float  pad;
};

StructuredBuffer<GpuLocalLight> gLights      : register(t4);
StructuredBuffer<float4x4>      gVolumeWorld : register(t5);

struct PSInput
{
    float4 position : SV_POSITION;
    nointerpolation uint iid : INSTANCEID;
};

PSInput VSMain(float3 pos : POSITION, uint iid : SV_InstanceID)
{
    PSInput o;
    float3 world = mul(float4(pos, 1.0f), gVolumeWorld[iid + baseIndex]).xyz;
    o.position   = mul(float4(world, 1.0f), viewProj);
    o.iid        = iid;
    return o;
}

PSInput VSFullscreen(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.iid      = 0;
    return o;
}

FogParams MakeFogParams()
{
    FogParams p;
    p.cameraPos            = cameraPos;
    p.lightDir             = float3(0, 1, 0);
    p.lightColor           = 0.0.xxx;
    p.ambientColor         = 0.0.xxx;
    p.fogColor             = fogColor;
    p.fogDensity           = fogDensity;
    p.heightFogDensity     = heightFogDensity;
    p.heightFogFalloff     = heightFogFalloff;
    p.heightFogHeight      = heightFogHeight;
    p.volumetricFogDensity = volumetricFogDensity;
    p.waterLevel           = waterLevel;
    p.volumetricHeight     = volumetricHeight;
    p.heightOrigin         = float2(heightOriginX, heightOriginZ);
    p.heightCellSize       = heightCellSize;
    p.heightWorldSize      = float2(heightWorldSizeX, heightWorldSizeZ);
    return p;
}

float LightAttenuation(GpuLocalLight light, float3 p)
{
    float3 toLight = light.pos - p;
    float  d       = length(toLight);
    float3 l       = toLight / max(d, 1e-4f);
    float  att     = windowedDistanceAttenuation(d * d, light.invRange2);
    if (light.type >= 0.5f)
        att *= spotAngleAttenuation(dot(-l, light.dir), light.innerCos, light.outerCos);
    return att;
}

float3 LocalLightFogScatter(float3 cam, float3 worldPos, GpuLocalLight light, FogParams fp)
{
    float3 ray  = worldPos - cam;
    float  dist = length(ray);
    if (dist < 1e-3f)
        return 0.0.xxx;
    float3 dir = ray / dist;
    const int kSteps = 8;
    float  dt = dist / float(kSteps);
    float  T  = 1.0f;
    float3 s  = 0.0.xxx;
    float3 albedo = FogAlbedo(fp);
    [unroll]
    for (int i = 0; i < kSteps; ++i)
    {
        float3 p        = cam + dir * ((float(i) + 0.5f) * dt);
        float  terrainY = FogSampleTerrainY(gHeightMap, gHeightSamp, p.xz, fp);
        float  d        = fp.fogDensity + FogHeightDensity(p.y, fp) + FogValleyDensity(p, terrainY, fp);
        // Clamp 1/d^2 so a sample at the light origin cannot dump candela into a solid ball.
        float  att      = min(LightAttenuation(light, p), 1.5f);
        s += T * (d * dt) * albedo * light.color * att * 0.0015f;
        T *= exp(-d * dt);
    }
    return saturate(s);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    if (lighting < 0.5f)
        discard;

    int2  texel = int2(input.position.xy);
    float depth = gDepth.Load(int3(texel, 0)).r;
    if (depth >= 1.0f - 1e-6f)
        discard;

    uint w, h;
    gDepth.GetDimensions(w, h);
    float  ndcX     = (input.position.x / float(w)) * 2.0f - 1.0f;
    float  ndcY     = 1.0f - (input.position.y / float(h)) * 2.0f;
    float3 worldPos = ReconstructWorldPos(ndcX, ndcY, depth, invViewProj);

    float4 albedo    = gAlbedo.Load(int3(texel, 0));
    float4 attrib    = gAttrib.Load(int3(texel, 0));
    float3 n         = DecodeOct(attrib.rg);
    float  roughness = attrib.b;
    float  metallic  = attrib.a;

    GpuLocalLight light   = gLights[baseIndex + input.iid];
    float3        toLight = light.pos - worldPos;
    float         d       = length(toLight);
    float3        l       = toLight / max(d, 1e-4f);
    float         cosTheta = dot(-l, light.dir);

    if (light.type < 0.5f)
    {
        if (d > light.range)
            discard;
    }
    else
    {
        if (cosTheta < light.outerCos)
            discard;
        if (d > light.range)
            discard;
    }

    float3 v   = normalize(cameraPos - worldPos);
    float3 lit = PbrPunctual(n, v, albedo.rgb, roughness, metallic, toLight, light.color, light.sourceRadius);
    lit *= windowedDistanceAttenuation(d * d, light.invRange2);
    if (light.type >= 0.5f)
        lit *= spotAngleAttenuation(cosTheta, light.innerCos, light.outerCos);

    FogParams fp  = MakeFogParams();
    FogResult fog = FogIntegrate(cameraPos, worldPos, fp, gHeightMap, gHeightSamp, 1.0f);
    lit *= fog.transmittance;
    lit += LocalLightFogScatter(cameraPos, worldPos, light, fp);

    return float4(lit, 0.0f);
}

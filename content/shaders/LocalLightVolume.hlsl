// Additive deferred point/spot volumes. Depth is a G-buffer SRV; coverage is analytic in the PS.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "PbrLighting.hlsli"
#include "Fog.hlsli"
#include "Ultraviolet.hlsli"

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
    float    localShadowDebug;
};

Texture2D    gAlbedo     : register(t0);
Texture2D    gAttrib     : register(t1);
Texture2D    gDepth      : register(t2);
Texture2D    gHeightMap  : register(t6);
SamplerState gHeightSamp : register(s0);

struct GpuFogOccluder
{
    float4 bmin;
    float4 bmax;
};

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
    float  occluderCount;
    float  occluderPad0;
    float  occluderPad1;
    float  occluderPad2;
    GpuFogOccluder occluders[8]; // kMaxFogOccluders
};

StructuredBuffer<GpuLocalLight> gLights      : register(t4);
StructuredBuffer<float4x4>      gVolumeWorld : register(t5);

#include "LocalShadow.hlsli"

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

bool FogSlab(float origin, float delta, float mn, float mx, inout float t0, inout float t1)
{
    if (abs(delta) <= 1e-8f)
        return origin >= mn && origin <= mx;
    float inv = 1.0f / delta;
    float a = (mn - origin) * inv;
    float b = (mx - origin) * inv;
    if (a > b)
    {
        float s = a;
        a = b;
        b = s;
    }
    t0 = max(t0, a);
    t1 = min(t1, b);
    return t0 <= t1;
}

// Same rule as fogBoxBlocks. A hit within 0.02 m of the light, or of the shaded point, stays clear.
bool FogBoxBlocks(float3 from, float3 to, float4 bmin, float4 bmax)
{
    float3 delta = to - from;
    float travel = length(delta);
    if (travel < 1e-4f)
        return false;
    float t0 = 0.0f;
    float t1 = 1.0f;
    if (!FogSlab(from.x, delta.x, bmin.x, bmax.x, t0, t1))
        return false;
    if (!FogSlab(from.y, delta.y, bmin.y, bmax.y, t0, t1))
        return false;
    if (!FogSlab(from.z, delta.z, bmin.z, bmax.z, t0, t1))
        return false;
    float entry = t0 * travel;
    return entry > 0.02f && (travel - entry) > 0.02f;
}

float BoxVisibility(GpuLocalLight light, float3 p)
{
    int n = (int)light.occluderCount;
    if (n > 8)
        n = 8;
    [loop]
    for (int i = 0; i < n; ++i)
    {
        if (FogBoxBlocks(light.pos, p, light.occluders[i].bmin, light.occluders[i].bmax))
            return 0.0f;
    }
    return 1.0f;
}

float FogSampleShadow(GpuLocalLight light, float3 p)
{
    float s = light.pad >= 0.0f ? SampleLocalShadowTap(light, p) : 1.0f;
    return s * BoxVisibility(light, p);
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
        float  shadow   = FogSampleShadow(light, p);
        s += T * (d * dt) * albedo * light.color * att * shadow * 0.0015f;
        T *= exp(-d * dt);
    }
    return saturate(s);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Positive is lit. Negative is the ultraviolet strength. Zero discards.
    const float uvStrength = saturate(-lighting);
    if (!(lighting > 0.5f || uvStrength > 1.0e-4f))
        discard;

    int2  texel = int2(input.position.xy);
    float depth = gDepth.Load(int3(texel, 0)).r;
    if (IsSkyDepth(depth))
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
    int    faceId = 0;
    float  shadow = SampleLocalShadow(light, worldPos, n, faceId) * BoxVisibility(light, worldPos);
    float  att = windowedDistanceAttenuation(d * d, light.invRange2);
    if (light.type >= 0.5f)
        att *= spotAngleAttenuation(cosTheta, light.innerCos, light.outerCos);

    if (light.pad >= 0.0f && localShadowDebug > 0.5f && localShadowDebug < 1.5f)
        return float4(shadow, shadow, shadow, 0.0f);
    if (light.pad >= 0.0f && localShadowDebug >= 1.5f)
    {
        float3 faceColor = float3(1.0f, 0.0f, 0.0f);
        if (faceId == 1)
            faceColor = float3(0.35f, 0.0f, 0.0f);
        else if (faceId == 2)
            faceColor = float3(0.0f, 1.0f, 0.0f);
        else if (faceId == 3)
            faceColor = float3(0.0f, 0.35f, 0.0f);
        else if (faceId == 4)
            faceColor = float3(0.0f, 0.0f, 1.0f);
        else if (faceId == 5)
            faceColor = float3(0.0f, 0.0f, 0.35f);
        return float4(faceColor * shadow * att, 0.0f);
    }

    float3 surf = beeShift(albedo.rgb, albedo.a, uvStrength);
    float3 lit = PbrPunctual(n, v, surf, roughness, metallic, toLight, light.color, light.sourceRadius);
    lit *= att;
    lit *= shadow;

    FogParams fp  = MakeFogParams();
    if (uvStrength > 1.0e-4f)
        fp.fogColor = beeShift(fp.fogColor, kUvFog, uvStrength);
    FogResult fog = FogIntegrate(cameraPos, worldPos, fp, gHeightMap, gHeightSamp, 1.0f);
    lit *= fog.transmittance;
    lit += LocalLightFogScatter(cameraPos, worldPos, light, fp);

    return float4(lit, 0.0f);
}

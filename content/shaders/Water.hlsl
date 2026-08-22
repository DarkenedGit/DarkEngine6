// Multi-frequency Gerstner water. Vertex Y is rest water level; waves are
// evaluated here so every LOD uses the same closed form (no edge cracks).
// TEXCOORD0.y is terrain height at that XZ, used for shore fade.
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float3   cameraPos;
    float    time;
    float3   lightDir;
    float    waterLevel;
    float2   flowDir;
    float    flowStrength;
    float    specPower;
    float4   waves[4];     // xy dir, z freq, w amp
    float4   waveSpeed;
    float3   deepColor;
    float    opacity;
    float3   shallowColor;
    float    shoreDepth;
    float3   skyZenith;
    float    fresnelF0;
    float3   skyHorizon;
    float    steepness;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0; // y = terrain height
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normalWS  : TEXCOORD1;
    float  terrainY  : TEXCOORD2;
};

void Gerstner(float2 xz, out float3 offset, out float3 normal)
{
    float y = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float2 horiz = 0.0f;
    float Q = saturate(steepness);

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 D = waves[i].xy;
        float  k = waves[i].z;
        float  A = waves[i].w;
        if (A <= 0.0f || k <= 0.0f)
            continue;

        float dp = dot(D, xz) * k + time * waveSpeed[i];
        float s  = sin(dp);
        float c  = cos(dp);
        float wa = k * A;

        y     += A * s;
        horiz += D * (Q * A * c);
        nx    += D.x * wa * c;
        nz    += D.y * wa * c;
        ny    -= Q * wa * s;
    }

    offset = float3(horiz.x, y, horiz.y);
    normal = normalize(float3(-nx, ny, -nz));
}

PSInput VSMain(VSInput input)
{
    PSInput o;
    float3 offset;
    float3 n;
    Gerstner(input.position.xz, offset, n);

    float3 world = input.position + offset;
    o.worldPos  = world;
    o.normalWS  = n;
    o.terrainY  = input.uv.y;
    o.position  = mul(float4(world, 1.0f), worldViewProj);
    return o;
}

float3 SkyColor(float3 dir)
{
    float t = saturate(dir.y * 0.5f + 0.5f);
    return lerp(skyHorizon, skyZenith, t);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);

    float depth = waterLevel - input.terrainY;
    float shallow = saturate(1.0f - depth / max(shoreDepth, 1e-3f));
    float3 body = lerp(deepColor, shallowColor, shallow);
    float alpha = opacity * saturate(depth / max(shoreDepth * 0.35f, 1e-3f));

    if (specPower < 0.0f)
    {
        alpha = saturate(alpha);
        return float4(body, alpha);
    }

    float3 v = normalize(cameraPos - input.worldPos);
    float3 l = normalize(lightDir);

    float ndotv = saturate(dot(n, v));
    float fres  = fresnelF0 + (1.0f - fresnelF0) * pow(1.0f - ndotv, 5.0f);

    float3 r    = reflect(-v, n);
    float3 sky  = SkyColor(r);

    float3 h     = normalize(l + v);
    float  spec  = pow(saturate(dot(n, h)), specPower);
    float  ndotl = saturate(dot(n, l));

    float3 color = body * (0.18f + 0.55f * ndotl);
    color = lerp(color, sky, fres);
    color += spec * 0.85f;

    // Shore: fade out as the land rises through the surface.
    alpha = saturate(alpha + fres * 0.15f);
    return float4(color, alpha);
}

// Multi-layer height-map terrain. Vertex UV is 0-1 across the whole map;
// each layer is tiled in the pixel shader. Splat RGBA = layer weights.
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float3   lightDirWS;
    float    fogDensity;
    float4   layerTiling;
    float3   lightColor;
    float    cameraPosX;
    float3   ambientColor;
    float    cameraPosY;
    float3   fogColor;
    float    cameraPosZ;
    float    lighting; // 1 = lit, 0 = albedo only
};

Texture2D    gLayer0 : register(t0);
Texture2D    gLayer1 : register(t1);
Texture2D    gLayer2 : register(t2);
Texture2D    gLayer3 : register(t3);
Texture2D    gSplat  : register(t4);
SamplerState gSamp   : register(s0);

#define SHADOW_T t5
#include "Shadow.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    float4 wp = mul(float4(input.position, 1.0f), world);
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    o.worldPos = wp.xyz;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 splat = gSplat.Sample(gSamp, input.uv);
    float  wsum  = splat.r + splat.g + splat.b + splat.a + 1e-5f;
    splat /= wsum;

    float4 albedo =
          splat.r * gLayer0.Sample(gSamp, input.uv * layerTiling.x)
        + splat.g * gLayer1.Sample(gSamp, input.uv * layerTiling.y)
        + splat.b * gLayer2.Sample(gSamp, input.uv * layerTiling.z)
        + splat.a * gLayer3.Sample(gSamp, input.uv * layerTiling.w);

    albedo *= color;
    if (lighting < 0.5f)
        return albedo;

    float3 n     = normalize(input.normalWS);
    float3 l     = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float3 cam   = float3(cameraPosX, cameraPosY, cameraPosZ);
    // Heightfield is drawn into the cascades. A few centimetres of normal
    // offset (more on grazing slopes) hides LOD acne without leaping past a
    // 1m cube sitting on the ground. The old 0.75–2.5m offset sampled above
    // the cube and erased its contact shadow.
    float  recvOffset = 0.06f + 0.28f * (1.0f - ndotl) * (1.0f - ndotl);
    float  shadow = ComputeShadow(input.worldPos + n * recvOffset, cam);
    float3 ambient = ambientColor * albedo.rgb;
    float3 diffuse = ndotl * lightColor * albedo.rgb * shadow;
    float3 lit     = ambient + diffuse;
    float dist = length(input.worldPos - cam);
    float fog  = 1.0f - exp(-fogDensity * dist);
    lit = lerp(lit, fogColor, saturate(fog));
    return float4(lit, albedo.a);
}

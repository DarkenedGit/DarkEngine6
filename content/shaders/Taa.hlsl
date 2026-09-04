// Temporal AA: reproject history with the G-buffer velocity, neighborhood-clamp, blend.
#pragma pack_matrix(row_major)

cbuffer TaaConstants : register(b0)
{
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float    blend; // current-frame weight (0.1 typical)
    float    reset; // 1 = ignore history
    float    invWidth;
    float    invHeight;
};

Texture2D    gCurrent  : register(t0);
Texture2D    gHistory  : register(t1);
Texture2D    gVelocity : register(t2);
Texture2D    gDepth    : register(t3);
SamplerState gLin      : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float2 ReconstructCameraVelocity(float2 pixel, float depth)
{
    float2 uvCurr = pixel * float2(invWidth, invHeight);
    float2 ndc    = float2(uvCurr.x * 2.0f - 1.0f, 1.0f - uvCurr.y * 2.0f);
    float4 world  = mul(float4(ndc, depth, 1.0f), invViewProj);
    world /= max(world.w, 1e-6f);
    float4 prevClip = mul(world, prevViewProj);
    float2 prevNdc  = prevClip.xy / max(abs(prevClip.w), 1e-5f);
    float2 uvPrev   = prevNdc * float2(0.5f, -0.5f) + 0.5f;
    return uvCurr - uvPrev;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2   texel   = int2(input.position.xy);
    float3 current = gCurrent.Load(int3(texel, 0)).rgb;
    if (reset > 0.5f)
        return float4(current, 1);

    float  depth = gDepth.Load(int3(texel, 0)).r;
    float2 vel   = gVelocity.Load(int3(texel, 0)).rg;
    if (depth >= 1.0f - 1e-5f)
        vel = ReconstructCameraVelocity(input.position.xy, depth);

    float2 uv     = input.position.xy * float2(invWidth, invHeight);
    float2 histUv = uv - vel;

    float3 history = current;
    bool   valid   = histUv.x > 0.0f && histUv.x < 1.0f && histUv.y > 0.0f && histUv.y < 1.0f;
    if (valid)
        history = gHistory.SampleLevel(gLin, histUv, 0).rgb;

    float3 boxMin = current;
    float3 boxMax = current;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            float3 n = gCurrent.Load(int3(texel + int2(x, y), 0)).rgb;
            boxMin = min(boxMin, n);
            boxMax = max(boxMax, n);
        }
    }
    history = clamp(history, boxMin, boxMax);

    float w = saturate(blend);
    if (!valid)
        w = 1.0f;
    float3 outRgb = lerp(history, current, w);
    return float4(outRgb, 1);
}

// Camera + object motion blur. Samples HDR along UV velocity from the G-buffer.
// Sky (depth ~ 1) has no velocity write; reconstruct camera motion from prev/curr clip.
#pragma pack_matrix(row_major)

cbuffer MotionBlurConstants : register(b0)
{
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float    strength;   // 0-1
    float    maxPixels;
    float    invWidth;
    float    invHeight;
};

Texture2D    gHdr      : register(t0);
Texture2D    gVelocity : register(t1);
Texture2D    gDepth    : register(t2);
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
    int2   texel = int2(input.position.xy);
    float3 sharp = gHdr.Load(int3(texel, 0)).rgb;
    if (strength <= 1e-4f)
        return float4(sharp, 1);

    float  depth = gDepth.Load(int3(texel, 0)).r;
    float2 vel   = gVelocity.Load(int3(texel, 0)).rg;
    if (depth >= 1.0f - 1e-5f)
        vel = ReconstructCameraVelocity(input.position.xy, depth);

    vel *= strength;
    float2 pix = vel * float2(1.0f / invWidth, 1.0f / invHeight);
    float  len = length(pix);
    if (len < 0.35f)
        return float4(sharp, 1);
    if (len > maxPixels && len > 1e-5f)
        pix *= (maxPixels / len);

    float3 acc = sharp;
    const int kTaps = 8;
    [unroll]
    for (int i = 1; i <= kTaps; ++i)
    {
        float  t  = ((float)i / (float)kTaps) - 0.5f;
        float2 uv = (input.position.xy + t * pix) * float2(invWidth, invHeight);
        uv        = saturate(uv);
        acc += gHdr.SampleLevel(gLin, uv, 0).rgb;
    }
    return float4(acc / (float)(kTaps + 1), 1);
}

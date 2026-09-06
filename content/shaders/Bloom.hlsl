// Karis bloom: ½-res extract, 4-tap downsample, 9-tap tent upsample, additive composite.
#pragma pack_matrix(row_major)

cbuffer BloomConstants : register(b0)
{
    float invSrcWidth;
    float invSrcHeight;
    float invDstWidth;
    float invDstHeight;
    float threshold; // 1.0
    float knee;      // 0.5
    float strength;
    float _pad;
};

Texture2D    gSrc : register(t0);
SamplerState gLin : register(s0);

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

float2 destUv(float2 pixel)
{
    return pixel * float2(invDstWidth, invDstHeight);
}

float luma(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 softKnee(float3 c)
{
    float l     = luma(c);
    float soft  = saturate((l - threshold + knee) / max(2.0f * knee, 1e-4f));
    float contrib = max(l - threshold, 0.0f) + knee * soft * soft;
    return c * (contrib / max(l, 1e-4f));
}

float3 sample4Tap(float2 uv)
{
    float2 t = float2(invSrcWidth, invSrcHeight);
    float3 c = gSrc.SampleLevel(gLin, uv + t * float2(-0.5f, -0.5f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(0.5f, -0.5f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(-0.5f, 0.5f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(0.5f, 0.5f), 0).rgb;
    return c * 0.25f;
}

float3 sampleKaris4(float2 uv)
{
    float2 t = float2(invSrcWidth, invSrcHeight);
    float3 c0 = gSrc.SampleLevel(gLin, uv + t * float2(-0.5f, -0.5f), 0).rgb;
    float3 c1 = gSrc.SampleLevel(gLin, uv + t * float2(0.5f, -0.5f), 0).rgb;
    float3 c2 = gSrc.SampleLevel(gLin, uv + t * float2(-0.5f, 0.5f), 0).rgb;
    float3 c3 = gSrc.SampleLevel(gLin, uv + t * float2(0.5f, 0.5f), 0).rgb;
    float w0 = 1.0f / (1.0f + luma(c0));
    float w1 = 1.0f / (1.0f + luma(c1));
    float w2 = 1.0f / (1.0f + luma(c2));
    float w3 = 1.0f / (1.0f + luma(c3));
    return (c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) / max(w0 + w1 + w2 + w3, 1e-4f);
}

float3 sampleTent9(float2 uv)
{
    float2 t = float2(invSrcWidth, invSrcHeight);
    float3 c = gSrc.SampleLevel(gLin, uv + t * float2(-1.0f, -1.0f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(0.0f, -1.0f), 0).rgb * 2.0f;
    c += gSrc.SampleLevel(gLin, uv + t * float2(1.0f, -1.0f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(-1.0f, 0.0f), 0).rgb * 2.0f;
    c += gSrc.SampleLevel(gLin, uv + t * float2(0.0f, 0.0f), 0).rgb * 4.0f;
    c += gSrc.SampleLevel(gLin, uv + t * float2(1.0f, 0.0f), 0).rgb * 2.0f;
    c += gSrc.SampleLevel(gLin, uv + t * float2(-1.0f, 1.0f), 0).rgb;
    c += gSrc.SampleLevel(gLin, uv + t * float2(0.0f, 1.0f), 0).rgb * 2.0f;
    c += gSrc.SampleLevel(gLin, uv + t * float2(1.0f, 1.0f), 0).rgb;
    return c * (1.0f / 16.0f);
}

float4 PSExtract(PSInput input) : SV_TARGET
{
    float3 c = sampleKaris4(destUv(input.position.xy));
    return float4(softKnee(c), 1.0f);
}

float4 PSDownsample(PSInput input) : SV_TARGET
{
    return float4(sample4Tap(destUv(input.position.xy)), 1.0f);
}

float4 PSUpsample(PSInput input) : SV_TARGET
{
    return float4(sampleTent9(destUv(input.position.xy)), 0.0f);
}

float4 PSComposite(PSInput input) : SV_TARGET
{
    float2 uv = destUv(input.position.xy);
    float3 c  = gSrc.SampleLevel(gLin, uv, 0).rgb;
    return float4(c * strength, 0.0f);
}

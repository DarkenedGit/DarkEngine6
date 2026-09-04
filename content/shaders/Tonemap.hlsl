// Fullscreen HDR → swap-chain. Optional CoC DoF + fade-to-black, then copy or ACES.
#pragma pack_matrix(row_major)

cbuffer TonemapConstants : register(b0)
{
    float exposure;
    float mode;         // 0 = saturate, 1 = Narkowicz ACES
    float blur;         // 0-1, scales max CoC in pixels
    float fade;         // 0-1, multiply toward black
    float focusZ;       // linear view-Z of the sharp plane (metres)
    float focusRange;   // CoC ramps over this many metres
    float nearZ;
    float farZ;
    float invWidth;
    float invHeight;
    float uniformBlur;  // 0 = depth CoC, 1 = full-frame defocus
    float _pad;
};

Texture2D    gHdr   : register(t0);
Texture2D    gDepth : register(t1);
SamplerState gLin   : register(s0);

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

float3 aces(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float linearViewZ(float depth)
{
    float z = max(depth, 1e-7f);
    float denom = farZ - z * (farZ - nearZ);
    return (nearZ * farZ) / max(denom, 1e-5f);
}

float3 tonemapColor(float3 hdr)
{
    if (mode < 0.5f)
        return saturate(hdr);
    return aces(hdr * exposure);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2   texel = int2(input.position.xy);
    float3 sharp = gHdr.Load(int3(texel, 0)).rgb;
    float3 rgb   = sharp;

    if (blur > 1e-4f)
    {
        float depth = gDepth.Load(int3(texel, 0)).r;
        float viewZ = linearViewZ(depth);
        float coc   = saturate(abs(viewZ - focusZ) / max(focusRange, 1.0f));
        float k     = lerp(1.0f, coc, saturate(1.0f - uniformBlur));
        float radius = blur * 16.0f * k;

        if (radius > 0.45f)
        {
            float3 acc = sharp;
            [unroll]
            for (int i = 0; i < 16; ++i)
            {
                float fi = (float)i + 0.5f;
                float r  = sqrt(fi / 16.0f) * radius;
                float a  = fi * 2.3999632f;
                float2 uv = (input.position.xy + float2(cos(a), sin(a)) * r) * float2(invWidth, invHeight);
                acc += gHdr.SampleLevel(gLin, uv, 0).rgb;
            }
            rgb = acc * (1.0f / 17.0f);
        }
    }

    float3 outRgb = tonemapColor(rgb) * saturate(1.0f - fade);
    return float4(outRgb, 1);
}

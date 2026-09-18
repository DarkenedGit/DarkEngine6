#ifndef DE_IBL_SAMPLING_HLSLI
#define DE_IBL_SAMPLING_HLSLI

#ifndef DE_PBR_PI
#define DE_PBR_PI 3.14159265f
#endif

// clipXY is Y-up NDC from the fullscreen VS. SV_POSITION is Y-down pixels — do not reconstruct from it.
float3 CubeFaceDir(float2 clipXY, uint face)
{
    float3 dir;
    if      (face == 0) dir = float3( 1.0f, clipXY.y, -clipXY.x);
    else if (face == 1) dir = float3(-1.0f, clipXY.y,  clipXY.x);
    else if (face == 2) dir = float3( clipXY.x,  1.0f, -clipXY.y);
    else if (face == 3) dir = float3( clipXY.x, -1.0f,  clipXY.y);
    else if (face == 4) dir = float3( clipXY.x,  clipXY.y,  1.0f);
    else                dir = float3(-clipXY.x, clipXY.y, -1.0f);
    return normalize(dir);
}

float2 DirToEquirectUv(float3 dir)
{
    dir = normalize(dir);
    float phi   = atan2(dir.x, dir.z);
    float theta = acos(clamp(dir.y, -1.0f, 1.0f));
    return float2(phi * (1.0f / (2.0f * DE_PBR_PI)) + 0.5f, theta * (1.0f / DE_PBR_PI));
}

float3 IblRotateY(float3 d, float rad)
{
    float s, c;
    sincos(rad, s, c);
    return float3(d.x * c + d.z * s, d.y, -d.x * s + d.z * c);
}

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), RadicalInverseVdC(i));
}

void IblTangentBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent   = normalize(cross(up, n));
    bitangent = cross(n, tangent);
}

float3 IblTangentToWorld(float3 n, float3 tangentLocal)
{
    float3 tangent, bitangent;
    IblTangentBasis(n, tangent, bitangent);
    return normalize(tangent * tangentLocal.x + bitangent * tangentLocal.y + n * tangentLocal.z);
}

float3 CosineSampleHemisphere(float2 xi, float3 n)
{
    float  phi      = 2.0f * DE_PBR_PI * xi.x;
    float  cosTheta = sqrt(max(0.0f, 1.0f - xi.y));
    float  sinTheta = sqrt(max(0.0f, xi.y));
    float3 t        = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return IblTangentToWorld(n, t);
}

float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness)
{
    float  a        = roughness * roughness;
    float  phi      = 2.0f * DE_PBR_PI * xi.x;
    float  denom    = max(1.0f + (a * a - 1.0f) * xi.y, 1e-6f);
    float  cosTheta = sqrt(saturate((1.0f - xi.y) / denom));
    float  sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float3 h        = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return IblTangentToWorld(n, h);
}

#endif

// Bake-only terrain erosion CS (first cs_5_0). Dispatch on a private DIRECT list, never the frame list.
// UAV only on bake targets. Thermal Jacobi (t0/u0 ping-pong). Mei pipe (u0..u3 in-place, stage selects flux vs transport).
#pragma pack_matrix(row_major)

cbuffer ErosionConstants : register(b0)
{
    uint  width;
    uint  height;
    float cellSize;
    float talusTan;
    float thermalRate;
    float evaporate;
    float capacity;
    float erode;
    float deposit;
    float gravity;
    float seaLevelRaw;
    uint  stage;
    float pipeDt;
    float rain;
    uint  _pad0;
    uint  _pad1;
};

Texture2D<float>    gSrcHeight : register(t0);
RWTexture2D<float>  gDstHeight : register(u0); // thermal dst / pipe height
RWTexture2D<float>  gWater     : register(u1);
RWTexture2D<float4> gFlux      : register(u2);
RWTexture2D<float>  gSediment  : register(u3);

static const int2 kThermalN[8] =
{
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0),              int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1)
};

uint2 ClampCoord(int2 c)
{
    c.x = clamp(c.x, 0, (int)width - 1);
    c.y = clamp(c.y, 0, (int)height - 1);
    return uint2(c);
}

[numthreads(8, 8, 1)]
void CSThermal(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;

    const float h = gSrcHeight[id.xy];
    float outH = h;
    if (id.x > 0 && id.y > 0 && id.x + 1 < width && id.y + 1 < height)
    {
        [unroll]
        for (int i = 0; i < 8; ++i)
        {
            const int2  n    = int2(id.xy) + kThermalN[i];
            const float nh   = gSrcHeight[ClampCoord(n)];
            const float dist = length(float2((float)kThermalN[i].x, (float)kThermalN[i].y)) * cellSize;
            const float dh   = h - nh;
            const float talus = talusTan * dist;
            if (dh > talus)
                outH -= thermalRate * (dh - talus) / 8.0;
        }
    }
    gDstHeight[id.xy] = outH;
}

float PipeOut(int2 p, int2 dir, float surface, float oldF)
{
    const int2 n = p + dir;
    if (n.x < 0 || n.y < 0 || n.x >= (int)width || n.y >= (int)height)
        return 0.0;
    const float nSurf = gDstHeight[uint2(n)] + max(gWater[uint2(n)] + rain, 0.0);
    const float dh    = surface - nSurf;
    return max(0.0, oldF + pipeDt * gravity * dh / max(cellSize, 1.0e-4));
}

void PipeFlux(uint2 id)
{
    const float h = gDstHeight[id];
    const float w = max(gWater[id] + rain, 0.0);
    const float surface = h + w;
    const int2  p = int2(id);
    float4      f = gFlux[id];
    f.x = PipeOut(p, int2(-1, 0), surface, f.x);
    f.y = PipeOut(p, int2(1, 0), surface, f.y);
    f.z = PipeOut(p, int2(0, -1), surface, f.z);
    f.w = PipeOut(p, int2(0, 1), surface, f.w);

    const float sum    = f.x + f.y + f.z + f.w;
    const float vol    = w * cellSize * cellSize;
    const float maxOut = vol / max(pipeDt, 1.0e-4);
    if (sum > maxOut && sum > 1.0e-8)
        f *= maxOut / sum;
    gFlux[id] = f;
}

void PipeTransport(uint2 id)
{
    const int2 p = int2(id);
    const float h = gDstHeight[id];
    float w = max(gWater[id] + rain, 0.0);
    float s = max(gSediment[id], 0.0);
    const float4 f = gFlux[id];

    float inflow = 0.0;
    if (p.x > 0)
        inflow += gFlux[uint2(p.x - 1, p.y)].y;
    if (p.x + 1 < (int)width)
        inflow += gFlux[uint2(p.x + 1, p.y)].x;
    if (p.y > 0)
        inflow += gFlux[uint2(p.x, p.y - 1)].w;
    if (p.y + 1 < (int)height)
        inflow += gFlux[uint2(p.x, p.y + 1)].z;

    const float outflow = f.x + f.y + f.z + f.w;
    const float area    = max(cellSize * cellSize, 1.0e-4);
    w = max(w + pipeDt * (inflow - outflow) / area, 0.0);

    const float vx  = (f.y - f.x) / max(cellSize * max(w, 1.0e-4), 1.0e-4);
    const float vz  = (f.w - f.z) / max(cellSize * max(w, 1.0e-4), 1.0e-4);
    const float vel = length(float2(vx, vz));
    const float cap = capacity * vel * w;

    float hNew = h;
    if (s < cap)
    {
        float amt = erode * (cap - s);
        if (hNew - amt < seaLevelRaw)
            amt = max(hNew - seaLevelRaw, 0.0);
        hNew -= amt;
        s += amt;
    }
    else
    {
        const float amt = deposit * (s - cap);
        hNew += amt;
        s -= amt;
    }

    w *= max(1.0 - evaporate, 0.0);
    gDstHeight[id] = hNew;
    gWater[id]     = w;
    gSediment[id]  = max(s, 0.0);
}

[numthreads(8, 8, 1)]
void CSPipe(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    if (stage == 0)
        PipeFlux(id.xy);
    else
        PipeTransport(id.xy);
}

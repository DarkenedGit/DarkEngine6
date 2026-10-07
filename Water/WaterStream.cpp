#include "Water/WaterStream.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Render/Camera3D.h"
#include "Render/DebugRenderState.h"
#include "Render/Fog.h"
#include "Render/Frustum3f.h"
#include "Render/ShadowSystem.h"
#include "Render/SsrSettings.h"
#include "Render/WaterPipeline.h"
#include "Terrain/HeightMap.h"

#include <cmath>
#include <cstring>

namespace Dark
{
    using namespace Math;

    namespace
    {

    // Streams are visual only. Walkability::cellWet and editor-play waterLevel stay on the still lake.
    // Swimming a creek is out of scope.

    constexpr float kStreamMinWidth       = 1.5f;
    constexpr float kStreamMaxWidth       = 12.0f;
    constexpr float kStreamSampleMeters   = 2.0f;
    constexpr float kStreamMaxLength      = 512.0f;
    constexpr int   kStreamMaxPoints      = 256;
    constexpr float kStreamMouthMeters    = 4.0f;
    constexpr float kStreamLakeOverlap    = 2.0f;
    constexpr float kStreamMouthBedSlack  = 2.0f;
    constexpr float kStreamFlatSlope      = 0.02f;
    constexpr int   kStreamFlatRun        = 3;
    constexpr int   kStreamAcross         = 5;

    void setStatus(std::string* errorOut, const char* msg)
    {
        if (errorOut)
            *errorOut = msg ? msg : "";
    }

    void copyMatrix(float dst[16], const Matrix4f& m)
    {
        std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
    }

    float clampWidth(float width)
    {
        return Clamp(width, kStreamMinWidth, kStreamMaxWidth);
    }

    float polyLength(const std::vector<Vector2f>& pts)
    {
        float length = 0.0f;
        for (size_t i = 1; i < pts.size(); ++i)
            length += (pts[i] - pts[i - 1]).Magnitude();
        return length;
    }

    std::vector<Vector2f> resamplePolyline(const std::vector<Vector2f>& pts)
    {
        std::vector<Vector2f> samples;
        if (pts.empty())
            return samples;
        samples.push_back(pts.front());
        float since = 0.0f;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const Vector2f a = pts[i - 1];
            Vector2f       delta = pts[i] - a;
            const float    seg = delta.Magnitude();
            if (seg <= 1.0e-5f)
                continue;
            delta = delta / seg;
            float walked = 0.0f;
            while (since + (seg - walked) >= kStreamSampleMeters - 1.0e-4f)
            {
                const float need = kStreamSampleMeters - since;
                walked += need;
                samples.push_back(a + delta * walked);
                since = 0.0f;
            }
            since += seg - walked;
        }
        const Vector2f gap = pts.back() - samples.back();
        if (gap.MagnitudeSqrd() > 1.0e-4f)
            samples.push_back(pts.back());
        return samples;
    }

    Vector2f centerlineTangent(const std::vector<Vector2f>& samples, size_t i)
    {
        Vector2f dir;
        if (samples.size() < 2)
            return Vector2f(1.0f, 0.0f);
        if (i == 0)
            dir = samples[1] - samples[0];
        else if (i + 1 >= samples.size())
            dir = samples[i] - samples[i - 1];
        else
            dir = samples[i + 1] - samples[i - 1];
        const float mag = dir.Magnitude();
        if (mag <= 1.0e-5f)
            return Vector2f(1.0f, 0.0f);
        return dir / mag;
    }

    } // namespace

    Vector2f blendFlowDirection(Vector2f lakeDir, Vector2f tangentXZ, float w)
    {
        // w == 0 is the lake heading, before the degenerate check. Matches Water.hlsl FlowHeading.
        if (w <= 0.0f)
            return lakeDir;
        const float tLen = tangentXZ.Magnitude();
        const Vector2f t = (tLen > 1.0e-5f) ? (tangentXZ / tLen) : lakeDir;
        const float blend = Clamp(w, 0.0f, 1.0f);
        const Vector2f raw = lakeDir * (1.0f - blend) + t * blend;
        if (raw.Magnitude() < 1.0e-4f)
            return t;
        return raw / raw.Magnitude();
    }

    bool followDownhill(
        const Terrain::HeightMap& height,
        Vector2f source,
        float waterLevel,
        std::vector<Vector2f>& outPoints,
        std::string* errorOut)
    {
        outPoints.clear();
        outPoints.push_back(source);

        const BedSlope start = sampleBedSlope(height, source.x, source.y);
        if (!start.ok)
        {
            setStatus(errorOut, "no height map");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: no height map");
            return false;
        }
        if (start.height <= waterLevel + 0.25f)
        {
            setStatus(errorOut, "reached lake");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: reached lake");
            return false;
        }

        Vector2f cur = source;
        float walked = 0.0f;
        int flatRun = 0;
        const char* reason = nullptr;
        while (outPoints.size() < static_cast<size_t>(kStreamMaxPoints))
        {
            const BedSlope bed = sampleBedSlope(height, cur.x, cur.y);
            if (!bed.ok)
            {
                reason = "no height map";
                break;
            }
            if (bed.slope >= kStreamFlatSlope)
                flatRun = 0;
            else
            {
                ++flatRun;
                if (flatRun >= kStreamFlatRun)
                {
                    reason = "flat";
                    break;
                }
            }
            if (walked + kStreamSampleMeters > kStreamMaxLength)
            {
                reason = "length";
                break;
            }

            const Vector2f next(cur.x + bed.downX * kStreamSampleMeters, cur.y + bed.downZ * kStreamSampleMeters);
            if (!height.containsXZ(next.x, next.y))
            {
                reason = "flat";
                break;
            }
            outPoints.push_back(next);
            walked += kStreamSampleMeters;
            if (height.heightAtWorld(next.x, next.y) <= waterLevel + 0.25f)
            {
                reason = "reached lake";
                break;
            }
            cur = next;
        }
        if (!reason && outPoints.size() >= static_cast<size_t>(kStreamMaxPoints))
            reason = "point cap";
        if (!reason)
            reason = "flat";

        const bool missingMap = reason && std::strcmp(reason, "no height map") == 0;
        const bool ok = !missingMap && outPoints.size() >= 2;
        setStatus(errorOut, reason);
        if (!ok)
            DE_LOG_ERROR(LogCategory::Render, "Water stream: {}", reason);
        return ok;
    }

    bool buildStreamRibbon(
        const Terrain::HeightMap& height,
        const StreamDesc& desc,
        float lakeLevel,
        MeshData& out,
        AABox3f* outBounds,
        std::string* errorOut)
    {
        out = MeshData{};
        if (outBounds)
            *outBounds = AABox3f::Empty();

        if (!height.valid())
        {
            setStatus(errorOut, "no height map");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: no height map");
            return false;
        }
        if (desc.pointsXZ.size() < 2)
        {
            setStatus(errorOut, "need two points");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: need at least two points");
            return false;
        }
        if (desc.pointsXZ.size() > static_cast<size_t>(kStreamMaxPoints))
        {
            setStatus(errorOut, "point cap");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: point cap");
            return false;
        }
        if (polyLength(desc.pointsXZ) > kStreamMaxLength)
        {
            setStatus(errorOut, "length");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: length");
            return false;
        }

        std::vector<Vector2f> pts = desc.pointsXZ;
        const float endBed = height.heightAtWorld(pts.back().x, pts.back().y);
        const bool mouth = endBed <= lakeLevel + kStreamMouthBedSlack;
        if (mouth && pts.size() < static_cast<size_t>(kStreamMaxPoints))
        {
            const Vector2f prev = pts[pts.size() - 2];
            Vector2f dir = pts.back() - prev;
            const float len = dir.Magnitude();
            if (len > 1.0e-5f)
            {
                dir = dir / len;
                pts.push_back(pts.back() + dir * kStreamLakeOverlap);
            }
        }

        const std::vector<Vector2f> samples = resamplePolyline(pts);
        if (samples.size() < 2)
        {
            setStatus(errorOut, "need two points");
            DE_LOG_ERROR(LogCategory::Render, "Water stream: need at least two points");
            return false;
        }

        std::vector<float> distFromEnd(samples.size(), 0.0f);
        for (size_t i = samples.size() - 1; i > 0; --i)
            distFromEnd[i - 1] = distFromEnd[i] + (samples[i] - samples[i - 1]).Magnitude();

        const float width = clampWidth(desc.width);
        const float clearance = desc.bedClearance;
        const size_t rings = samples.size();
        out.positions.reserve(rings * kStreamAcross);
        out.normals.reserve(rings * kStreamAcross);
        out.uvs.reserve(rings * kStreamAcross);
        out.tangents.reserve(rings * kStreamAcross);

        AABox3f bounds = AABox3f::Empty();
        for (size_t i = 0; i < rings; ++i)
        {
            const Vector2f sample = samples[i];
            const float bedY = height.heightAtWorld(sample.x, sample.y);
            const Vector3f normal = height.normalAtWorld(sample.x, sample.y);
            Vector3f center(sample.x, bedY, sample.y);
            center += normal * clearance;

            float blend = 1.0f;
            if (mouth)
            {
                blend = Clamp(distFromEnd[i] / kStreamMouthMeters, 0.0f, 1.0f);
                center.y = Lerp(lakeLevel, center.y, blend);
            }

            const Vector2f tangent = centerlineTangent(samples, i);
            const Vector2f right = tangent.Perpendicular();
            for (int s = 0; s < kStreamAcross; ++s)
            {
                const float across = (static_cast<float>(s) / 4.0f - 0.5f) * width;
                Vector3f point = center;
                point.x += right.x * across;
                point.z += right.y * across;
                const float bank = 1.0f - fabsf(static_cast<float>(s) / 4.0f * 2.0f - 1.0f);
                out.positions.push_back(point);
                out.normals.push_back(Vector3f(width, 1.0f, 0.0f));
                out.uvs.push_back(Vector2f(bank, bedY));
                out.tangents.push_back(Vector4f(tangent.x, 0.0f, tangent.y, blend));
                bounds.ExpandToInclude(point);
            }
        }

        for (size_t i = 0; i + 1 < rings; ++i)
        {
            for (int s = 0; s < kStreamAcross - 1; ++s)
            {
                const uint32_t bl = static_cast<uint32_t>(i * kStreamAcross + s);
                const uint32_t br = bl + 1;
                const uint32_t tl = bl + static_cast<uint32_t>(kStreamAcross);
                const uint32_t tr = tl + 1;
                out.indices.push_back(bl);
                out.indices.push_back(br);
                out.indices.push_back(tr);
                out.indices.push_back(bl);
                out.indices.push_back(tr);
                out.indices.push_back(tl);
            }
        }

        if (outBounds)
            *outBounds = bounds;
        setStatus(errorOut, "");
        return true;
    }

    bool drawStreamRibbon(
        ID3D12GraphicsCommandList* cmd,
        WaterPipeline& pipeline,
        const Mesh& mesh,
        const AABox3f& bounds,
        const WaterParams& params,
        const Camera3D& camera,
        const Frustum3f* frustum,
        const DebugRenderState* debug,
        float time,
        uint32_t frameIndex,
        uint32_t drawIndex,
        ID3D12DescriptorHeap* heightHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE heightGpu,
        const ShadowSystem* shadows,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCpu,
        D3D12_CPU_DESCRIPTOR_HANDLE depthCpu,
        const SsrSettings* ssrSettings,
        const Terrain::HeightMap* heightMap,
        const Sky::Environment* env)
    {
        // Caller copies the lake params and writes this ribbon's flowSpeed first.
        if (!cmd || !pipeline.isValid() || !mesh.valid())
            return false;
        if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            return false;
        if (frustum && !frustum->Intersects(bounds))
            return false;

        const DebugFill fill = debug ? debug->fill : DebugFill::Solid;
        const bool lighting = !debug || debug->lightingActive();
        pipeline.bind(cmd, fill, true);

        WaterFrameConstants cb{};
        const Matrix4f viewProj = camera.GetViewProj();
        copyMatrix(cb.worldViewProj, viewProj);
        const Vector3f cam = camera.GetPosition();
        const float camPos[3] = { cam.x, cam.y, cam.z };
        const float light[3] = { 0.35f, 0.85f, -0.35f };
        WaterPipeline::fillConstants(cb, cb.worldViewProj, camPos, time, light, params, env, lighting);
        FogGpu fogMap{};
        fillFogHeightMap(fogMap, heightMap);
        cb.heightOriginX = fogMap.heightOriginX;
        cb.heightOriginZ = fogMap.heightOriginZ;
        cb.heightCellSize = fogMap.heightCellSize;
        cb.heightWorldSizeX = fogMap.heightWorldSizeX;
        cb.heightWorldSizeZ = fogMap.heightWorldSizeZ;
        const bool hasSsr = sceneColorCpu.ptr != 0 && depthCpu.ptr != 0;
        WaterPipeline::fillSsr(cb, camera, ssrSettings, !debug || debug->ssrEnabled, hasSsr, env);
        pipeline.setConstants(cmd, cb, frameIndex, drawIndex);
        pipeline.setLights(cmd, 0);
        pipeline.setSsrSrvs(sceneColorCpu, depthCpu);
        if (pipeline.hasReceiverSrvs())
            pipeline.bindReceiverSrvs(cmd);
        else
            pipeline.setHeightMap(cmd, heightHeap, heightGpu);
        if (shadows && shadows->isValid())
            shadows->bindReceiverCbv(cmd, WaterPipeline::kRootShadowCbv);

        mesh.draw(cmd, fill == DebugFill::Points);
        return true;
    }

} // namespace Dark

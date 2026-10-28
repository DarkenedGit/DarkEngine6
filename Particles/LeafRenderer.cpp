#include "Particles/LeafRenderer.h"

#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Particles/ParticleMaterials.h"
#include "Render/Camera3D.h"
#include "Render/GpuResourceCache.h"
#include "Render/ParticlePipeline.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Dark
{
    namespace
    {
        constexpr float kLeafSizeM = 0.16f;
        constexpr float kGroundLiftM = 0.04f;
        constexpr float kLeafR = 0.55f;
        constexpr float kLeafG = 0.34f;
        constexpr float kLeafB = 0.10f;

        using Math::Vector3f;

        Vector3f safeNormal(float x, float y, float z)
        {
            Vector3f n(x, y, z);
            const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
            if (len2 < 1.0e-8f)
                return Vector3f(0.0f, 1.0f, 0.0f);
            n.Normalize();
            return n;
        }

        Vector3f safeUnit(const Vector3f& v, const Vector3f& fallback)
        {
            const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
            if (len2 < 1.0e-8f)
                return fallback;
            Vector3f u = v;
            u.Normalize();
            return u;
        }

        void writeQuad(const Vector3f& center, const Vector3f& axisU, const Vector3f& axisV, float alpha, ParticleVertex outVerts[6])
        {
            const float hx = kLeafSizeM * 0.5f;
            const float hy = kLeafSizeM * 0.5f;
            const Vector3f r = axisU * hx;
            const Vector3f u = axisV * hy;
            const Vector3f bl(center.x - r.x - u.x, center.y - r.y - u.y, center.z - r.z - u.z);
            const Vector3f br(center.x + r.x - u.x, center.y + r.y - u.y, center.z + r.z - u.z);
            const Vector3f tr(center.x + r.x + u.x, center.y + r.y + u.y, center.z + r.z + u.z);
            const Vector3f tl(center.x - r.x + u.x, center.y - r.y + u.y, center.z - r.z + u.z);

            const Vector3f corners[6] = { bl, br, tr, bl, tr, tl };
            const float    uu[6]      = { 0, 1, 1, 0, 1, 0 };
            const float    vv[6]      = { 1, 1, 0, 1, 0, 0 };
            for (int i = 0; i < 6; ++i)
            {
                ParticleVertex& vert = outVerts[i];
                vert.px              = corners[i].x;
                vert.py              = corners[i].y;
                vert.pz              = corners[i].z;
                vert.u               = uu[i];
                vert.v               = vv[i];
                vert.r               = kLeafR;
                vert.g               = kLeafG;
                vert.b               = kLeafB;
                vert.a               = alpha;
            }
        }

        float cardAlpha(const LeafCardInput& leaf)
        {
            if (leaf.phase == LeafCardPhase::Fade)
            {
                const float t = Math::Clamp(leaf.fade01, 0.0f, 1.0f);
                return 1.0f - t;
            }
            return 1.0f;
        }
    } // namespace

    bool buildLeafCard(const LeafCardInput& leaf, const Math::Vector3f& cameraRight, const Math::Vector3f& cameraUp, ParticleVertex outVerts[6])
    {
        if (!outVerts || leaf.phase == LeafCardPhase::None)
            return false;

        Vector3f axisU;
        Vector3f axisV;
        Vector3f center(leaf.x, leaf.y, leaf.z);
        if (leaf.phase == LeafCardPhase::Airborne)
        {
            axisU = safeUnit(cameraRight, Vector3f(1.0f, 0.0f, 0.0f));
            axisV = safeUnit(cameraUp, Vector3f(0.0f, 1.0f, 0.0f));
        }
        else
        {
            const Vector3f n = safeNormal(leaf.nx, leaf.ny, leaf.nz);
            const Vector3f hint = std::fabs(n.y) < 0.9f ? Vector3f(0.0f, 1.0f, 0.0f) : Vector3f(1.0f, 0.0f, 0.0f);
            axisU = safeUnit(n.Cross(hint), Vector3f(1.0f, 0.0f, 0.0f));
            axisV = safeUnit(n.Cross(axisU), Vector3f(0.0f, 0.0f, 1.0f));
            center = Vector3f(leaf.x + n.x * kGroundLiftM, leaf.y + n.y * kGroundLiftM, leaf.z + n.z * kGroundLiftM);
        }

        writeQuad(center, axisU, axisV, cardAlpha(leaf), outVerts);
        return true;
    }

    int buildLeafCards(const LeafCardInput* leaves, int count, const Math::Vector3f& cameraRight, const Math::Vector3f& cameraUp,
                       ParticleVertex* outVerts, int outVertCapacity)
    {
        if (!leaves || !outVerts || count <= 0 || outVertCapacity < 6)
            return 0;
        int quads = 0;
        int used  = 0;
        for (int i = 0; i < count; ++i)
        {
            if (used + 6 > outVertCapacity)
                break;
            if (!buildLeafCard(leaves[i], cameraRight, cameraUp, outVerts + used))
                continue;
            used += 6;
            ++quads;
        }
        return quads;
    }

    namespace
    {
        bool failedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        uint32_t crownIdAt(float x, float z)
        {
            const int ix = static_cast<int>(std::floor(x * 2.0f));
            const int iz = static_cast<int>(std::floor(z * 2.0f));
            uint32_t h = static_cast<uint32_t>(ix) * 0x9E3779B1u;
            h ^= static_cast<uint32_t>(iz) + 0x85EBCA6Bu + (h << 6) + (h >> 2);
            return h == 0u ? 1u : h;
        }

        // worldToTile is private. Same floor-and-clamp the foliage draw uses, then the public resident test.
        bool tileResident(const Terrain::TerrainGrid& grid, float x, float z)
        {
            if (!grid.valid())
                return false;
            const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
            if (!(tileWorld > 0.0f))
                return false;
            const int tilesX = static_cast<int>(grid.tilesX());
            const int tilesZ = static_cast<int>(grid.tilesZ());
            if (tilesX <= 0 || tilesZ <= 0)
                return false;
            const Math::Vector3f& origin = grid.origin();
            int tx = static_cast<int>(std::floor((x - origin.x) / tileWorld));
            int tz = static_cast<int>(std::floor((z - origin.z) / tileWorld));
            if (tx < 0)
                tx = 0;
            if (tz < 0)
                tz = 0;
            if (tx >= tilesX)
                tx = tilesX - 1;
            if (tz >= tilesZ)
                tz = tilesZ - 1;
            return grid.isResident(tx, tz);
        }

        bool pushCrown(const Terrain::FoliageRecord& rec, const Math::Vector3f& camera, const Frustum3f* frustum, float treeTopY,
                       Terrain::LeafField::Crown* out, int cap, int& count)
        {
            if (rec.kind != static_cast<uint8_t>(Terrain::FoliageKind::Tree))
                return true;
            const float dx = rec.x - camera.x;
            const float dz = rec.z - camera.z;
            const float radius = Terrain::LeafField::kGatherRadiusM;
            if (dx * dx + dz * dz > radius * radius)
                return true;
            const float scale = rec.scale > 0.05f ? rec.scale : 1.0f;
            const float top = treeTopY > 0.05f ? treeTopY : 12.0f;
            const float crownY = rec.y + top * scale;
            if (frustum && !frustum->Contains(Math::Vector3f(rec.x, crownY, rec.z)))
                return true;
            if (count >= cap)
                return false;
            Terrain::LeafField::Crown& crown = out[count++];
            crown.id     = crownIdAt(rec.x, rec.z);
            crown.x      = rec.x;
            crown.seatY  = rec.y;
            crown.z      = rec.z;
            crown.crownY = crownY;
            return true;
        }

        // Tree indices in host order for one vector (pointer + size). Not crown positions.
        // Rebuild when that key changes or a cached index is no longer a tree.
        struct HostTreeIndex
        {
            const Terrain::FoliageRecord* data = nullptr;
            size_t                        size = 0;
            std::vector<uint32_t>         trees;
        };

        bool hostTreeIndexCurrent(const HostTreeIndex& cache, const std::vector<Terrain::FoliageRecord>& host)
        {
            if (cache.data != host.data() || cache.size != host.size())
                return false;
            const uint8_t tree = static_cast<uint8_t>(Terrain::FoliageKind::Tree);
            for (uint32_t index : cache.trees)
            {
                if (static_cast<size_t>(index) >= host.size() || host[index].kind != tree)
                    return false;
            }
            return true;
        }

        void rebuildHostTreeIndex(HostTreeIndex& cache, const std::vector<Terrain::FoliageRecord>& host)
        {
            cache.data = host.data();
            cache.size = host.size();
            cache.trees.clear();
            const uint8_t tree = static_cast<uint8_t>(Terrain::FoliageKind::Tree);
            for (size_t i = 0; i < host.size(); ++i)
            {
                if (host[i].kind != tree)
                    continue;
                cache.trees.push_back(static_cast<uint32_t>(i));
            }
        }
    } // namespace

    int gatherLeafCrowns(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>* hostRecords,
                         const Math::Vector3f& camera, const Frustum3f* frustum, float treeTopY, Terrain::LeafField::Crown* out, int cap)
    {
        if (!out || cap <= 0 || !grid.valid())
            return 0;
        int count = 0;
        if (hostRecords)
        {
            static HostTreeIndex treeIndex;
            if (!hostTreeIndexCurrent(treeIndex, *hostRecords))
                rebuildHostTreeIndex(treeIndex, *hostRecords);
            for (uint32_t index : treeIndex.trees)
            {
                const Terrain::FoliageRecord& rec = (*hostRecords)[index];
                if (!tileResident(grid, rec.x, rec.z))
                    continue;
                if (!pushCrown(rec, camera, frustum, treeTopY, out, cap, count))
                    break;
            }
            return count;
        }

        const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
        if (!(tileWorld > 0.0f))
            return 0;
        const float radius = Terrain::LeafField::kGatherRadiusM;
        const Math::Vector3f origin = grid.origin();
        int minTx = static_cast<int>(std::floor((camera.x - radius - origin.x) / tileWorld));
        int maxTx = static_cast<int>(std::floor((camera.x + radius - origin.x) / tileWorld));
        int minTz = static_cast<int>(std::floor((camera.z - radius - origin.z) / tileWorld));
        int maxTz = static_cast<int>(std::floor((camera.z + radius - origin.z) / tileWorld));
        if (minTx < 0)
            minTx = 0;
        if (minTz < 0)
            minTz = 0;
        const int tilesX = static_cast<int>(grid.tilesX());
        const int tilesZ = static_cast<int>(grid.tilesZ());
        if (maxTx >= tilesX)
            maxTx = tilesX - 1;
        if (maxTz >= tilesZ)
            maxTz = tilesZ - 1;
        if (minTx > maxTx || minTz > maxTz)
            return 0;
        for (int tz = minTz; tz <= maxTz && count < cap; ++tz)
        {
            for (int tx = minTx; tx <= maxTx && count < cap; ++tx)
            {
                const std::vector<Terrain::FoliageRecord>* records = grid.residentFoliage(tx, tz);
                if (!records)
                    continue;
                for (const Terrain::FoliageRecord& rec : *records)
                {
                    if (!pushCrown(rec, camera, frustum, treeTopY, out, cap, count))
                        return count;
                }
            }
        }
        return count;
    }

    bool LeafRenderer::createUpload(Renderer& renderer)
    {
        const uint64_t verts = static_cast<uint64_t>(kLeafQuadCapacity) * 6u * kFrameCount;
        const uint64_t bytes = verts * sizeof(ParticleVertex);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = bytes;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (failedHr(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                IID_PPV_ARGS(&m_upload)),
                     "Leaf upload VB"))
            return false;
        D3D12_RANGE readRange{ 0, 0 };
        void* mapped = nullptr;
        if (failedHr(m_upload->Map(0, &readRange, &mapped), "Map leaf VB"))
        {
            m_upload.Reset();
            return false;
        }
        m_mapped = static_cast<uint8_t*>(mapped);
        m_gpu    = m_upload->GetGPUVirtualAddress();
        return true;
    }

    bool LeafRenderer::create(Renderer& renderer, AssetManager& assets)
    {
        m_renderer = &renderer;
        m_ready    = false;
        if (!m_pipe.create(renderer.device(), false, 0, 0.0f, renderer.sceneColorFormat()))
        {
            DE_LOG_ERROR(LogCategory::Render, "LeafRenderer: pipeline create failed");
            return false;
        }
        m_sprite = internParticleSpriteMaterial(assets, false);
        if (!m_sprite)
        {
            DE_LOG_ERROR(LogCategory::Render, "LeafRenderer: sprite material failed");
            return false;
        }
        if (!renderer.gpuResources().ensureMaterial(m_sprite))
        {
            DE_LOG_ERROR(LogCategory::Render, "LeafRenderer: sprite GPU material failed");
            return false;
        }
        if (!createUpload(renderer))
        {
            DE_LOG_ERROR(LogCategory::Render, "LeafRenderer: upload create failed");
            return false;
        }
        m_ready = true;
        return true;
    }

    void LeafRenderer::destroy(Renderer& renderer)
    {
        renderer.waitForGpu();
        if (m_upload && m_mapped)
            m_upload->Unmap(0, nullptr);
        m_mapped = nullptr;
        m_gpu    = 0;
        m_upload.Reset();
        m_sprite.reset();
        m_ready    = false;
        m_renderer = nullptr;
    }

    void LeafRenderer::draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const Terrain::LeafField& field)
    {
        if (!m_ready || !cmd || !m_mapped || field.livingCount() <= 0)
            return;
        const uint32_t frame     = m_renderer ? m_renderer->frameIndex() : 0u;
        const uint32_t frameSlot = frame % kFrameCount;

        ParticleVertex verts[kLeafQuadCapacity * 6];
        int quads = 0;
        const Math::Vector3f right = camera.GetRight();
        const Math::Vector3f up    = camera.GetUp();
        for (int i = 0; i < Terrain::LeafField::kPoolSize && quads < kLeafQuadCapacity; ++i)
        {
            Terrain::LeafSlot slot;
            if (!field.copySlot(i, slot) || slot.phase == Terrain::LeafPhase::Free)
                continue;
            LeafCardInput card;
            card.x  = slot.x;
            card.y  = slot.y;
            card.z  = slot.z;
            card.nx = slot.nx;
            card.ny = slot.ny;
            card.nz = slot.nz;
            if (slot.phase == Terrain::LeafPhase::Airborne)
                card.phase = LeafCardPhase::Airborne;
            else if (slot.phase == Terrain::LeafPhase::Rest)
                card.phase = LeafCardPhase::Rest;
            else
            {
                card.phase  = LeafCardPhase::Fade;
                card.fade01 = Terrain::LeafField::kFadeSeconds > 0.0f ? slot.phaseTime / Terrain::LeafField::kFadeSeconds : 1.0f;
            }
            if (!buildLeafCard(card, right, up, verts + quads * 6))
                continue;
            ++quads;
        }
        if (quads <= 0)
            return;

        const uint32_t vertOffset = frameSlot * static_cast<uint32_t>(kLeafQuadCapacity) * 6u;
        std::memcpy(m_mapped + static_cast<size_t>(vertOffset) * sizeof(ParticleVertex), verts, static_cast<size_t>(quads) * 6u * sizeof(ParticleVertex));

        m_pipe.bind(cmd);
        ParticleFrameConstants constants{};
        const Math::Matrix4f viewProj = camera.GetViewProj();
        viewProj.store(constants.viewProj);
        m_pipe.setConstants(cmd, constants);

        if (!m_renderer || !m_sprite || !m_sprite->albedo())
            return;
        m_renderer->gpuResources().ensureMaterial(m_sprite);
        std::shared_ptr<Texture2D> tex = m_renderer->gpuResources().texture(m_sprite->albedo()->id);
        if (!tex || !tex->valid())
            return;
        tex->bind(cmd, ParticlePipeline::kRootSrv);

        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = m_gpu + static_cast<UINT64>(vertOffset) * sizeof(ParticleVertex);
        view.StrideInBytes  = sizeof(ParticleVertex);
        view.SizeInBytes    = static_cast<UINT>(quads * 6 * sizeof(ParticleVertex));
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &view);
        cmd->DrawInstanced(static_cast<UINT>(quads * 6), 1, 0, 0);
    }

} // namespace Dark

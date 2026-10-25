#include "Render/PassTable.h"

#include "Core/Log.h"

#include <initializer_list>

namespace Dark
{

namespace
{

PassNode makeNode(PassId id, const char* scope, bool optional, std::initializer_list<RenderResource> reads, std::initializer_list<RenderResource> writes)
{
    PassNode node{};
    node.id        = id;
    node.scopeName = scope;
    node.optional  = optional;
    node.readCount = 0;
    node.writeCount = 0;
    for (RenderResource resource : reads)
    {
        if (node.readCount >= 8)
            break;
        node.reads[node.readCount++] = resource;
    }
    for (RenderResource resource : writes)
    {
        if (node.writeCount >= 4)
            break;
        node.writes[node.writeCount++] = resource;
    }
    return node;
}

bool sequenceFail()
{
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        DE_LOG_ERROR(LogCategory::Render, "passSequenceOk: host pass order does not match the table");
    }
    return false;
}

} // namespace

bool passSequenceOk(const PassNode* table, uint32_t tableCount, const PassId* played, uint32_t playedCount)
{
    if ((table == nullptr && tableCount > 0) || (played == nullptr && playedCount > 0))
        return sequenceFail();

    uint32_t tableIndex = 0;
    for (uint32_t playedIndex = 0; playedIndex < playedCount; ++playedIndex)
    {
        bool found = false;
        while (tableIndex < tableCount)
        {
            if (table[tableIndex].id == played[playedIndex])
            {
                found = true;
                ++tableIndex;
                break;
            }
            if (!table[tableIndex].optional)
                return sequenceFail();
            ++tableIndex;
        }
        if (!found)
            return sequenceFail();
    }
    while (tableIndex < tableCount)
    {
        if (!table[tableIndex].optional)
            return sequenceFail();
        ++tableIndex;
    }
    return true;
}

const PassNode* sandboxDeferredPasses(uint32_t& count)
{
    static const PassNode kTable[] = {
        makeNode(PassId::Shadows, nullptr, true, {}, { RenderResource::Shadow }),
        makeNode(PassId::LocalShadows, nullptr, true, {}, { RenderResource::LocalShadow }),
        makeNode(PassId::GBuffer, "GBuffer", false, {}, { RenderResource::Albedo, RenderResource::Attrib, RenderResource::Velocity, RenderResource::Depth }),
        makeNode(PassId::Decals, nullptr, false, { RenderResource::Depth }, { RenderResource::Albedo, RenderResource::Attrib }),
        makeNode(PassId::Gtao, nullptr, false, { RenderResource::Depth, RenderResource::Attrib }, { RenderResource::Ao }),
        makeNode(PassId::Ssr, nullptr, false, { RenderResource::Depth, RenderResource::Albedo }, {}),
        makeNode(PassId::Lighting, nullptr, false, { RenderResource::Albedo, RenderResource::Attrib, RenderResource::Depth, RenderResource::Shadow }, { RenderResource::Hdr }),
        makeNode(PassId::LocalVolumes, nullptr, false, { RenderResource::Hdr }, { RenderResource::Hdr }),
        makeNode(PassId::Sky, nullptr, true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::SsrCapture, nullptr, true, { RenderResource::Hdr }, { RenderResource::Post }),
        makeNode(PassId::Water, "Water", true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Clouds, nullptr, true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Camouflage, nullptr, true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Translucent, "Translucent", false, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Particles, "Particles", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Underwater, "Underwater", true, { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Post, nullptr, false, { RenderResource::Hdr }, { RenderResource::Swap }),
        makeNode(PassId::Overlay, nullptr, true, {}, { RenderResource::Swap }),
    };
    count = static_cast<uint32_t>(sizeof(kTable) / sizeof(kTable[0]));
    return kTable;
}

const PassNode* sandboxForwardPasses(uint32_t& count)
{
    static const PassNode kTable[] = {
        makeNode(PassId::Shadows, nullptr, true, {}, { RenderResource::Shadow }),
        makeNode(PassId::LocalShadows, nullptr, true, {}, { RenderResource::LocalShadow }),
        makeNode(PassId::Sky, nullptr, true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::SsrCapture, nullptr, true, { RenderResource::Hdr }, { RenderResource::Post }),
        makeNode(PassId::Water, "Water", true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Camouflage, nullptr, true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Translucent, "Translucent", false, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Particles, "Particles", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Underwater, "Underwater", true, { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Post, nullptr, false, { RenderResource::Hdr }, { RenderResource::Swap }),
        makeNode(PassId::Overlay, nullptr, true, {}, { RenderResource::Swap }),
    };
    count = static_cast<uint32_t>(sizeof(kTable) / sizeof(kTable[0]));
    return kTable;
}

const PassNode* editorDeferredPasses(uint32_t& count)
{
    static const PassNode kTable[] = {
        makeNode(PassId::Shadows, nullptr, true, {}, { RenderResource::Shadow }),
        makeNode(PassId::LocalShadows, nullptr, true, {}, { RenderResource::LocalShadow }),
        makeNode(PassId::GBuffer, "GBuffer", false, {}, { RenderResource::Albedo, RenderResource::Attrib, RenderResource::Velocity, RenderResource::Depth }),
        makeNode(PassId::Decals, nullptr, false, { RenderResource::Depth }, { RenderResource::Albedo, RenderResource::Attrib }),
        makeNode(PassId::Gtao, nullptr, false, { RenderResource::Depth, RenderResource::Attrib }, { RenderResource::Ao }),
        makeNode(PassId::Ssr, nullptr, false, { RenderResource::Depth, RenderResource::Albedo }, {}),
        makeNode(PassId::Lighting, nullptr, false, { RenderResource::Albedo, RenderResource::Attrib, RenderResource::Depth, RenderResource::Shadow }, { RenderResource::Hdr }),
        makeNode(PassId::LocalVolumes, nullptr, false, { RenderResource::Hdr }, { RenderResource::Hdr }),
        makeNode(PassId::Sky, nullptr, true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::SsrCapture, nullptr, false, { RenderResource::Hdr }, { RenderResource::Post }),
        makeNode(PassId::Water, "Water", true, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Clouds, nullptr, true, {}, { RenderResource::Hdr }),
        makeNode(PassId::EditorGrid, "Editor Grid", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::EditorGizmos, "Light Gizmos", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Translucent, "Translucent", false, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Particles, "Particles", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Underwater, "Underwater", true, { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Post, nullptr, false, { RenderResource::Hdr }, { RenderResource::Swap }),
        makeNode(PassId::Overlay, nullptr, true, {}, { RenderResource::Swap }),
    };
    count = static_cast<uint32_t>(sizeof(kTable) / sizeof(kTable[0]));
    return kTable;
}

const PassNode* editorForwardPasses(uint32_t& count)
{
    static const PassNode kTable[] = {
        makeNode(PassId::Shadows, nullptr, true, {}, { RenderResource::Shadow }),
        makeNode(PassId::LocalShadows, nullptr, true, {}, { RenderResource::LocalShadow }),
        makeNode(PassId::EditorGrid, "Editor Grid", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::EditorGizmos, "Light Gizmos", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Translucent, "Translucent", false, { RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Particles, "Particles", true, {}, { RenderResource::Hdr }),
        makeNode(PassId::Underwater, "Underwater", true, { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }),
        makeNode(PassId::Post, nullptr, false, { RenderResource::Hdr }, { RenderResource::Swap }),
        makeNode(PassId::Overlay, nullptr, true, {}, { RenderResource::Swap }),
    };
    count = static_cast<uint32_t>(sizeof(kTable) / sizeof(kTable[0]));
    return kTable;
}

} // namespace Dark

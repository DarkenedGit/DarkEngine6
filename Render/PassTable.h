#pragma once

#include <cstdint>

namespace Dark
{

enum class RenderResource : uint8_t
{
    Swap = 0,
    Depth,
    Shadow,
    LocalShadow,
    Albedo,
    Attrib,
    Velocity,
    Ao,
    Hdr,
    Post,
    StaticItems,
    ViewList,
};

enum class PassId : uint8_t
{
    Shadows,
    LocalShadows,
    GBuffer,
    Decals,
    Gtao,
    Ssr,
    Lighting,
    LocalVolumes,
    Sky,
    SsrCapture,
    Water,
    Clouds,
    Camouflage,
    Translucent,
    Particles,
    Post,
    Overlay,
    EditorGrid,
    EditorGizmos,
};

struct PassNode
{
    PassId         id;
    const char*    scopeName;
    bool           optional;
    uint8_t        readCount;
    uint8_t        writeCount;
    RenderResource reads[8];
    RenderResource writes[4];
};

// `played` must be a subsequence of `table` in table order.
// Skipping optional nodes is success. Skipping a required node, or playing
// a node that is not still ahead in the table, returns false.
// Does not reorder `played` and does not throw. Logs once on failure.
bool passSequenceOk(const PassNode* table, uint32_t tableCount, const PassId* played, uint32_t playedCount);

const PassNode* sandboxDeferredPasses(uint32_t& count);
const PassNode* sandboxForwardPasses(uint32_t& count);
const PassNode* editorDeferredPasses(uint32_t& count);
const PassNode* editorForwardPasses(uint32_t& count);

} // namespace Dark

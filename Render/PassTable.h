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
    Underwater,
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
// Does not reorder `played`. Logs once per process on the first failure.
bool passSequenceOk(const PassNode* table, uint32_t tableCount, const PassId* played, uint32_t playedCount);

// Host path: a failure latches the run as unsuitable for a baseline and logs the host,
// expected table, and actual order (once per table, not silenced by unit-test probes).
// The counter counts frames with a failed host check. Query it in capture reports.
bool validateHostPassSequence(const char* host, const PassNode* table, uint32_t tableCount, const PassId* played, uint32_t playedCount);
uint32_t hostPassSequenceFailures();
const char* passName(PassId id);

const PassNode* sandboxDeferredPasses(uint32_t& count);
const PassNode* editorDeferredPasses(uint32_t& count);

} // namespace Dark

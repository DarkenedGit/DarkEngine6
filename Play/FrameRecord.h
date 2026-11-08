#pragma once

#include "Core/JobSystem.h"

#include <cstdint>

namespace Dark
{

// Publish the slot whose allocator beginFrame already waited. Does not start threads.
void frameNoteBeginFrame(JobSystem& jobs, uint32_t frameIndex);

// Blocks until that slot's render jobs return. Idle slots return immediately.
bool frameJoinRender(JobSystem& jobs, uint32_t frameIndex);

// Request the shadow worker. Default is off. The record path ignores the request
// until the depth resource set exists, so leaving this off keeps the main-thread list.
void setShadowWorker(bool enabled);
bool shadowWorker();

// Fixed table of 16. Foundation never sees a command list; the record hook is void*.
struct PassDesc
{
    const char* name            = nullptr;
    int         order           = 0; // ExecuteCommandLists order. 0 is the shadow list. The main list is last and is not a row.
    uint32_t    writes          = 0;
    bool        recordOffThread = false;
    JobStatus (*record)(void* user) = nullptr;
    void*       user            = nullptr;
};

// False if the table is full, two off-thread rows share write bits, or writes set a main-only bit.
bool registerFramePass(const PassDesc& pass);

// Drops every row. Play does not call this; a test join must not call a dead user pointer.
void resetFramePasses();

} // namespace Dark

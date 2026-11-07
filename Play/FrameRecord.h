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

} // namespace Dark

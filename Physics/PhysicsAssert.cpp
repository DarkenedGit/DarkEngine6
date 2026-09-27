#include "Core/Log.h"

#include <box3d/box3d.h>

namespace Dark::Physics
{
    namespace
    {
        int box3dAssert(const char* condition, const char* fileName, int lineNumber)
        {
            DE_LOG_ERROR(LogCategory::Collision, "Box3D assert: {} ({}:{})", condition ? condition : "?", fileName ? fileName : "?", lineNumber);
            DE_ASSERT(false);
            // 0 = skip Box3D's own debugger break (DE_ASSERT already broke in debug).
            return 0;
        }
    } // namespace

    void installAssertHook()
    {
        static bool installed = false;
        if (installed)
            return;
        b3SetAssertFcn(&box3dAssert);
        installed = true;
    }
} // namespace Dark::Physics

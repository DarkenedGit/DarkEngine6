#pragma once

#include <cstdio>

namespace Dark
{

    struct NpcInfoOverlaySettings
    {
        bool enabled    = true;
        bool showState  = true;
        bool showHealth = false;
        bool showTarget = false;
    };

    struct NpcInfoSample
    {
        const char* state     = nullptr;
        bool        hasHealth = false;
        float       hp        = 0.0f;
        float       maxHp     = 0.0f;
        bool        hasTarget = false;
    };

    // Writes a newline-separated box. Returns characters written (0 if nothing to show).
    inline int formatNpcInfoBox(const NpcInfoOverlaySettings& settings, const NpcInfoSample& sample, char* out, int cap)
    {
        if (!out || cap <= 1)
            return 0;
        out[0] = '\0';
        if (!settings.enabled)
            return 0;

        int used = 0;
        auto appendLine = [&](const char* line) {
            if (!line || line[0] == '\0' || used >= cap - 1)
                return;
            if (used > 0)
            {
                if (used >= cap - 2)
                    return;
                out[used++] = '\n';
                out[used]   = '\0';
            }
            const int n = std::snprintf(out + used, static_cast<size_t>(cap - used), "%s", line);
            if (n > 0)
                used += n;
            if (used >= cap)
                used = cap - 1;
            out[used] = '\0';
        };

        if (settings.showState && sample.state && sample.state[0] != '\0')
            appendLine(sample.state);

        if (settings.showHealth && sample.hasHealth)
        {
            char line[48];
            std::snprintf(line, sizeof(line), "HP %.0f/%.0f", static_cast<double>(sample.hp), static_cast<double>(sample.maxHp));
            appendLine(line);
        }

        if (settings.showTarget)
            appendLine(sample.hasTarget ? "Target yes" : "Target no");

        return used;
    }

} // namespace Dark

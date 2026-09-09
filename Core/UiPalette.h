#pragma once

#include <cstdint>

namespace Dark
{
    // Host accent. Chrome (navy surfaces, steel text) is shared; only the accent changes.
    // Sampled from content/textures/loading/*_logo.png.
    enum class UiAccent : uint8_t
    {
        Engine = 0, // #40B0F8
        Editor,     // #C878F8
        Sandbox,    // #50C888
        Sandbox2D,  // #F0B040
    };

    struct UiColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        constexpr UiColor withAlpha(float alpha) const
        {
            return { r, g, b, alpha };
        }

        constexpr void store(float out[4]) const
        {
            out[0] = r;
            out[1] = g;
            out[2] = b;
            out[3] = a;
        }

        constexpr bool operator==(const UiColor&) const = default;
    };

    constexpr UiColor uiRgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return { static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f };
    }

    constexpr UiColor uiLerp(UiColor a, UiColor b, float t)
    {
        return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
    }

    namespace UiPalette
    {
        // Surfaces (logo fill #081018 and steps above/below it).
        constexpr UiColor kVoid   = uiRgb8(8, 16, 24);  // #081018 canvas / window
        constexpr UiColor kPanel  = uiRgb8(12, 24, 36); // #0C1824 popups
        constexpr UiColor kRaised = uiRgb8(20, 32, 48); // #142030 frames, menu bar
        constexpr UiColor kInset  = uiRgb8(5, 10, 16);  // #050A10 inputs, HUD well
        constexpr UiColor kHover  = uiRgb8(26, 42, 60); // #1A2A3C generic hover

        // Text / borders (logo subtitle #A0B8D8).
        constexpr UiColor kText         = uiRgb8(230, 238, 246); // #E6EEF6
        constexpr UiColor kTextMuted    = uiRgb8(160, 184, 216); // #A0B8D8
        constexpr UiColor kTextDisabled = uiRgb8(90, 112, 136);  // #5A7088
        constexpr UiColor kBorder       = uiRgb8(58, 80, 104);   // #3A5068

        // Per-host accents (logo title + stroke).
        constexpr UiColor kAccentEngine    = uiRgb8(64, 176, 248);  // #40B0F8
        constexpr UiColor kAccentEditor    = uiRgb8(200, 120, 248); // #C878F8
        constexpr UiColor kAccentSandbox   = uiRgb8(80, 200, 136);  // #50C888
        constexpr UiColor kAccentSandbox2D = uiRgb8(240, 176, 64);  // #F0B040

        // Semantic (HUD, logs, plots). Ok/Warn reuse Sandbox / Sandbox2D accents.
        constexpr UiColor kOk     = kAccentSandbox;      // #50C888
        constexpr UiColor kWarn   = kAccentSandbox2D;    // #F0B040
        constexpr UiColor kInfo   = kAccentEngine;       // #40B0F8
        constexpr UiColor kDanger = uiRgb8(232, 93, 76); // #E85D4C pawn red
        constexpr UiColor kFatal  = uiRgb8(240, 64, 64); // #F04040

        inline constexpr UiColor accent(UiAccent which)
        {
            switch (which)
            {
            case UiAccent::Editor:
                return kAccentEditor;
            case UiAccent::Sandbox:
                return kAccentSandbox;
            case UiAccent::Sandbox2D:
                return kAccentSandbox2D;
            case UiAccent::Engine:
                return kAccentEngine;
            }
            return kAccentEngine;
        }

        // Health bar fill: high = ok, mid = warn, low = danger.
        inline constexpr UiColor healthFill(float ratio)
        {
            if (ratio <= 0.25f)
                return kDanger;
            if (ratio <= 0.5f)
                return kWarn;
            return kOk;
        }
    } // namespace UiPalette
} // namespace Dark

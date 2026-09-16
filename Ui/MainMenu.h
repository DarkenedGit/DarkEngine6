#pragma once

#include "Core/UiPalette.h"
#include "Math/Matrix4f.h"
#include "Render/Mesh.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class Input;
    class Renderer;

    enum class MainMenuKind : uint8_t
    {
        Scene = 0,
        BuiltIn,
        Quit
    };

    enum class MainMenuResult : uint8_t
    {
        None = 0,
        Confirm,
        Quit
    };

    struct MainMenuEntry
    {
        MainMenuKind          kind = MainMenuKind::Scene;
        std::string           id;
        std::string           title;
        std::string           subtitle;
        std::filesystem::path path;
    };

    // CPU scene picker + optional GPU overlay (SpritePipeline + bitmap font).
    // create() is not required for update() / pollResult(); draw() no-ops until create().
    class MainMenu
    {
    public:
        void setTitle(std::string title);
        void setHint(std::string hint);
        void setAccent(UiAccent accent);

        void clearEntries();
        void addScene(std::string id, std::string title, std::filesystem::path path, std::string subtitle = {});
        void addBuiltIn(std::string id, std::string title, std::string subtitle = {});
        void addQuit(std::string title = "Quit");

        bool selectById(std::string_view id);
        void setSelected(size_t index);

        void show();
        void hide();
        bool visible() const { return m_visible; }

        const std::vector<MainMenuEntry>& entries() const { return m_entries; }
        const MainMenuEntry*              selected() const;
        size_t                            selectedIndex() const { return m_selected; }

        // Keyboard (arrows/WASD, Enter, Esc), mouse, D-pad / A / B. view size is for hit tests.
        void            update(const Input& input, uint32_t viewW, uint32_t viewH);
        MainMenuResult  pollResult();

        bool create(Renderer& renderer, UiAccent accent = UiAccent::Engine);
        void draw(Renderer& renderer);
        void shutdown(Renderer& renderer);
        bool isReady() const { return m_pipe.isValid() && m_quad.valid() && m_white.valid(); }

    private:
        struct Rect
        {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        struct LabelGpu
        {
            std::string text;
            Texture2D   texture;
            uint32_t    width  = 0;
            uint32_t    height = 0;
        };

        struct Layout
        {
            Rect              panel{};
            Rect              title{};
            std::vector<Rect> rows;
            Rect              hint{};
        };

        Layout computeLayout(uint32_t viewW, uint32_t viewH) const;
        bool   hit(const Rect& r, float mx, float my) const;
        void   moveSelection(int delta);
        void   confirmSelection();
        bool   ensureLabels(Renderer& renderer);
        const LabelGpu* findLabel(std::string_view text) const;
        void drawRect(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, const Rect& r, uint32_t viewH, float red, float green, float blue, float alpha) const;
        void drawLabel(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, const Rect& r, uint32_t viewH, std::string_view text, const UiColor& color, float scale) const;

        std::string                m_title = "DarkEngine6";
        std::string                m_hint  = "Arrows / WASD  Enter  Esc";
        UiAccent                   m_accent = UiAccent::Engine;
        std::vector<MainMenuEntry> m_entries;
        size_t                     m_selected = 0;
        bool                       m_visible  = false;
        MainMenuResult             m_pending  = MainMenuResult::None;

        SpritePipeline        m_pipe;
        Mesh                  m_quad;
        Texture2D             m_white;
        std::vector<LabelGpu> m_labels;
        bool                  m_labelsDirty = true;
    };

} // namespace Dark

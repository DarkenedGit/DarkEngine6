#include "Ui/MainMenu.h"

#include "Core/Log.h"
#include "Input/Input.h"
#include "Math/Matrix4f.h"
#include "Render/MeshGen.h"
#include "Render/Renderer.h"

#include <cstring>
#include <vector>

#include "Render/LoadingScreenFont.inl"

namespace Dark
{
    using Math::Matrix4f;

    namespace
    {
        void copyMatrix(float dst[16], const Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }

        int glyphIndex(uint32_t cp)
        {
            if (cp >= 0x20 && cp <= 0x7E)
                return static_cast<int>(cp - 0x20);
            if (cp == 0xA9)
                return 95;
            if (cp == 0xB7)
                return 96;
            return static_cast<int>('?' - 0x20);
        }

        bool rasterizeText(const std::string& text, std::vector<uint8_t>& rgba, uint32_t& outW, uint32_t& outH)
        {
            if (text.empty())
                return false;

            std::vector<int> indices;
            indices.reserve(text.size());
            for (unsigned char c : text)
                indices.push_back(glyphIndex(static_cast<uint32_t>(c)));

            constexpr int kPad     = 1;
            const uint32_t w       = static_cast<uint32_t>(indices.size() * (kGlyphW + kPad) + kPad);
            const uint32_t h       = static_cast<uint32_t>(kGlyphH);
            rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);

            for (size_t n = 0; n < indices.size(); ++n)
            {
                const int gi = indices[n];
                if (gi < 0 || gi >= kGlyphCount)
                    continue;
                const int ox = static_cast<int>(n) * (kGlyphW + kPad) + kPad;
                for (int row = 0; row < kGlyphH; ++row)
                {
                    const uint8_t bits = kGlyphs[gi][row];
                    for (int col = 0; col < kGlyphW; ++col)
                    {
                        if ((bits & static_cast<uint8_t>(0x80 >> col)) == 0)
                            continue;
                        const size_t p = (static_cast<size_t>(row) * w + static_cast<size_t>(ox + col)) * 4u;
                        rgba[p + 0]    = 255;
                        rgba[p + 1]    = 255;
                        rgba[p + 2]    = 255;
                        rgba[p + 3]    = 255;
                    }
                }
            }
            outW = w;
            outH = h;
            return true;
        }

        constexpr float kPanelW     = 560.0f;
        constexpr float kRowH       = 56.0f;
        constexpr float kRowGap     = 10.0f;
        constexpr float kTitleH     = 40.0f;
        constexpr float kHintH      = 22.0f;
        constexpr float kPanelPad   = 28.0f;
        constexpr float kTitleGap   = 18.0f;
        constexpr float kHintGap    = 16.0f;
    } // namespace

    void MainMenu::setTitle(std::string title)
    {
        if (title.empty())
            title = "DarkEngine6";
        if (m_title == title)
            return;
        m_title       = std::move(title);
        m_labelsDirty = true;
    }

    void MainMenu::setHint(std::string hint)
    {
        if (m_hint == hint)
            return;
        m_hint        = std::move(hint);
        m_labelsDirty = true;
    }

    void MainMenu::setAccent(UiAccent accent)
    {
        m_accent = accent;
    }

    void MainMenu::clearEntries()
    {
        m_entries.clear();
        m_selected    = 0;
        m_labelsDirty = true;
    }

    void MainMenu::addScene(std::string id, std::string title, std::filesystem::path path, std::string subtitle)
    {
        MainMenuEntry e{};
        e.kind     = MainMenuKind::Scene;
        e.id       = std::move(id);
        e.title    = title.empty() ? e.id : std::move(title);
        e.subtitle = std::move(subtitle);
        e.path     = std::move(path);
        m_entries.push_back(std::move(e));
        m_labelsDirty = true;
    }

    void MainMenu::addBuiltIn(std::string id, std::string title, std::string subtitle)
    {
        MainMenuEntry e{};
        e.kind     = MainMenuKind::BuiltIn;
        e.id       = std::move(id);
        e.title    = title.empty() ? e.id : std::move(title);
        e.subtitle = std::move(subtitle);
        m_entries.push_back(std::move(e));
        m_labelsDirty = true;
    }

    void MainMenu::addQuit(std::string title)
    {
        MainMenuEntry e{};
        e.kind  = MainMenuKind::Quit;
        e.id    = "quit";
        e.title = title.empty() ? "Quit" : std::move(title);
        m_entries.push_back(std::move(e));
        m_labelsDirty = true;
    }

    bool MainMenu::selectById(std::string_view id)
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].id == id)
            {
                m_selected = i;
                return true;
            }
        }
        return false;
    }

    void MainMenu::setSelected(size_t index)
    {
        if (m_entries.empty())
        {
            m_selected = 0;
            return;
        }
        m_selected = (index < m_entries.size()) ? index : (m_entries.size() - 1);
    }

    void MainMenu::show()
    {
        m_visible = true;
        m_pending = MainMenuResult::None;
    }

    void MainMenu::hide()
    {
        m_visible = false;
        m_pending = MainMenuResult::None;
    }

    const MainMenuEntry* MainMenu::selected() const
    {
        if (m_selected >= m_entries.size())
            return nullptr;
        return &m_entries[m_selected];
    }

    MainMenuResult MainMenu::pollResult()
    {
        const MainMenuResult r = m_pending;
        m_pending              = MainMenuResult::None;
        return r;
    }

    void MainMenu::moveSelection(int delta)
    {
        if (m_entries.empty() || delta == 0)
            return;
        const int n   = static_cast<int>(m_entries.size());
        int       idx = static_cast<int>(m_selected) + delta;
        while (idx < 0)
            idx += n;
        while (idx >= n)
            idx -= n;
        m_selected = static_cast<size_t>(idx);
    }

    void MainMenu::confirmSelection()
    {
        const MainMenuEntry* e = selected();
        if (!e)
            return;
        m_pending = (e->kind == MainMenuKind::Quit) ? MainMenuResult::Quit : MainMenuResult::Confirm;
    }

    bool MainMenu::hit(const Rect& r, float mx, float my) const
    {
        return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
    }

    MainMenu::Layout MainMenu::computeLayout(uint32_t viewW, uint32_t viewH) const
    {
        Layout layout{};
        const float vw = static_cast<float>(viewW);
        const float vh = static_cast<float>(viewH);
        if (vw < 8.0f || vh < 8.0f)
            return layout;

        const size_t rows = m_entries.size();
        const float  rowsH = (rows == 0) ? 0.0f : (static_cast<float>(rows) * kRowH + static_cast<float>(rows - 1) * kRowGap);
        const float  innerH = kTitleH + kTitleGap + rowsH + kHintGap + kHintH;
        const float  panelH = innerH + kPanelPad * 2.0f;
        const float  panelW = (kPanelW + kPanelPad * 2.0f > vw - 32.0f) ? (vw - 32.0f) : kPanelW;

        layout.panel.w = panelW;
        layout.panel.h = panelH;
        layout.panel.x = (vw - panelW) * 0.5f;
        layout.panel.y = (vh - panelH) * 0.5f;

        layout.title.x = layout.panel.x + kPanelPad;
        layout.title.y = layout.panel.y + kPanelPad;
        layout.title.w = panelW - kPanelPad * 2.0f;
        layout.title.h = kTitleH;

        float y = layout.title.y + layout.title.h + kTitleGap;
        layout.rows.resize(rows);
        for (size_t i = 0; i < rows; ++i)
        {
            Rect& r = layout.rows[i];
            r.x     = layout.panel.x + kPanelPad;
            r.y     = y;
            r.w     = panelW - kPanelPad * 2.0f;
            r.h     = kRowH;
            y += kRowH + kRowGap;
        }

        layout.hint.x = layout.panel.x + kPanelPad;
        layout.hint.y = layout.panel.y + panelH - kPanelPad - kHintH;
 
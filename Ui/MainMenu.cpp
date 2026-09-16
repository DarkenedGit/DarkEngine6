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
        layout.hint.w = panelW - kPanelPad * 2.0f;
        layout.hint.h = kHintH;
        return layout;
    }

    void MainMenu::update(const Input& input, uint32_t viewW, uint32_t viewH)
    {
        if (!m_visible)
            return;

        if (input.keyPressed(Key::Down) || input.keyPressed(Key::S) || input.buttonPressed(GamepadButton::DPadDown))
            moveSelection(1);
        if (input.keyPressed(Key::Up) || input.keyPressed(Key::W) || input.buttonPressed(GamepadButton::DPadUp))
            moveSelection(-1);

        const Layout layout = computeLayout(viewW, viewH);
        const float  mx     = static_cast<float>(input.mouseX());
        const float  my     = static_cast<float>(input.mouseY());
        for (size_t i = 0; i < layout.rows.size(); ++i)
        {
            if (hit(layout.rows[i], mx, my))
            {
                m_selected = i;
                if (input.mousePressed(MouseButton::Left))
                {
                    confirmSelection();
                    return;
                }
            }
        }

        if (input.keyPressed(Key::Enter) || input.keyPressed(Key::Space) || input.buttonPressed(GamepadButton::A))
        {
            confirmSelection();
            return;
        }

        if (input.keyPressed(Key::Escape) || input.buttonPressed(GamepadButton::B) || input.buttonPressed(GamepadButton::Back))
            m_pending = MainMenuResult::Quit;
    }

    bool MainMenu::create(Renderer& renderer, UiAccent accent)
    {
        m_accent = accent;
        if (!m_pipe.create(renderer.device(), false))
        {
            DE_LOG_ERROR(LogCategory::Render, "MainMenu: sprite pipeline failed");
            return false;
        }
        MeshData quad;
        if (!CreateQuadXY(quad, 1.0f, 1.0f) || !Mesh::tryCreate(renderer, quad, m_quad))
        {
            DE_LOG_ERROR(LogCategory::Render, "MainMenu: quad mesh failed");
            return false;
        }
        if (!m_white.createSolidColor(renderer, 255, 255, 255, 255))
        {
            DE_LOG_ERROR(LogCategory::Render, "MainMenu: white texture failed");
            return false;
        }
        m_labelsDirty = true;
        return ensureLabels(renderer);
    }

    bool MainMenu::ensureLabels(Renderer& renderer)
    {
        if (!m_labelsDirty)
            return true;

        m_labels.clear();
        auto add = [&](const std::string& text) {
            if (text.empty())
                return;
            for (const LabelGpu& existing : m_labels)
            {
                if (existing.text == text)
                    return;
            }
            std::vector<uint8_t> rgba;
            uint32_t             w = 0;
            uint32_t             h = 0;
            if (!rasterizeText(text, rgba, w, h))
                return;
            LabelGpu lab;
            lab.text = text;
            if (!lab.texture.createFromRGBA(renderer, rgba.data(), w, h, w * 4u))
            {
                DE_LOG_ERROR(LogCategory::Render, "MainMenu: text texture failed for '{}'", text);
                return;
            }
            lab.width  = w;
            lab.height = h;
            m_labels.push_back(std::move(lab));
        };

        add(m_title);
        add(m_hint);
        for (const MainMenuEntry& e : m_entries)
        {
            add(e.title);
            add(e.subtitle);
        }
        m_labelsDirty = false;
        return true;
    }

    const MainMenu::LabelGpu* MainMenu::findLabel(std::string_view text) const
    {
        for (const LabelGpu& lab : m_labels)
        {
            if (lab.text == text)
                return &lab;
        }
        return nullptr;
    }

    void MainMenu::drawRect(
        ID3D12GraphicsCommandList* cmd,
        const Matrix4f& viewProj,
        const Rect& r,
        uint32_t viewH,
        float red,
        float green,
        float blue,
        float alpha) const
    {
        if (!cmd || r.w <= 0.0f || r.h <= 0.0f)
            return;
        const float cx = r.x + r.w * 0.5f;
        const float cy = static_cast<float>(viewH) - (r.y + r.h * 0.5f);
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(r.w, r.h, 1.0f) * Matrix4f::TranslationMatrix(cx, cy, 0.05f);
        SpriteConstants cb{};
        copyMatrix(cb.worldViewProj, world * viewProj);
        cb.color[0]    = red;
        cb.color[1]    = green;
        cb.color[2]    = blue;
        cb.color[3]    = alpha;
        cb.uvScale[0]  = 1.0f;
        cb.uvScale[1]  = 1.0f;
        cb.uvOffset[0] = 0.0f;
        cb.uvOffset[1] = 0.0f;
        m_white.bind(cmd, SpritePipeline::kRootAlbedoSrv);
        m_pipe.setConstants(cmd, cb);
        m_quad.draw(cmd);
    }

    void MainMenu::drawLabel(
        ID3D12GraphicsCommandList* cmd,
        const Matrix4f& viewProj,
        const Rect& r,
        uint32_t viewH,
        std::string_view text,
        const UiColor& color,
        float scale) const
    {
        if (!cmd || text.empty())
            return;
        const LabelGpu* lab = findLabel(text);
        if (!lab || !lab->texture.valid() || lab->width == 0 || lab->height == 0)
            return;

        const float tw = static_cast<float>(lab->width) * scale;
        const float th = static_cast<float>(lab->height) * scale;
        const float x  = r.x + 16.0f;
        const float y  = r.y + (r.h - th) * 0.5f;
        const float cx = x + tw * 0.5f;
        const float cy = static_cast<float>(viewH) - (y + th * 0.5f);

        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(tw, th, 1.0f) * Matrix4f::TranslationMatrix(cx, cy, 0.08f);
        SpriteConstants cb{};
        copyMatrix(cb.worldViewProj, world * viewProj);
        cb.color[0]    = color.r;
        cb.color[1]    = color.g;
        cb.color[2]    = color.b;
        cb.color[3]    = color.a;
        cb.uvScale[0]  = 1.0f;
        cb.uvScale[1]  = 1.0f;
        cb.uvOffset[0] = 0.0f;
        cb.uvOffset[1] = 0.0f;
        lab->texture.bind(cmd, SpritePipeline::kRootAlbedoSrv);
        m_pipe.setConstants(cmd, cb);
        m_quad.draw(cmd);
    }

    void MainMenu::draw(Renderer& renderer)
    {
        if (!m_visible || !isReady())
            return;

        auto* cmd = renderer.commandList();
        const uint32_t viewW = renderer.width();
        const uint32_t viewH = renderer.height();
        if (!cmd || viewW < 8 || viewH < 8)
            return;

        const Layout layout = computeLayout(viewW, viewH);
        const float  w      = static_cast<float>(viewW);
        const float  h      = static_cast<float>(viewH);
        const Matrix4f proj = Matrix4f::OrthographicOffCenterLHMatrix(0.0f, w, 0.0f, h, 0.0f, 1.0f);
        const UiColor accent = UiPalette::accent(m_accent);

        m_pipe.bind(cmd);

        const Rect screen{ 0.0f, 0.0f, w, h };
        drawRect(cmd, proj, screen, viewH, UiPalette::kVoid.r, UiPalette::kVoid.g, UiPalette::kVoid.b, 0.72f);
        drawRect(cmd, proj, layout.panel, viewH, UiPalette::kPanel.r, UiPalette::kPanel.g, UiPalette::kPanel.b, 0.94f);

        Rect titleBar = layout.panel;
        titleBar.h    = 4.0f;
        drawRect(cmd, proj, titleBar, viewH, accent.r, accent.g, accent.b, 1.0f);

        drawLabel(cmd, proj, layout.title, viewH, m_title, UiPalette::kText, 2.5f);

        for (size_t i = 0; i < layout.rows.size(); ++i)
        {
            const Rect& r       = layout.rows[i];
            const bool  selected = (i == m_selected);
            const UiColor fill  = selected ? UiPalette::kHover : UiPalette::kRaised;
            drawRect(cmd, proj, r, viewH, fill.r, fill.g, fill.b, selected ? 0.95f : 0.88f);
            if (selected)
            {
                Rect accentBar = r;
                accentBar.w    = 6.0f;
                drawRect(cmd, proj, accentBar, viewH, accent.r, accent.g, accent.b, 1.0f);
            }
            if (i < m_entries.size())
            {
                const MainMenuEntry& e     = m_entries[i];
                const UiColor        label = (e.kind == MainMenuKind::Quit) ? UiPalette::kDanger : UiPalette::kText;
                Rect                 titleR = r;
                if (!e.subtitle.empty())
                {
                    titleR.h = r.h * 0.58f;
                    Rect subR = r;
                    subR.y    = r.y + r.h * 0.48f;
                    subR.h    = r.h * 0.42f;
                    drawLabel(cmd, proj, titleR, viewH, e.title, label, 2.0f);
                    drawLabel(cmd, proj, subR, viewH, e.subtitle, UiPalette::kTextMuted, 1.5f);
                }
                else
                    drawLabel(cmd, proj, titleR, viewH, e.title, label, 2.0f);
            }
        }

        if (!m_hint.empty())
            drawLabel(cmd, proj, layout.hint, viewH, m_hint, UiPalette::kTextMuted, 1.5f);
    }

    void MainMenu::shutdown(Renderer& renderer)
    {
        if (renderer.device() && renderer.queue())
            renderer.waitForGpu();
        m_labels.clear();
        m_white = Texture2D{};
        m_quad  = Mesh{};
        m_pipe  = SpritePipeline{};
        m_labelsDirty = true;
    }

} // namespace Dark

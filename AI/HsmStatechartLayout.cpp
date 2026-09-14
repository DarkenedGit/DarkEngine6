#include "AI/HsmStatechartLayout.h"

namespace Dark
{
    namespace
    {
        float nameWidth(const std::string& name)
        {
            const float w = static_cast<float>(name.size()) * kHsmChartCharW + 24.0f;
            return w < kHsmChartMinLeafW ? kHsmChartMinLeafW : w;
        }

        struct Measured
        {
            int                   index = -1;
            float                 w     = 0.0f;
            float                 h     = 0.0f;
            std::vector<Measured> kids;
        };

        void collectChildren(const HsmGraphDef& def, const std::string& parent, std::vector<int>& out)
        {
            out.clear();
            for (uint32_t i = 0; i < def.states.size(); ++i)
            {
                if (def.states[i].parent == parent && def.states[i].name != parent)
                    out.push_back(static_cast<int>(i));
            }
        }

        Measured measure(const HsmGraphDef& def, int index, std::vector<uint8_t>& visiting)
        {
            Measured m;
            m.index = index;
            if (index < 0 || static_cast<uint32_t>(index) >= def.states.size())
                return m;
            if (visiting[static_cast<uint32_t>(index)])
            {
                m.w = nameWidth(def.states[static_cast<uint32_t>(index)].name);
                m.h = kHsmChartLeafH;
                return m;
            }
            visiting[static_cast<uint32_t>(index)] = 1;

            std::vector<int> childIdx;
            collectChildren(def, def.states[static_cast<uint32_t>(index)].name, childIdx);
            if (childIdx.empty())
            {
                m.w = nameWidth(def.states[static_cast<uint32_t>(index)].name);
                m.h = kHsmChartLeafH;
                visiting[static_cast<uint32_t>(index)] = 0;
                return m;
            }

            float innerW = kHsmChartPad;
            float innerH = 0.0f;
            for (int ci : childIdx)
            {
                Measured kid = measure(def, ci, visiting);
                innerW += kid.w + kHsmChartGap;
                if (kid.h > innerH)
                    innerH = kid.h;
                m.kids.push_back(std::move(kid));
            }
            innerW = innerW - kHsmChartGap + kHsmChartPad;
            const float titleW = nameWidth(def.states[static_cast<uint32_t>(index)].name) + 28.0f;
            m.w = innerW > titleW ? innerW : titleW;
            m.h = kHsmChartTitleH + kHsmChartPad + innerH + kHsmChartPad;
            visiting[static_cast<uint32_t>(index)] = 0;
            return m;
        }

        void place(const Measured& m, float x, float y, int depth, HsmChartLayout& out)
        {
            HsmChartBox box;
            box.stateIndex = m.index;
            box.depth      = depth;
            box.x          = x;
            box.y          = y;
            box.w          = m.w;
            box.h          = m.h;
            out.boxes.push_back(box);
            float cx = x + kHsmChartPad;
            float cy = y + kHsmChartTitleH + kHsmChartPad;
            for (const Measured& kid : m.kids)
            {
                place(kid, cx, cy, depth + 1, out);
                cx += kid.w + kHsmChartGap;
            }
        }
    } // namespace

    bool layoutHsmStatechart(const HsmGraphDef& def, HsmChartLayout& out)
    {
        out.boxes.clear();
        out.width  = 0.0f;
        out.height = 0.0f;
        if (def.states.empty())
            return false;

        int root = def.findStateIndex(def.root);
        if (root < 0)
            root = 0;

        std::vector<uint8_t> visiting(def.states.size(), 0);
        Measured tree = measure(def, root, visiting);
        place(tree, 0.0f, 0.0f, 0, out);

        std::vector<uint8_t> placed(def.states.size(), 0);
        for (const HsmChartBox& b : out.boxes)
        {
            if (b.stateIndex >= 0)
                placed[static_cast<uint32_t>(b.stateIndex)] = 1;
        }

        float extraX = tree.w + kHsmChartGap;
        for (uint32_t i = 0; i < def.states.size(); ++i)
        {
            if (placed[i])
                continue;
            visiting.assign(def.states.size(), 0);
            Measured extra = measure(def, static_cast<int>(i), visiting);
            place(extra, extraX, 0.0f, 0, out);
            extraX += extra.w + kHsmChartGap;
            if (extra.h > tree.h)
                tree.h = extra.h;
        }

        out.width  = extraX > tree.w ? extraX - kHsmChartGap : tree.w;
        out.height = tree.h;
        return !out.boxes.empty();
    }

    const HsmChartBox* findHsmChartBox(const HsmChartLayout& layout, int stateIndex)
    {
        for (const HsmChartBox& b : layout.boxes)
        {
            if (b.stateIndex == stateIndex)
                return &b;
        }
        return nullptr;
    }

    int hitTestHsmChart(const HsmChartLayout& layout, float x, float y)
    {
        for (int i = static_cast<int>(layout.boxes.size()) - 1; i >= 0; --i)
        {
            const HsmChartBox& b = layout.boxes[static_cast<uint32_t>(i)];
            if (x >= b.x && y >= b.y && x <= b.x + b.w && y <= b.y + b.h)
                return b.stateIndex;
        }
        return -1;
    }
} // namespace Dark

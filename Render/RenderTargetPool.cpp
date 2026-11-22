#include "Render/RenderTargetPool.h"

#include <bit>
#include <cmath>
#include <utility>

namespace Dark::Rg
{

    bool resolveTargetExtent(const TargetDesc& desc, uint32_t outputWidth, uint32_t outputHeight, TargetExtent& out)
    {
        out = {};
        if (desc.mips == 0 || desc.arraySize == 0 || desc.format == 0 || (desc.rtv && desc.dsv))
            return false;
        if (!std::isfinite(desc.depth))
            return false;
        for (float component : desc.color)
            if (!std::isfinite(component))
                return false;
        if (desc.size == TargetSize::Absolute)
        {
            out = { desc.width, desc.height };
            return out.width != 0 && out.height != 0;
        }
        if ((desc.size != TargetSize::OutputFloor && desc.size != TargetSize::OutputCeil) || desc.divisor == 0 || outputWidth == 0 || outputHeight == 0)
            return false;
        const auto divide = [&](uint32_t size)
        {
            uint32_t result = size / desc.divisor;
            if (desc.size == TargetSize::OutputCeil && size % desc.divisor != 0)
                ++result;
            return result == 0 ? 1u : result;
        };
        out = { divide(outputWidth), divide(outputHeight) };
        return true;
    }

    uint64_t hashTargetDesc(const TargetDesc& desc)
    {
        // Hash fields explicitly: no padding bytes, and +0/-0 compare equal so must hash equally.
        uint64_t   hash = 14695981039346656037ull;
        const auto add  = [&](uint32_t value)
        {
            for (uint32_t byte = 0; byte < 4; ++byte)
            {
                hash ^= (value >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ull;
            }
        };
        const auto addFloat = [&](float value) { add(value == 0.0f ? 0u : std::bit_cast<uint32_t>(value)); };
        add(static_cast<uint32_t>(desc.size));
        add(desc.width);
        add(desc.height);
        add(desc.divisor);
        add(desc.mips);
        add(desc.arraySize);
        add(desc.format);
        add(desc.srvFormat);
        add(desc.targetFormat);
        add(desc.flags);
        add(desc.initialState);
        add(desc.srv ? 1u : 0u);
        add(desc.rtv ? 1u : 0u);
        add(desc.dsv ? 1u : 0u);
        add(desc.clear ? 1u : 0u);
        for (float component : desc.color)
            addFloat(component);
        addFloat(desc.depth);
        add(desc.stencil);
        return hash;
    }

    RenderTargetPool::~RenderTargetPool()
    {
        clear();
    }

    const RenderTargetPool::Entry* RenderTargetPool::entry(TargetId id) const
    {
        if (!id.valid() || id.index >= m_entries.size())
            return nullptr;
        const Entry& candidate = m_entries[id.index];
        return candidate.storage && candidate.serial == id.serial ? &candidate : nullptr;
    }

    TargetId RenderTargetPool::find(const char* name) const
    {
        if (!name || !name[0])
            return {};
        for (uint32_t index = 0; index < m_entries.size(); ++index)
        {
            const Entry& e = m_entries[index];
            if (e.storage && e.name == name)
                return { index, e.serial };
        }
        return {};
    }

    TargetId RenderTargetPool::registerTexture(const char* name, const TargetDesc& desc)
    {
        if (!name || !name[0])
            return {};
        const TargetId existing = find(name);
        if (existing.valid())
            return m_entries[existing.index].desc == desc ? existing : TargetId{};
        TargetExtent extent;
        if (!resolveTargetExtent(desc, m_width, m_height, extent))
            return {};
        std::unique_ptr<TargetStorage> storage = m_backend.create(desc, extent, name);
        if (!storage)
            return {};
        uint32_t index = 0;
        for (; index < m_entries.size(); ++index)
            if (!m_entries[index].storage)
                break;
        if (index == m_entries.size())
            m_entries.emplace_back();
        Entry& e     = m_entries[index];
        e.name       = name;
        e.desc       = desc;
        e.extent     = extent;
        e.storage    = std::move(storage);
        e.serial     = m_nextSerial++;
        e.generation = 1;
        ++m_count;
        return { index, e.serial };
    }

    bool RenderTargetPool::unregisterTexture(TargetId id)
    {
        if (!entry(id))
            return false;
        Entry& e = m_entries[id.index];
        m_backend.retire(std::move(e.storage));
        e = {};
        --m_count;
        return true;
    }

    TargetInfo RenderTargetPool::get(TargetId id) const
    {
        const Entry* e = entry(id);
        return e ? TargetInfo{ e->storage.get(), e->extent, e->generation } : TargetInfo{};
    }

    bool RenderTargetPool::resizeAfterDrain(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || (width == m_width && height == m_height))
            return true;
        struct Replacement
        {
            uint32_t                       index;
            TargetExtent                   extent;
            std::unique_ptr<TargetStorage> storage;
        };
        std::vector<Replacement> replacements;
        for (uint32_t index = 0; index < m_entries.size(); ++index)
        {
            const Entry& e = m_entries[index];
            if (!e.storage)
                continue;
            TargetExtent extent;
            if (!resolveTargetExtent(e.desc, width, height, extent))
                return false;
            if (extent == e.extent)
                continue;
            std::unique_ptr<TargetStorage> storage = m_backend.create(e.desc, extent, e.name.c_str());
            if (!storage)
                return false; // temporary replacements have never been submitted; free immediately
            replacements.push_back({ index, extent, std::move(storage) });
        }
        for (Replacement& replacement : replacements)
        {
            Entry& e = m_entries[replacement.index];
            m_backend.retire(std::move(e.storage));
            e.storage = std::move(replacement.storage);
            e.extent  = replacement.extent;
            ++e.generation;
        }
        m_width  = width;
        m_height = height;
        return true;
    }

    void RenderTargetPool::clear()
    {
        for (Entry& e : m_entries)
            if (e.storage)
                m_backend.retire(std::move(e.storage));
        m_entries.clear();
        m_count = 0;
        // Serial numbers are intentionally NOT reset: IDs from before clear() remain stale.
    }

} // namespace Dark::Rg

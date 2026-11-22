#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID3D12Resource;

namespace Dark::Rg
{

    // Screen-size policies deliberately distinguish rounded-up GTAO/SSR half sizes from the
    // rounded-down bloom/exposure chain. No floating-point scale or platform-dependent padding
    // participates in the descriptor hash.
    enum class TargetSize : uint8_t
    {
        Absolute,
        OutputFloor,
        OutputCeil,
    };

    // DXGI_FORMAT / D3D12_RESOURCE_FLAGS / D3D12_RESOURCE_STATES are integers here: this registry and
    // its lifetime/resize tests run without Windows. The D3D12 backend interprets those fields.
    struct TargetDesc
    {
        TargetSize size  = TargetSize::OutputFloor;
        uint32_t   width = 0, height = 0; // Absolute only
        uint32_t   divisor = 1;           // output-relative only; each dimension is at least 1
        uint16_t   mips = 1, arraySize = 1;
        uint32_t   format = 0, srvFormat = 0, targetFormat = 0; // 0 view format = resource format
        uint32_t   flags = 0, initialState = 0;
        bool       srv = true, rtv = false, dsv = false;
        bool       clear    = false;
        float      color[4] = {};
        float      depth    = 0.0f;
        uint8_t    stencil  = 0;

        bool operator==(const TargetDesc&) const = default;
    };

    struct TargetExtent
    {
        uint32_t width = 0, height = 0;
        bool     operator==(const TargetExtent&) const = default;
    };

    // False for an invalid descriptor or minimized output. Resolution is overflow safe, including
    // ceil division at UINT32_MAX; there is no allocation while the window is minimized.
    bool     resolveTargetExtent(const TargetDesc& desc, uint32_t outputWidth, uint32_t outputHeight, TargetExtent& out);
    uint64_t hashTargetDesc(const TargetDesc& desc);

    struct TargetId
    {
        uint32_t index  = UINT32_MAX;
        uint64_t serial = 0; // prevents a stale ID from observing a subsequently reused slot
        bool     valid() const
        {
            return index != UINT32_MAX && serial != 0;
        }
        bool operator==(const TargetId&) const = default;
    };

    // Backend-owned texture plus graph-owned CPU views (never a shader-visible copy source).
    // Destruction releases views and memory together. The backend's retire() controls when it is
    // safe to destroy them. The pool never waits for the GPU and never owns a shader-visible ring.
    struct TargetStorage
    {
        virtual ~TargetStorage() = default;
        ID3D12Resource* native   = nullptr;
        uintptr_t       srv = 0, rtv = 0, dsv = 0, readOnlyDsv = 0;
    };

    class TargetBackend
    {
    public:
        virtual ~TargetBackend()                                                                                     = default;
        virtual std::unique_ptr<TargetStorage> create(const TargetDesc& desc, TargetExtent extent, const char* name) = 0;
        // Called on replacement, unregister and pool teardown. The implementation must retain the
        // entire storage (including descriptors and allocation), not merely its native resource.
        virtual void retire(std::unique_ptr<TargetStorage> storage) = 0;
    };

    struct TargetInfo
    {
        const TargetStorage* storage = nullptr;
        TargetExtent         extent;
        uint64_t             generation = 0; // changes on actual recreation, not merely an output-size change
    };

    // Persistent render-graph registry. Stable IDs; names are unique; no aliasing (Phase 9).
    // The backend must outlive this pool. Neither is thread safe; registration and resize happen
    // on the render thread, before any recording that uses the affected targets.
    class RenderTargetPool
    {
    public:
        explicit RenderTargetPool(TargetBackend& backend) :
            m_backend(backend)
        {
        }
        ~RenderTargetPool();
        RenderTargetPool(const RenderTargetPool&)            = delete;
        RenderTargetPool& operator=(const RenderTargetPool&) = delete;

        // The same name+desc returns the existing ID. A conflicting desc is rejected, not silently
        // recreated mid-frame. For an intentional format change, unregister the old target first.
        TargetId   registerTexture(const char* name, const TargetDesc& desc);
        bool       unregisterTexture(TargetId id);
        TargetInfo get(TargetId id) const;
        TargetId   find(const char* name) const;

        // Called after Renderer has drained the GPU exactly once. Creates only resources whose
        // resolved extents changed. Transactional: a failed allocation keeps ALL old targets,
        // generations and the old output dimensions, so retrying the same resize works.
        // Zero dimensions mean minimized: retain everything and return true.
        bool     resizeAfterDrain(uint32_t width, uint32_t height);
        void     clear();
        uint32_t count() const
        {
            return m_count;
        }
        uint32_t outputWidth() const
        {
            return m_width;
        }
        uint32_t outputHeight() const
        {
            return m_height;
        }

    private:
        struct Entry
        {
            std::string                    name;
            TargetDesc                     desc;
            TargetExtent                   extent;
            std::unique_ptr<TargetStorage> storage;
            uint64_t                       serial     = 0;
            uint64_t                       generation = 0;
        };
        const Entry*       entry(TargetId id) const;
        TargetBackend&     m_backend;
        std::vector<Entry> m_entries;
        uint32_t           m_width = 0, m_height = 0, m_count = 0;
        uint64_t           m_nextSerial = 1;
    };

} // namespace Dark::Rg

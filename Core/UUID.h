#pragma once
#include <cstdint>
#include <functional>

namespace Dark
{
    class UUID 
    {
    public:
        UUID();                          // generates a new random ID
        explicit UUID(uint64_t id);

        operator uint64_t() const { return m_id; }
        bool operator==(const UUID& o) const = default;

    private:
        uint64_t m_id;
    };
} // namespace Dark

template<> struct std::hash<Dark::UUID>
{
    size_t operator()(const Dark::UUID& id) const noexcept
    {
        return std::hash<uint64_t>{}(static_cast<uint64_t>(id));
    }
};

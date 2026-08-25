#pragma once

#include <cstdint>

namespace Dark
{

    class PacketWriter
    {
    public:
        bool begin(uint8_t* buf, uint32_t cap);
        bool writeU8(uint8_t v);
        bool writeU16(uint16_t v);
        bool writeU32(uint32_t v);
        bool writeU64(uint64_t v);
        bool writeF32(float v);
        bool writeBytes(const void* p, uint32_t n);
        uint32_t size() const { return m_pos; }

    private:
        bool ensure(uint32_t n) const;

        uint8_t* m_buf = nullptr;
        uint32_t m_cap = 0;
        uint32_t m_pos = 0;
    };

    class PacketReader
    {
    public:
        bool begin(const uint8_t* buf, uint32_t size);
        bool readU8(uint8_t& v);
        bool readU16(uint16_t& v);
        bool readU32(uint32_t& v);
        bool readU64(uint64_t& v);
        bool readF32(float& v);
        bool readBytes(void* p, uint32_t n);
        uint32_t remaining() const { return (m_pos <= m_size) ? (m_size - m_pos) : 0; }

    private:
        bool ensure(uint32_t n) const;

        const uint8_t* m_buf  = nullptr;
        uint32_t       m_size = 0;
        uint32_t       m_pos  = 0;
    };

} // namespace Dark

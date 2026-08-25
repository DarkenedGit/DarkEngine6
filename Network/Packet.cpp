#include "Network/Packet.h"

#include <bit>
#include <cstring>

namespace Dark
{

    bool PacketWriter::begin(uint8_t* buf, uint32_t cap)
    {
        if (!buf)
        {
            m_buf = nullptr;
            m_cap = 0;
            m_pos = 0;
            return false;
        }
        m_buf = buf;
        m_cap = cap;
        m_pos = 0;
        return true;
    }

    bool PacketWriter::ensure(uint32_t n) const
    {
        if (!m_buf)
            return false;
        if (m_pos > m_cap || n > m_cap - m_pos)
            return false;
        return true;
    }

    bool PacketWriter::writeU8(uint8_t v)
    {
        if (!ensure(1))
            return false;
        m_buf[m_pos] = v;
        ++m_pos;
        return true;
    }

    bool PacketWriter::writeU16(uint16_t v)
    {
        if (!ensure(2))
            return false;
        m_buf[m_pos + 0] = static_cast<uint8_t>(v);
        m_buf[m_pos + 1] = static_cast<uint8_t>(v >> 8);
        m_pos += 2;
        return true;
    }

    bool PacketWriter::writeU32(uint32_t v)
    {
        if (!ensure(4))
            return false;
        m_buf[m_pos + 0] = static_cast<uint8_t>(v);
        m_buf[m_pos + 1] = static_cast<uint8_t>(v >> 8);
        m_buf[m_pos + 2] = static_cast<uint8_t>(v >> 16);
        m_buf[m_pos + 3] = static_cast<uint8_t>(v >> 24);
        m_pos += 4;
        return true;
    }

    bool PacketWriter::writeU64(uint64_t v)
    {
        return writeU32(static_cast<uint32_t>(v)) && writeU32(static_cast<uint32_t>(v >> 32));
    }

    bool PacketWriter::writeF32(float v)
    {
        return writeU32(std::bit_cast<uint32_t>(v));
    }

    bool PacketWriter::writeBytes(const void* p, uint32_t n)
    {
        if (n == 0)
            return m_buf != nullptr;
        if (!p || !ensure(n))
            return false;
        std::memcpy(m_buf + m_pos, p, n);
        m_pos += n;
        return true;
    }

    bool PacketReader::begin(const uint8_t* buf, uint32_t size)
    {
        if (!buf)
        {
            m_buf  = nullptr;
            m_size = 0;
            m_pos  = 0;
            return false;
        }
        m_buf  = buf;
        m_size = size;
        m_pos  = 0;
        return true;
    }

    bool PacketReader::ensure(uint32_t n) const
    {
        if (!m_buf)
            return false;
        if (m_pos > m_size || n > m_size - m_pos)
            return false;
        return true;
    }

    bool PacketReader::readU8(uint8_t& v)
    {
        if (!ensure(1))
            return false;
        v = m_buf[m_pos];
        ++m_pos;
        return true;
    }

    bool PacketReader::readU16(uint16_t& v)
    {
        if (!ensure(2))
            return false;
        v = static_cast<uint16_t>(m_buf[m_pos] | (static_cast<uint16_t>(m_buf[m_pos + 1]) << 8));
        m_pos += 2;
        return true;
    }

    bool PacketReader::readU32(uint32_t& v)
    {
        if (!ensure(4))
            return false;
        v = static_cast<uint32_t>(m_buf[m_pos]) | (static_cast<uint32_t>(m_buf[m_pos + 1]) << 8)
            | (static_cast<uint32_t>(m_buf[m_pos + 2]) << 16) | (static_cast<uint32_t>(m_buf[m_pos + 3]) << 24);
        m_pos += 4;
        return true;
    }

    bool PacketReader::readU64(uint64_t& v)
    {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!readU32(lo) || !readU32(hi))
            return false;
        v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }

    bool PacketReader::readF32(float& v)
    {
        uint32_t bits = 0;
        if (!readU32(bits))
            return false;
        v = std::bit_cast<float>(bits);
        return true;
    }

    bool PacketReader::readBytes(void* p, uint32_t n)
    {
        if (n == 0)
            return m_buf != nullptr;
        if (!p || !ensure(n))
            return false;
        std::memcpy(p, m_buf + m_pos, n);
        m_pos += n;
        return true;
    }

} // namespace Dark

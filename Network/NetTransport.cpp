// NetTransport.cpp — UDP socket I/O, send/recv plumbing, LAN beacon/discovery (NetworkSystem::)
#include "Network/NetworkSystem.h"
#include "Network/NetworkSystemInternal.h"
#include "Network/Protocol.h"
#include "Network/UdpSocket.h"
#include "Core/Log.h"

#include <cstring>
#include <memory>
#include <vector>

namespace Dark
{
    using namespace NetSysDetail;

    ITransport* NetworkSystem::transport() const
    {
        return m_injected ? m_injected : m_owned.get();
    }

    bool NetworkSystem::ensureSocket(uint16_t port)
    {
        if (m_injected)
            return true;
        closeOwned();
        m_owned = std::make_unique<UdpSocket>();
        if (!m_owned->open(port))
        {
            m_owned.reset();
            return false;
        }
        return true;
    }

    void NetworkSystem::closeOwned()
    {
        if (m_owned)
        {
            m_owned->close();
            m_owned.reset();
        }
    }

    void NetworkSystem::drainInjected()
    {
        // FakeTransport keeps a queue across sessions; owned UdpSocket close/open already drops the OS buffer.
        // Do not close() injected endpoints — tests reuse them and FakeHub would unregister.
        ITransport* t = m_injected;
        if (!t)
            return;
        Address  src{};
        uint8_t  buf[kNetMaxPayload];
        uint32_t n = 0;
        while (t->recvFrom(src, buf, sizeof(buf), n))
        {
        }
    }

    bool NetworkSystem::flushPeer(Peer& peer)
    {
        ITransport* t = transport();
        if (!t)
            return false;
        OutCountTransport counted;
        counted.inner      = t;
        counted.packetsOut = &m_packetsOut;
        counted.bytesOut   = &m_bytesOut;
        return peer.channel.flush(counted, peer.addr, m_now);
    }

    bool NetworkSystem::sendRawUnreliable(const Address& dest, uint8_t opcode, const void* payload, uint32_t len, uint32_t token)
    {
        return sendRawUnreliableOn(transport(), dest, opcode, payload, len, token);
    }

    bool NetworkSystem::sendRawUnreliableOn(ITransport* t, const Address& dest, uint8_t opcode, const void* payload, uint32_t len, uint32_t token)
    {
        if (!t)
            return false;
        if (len > 0 && !payload)
            return false;

        uint8_t      buf[kNetMaxPayload];
        PacketWriter w;
        if (!w.begin(buf, kNetMaxPayload))
            return false;

        m_rawSeq = nextId(m_rawSeq);

        DatagramHeader h{};
        h.magic         = kNetMagic;
        h.version       = kNetProtocolVersion;
        h.headerFlags   = 0;
        h.headerSize    = kNetHeaderSize;
        h.reserved      = 0;
        h.seq           = m_rawSeq;
        h.ack           = 0;
        h.ackBits       = 0;
        h.connToken     = token;
        h.reliableAck   = 0;
        h.reliableCount = 0;
        h.pad           = 0;
        if (!writeDatagramHeader(w, h) || !w.writeU8(opcode))
            return false;
        if (len && !w.writeBytes(payload, len))
            return false;
        if (!t->sendTo(dest, buf, w.size()))
            return false;
        ++m_packetsOut;
        m_bytesOut += w.size();
        return true;
    }

    bool NetworkSystem::sendConnectRequest()
    {
        Peer* peer = findPeerByAddr(m_serverAddr);
        if (!peer)
            return false;

        ConnectRequestPayload p{};
        std::memcpy(p.name, m_name, 32);
        p.playerId  = static_cast<uint64_t>(m_playerId);
        p.wantsPawn = m_wantsPawn;
        p.sceneMode = m_sceneMode;

        uint8_t      buf[kConnectRequestBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeConnectRequest(w, p))
            return false;
        if (!peer->channel.setUnreliable(static_cast<uint8_t>(NetOpcode::ConnectRequest), buf, w.size()))
            return false;
        return flushPeer(*peer);
    }

    bool NetworkSystem::sendAccept(Peer& peer)
    {
        ConnectAcceptPayload p{};
        p.clientId   = static_cast<uint8_t>(peer.id);
        p.connToken  = m_sessionToken;
        p.netTickHz  = static_cast<uint8_t>(kNetTickHz);
        p.maxClients = static_cast<uint8_t>(kNetMaxClients);
        p.serverTick = m_serverTick;
        p.sceneMode  = m_sceneMode;

        uint8_t      buf[kConnectAcceptBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeConnectAccept(w, p))
            return false;
        if (!peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::ConnectAccept), buf, w.size()))
            return false;
        return flushPeer(peer);
    }

    bool NetworkSystem::sendReject(const Address& dest, ConnectRejectReason reason)
    {
        const uint8_t r = static_cast<uint8_t>(reason);
        return sendRawUnreliable(dest, static_cast<uint8_t>(NetOpcode::ConnectReject), &r, 1, 0);
    }

    bool NetworkSystem::sendDisconnectBurst(Peer& peer, uint8_t reason)
    {
        bool ok = true;
        for (uint8_t i = 0; i < kNetDisconnectBurst; ++i)
        {
            if (!peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::Disconnect), &reason, 1))
                ok = false;
            else if (!flushPeer(peer))
                ok = false;
        }
        return ok;
    }

    bool NetworkSystem::sendHeartbeat(Peer& peer, uint32_t tick)
    {
        uint8_t      buf[4];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !w.writeU32(tick))
            return false;
        if (!peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::Heartbeat), buf, w.size()))
            return false;
        return true;
    }

    bool NetworkSystem::allowPacket(const Address& src)
    {
        RateSlot* slot = nullptr;
        for (RateSlot& s : m_rates)
        {
            if (s.addr == src)
            {
                slot = &s;
                break;
            }
        }
        if (!slot)
        {
            if (m_rates.size() >= kRateSlotCap)
            {
                warnDrop("rate table full");
                return false;
            }
            m_rates.push_back(RateSlot{ src, 0, m_now });
            slot = &m_rates.back();
        }
        if (m_now - slot->windowStart >= 1.f)
        {
            slot->windowStart = m_now;
            slot->count       = 0;
        }
        ++slot->count;
        if (slot->count > kNetMaxPktPerSec)
        {
            warnDrop("rate limit");
            return false;
        }
        return true;
    }

    ITransport* NetworkSystem::discoveryTransport() const
    {
        return m_injected ? m_injected : m_discovery.get();
    }

    bool NetworkSystem::browse()
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: browse() while not Idle");
            return false;
        }
        if (m_browsing)
            return true;
        if (m_browseFailed)
            return false;

        if (m_injected)
        {
            m_browsing = true;
            m_sessions.clear();
            DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: browsing (injected transport)");
            return true;
        }

        m_discovery = std::make_unique<UdpSocket>();
        if (!m_discovery->open(kNetBeaconPort, true, false))
        {
            m_discovery.reset();
            m_browseFailed = true;
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: browse bind :{} failed; typed IP / CLI still work", kNetBeaconPort);
            return false;
        }

        m_browsing = true;
        m_sessions.clear();
        DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: browsing :{} (same-PC two binds of :{} is unreliable)", kNetBeaconPort, kNetBeaconPort);
        return true;
    }

    void NetworkSystem::stopBrowse()
    {
        m_browsing     = false;
        m_browseFailed = false;
        m_sessions.clear();
        if (!m_beaconing && m_discovery)
        {
            m_discovery->close();
            m_discovery.reset();
        }
    }

    uint32_t NetworkSystem::sessionCount() const
    {
        return static_cast<uint32_t>(m_sessions.size());
    }

    bool NetworkSystem::sessionAt(uint32_t i, NetSessionInfo& out) const
    {
        if (i >= m_sessions.size())
            return false;
        out = m_sessions[i];
        return true;
    }

    void NetworkSystem::startBeacon(uint16_t gamePort)
    {
        stopBeacon();
        m_beaconHostPort = gamePort;
        if (m_beaconHostPort == 0)
            m_beaconHostPort = kNetDefaultPort;

        if (m_injected)
        {
            m_beaconing   = true;
            m_beaconAccum = kNetBeaconIntervalSec; // first flush sends; host() must not mix with session recvs
            return;
        }

        m_discovery = std::make_unique<UdpSocket>();
        if (!m_discovery->open(0, false, true))
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: beacon socket failed; typed IP / CLI still work");
            m_discovery.reset();
            m_beaconing = false;
            return;
        }

        m_beaconing   = true;
        m_beaconAccum = kNetBeaconIntervalSec;
        DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: beaconing 1 Hz to 255.255.255.255:{}", kNetBeaconPort);
    }

    void NetworkSystem::stopBeacon()
    {
        m_beaconing      = false;
        m_beaconAccum    = 0.f;
        m_beaconHostPort = 0;
        if (!m_browsing && m_discovery)
        {
            m_discovery->close();
            m_discovery.reset();
        }
    }

    void NetworkSystem::tickBeacon(float dt)
    {
        if (!m_beaconing)
            return;
        m_beaconAccum += dt;
        if (m_beaconAccum < kNetBeaconIntervalSec)
            return;
        m_beaconAccum = 0.f;
        sendBeacon();
    }

    bool NetworkSystem::sendBeacon()
    {
        ITransport* t = discoveryTransport();
        if (!t)
            return false;

        BeaconPayload p{};
        p.hostPort  = m_beaconHostPort;
        p.peerCount = static_cast<uint8_t>(peerCount() > 255u ? 255u : peerCount());
        p.sceneMode = m_sceneMode;
        std::memcpy(p.name, m_name, 32);

        uint8_t      buf[kBeaconBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeBeacon(w, p))
            return false;

        Address dest{};
        dest.ipv4 = 0xFFFFFFFFu;
        dest.port = kNetBeaconPort;
        return sendRawUnreliableOn(t, dest, static_cast<uint8_t>(NetOpcode::Beacon), buf, w.size(), 0);
    }

    void NetworkSystem::pollBrowse(float dt)
    {
        ageSessions(dt);
        ITransport* t = discoveryTransport();
        if (!t)
            return;

        uint32_t recvd = 0;
        for (; recvd < kNetRecvBudget; ++recvd)
        {
            Address  src{};
            uint8_t  buf[kNetMaxPayload];
            uint32_t n = 0;
            if (!t->recvFrom(src, buf, sizeof(buf), n))
                break;
            if (n == 0)
                continue;
            const ParsedDatagram parsed = parseDatagram(buf, n);
            if (!parsed.framed || !parsed.hasUnreliable)
                continue;
            if (parsed.opcode != static_cast<uint8_t>(NetOpcode::Beacon))
                continue;
            if (parsed.header.version != kNetProtocolVersion)
                continue;
            applyBeacon(src, parsed.payload, parsed.payloadSize);
        }
    }

    void NetworkSystem::applyBeacon(const Address& src, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        BeaconPayload p{};
        if (!r.begin(payload, size) || !readBeacon(r, p))
            return;

        Address game = src;
        game.port    = p.hostPort != 0 ? p.hostPort : kNetDefaultPort;

        for (NetSessionInfo& s : m_sessions)
        {
            if (s.address == game)
            {
                copyNetName(s.name, p.name);
                s.sceneMode = p.sceneMode;
                s.peerCount = p.peerCount;
                s.ageSec    = 0.f;
                return;
            }
        }

        if (m_sessions.size() >= kNetMaxClients * 4u)
            return;

        NetSessionInfo s{};
        s.address   = game;
        copyNetName(s.name, p.name);
        s.sceneMode = p.sceneMode;
        s.peerCount = p.peerCount;
        s.ageSec    = 0.f;
        m_sessions.push_back(s);
    }

    void NetworkSystem::ageSessions(float dt)
    {
        for (size_t i = 0; i < m_sessions.size();)
        {
            m_sessions[i].ageSec += dt;
            if (m_sessions[i].ageSec >= kNetSessionAgeOutSec)
            {
                m_sessions[i] = m_sessions.back();
                m_sessions.pop_back();
            }
            else
                ++i;
        }
    }

} // namespace Dark

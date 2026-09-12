// NetworkSystemDispatch.cpp — datagram handlers, timeouts, poll/flush (NetworkSystem::)
#include "Network/NetworkSystem.h"
#include "Network/NetworkSystemInternal.h"
#include "Network/Protocol.h"
#include "Network/UdpSocket.h"
#include "Core/Log.h"
#include "Core/UUID.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <cstring>
#include <memory>
#include <vector>

namespace Dark
{
    using namespace NetSysDetail;

    void NetworkSystem::handleConnectRequest(World& world, const Address& src, const uint8_t* payload, uint32_t size)
    {
        if (m_role != NetRole::Host)
            return;

        PacketReader r;
        ConnectRequestPayload req{};
        if (!payload || !r.begin(payload, size) || !readConnectRequest(r, req))
            return;

        if (req.sceneMode != m_sceneMode)
        {
            sendReject(src, ConnectRejectReason::Mode);
            return;
        }

        const ClientId id = allocateClientId();
        if (id == ClientId::Invalid)
        {
            sendReject(src, ConnectRejectReason::Full);
            return;
        }

        auto peer         = std::make_unique<Peer>();
        peer->id          = id;
        peer->addr        = src;
        peer->playerId    = UUID{ req.playerId };
        peer->wantsPawn   = req.wantsPawn != 0;
        peer->lastRecvSec = m_now;
        peer->channel.setToken(m_sessionToken);

        Peer* raw = peer.get();
        m_peers.push_back(std::move(peer));
        sendAccept(*raw);
        queueJoinSpawns(*raw, world);
        notifyPeer(*raw, NetPeerEvent::Joined);
        DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: client {} joined (token {:04x})", static_cast<unsigned>(raw->id), m_sessionToken & 0xFFFFu);
    }

    void NetworkSystem::onConnectAccept(Peer& peer, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        ConnectAcceptPayload acc{};
        if (!payload || !r.begin(payload, size) || !readConnectAccept(r, acc))
            return;

        if (acc.clientId == 0 || acc.clientId >= static_cast<uint8_t>(kNetMaxClients) || acc.connToken == 0)
            return;

        m_localId      = static_cast<ClientId>(acc.clientId);
        m_sessionToken = acc.connToken;
        m_echoTick     = acc.serverTick;
        peer.channel.setToken(acc.connToken);
        peer.tokenSeen = true;
        m_role         = NetRole::Client;
        DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: became client {} (token {:04x})", acc.clientId, acc.connToken & 0xFFFFu);
    }

    void NetworkSystem::onConnectReject(const uint8_t* payload, uint32_t size)
    {
        uint8_t reason = 0;
        if (payload && size >= 1)
            reason = payload[0];
        DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: ConnectReject reason {}", reason);
        closeOwned();
        resetToIdle();
    }

    void NetworkSystem::handleJoinPacket(World& world, Peer& peer, const uint8_t* data, uint32_t size)
    {
        const ParsedDatagram parsed = parseDatagram(data, size);
        if (!parsed.headerOk)
        {
            warnDrop("truncated");
            return;
        }
        if (parsed.header.magic != kNetMagic)
        {
            if (m_lastMagicWarnSec < 0.f || (m_now - m_lastMagicWarnSec) >= 1.f)
            {
                m_lastMagicWarnSec = m_now;
                DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: unknown magic");
            }
            ++m_packetsDropped;
            return;
        }
        if (!parsed.framed)
        {
            warnDrop("bad frame");
            return;
        }
        if (parsed.header.version != kNetProtocolVersion)
        {
            warnDrop("version");
            return;
        }

        peer.lastRecvSec = m_now;
        if (!peer.channel.receive(data, size))
        {
            warnDrop("receive");
            return;
        }

        if (!peer.channel.hasUnreliable())
            return;

        const uint8_t op = peer.channel.unreliableOpcode();
        if (op == static_cast<uint8_t>(NetOpcode::ConnectAccept))
        {
            onConnectAccept(peer, peer.channel.unreliablePayload().data(), static_cast<uint32_t>(peer.channel.unreliablePayload().size()));
            if (m_role == NetRole::Client)
                applyDelivered(world, peer);
        }
        else if (op == static_cast<uint8_t>(NetOpcode::ConnectReject))
            onConnectReject(peer.channel.unreliablePayload().data(), static_cast<uint32_t>(peer.channel.unreliablePayload().size()));
    }

    void NetworkSystem::handleConnectedPacket(World& world, Peer& peer, const uint8_t* data, uint32_t size)
    {
        const ParsedDatagram parsed = parseDatagram(data, size);
        if (!parsed.headerOk)
        {
            warnDrop("truncated");
            return;
        }
        if (parsed.header.magic != kNetMagic)
        {
            if (m_lastMagicWarnSec < 0.f || (m_now - m_lastMagicWarnSec) >= 1.f)
            {
                m_lastMagicWarnSec = m_now;
                DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: unknown magic");
            }
            ++m_packetsDropped;
            return;
        }
        if (!parsed.framed)
        {
            warnDrop("bad frame");
            return;
        }

        if (parsed.hasUnreliable && parsed.opcode == static_cast<uint8_t>(NetOpcode::ConnectRequest) && m_role == NetRole::Host)
        {
            peer.lastRecvSec = m_now;
            if (!peer.tokenSeen)
                sendAccept(peer);
            return;
        }

        if (parsed.header.version != kNetProtocolVersion)
        {
            warnDrop("version");
            return;
        }

        const uint32_t token = peer.channel.token();
        if (token != 0 && parsed.header.connToken == token)
            peer.tokenSeen = true;
        else if (token != 0 && parsed.header.connToken != token)
        {
            warnDrop("token");
            return;
        }

        peer.lastRecvSec = m_now;
        if (!peer.channel.receive(data, size))
        {
            warnDrop("receive");
            return;
        }

        if (peer.channel.pendingOverflow())
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: pending overflow, dropping peer {}", static_cast<unsigned>(peer.id));
            dropPeer(peer, true, true, kNetDisconnectUser);
            return;
        }

        applyDelivered(world, peer);
    }

    void NetworkSystem::handleDatagram(World& world, const Address& src, const uint8_t* data, uint32_t size)
    {
        const ParsedDatagram probe = parseDatagram(data, size);
        if (probe.framed && probe.hasUnreliable && probe.opcode == static_cast<uint8_t>(NetOpcode::Beacon))
            return; // listen-only on browse(); never mix with session, never reply

        Peer* peer = findPeerByAddr(src);
        if (peer)
        {
            if (m_role == NetRole::Joining)
                handleJoinPacket(world, *peer, data, size);
            else
                handleConnectedPacket(world, *peer, data, size);
            return;
        }

        const ParsedDatagram parsed = parseDatagram(data, size);
        if (!parsed.headerOk)
        {
            warnDrop("truncated");
            return;
        }
        if (parsed.header.magic != kNetMagic)
        {
            if (m_lastMagicWarnSec < 0.f || (m_now - m_lastMagicWarnSec) >= 1.f)
            {
                m_lastMagicWarnSec = m_now;
                DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: unknown magic");
            }
            ++m_packetsDropped;
            return;
        }
        if (!parsed.framed || !parsed.hasUnreliable)
        {
            warnDrop("unknown source");
            return;
        }

        if (parsed.opcode != static_cast<uint8_t>(NetOpcode::ConnectRequest))
            return;

        if (m_role != NetRole::Host)
            return;

        if (parsed.header.version != kNetProtocolVersion)
        {
            sendReject(src, ConnectRejectReason::Version);
            return;
        }

        handleConnectRequest(world, src, parsed.payload, parsed.payloadSize);
    }

    void NetworkSystem::retryAccepts(float dt)
    {
        if (m_role != NetRole::Host)
            return;
        for (auto& p : m_peers)
        {
            if (!p || p->tokenSeen)
                continue;
            p->acceptElapsedSec += dt;
            p->acceptRetrySec += dt;
            if (p->acceptElapsedSec > kNetJoinTimeoutSec)
                continue;
            if (p->acceptRetrySec >= kNetAcceptRetrySec)
            {
                p->acceptRetrySec = 0.f;
                sendAccept(*p);
            }
        }
    }

    void NetworkSystem::checkPeerTimeouts()
    {
        if (m_role != NetRole::Host && m_role != NetRole::Client)
            return;

        for (int i = static_cast<int>(m_peers.size()) - 1; i >= 0; --i)
        {
            Peer* p = m_peers[static_cast<size_t>(i)].get();
            if (!p)
                continue;
            if ((m_now - p->lastRecvSec) < kNetPeerTimeoutSec)
                continue;
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: peer {} timed out", static_cast<unsigned>(p->id));
            dropPeer(*p, m_role == NetRole::Host, true, kNetDisconnectTimeout);
            if (m_role == NetRole::Idle)
                return;
        }
    }

    void NetworkSystem::checkPendingOverflow()
    {
        for (int i = static_cast<int>(m_peers.size()) - 1; i >= 0; --i)
        {
            Peer* p = m_peers[static_cast<size_t>(i)].get();
            if (!p || !p->channel.pendingOverflow())
                continue;
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: pending overflow, dropping peer {}", static_cast<unsigned>(p->id));
            dropPeer(*p, m_role == NetRole::Host, true, kNetDisconnectUser);
            if (m_role == NetRole::Idle)
                return;
        }
    }

    void NetworkSystem::poll(World& world, float dt)
    {
        bindWorld(world);
        if (dt < 0.f)
            dt = 0.f;
        if (m_browsing)
            pollBrowse(dt);

        if (m_role == NetRole::Idle)
            return;
        ITransport* t = transport();
        if (!t)
            return;

        m_now += dt;

        if (m_role == NetRole::Host)
            assignIdleNetIds(world);

        uint32_t recvd = 0;
        for (; recvd < kNetRecvBudget; ++recvd)
        {
            Address  src{};
            uint8_t  buf[kNetMaxPayload];
            uint32_t n = 0;
            if (!t->recvFrom(src, buf, sizeof(buf), n))
                break;
            if (!allowPacket(src))
                continue;
            if (n == 0)
            {
                warnDrop("empty");
                continue;
            }
            ++m_packetsIn;
            m_bytesIn += n;
            handleDatagram(world, src, buf, n);
            if (m_role == NetRole::Idle)
                break;
        }
        if (recvd == kNetRecvBudget)
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: recv budget {} hit", kNetRecvBudget);

        if (m_role == NetRole::Joining)
        {
            m_joinRetryAccum += dt;
            m_joinTimeoutAccum += dt;
            if (m_joinRetryAccum >= kNetConnectRetrySec)
            {
                m_joinRetryAccum = 0.f;
                sendConnectRequest();
            }
            if (m_role == NetRole::Joining && m_joinTimeoutAccum >= kNetJoinTimeoutSec)
                failJoin("timeout");
        }

        retryAccepts(dt);
        checkPeerTimeouts();
        checkPendingOverflow();
    }

    void NetworkSystem::flush(World& world, float dt)
    {
        bindWorld(world);
        if (m_role == NetRole::Idle)
            return;
        if (!transport())
            return;

        if (dt < 0.f)
            dt = 0.f;

        if (m_role == NetRole::Joining)
        {
            for (auto& p : m_peers)
            {
                if (p)
                    flushPeer(*p);
            }
            return;
        }

        if (m_role == NetRole::Host)
            assignIdleNetIds(world);

        // Writeback after onUpdate so local pawn motion is not overwritten this frame.
        if (m_role == NetRole::Client)
            writeInterp(world);

        m_netAccum += dt;
        const float interval = 1.f / kNetTickHz;
        uint32_t    steps    = 0;
        while (m_netAccum >= interval && steps < 2)
        {
            m_netAccum -= interval;
            ++steps;
            if (m_role == NetRole::Host)
                ++m_serverTick;

            for (auto& p : m_peers)
            {
                if (!p)
                    continue;
                if (m_role == NetRole::Host && !p->tokenSeen)
                    continue;
                if (m_role == NetRole::Host)
                    sendSnapshot(*p, world);
                else if (localPawn().valid())
                    sendPawnState(*p, world);
                else
                    sendHeartbeat(*p, m_echoTick);
                flushPeer(*p);
            }
        }

        for (auto& p : m_peers)
        {
            if (p)
                flushPeer(*p);
        }

        checkPendingOverflow();

        if (m_beaconing)
            tickBeacon(dt);
    }

} // namespace Dark

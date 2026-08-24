#include "Network/NetworkSystem.h"
#include "Network/Protocol.h"
#include "Network/UdpSocket.h"
#include "Core/Log.h"
#include "Core/UUID.h"
#include <cstddef>
#include <cstring>
#include <vector>

namespace Dark
{

    namespace
    {
        constexpr uint32_t kRateSlotCap = 64;

        uint16_t nextId(uint16_t id)
        {
            ++id;
            if (id == 0)
                id = 1;
            return id;
        }

        struct ParsedDatagram
        {
            DatagramHeader   header{};
            uint8_t          opcode       = 0;
            const uint8_t*   payload      = nullptr;
            uint32_t         payloadSize  = 0;
            bool             hasUnreliable = false;
            bool             headerOk      = false;
            bool             framed        = false;
        };

        ParsedDatagram parseDatagram(const uint8_t* data, uint32_t size)
        {
            ParsedDatagram out;
            if (!data || size < kNetHeaderSize || size > kNetMaxPayload)
                return out;

            PacketReader r;
            if (!r.begin(data, size) || !readDatagramHeader(r, out.header))
                return out;
            out.headerOk = true;
            if (out.header.magic != kNetMagic)
                return out;
            if (out.header.headerSize < kNetHeaderSize || out.header.headerSize > size)
                return out;
            if (out.header.seq == 0)
                return out;

            const uint32_t extra = static_cast<uint32_t>(out.header.headerSize) - kNetHeaderSize;
            if (extra)
            {
                uint8_t skip[256];
                if (extra > sizeof(skip) || !r.readBytes(skip, extra))
                    return out;
            }

            for (uint8_t i = 0; i < out.header.reliableCount; ++i)
            {
                uint16_t id  = 0;
                uint16_t len = 0;
                uint8_t  op  = 0;
                if (!r.readU16(id) || !r.readU16(len) || !r.readU8(op))
                    return out;
                if (len == 0 || len > kNetMaxReliableLen)
                    return out;
                const uint32_t payloadLen = static_cast<uint32_t>(len) - 1u;
                if (payloadLen)
                {
                    uint8_t skip[kNetMaxReliableLen];
                    if (!r.readBytes(skip, payloadLen))
                        return out;
                }
            }

            if (r.remaining() > 0)
            {
                if (!r.readU8(out.opcode))
                    return out;
                out.hasUnreliable = true;
                out.payloadSize   = r.remaining();
                out.payload       = data + (size - out.payloadSize);
            }

            out.framed = true;
            return out;
        }

        struct OutCountTransport final : ITransport
        {
            ITransport* inner      = nullptr;
            uint64_t*   packetsOut = nullptr;
            uint64_t*   bytesOut   = nullptr;

            bool sendTo(const Address& dest, const void* data, uint32_t size) override
            {
                if (!inner || !inner->sendTo(dest, data, size))
                    return false;
                if (packetsOut)
                    ++*packetsOut;
                if (bytesOut)
                    *bytesOut += size;
                return true;
            }

            bool recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize) override
            {
                return inner ? inner->recvFrom(src, buffer, capacity, outSize) : false;
            }

            Address localAddress() const override { return inner ? inner->localAddress() : Address{}; }
            void    close() override {}
        };
    } // namespace

    NetworkSystem::NetworkSystem() = default;

    NetworkSystem::~NetworkSystem()
    {
        shutdown();
    }

    ITransport* NetworkSystem::transport() const
    {
        return m_injected ? m_injected : m_owned.get();
    }

    void NetworkSystem::setTransport(ITransport* transport)
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: setTransport while not Idle");
            return;
        }
        closeOwned();
        m_injected = transport;
    }

    void NetworkSystem::setPeerCallback(NetPeerFn fn, void* user)
    {
        m_peerFn   = fn;
        m_peerUser = user;
    }

    void NetworkSystem::setSceneMode(uint8_t mode)
    {
        m_sceneMode = mode;
    }

    void NetworkSystem::setWantsPawn(bool wants)
    {
        m_wantsPawn = wants ? 1 : 0;
    }

    void NetworkSystem::setPlayerName(const char* name)
    {
        copyNetName(m_name, name);
    }

    bool NetworkSystem::isValid() const
    {
        return transport() != nullptr;
    }

    Address NetworkSystem::boundAddress() const
    {
        ITransport* t = transport();
        return t ? t->localAddress() : Address{};
    }

    uint32_t NetworkSystem::peerCount() const
    {
        if (m_role != NetRole::Host)
            return 0;
        return static_cast<uint32_t>(m_peers.size());
    }

    bool NetworkSystem::isConnected(ClientId id) const
    {
        if (m_role == NetRole::Idle || m_role == NetRole::Joining)
            return false;
        if (id == ClientId::Host && (m_role == NetRole::Host || m_localId != ClientId::Invalid))
            return true;
        return findPeerById(id) != nullptr;
    }

    float NetworkSystem::rttMs(ClientId) const
    {
        return 0.f;
    }

    uint64_t NetworkSystem::reliableResends() const
    {
        uint64_t n = m_reliableResends;
        for (const auto& p : m_peers)
        {
            if (p)
                n += p->channel.resends();
        }
        return n;
    }

    void NetworkSystem::logAddress(const char* prefix, const Address& addr) const
    {
        const uint32_t ip = addr.ipv4;
        DE_LOG_INFO(LogCategory::Networking, "{} {}.{}.{}.{}:{}", prefix, (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, addr.port);
    }

    void NetworkSystem::warnDrop(const char* why)
    {
        ++m_packetsDropped;
        if (m_lastDropWarnSec < 0.f || (m_now - m_lastDropWarnSec) >= 1.f)
        {
            m_lastDropWarnSec = m_now;
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: dropped packet ({})", why);
        }
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

    void NetworkSystem::resetToIdle()
    {
        m_role              = NetRole::Idle;
        m_localId           = ClientId::Invalid;
        m_peers.clear();
        m_rates.clear();
        m_sessionToken      = 0;
        m_serverTick        = 0;
        m_echoTick          = 0;
        m_joinTimeoutAccum  = 0.f;
        m_joinRetryAccum    = 0.f;
        m_netAccum          = 0.f;
        m_serverAddr        = {};
        m_playerId          = UUID{ 0ull };
        drainInjected();
    }

    NetworkSystem::Peer* NetworkSystem::findPeerByAddr(const Address& addr)
    {
        for (auto& p : m_peers)
        {
            if (p && p->addr == addr)
                return p.get();
        }
        return nullptr;
    }

    const NetworkSystem::Peer* NetworkSystem::findPeerByAddr(const Address& addr) const
    {
        for (const auto& p : m_peers)
        {
            if (p && p->addr == addr)
                return p.get();
        }
        return nullptr;
    }

    NetworkSystem::Peer* NetworkSystem::findPeerById(ClientId id)
    {
        for (auto& p : m_peers)
        {
            if (p && p->id == id)
                return p.get();
        }
        return nullptr;
    }

    const NetworkSystem::Peer* NetworkSystem::findPeerById(ClientId id) const
    {
        for (const auto& p : m_peers)
        {
            if (p && p->id == id)
                return p.get();
        }
        return nullptr;
    }

    ClientId NetworkSystem::allocateClientId() const
    {
        for (uint8_t i = 1; i < static_cast<uint8_t>(kNetMaxClients); ++i)
        {
            const ClientId id = static_cast<ClientId>(i);
            if (!findPeerById(id))
                return id;
        }
        return ClientId::Invalid;
    }

    void NetworkSystem::erasePeer(Peer* peer)
    {
        for (size_t i = 0; i < m_peers.size(); ++i)
        {
            if (m_peers[i].get() == peer)
            {
                m_reliableResends += m_peers[i]->channel.resends();
                m_peers.erase(m_peers.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    void NetworkSystem::notifyPeer(const Peer& peer, NetPeerEvent event)
    {
        if (!m_peerFn)
            return;
        NetPeerInfo info;
        info.id        = peer.id;
        info.addr      = peer.addr;
        info.playerId  = peer.playerId;
        info.wantsPawn = peer.wantsPawn;
        m_peerFn(info, event, m_peerUser);
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
        ITransport* t = transport();
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

    bool NetworkSystem::host(uint16_t port)
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: host() while not Idle");
            return false;
        }
        if (!ensureSocket(port))
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: host() bind failed");
            return false;
        }

        drainInjected();
        m_role         = NetRole::Host;
        m_localId      = ClientId::Host;
        m_playerId     = UUID{};
        m_sessionToken = static_cast<uint32_t>(static_cast<uint64_t>(UUID{}));
        if (m_sessionToken == 0)
            m_sessionToken = 1;
        m_serverTick = 0;
        m_echoTick   = 0;
        m_peers.clear();
        logAddress("NetworkSystem: hosting", boundAddress());
        return true;
    }

    bool NetworkSystem::join(const Address& server)
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: join() while not Idle");
            return false;
        }
        if (!ensureSocket(0))
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: join() bind failed");
            return false;
        }

        drainInjected();
        m_serverAddr       = server;
        m_playerId         = UUID{};
        m_localId          = ClientId::Invalid;
        m_sessionToken     = 0;
        m_joinTimeoutAccum = 0.f;
        m_joinRetryAccum   = 0.f;
        m_echoTick         = 0;

        auto peer           = std::make_unique<Peer>();
        peer->id            = ClientId::Host;
        peer->addr          = server;
        peer->lastRecvSec   = m_now;
        peer->channel.setToken(0);

        m_peers.clear();
        m_peers.push_back(std::move(peer));
        m_role = NetRole::Joining;

        if (!sendConnectRequest())
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: join() failed to send ConnectRequest");
            closeOwned();
            resetToIdle();
            return false;
        }

        logAddress("NetworkSystem: joining", server);
        return true;
    }

    void NetworkSystem::disconnect()
    {
        if (m_role == NetRole::Idle)
        {
            closeOwned();
            return;
        }

        const bool wasHost = (m_role == NetRole::Host);
        for (auto& p : m_peers)
        {
            if (!p)
                continue;
            sendDisconnectBurst(*p, kNetDisconnectUser);
            if (wasHost)
                notifyPeer(*p, NetPeerEvent::Left);
        }

        closeOwned();
        resetToIdle();
    }

    void NetworkSystem::shutdown()
    {
        if (m_role != NetRole::Idle)
            disconnect();
        else
            closeOwned();
    }

    void NetworkSystem::failJoin(const char* why)
    {
        DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: join failed ({})", why);
        for (auto& p : m_peers)
        {
            if (p)
                sendDisconnectBurst(*p, kNetDisconnectUser);
        }
        closeOwned();
        resetToIdle();
    }

    void NetworkSystem::dropPeer(Peer& peer, bool notifyLeft, bool sendDisconnect, uint8_t disconnectReason)
    {
        if (sendDisconnect)
            sendDisconnectBurst(peer, disconnectReason);
        if (notifyLeft)
            notifyPeer(peer, NetPeerEvent::Left);

        if (m_role == NetRole::Client || m_role == NetRole::Joining)
        {
            closeOwned();
            resetToIdle();
            return;
        }

        erasePeer(&peer);
    }

    void NetworkSystem::handleConnectRequest(const Address& src, const uint8_t* payload, uint32_t size)
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

    void NetworkSystem::handleJoinPacket(Peer& peer, const uint8_t* data, uint32_t size)
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
            onConnectAccept(peer, peer.channel.unreliablePayload().data(), static_cast<uint32_t>(peer.channel.unreliablePayload().size()));
        else if (op == static_cast<uint8_t>(NetOpcode::ConnectReject))
            onConnectReject(peer.channel.unreliablePayload().data(), static_cast<uint32_t>(peer.channel.unreliablePayload().size()));
    }

    void NetworkSystem::handleConnectedPacket(Peer& peer, const uint8_t* data, uint32_t size)
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

        uint8_t              relOp = 0;
        std::vector<uint8_t> relPayload;
        while (peer.channel.popReliable(relOp, relPayload))
        {
            (void)relOp;
            (void)relPayload;
        }

        if (!peer.channel.hasUnreliable())
            return;

        const uint8_t op = peer.channel.unreliableOpcode();
        if (op == static_cast<uint8_t>(NetOpcode::Heartbeat))
        {
            PacketReader r;
            uint32_t     tick = 0;
            const auto&  pl   = peer.channel.unreliablePayload();
            if (r.begin(pl.data(), static_cast<uint32_t>(pl.size())) && r.readU32(tick))
            {
                if (m_role == NetRole::Client)
                    m_echoTick = tick;
            }
        }
        else if (op == static_cast<uint8_t>(NetOpcode::Disconnect))
        {
            DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: peer {} disconnected", static_cast<unsigned>(peer.id));
            dropPeer(peer, m_role == NetRole::Host, false, kNetDisconnectUser);
        }
    }

    void NetworkSystem::handleDatagram(const Address& src, const uint8_t* data, uint32_t size)
    {
        Peer* peer = findPeerByAddr(src);
        if (peer)
        {
            if (m_role == NetRole::Joining)
                handleJoinPacket(*peer, data, size);
            else
                handleConnectedPacket(*peer, data, size);
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

        handleConnectRequest(src, parsed.payload, parsed.payloadSize);
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
        (void)world;
        if (m_role == NetRole::Idle)
            return;
        ITransport* t = transport();
        if (!t)
            return;

        if (dt < 0.f)
            dt = 0.f;
        m_now += dt;

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
            handleDatagram(src, buf, n);
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
        (void)world;
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

        m_netAccum += dt;
        const float interval = 1.f / kNetTickHz;
        uint32_t    steps    = 0;
        while (m_netAccum >= interval && steps < 2)
        {
            m_netAccum -= interval;
            ++steps;
            if (m_role == NetRole::Host)
                ++m_serverTick;
        }

        if (steps > 0)
        {
            const uint32_t tick = (m_role == NetRole::Host) ? m_serverTick : m_echoTick;
            for (auto& p : m_peers)
            {
                if (!p)
                    continue;
                if (m_role == NetRole::Host && !p->tokenSeen)
                    continue;
                sendHeartbeat(*p, tick);
            }
        }

        for (auto& p : m_peers)
        {
            if (p)
                flushPeer(*p);
        }

        checkPendingOverflow();
    }

} // namespace Dark

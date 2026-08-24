#include "Network/NetworkSystem.h"
#include "Network/Protocol.h"
#include "Network/UdpSocket.h"
#include "Core/Log.h"
#include "Core/UUID.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include <cmath>
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

        NetId nextNetIdValue(NetId id)
        {
            ++id;
            if (id == 0)
                id = 1;
            return id;
        }

        bool finite3(float x, float y, float z)
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }

        bool finite4(float w, float x, float y, float z)
        {
            return std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }

        void normalizeQuat(float& w, float& x, float& y, float& z)
        {
            Math::Quaternion q(w, x, y, z);
            q.Normalize();
            w = q.w;
            x = q.x;
            y = q.y;
            z = q.z;
        }

        const char* tagForPrefab(NetPrefab prefab)
        {
            switch (prefab)
            {
            case NetPrefab::Cube:
                return "Cube";
            case NetPrefab::Sphere:
                return "Sphere";
            case NetPrefab::PlayerPawn:
                return "PlayerPawn";
            case NetPrefab::Platform:
                return "Platform";
            case NetPrefab::Coin:
                return "Coin";
            case NetPrefab::Player2D:
                return "Player2D";
            default:
                return "Net";
            }
        }

        bool isPawnPrefab(NetPrefab prefab)
        {
            return prefab == NetPrefab::PlayerPawn || prefab == NetPrefab::Player2D;
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
        // World may already be destroyed in tests that declare NetworkSystem first.
        m_world = nullptr;
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

    void NetworkSystem::setSpawnCallback(NetSpawnFn fn, void* user)
    {
        m_spawnFn   = fn;
        m_spawnUser = user;
    }

    void NetworkSystem::setDespawnCallback(NetDespawnFn fn, void* user)
    {
        m_despawnFn   = fn;
        m_despawnUser = user;
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

    void NetworkSystem::warnNan(const char* why)
    {
        if (m_lastNanWarnSec < 0.f || (m_now - m_lastNanWarnSec) >= 1.f)
        {
            m_lastNanWarnSec = m_now;
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: non-finite pose ({})", why);
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
        const NetRole previous = m_role;
        m_role                 = NetRole::Idle;
        m_localId              = ClientId::Invalid;
        m_peers.clear();
        m_rates.clear();
        m_sessionToken     = 0;
        m_serverTick       = 0;
        m_echoTick         = 0;
        m_joinTimeoutAccum = 0.f;
        m_joinRetryAccum   = 0.f;
        m_netAccum         = 0.f;
        m_serverAddr       = {};
        m_playerId         = UUID{ 0ull };
        m_latestRecvTick   = 0;
        m_hasRecvTick      = false;
        if (previous == NetRole::Client)
            destroyRemoteReplicas();
        else if (previous == NetRole::Host)
            parkHostReplicas();
        else
            clearReplicaMaps();
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
        m_serverTick     = 0;
        m_echoTick       = 0;
        m_latestRecvTick = 0;
        m_hasRecvTick    = false;
        m_peers.clear();
        m_interp.clear();
        m_pawnLast.clear();
        if (m_world)
            assignIdleNetIds(*m_world);
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
        if (m_role == NetRole::Idle)
            return;
        ITransport* t = transport();
        if (!t)
            return;

        if (dt < 0.f)
            dt = 0.f;
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
    }

    void NetworkSystem::bindWorld(World& world)
    {
        m_world = &world;
    }

    void NetworkSystem::clearReplicaMaps()
    {
        m_netToEntity.clear();
        m_entityToNet.clear();
        m_interp.clear();
        m_pawnLast.clear();
        m_nextNetId      = 1;
        m_latestRecvTick = 0;
        m_hasRecvTick    = false;
    }

    void NetworkSystem::destroyRemoteReplicas()
    {
        if (!m_world)
        {
            clearReplicaMaps();
            return;
        }

        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);

        World& world = *m_world;
        for (EntityID eid : ids)
        {
            const auto it = m_entityToNet.find(eid);
            const NetId nid = (it != m_entityToNet.end()) ? it->second : NULL_NET_ID;
            teardownEntity(world, Entity{ eid }, nid, false);
        }
        clearReplicaMaps();
    }

    void NetworkSystem::parkHostReplicas()
    {
        if (m_world)
        {
            m_world->each<NetworkedComponent>([](Entity, NetworkedComponent& nc) { nc.netId = NULL_NET_ID; });
            std::unordered_map<EntityID, NetId> parked;
            m_world->each<NetworkedComponent>([&](Entity e, NetworkedComponent&) { parked[e.id()] = NULL_NET_ID; });
            m_entityToNet.swap(parked);
        }
        else
        {
            for (auto& kv : m_entityToNet)
                kv.second = NULL_NET_ID;
        }
        m_netToEntity.clear();
        m_interp.clear();
        m_pawnLast.clear();
        m_nextNetId      = 1;
        m_latestRecvTick = 0;
        m_hasRecvTick    = false;
    }

    void NetworkSystem::assignIdleNetIds(World& world)
    {
        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);

        world.each<NetworkedComponent>([&](Entity e, NetworkedComponent&) {
            if (!m_entityToNet.contains(e.id()))
                ids.push_back(e.id());
        });

        for (EntityID eid : ids)
        {
            Entity e{ eid };
            NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (!nc || nc->netId != NULL_NET_ID)
                continue;
            const NetId id = m_nextNetId;
            m_nextNetId    = nextNetIdValue(m_nextNetId);
            nc->netId          = id;
            m_entityToNet[eid] = id;
            m_netToEntity[id]  = eid;
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                rememberPawnPose(id, xf->position.x, xf->position.y, xf->position.z);
            else
                rememberPawnPose(id, 0.f, 0.f, 0.f);
        }
    }

    bool NetworkSystem::queueSpawn(Peer& peer, World& world, Entity e, const NetworkedComponent& nc)
    {
        if (nc.netId == NULL_NET_ID)
            return false;

        SpawnPayload p{};
        p.netId      = nc.netId;
        p.prefab     = static_cast<uint8_t>(nc.prefab);
        p.owner      = static_cast<uint8_t>(nc.owner);
        p.flags      = nc.replicateTransform ? 1u : 0u;
        p.colorRgba8 = nc.colorRgba8;
        if (const TransformComponent* xf = world.get<TransformComponent>(e))
        {
            p.px = xf->position.x;
            p.py = xf->position.y;
            p.pz = xf->position.z;
            p.qw = xf->rotation.w;
            p.qx = xf->rotation.x;
            p.qy = xf->rotation.y;
            p.qz = xf->rotation.z;
            p.sx = xf->scale.x;
            p.sy = xf->scale.y;
            p.sz = xf->scale.z;
        }
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz) || !finite3(p.sx, p.sy, p.sz))
        {
            warnNan("spawn");
            return false;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        uint8_t      buf[kSpawnBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeSpawn(w, p))
            return false;
        return peer.channel.queueReliable(static_cast<uint8_t>(NetOpcode::Spawn), buf, w.size());
    }

    void NetworkSystem::queueSpawnAllPeers(World& world, Entity e, const NetworkedComponent& nc)
    {
        for (auto& p : m_peers)
        {
            if (p)
                queueSpawn(*p, world, e, nc);
        }
    }

    void NetworkSystem::queueDespawnAllPeers(NetId id)
    {
        if (id == NULL_NET_ID)
            return;
        uint8_t      buf[kDespawnBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeDespawn(w, id))
            return;
        for (auto& p : m_peers)
        {
            if (p)
                p->channel.queueReliable(static_cast<uint8_t>(NetOpcode::Despawn), buf, w.size());
        }
    }

    void NetworkSystem::queueJoinSpawns(Peer& peer, World& world)
    {
        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);
        for (EntityID eid : ids)
        {
            Entity e{ eid };
            const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (nc && nc->netId != NULL_NET_ID)
                queueSpawn(peer, world, e, *nc);
        }
    }

    void NetworkSystem::rememberPawnPose(NetId id, float px, float py, float pz)
    {
        PawnAccepted& acc = m_pawnLast[id];
        acc.px            = px;
        acc.py            = py;
        acc.pz            = pz;
        acc.timeSec       = m_now;
        acc.has           = true;
    }

    void NetworkSystem::teardownEntity(World& world, Entity e, NetId id, bool queueDespawn)
    {
        if (queueDespawn && m_role == NetRole::Host && id != NULL_NET_ID)
            queueDespawnAllPeers(id);

        if (id != NULL_NET_ID)
        {
            m_netToEntity.erase(id);
            m_interp.erase(id);
            m_pawnLast.erase(id);
        }
        m_entityToNet.erase(e.id());

        if (world.has<NetworkedComponent>(e))
            world.remove<NetworkedComponent>(e);

        if (m_despawnFn)
            m_despawnFn(world, e, id, m_despawnUser);

        if (world.alive(e))
            world.destroyEntity(e);
    }

    bool NetworkSystem::registerEntity(World& world, Entity e, NetPrefab prefab, ClientId owner, uint32_t colorRgba8)
    {
        bindWorld(world);
        if (m_role != NetRole::Idle && m_role != NetRole::Host)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity rejected in role {}", static_cast<unsigned>(m_role));
            return false;
        }
        if (!e.valid() || !world.alive(e))
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity on invalid entity");
            return false;
        }
        if (world.has<NetworkedComponent>(e))
            return true;
        if (m_entityToNet.size() >= kNetMaxReplicated)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity cap {} reached", kNetMaxReplicated);
            return false;
        }

        NetworkedComponent nc;
        nc.netId              = NULL_NET_ID;
        nc.owner              = owner;
        nc.prefab             = prefab;
        nc.colorRgba8         = colorRgba8;
        nc.replicateTransform = true;

        if (m_role == NetRole::Host)
        {
            nc.netId   = m_nextNetId;
            m_nextNetId = nextNetIdValue(m_nextNetId);
        }

        world.emplace<NetworkedComponent>(e, nc);
        m_entityToNet[e.id()] = nc.netId;
        if (nc.netId != NULL_NET_ID)
        {
            m_netToEntity[nc.netId] = e.id();
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                rememberPawnPose(nc.netId, xf->position.x, xf->position.y, xf->position.z);
            else
                rememberPawnPose(nc.netId, 0.f, 0.f, 0.f);
            queueSpawnAllPeers(world, e, nc);
        }
        return true;
    }

    void NetworkSystem::unregisterEntity(World& world, Entity e)
    {
        bindWorld(world);
        if (!world.has<NetworkedComponent>(e))
            return;
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const NetId id = nc ? nc->netId : netIdFor(e);
        teardownEntity(world, e, id, true);
    }

    Entity NetworkSystem::entityFor(NetId id) const
    {
        if (id == NULL_NET_ID)
            return {};
        const auto it = m_netToEntity.find(id);
        if (it == m_netToEntity.end())
            return {};
        return Entity{ it->second };
    }

    NetId NetworkSystem::netIdFor(Entity e) const
    {
        const auto it = m_entityToNet.find(e.id());
        if (it == m_entityToNet.end())
            return NULL_NET_ID;
        return it->second;
    }

    Entity NetworkSystem::localPawn() const
    {
        if (!m_world || m_localId == ClientId::Invalid)
            return {};
        for (const auto& kv : m_entityToNet)
        {
            Entity e{ kv.first };
            const NetworkedComponent* nc = m_world->get<NetworkedComponent>(e);
            if (nc && nc->owner == m_localId && isPawnPrefab(nc->prefab))
                return e;
        }
        return {};
    }

    void NetworkSystem::applyDelivered(World& world, Peer& peer)
    {
        uint8_t              relOp = 0;
        std::vector<uint8_t> relPayload;
        while (peer.channel.popReliable(relOp, relPayload))
            applyReliable(world, peer, relOp, relPayload);

        if (!peer.channel.hasUnreliable())
            return;

        const uint8_t op = peer.channel.unreliableOpcode();
        const auto&   pl = peer.channel.unreliablePayload();
        applyUnreliable(world, peer, op, pl.data(), static_cast<uint32_t>(pl.size()));
    }

    void NetworkSystem::applyReliable(World& world, Peer& peer, uint8_t op, const std::vector<uint8_t>& payload)
    {
        (void)peer;
        if (op == static_cast<uint8_t>(NetOpcode::Spawn) || op == static_cast<uint8_t>(NetOpcode::Despawn))
        {
            if (m_role != NetRole::Client)
            {
                warnDrop("host-only opcode");
                return;
            }
            if (op == static_cast<uint8_t>(NetOpcode::Spawn))
                applySpawn(world, payload.data(), static_cast<uint32_t>(payload.size()));
            else
                applyDespawn(world, payload.data(), static_cast<uint32_t>(payload.size()));
            return;
        }
    }

    void NetworkSystem::applyUnreliable(World& world, Peer& peer, uint8_t op, const uint8_t* payload, uint32_t size)
    {
        if (op == static_cast<uint8_t>(NetOpcode::Heartbeat))
        {
            PacketReader r;
            uint32_t     tick = 0;
            if (payload && r.begin(payload, size) && r.readU32(tick))
            {
                if (m_role == NetRole::Client)
                    m_echoTick = tick;
            }
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Disconnect))
        {
            DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: peer {} disconnected", static_cast<unsigned>(peer.id));
            dropPeer(peer, m_role == NetRole::Host, false, kNetDisconnectUser);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Snapshot))
        {
            if (m_role != NetRole::Client)
            {
                warnDrop("host-only opcode");
                return;
            }
            applySnapshot(world, payload, size);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::PawnState))
        {
            if (m_role != NetRole::Host)
            {
                warnDrop("pawn from non-client");
                return;
            }
            applyPawnState(world, peer, payload, size);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Spawn) || op == static_cast<uint8_t>(NetOpcode::Despawn))
        {
            warnDrop("host-only opcode");
            return;
        }
    }

    void NetworkSystem::applySpawn(World& world, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        SpawnPayload p{};
        if (!payload || !r.begin(payload, size) || !readSpawn(r, p) || p.netId == NULL_NET_ID)
            return;
        if (m_netToEntity.contains(p.netId))
            return;
        if (m_entityToNet.size() >= kNetMaxReplicated)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: inbound Spawn dropped, cap {}", kNetMaxReplicated);
            return;
        }
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz) || !finite3(p.sx, p.sy, p.sz))
        {
            warnNan("spawn");
            return;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        Entity e = world.createEntity();
        TransformComponent xf;
        xf.position = Math::Vector3f(p.px, p.py, p.pz);
        xf.rotation = Math::Quaternion(p.qw, p.qx, p.qy, p.qz);
        xf.scale    = Math::Vector3f(p.sx, p.sy, p.sz);
        world.emplace<TagComponent>(e, TagComponent{ tagForPrefab(static_cast<NetPrefab>(p.prefab)) });
        world.emplace<TransformComponent>(e, xf);

        NetworkedComponent nc;
        nc.netId              = p.netId;
        nc.owner              = static_cast<ClientId>(p.owner);
        nc.prefab             = static_cast<NetPrefab>(p.prefab);
        nc.colorRgba8         = p.colorRgba8;
        nc.replicateTransform = (p.flags & 1u) != 0u;
        world.emplace<NetworkedComponent>(e, nc);

        m_netToEntity[p.netId] = e.id();
        m_entityToNet[e.id()]  = p.netId;
        rememberPawnPose(p.netId, p.px, p.py, p.pz);

        if (m_spawnFn && !m_spawnFn(world, e, nc.prefab, xf, p.colorRgba8, m_spawnUser))
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: spawn callback rejected NetId {}", p.netId);
            m_netToEntity.erase(p.netId);
            m_entityToNet.erase(e.id());
            m_pawnLast.erase(p.netId);
            if (world.alive(e))
                world.destroyEntity(e);
        }
    }

    void NetworkSystem::applyDespawn(World& world, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        uint32_t     netId = 0;
        if (!payload || !r.begin(payload, size) || !readDespawn(r, netId) || netId == NULL_NET_ID)
            return;
        const Entity e = entityFor(netId);
        if (!e.valid())
            return;
        teardownEntity(world, e, netId, false);
    }

    void NetworkSystem::pushInterpSlot(NetId id, uint32_t tick, float px, float py, float pz, float qw, float qx, float qy, float qz)
    {
        InterpTrack& tr = m_interp[id];
        for (uint8_t i = 0; i < tr.count; ++i)
        {
            if (tr.slots[i].tick == tick)
            {
                tr.slots[i] = InterpPose{ tick, px, py, pz, qw, qx, qy, qz };
                if (!tr.hasLatest || static_cast<int32_t>(tick - tr.latestTick) >= 0)
                {
                    tr.latestTick = tick;
                    tr.hasLatest  = true;
                }
                return;
            }
        }

        InterpPose pose{ tick, px, py, pz, qw, qx, qy, qz };
        if (tr.count < kInterpSlots)
        {
            tr.slots[tr.count++] = pose;
        }
        else
        {
            uint8_t oldest = 0;
            for (uint8_t i = 1; i < kInterpSlots; ++i)
            {
                if (static_cast<int32_t>(tick - tr.slots[i].tick) > static_cast<int32_t>(tick - tr.slots[oldest].tick))
                    oldest = i;
            }
            tr.slots[oldest] = pose;
        }

        if (!tr.hasLatest || static_cast<int32_t>(tick - tr.latestTick) >= 0)
        {
            tr.latestTick = tick;
            tr.hasLatest  = true;
        }
    }

    bool NetworkSystem::sampleInterp(const InterpTrack& tr, uint32_t latestRecv, InterpPose& out) const
    {
        if (tr.count == 0)
            return false;
        if (tr.count == 1)
        {
            out = tr.slots[0];
            return true;
        }

        const InterpPose* latest = &tr.slots[0];
        for (uint8_t i = 1; i < tr.count; ++i)
        {
            if (static_cast<int32_t>(tr.slots[i].tick - latest->tick) > 0)
                latest = &tr.slots[i];
        }

        const int32_t     delay = static_cast<int32_t>(kNetInterpDelayTicks);
        const InterpPose* older = nullptr;
        const InterpPose* newer = nullptr;
        int32_t           olderDelta = 0;
        int32_t           newerDelta = 0;
        for (uint8_t i = 0; i < tr.count; ++i)
        {
            const int32_t d = static_cast<int32_t>(latestRecv - tr.slots[i].tick);
            if (d >= delay)
            {
                if (!older || d < olderDelta)
                {
                    older      = &tr.slots[i];
                    olderDelta = d;
                }
            }
            if (d <= delay)
            {
                if (!newer || d > newerDelta)
                {
                    newer      = &tr.slots[i];
                    newerDelta = d;
                }
            }
        }

        if (!older)
        {
            out = *latest;
            return true;
        }
        if (!newer || older == newer)
        {
            out = *older;
            return true;
        }

        const int32_t span = static_cast<int32_t>(newer->tick - older->tick);
        if (span <= 0)
        {
            out = *older;
            return true;
        }

        float t = static_cast<float>(olderDelta - delay) / static_cast<float>(span);
        if (t < 0.f)
            t = 0.f;
        if (t > 1.f)
            t = 1.f;

        const Math::Vector3f a(older->px, older->py, older->pz);
        const Math::Vector3f b(newer->px, newer->py, newer->pz);
        const Math::Vector3f p = a + (b - a) * t;
        Math::Quaternion     qa(older->qw, older->qx, older->qy, older->qz);
        Math::Quaternion     qb(newer->qw, newer->qx, newer->qy, newer->qz);
        Math::Quaternion     qr = Math::Quaternion::Slerp(qa, qb, t);
        qr.Normalize();

        out.tick = older->tick;
        out.px   = p.x;
        out.py   = p.y;
        out.pz   = p.z;
        out.qw   = qr.w;
        out.qx   = qr.x;
        out.qy   = qr.y;
        out.qz   = qr.z;
        return true;
    }

    void NetworkSystem::writeInterp(World& world)
    {
        if (m_role != NetRole::Client || !m_hasRecvTick)
            return;

        for (const auto& kv : m_netToEntity)
        {
            const NetId id = kv.first;
            Entity      e{ kv.second };
            NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (!nc || !nc->replicateTransform)
                continue;
            if (nc->owner == m_localId)
                continue;
            TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                continue;
            const auto it = m_interp.find(id);
            if (it == m_interp.end() || it->second.count == 0)
                continue;

            InterpPose pose{};
            if (!sampleInterp(it->second, m_latestRecvTick, pose))
                continue;
            xf->position = Math::Vector3f(pose.px, pose.py, pose.pz);
            xf->rotation = Math::Quaternion(pose.qw, pose.qx, pose.qy, pose.qz);
            xf->rotation.Normalize();
        }
    }

    void NetworkSystem::applySnapshot(World& world, const uint8_t* payload, uint32_t size)
    {
        (void)world;
        PacketReader r;
        uint32_t     serverTick = 0;
        uint8_t      count      = 0;
        if (!payload || !r.begin(payload, size) || !readSnapshotHeader(r, serverTick, count))
            return;
        if (count > kNetMaxReplicated)
        {
            warnDrop("snapshot count");
            return;
        }
        if (m_hasRecvTick && static_cast<int32_t>(serverTick - m_latestRecvTick) < 0)
            return;

        m_latestRecvTick = serverTick;
        m_hasRecvTick    = true;
        m_echoTick       = serverTick;

        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t netId = 0;
            float    px = 0.f, py = 0.f, pz = 0.f;
            float    qw = 1.f, qx = 0.f, qy = 0.f, qz = 0.f;
            if (!readSnapshotPose(r, netId, px, py, pz, qw, qx, qy, qz))
                return;
            if (netId == NULL_NET_ID || !m_netToEntity.contains(netId))
                continue;
            if (!finite3(px, py, pz) || !finite4(qw, qx, qy, qz))
            {
                warnNan("snapshot");
                continue;
            }
            normalizeQuat(qw, qx, qy, qz);
            pushInterpSlot(netId, serverTick, px, py, pz, qw, qx, qy, qz);
        }
    }

    void NetworkSystem::applyPawnState(World& world, Peer& peer, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        PawnStatePayload p{};
        if (!payload || !r.begin(payload, size) || !readPawnState(r, p) || p.netId == NULL_NET_ID)
            return;
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz))
        {
            warnNan("pawn");
            return;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        const Entity e = entityFor(p.netId);
        if (!e.valid())
            return;
        NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        if (!nc || nc->owner != peer.id)
        {
            warnDrop("pawn owner");
            return;
        }

        PawnAccepted& last = m_pawnLast[p.netId];
        if (last.has)
        {
            const float dt = m_now - last.timeSec;
            const Math::Vector3f delta(p.px - last.px, p.py - last.py, p.pz - last.pz);
            const float dist = delta.Magnitude();
            if (dt <= 1.0e-6f)
            {
                if (dist > 1.0e-3f)
                {
                    warnDrop("pawn speed");
                    return;
                }
            }
            else if ((dist / dt) > kNetPawnMaxSpeed)
            {
                warnDrop("pawn speed");
                return;
            }
        }

        TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf)
            return;
        xf->position = Math::Vector3f(p.px, p.py, p.pz);
        xf->rotation = Math::Quaternion(p.qw, p.qx, p.qy, p.qz);
        xf->rotation.Normalize();
        rememberPawnPose(p.netId, p.px, p.py, p.pz);
    }

    bool NetworkSystem::sendSnapshot(Peer& peer, World& world)
    {
        struct Rec
        {
            uint32_t netId;
            float    px, py, pz;
            float    qw, qx, qy, qz;
        };
        Rec     recs[kNetMaxReplicated];
        uint8_t count = 0;
        world.each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            if (count >= kNetMaxReplicated || nc.netId == NULL_NET_ID || !nc.replicateTransform)
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            float qw = xf->rotation.w, qx = xf->rotation.x, qy = xf->rotation.y, qz = xf->rotation.z;
            if (!finite3(xf->position.x, xf->position.y, xf->position.z) || !finite4(qw, qx, qy, qz))
                return;
            normalizeQuat(qw, qx, qy, qz);
            recs[count++] = Rec{ nc.netId, xf->position.x, xf->position.y, xf->position.z, qw, qx, qy, qz };
        });

        uint8_t      buf[kNetMaxPayload];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeSnapshotHeader(w, m_serverTick, count))
            return false;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (!writeSnapshotPose(w, recs[i].netId, recs[i].px, recs[i].py, recs[i].pz, recs[i].qw, recs[i].qx, recs[i].qy, recs[i].qz))
                return false;
        }
        return peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::Snapshot), buf, w.size());
    }

    bool NetworkSystem::sendPawnState(Peer& peer, World& world)
    {
        const Entity e = localPawn();
        if (!e.valid())
            return false;
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const TransformComponent* xf = world.get<TransformComponent>(e);
        if (!nc || !xf || nc->netId == NULL_NET_ID)
            return false;

        PawnStatePayload p{};
        p.netId = nc->netId;
        p.px    = xf->position.x;
        p.py    = xf->position.y;
        p.pz    = xf->position.z;
        p.qw    = xf->rotation.w;
        p.qx    = xf->rotation.x;
        p.qy    = xf->rotation.y;
        p.qz    = xf->rotation.z;
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz))
            return false;
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        uint8_t      buf[kPawnStateBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writePawnState(w, p))
            return false;
        return peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::PawnState), buf, w.size());
    }

} // namespace Dark



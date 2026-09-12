// NetworkSystem.cpp — façade: ctor, setters, peers, logging helpers
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

    NetworkSystem::NetworkSystem() = default;

    NetworkSystem::~NetworkSystem()
    {
        // World may already be destroyed in tests that declare NetworkSystem first.
        m_world = nullptr;
        shutdown();
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

} // namespace Dark

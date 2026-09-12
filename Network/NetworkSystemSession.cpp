// NetworkSystemSession.cpp — host/join/disconnect and peer drop (NetworkSystem::)
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

    bool NetworkSystem::host(uint16_t port)
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: host() while not Idle");
            return false;
        }
        stopBrowse();
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

        uint16_t advertised = port;
        if (m_injected)
        {
            advertised = boundAddress().port;
            if (advertised == 0)
                advertised = (port != 0) ? port : kNetDefaultPort;
        }
        else if (advertised == 0)
            advertised = boundAddress().port ? boundAddress().port : kNetDefaultPort;
        startBeacon(advertised);
        return true;
    }

    bool NetworkSystem::join(const Address& server)
    {
        if (m_role != NetRole::Idle)
        {
            DE_LOG_ERROR(LogCategory::Networking, "NetworkSystem: join() while not Idle");
            return false;
        }
        stopBrowse();
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
        stopBeacon();
        stopBrowse();
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
        stopBeacon();
        stopBrowse();
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

} // namespace Dark

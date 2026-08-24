#pragma once

#include "Network/NetTypes.h"
#include "Network/Reliability.h"
#include "Network/Transport.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Dark
{

    class World;
    class UdpSocket;

    class NetworkSystem
    {
    public:
        NetworkSystem();
        ~NetworkSystem();

        NetworkSystem(const NetworkSystem&)            = delete;
        NetworkSystem& operator=(const NetworkSystem&) = delete;

        // Non-owning. Must outlive this system until shutdown()/setTransport(nullptr).
        // nullptr → host()/join() create an owned UdpSocket.
        void setTransport(ITransport* transport);

        void setPeerCallback(NetPeerFn fn, void* user);
        void setSceneMode(uint8_t mode);
        void setWantsPawn(bool wants);
        void setPlayerName(const char* name);

        uint8_t sceneMode() const { return m_sceneMode; }
        bool    wantsPawn() const { return m_wantsPawn != 0; }

        bool host(uint16_t port = kNetDefaultPort); // Idle only. Injected ITransport: skip bind, ignore port.
        bool join(const Address& server);           // Idle only; async → Joining. Injected: skip bind.
        void disconnect();
        void shutdown(); // idempotent

        NetRole  role() const { return m_role; }
        ClientId localClientId() const { return m_localId; }
        uint32_t peerCount() const;
        bool     isConnected(ClientId id) const;
        bool     isValid() const;

        void poll(World& world, float dt);
        void flush(World& world, float dt);

        float   rttMs(ClientId id) const;
        Address boundAddress() const;

        uint64_t packetsIn() const { return m_packetsIn; }
        uint64_t packetsOut() const { return m_packetsOut; }
        uint64_t bytesIn() const { return m_bytesIn; }
        uint64_t bytesOut() const { return m_bytesOut; }
        uint64_t packetsDropped() const { return m_packetsDropped; }
        uint64_t reliableResends() const;

    private:
        struct Peer
        {
            ClientId           id               = ClientId::Invalid;
            Address            addr{};
            UUID               playerId{ 0ull };
            bool               wantsPawn        = false;
            bool               tokenSeen        = false;
            ReliabilityChannel channel;
            float              lastRecvSec      = 0.f;
            float              acceptRetrySec   = 0.f;
            float              acceptElapsedSec = 0.f;
        };

        struct RateSlot
        {
            Address  addr{};
            uint32_t count       = 0;
            float    windowStart = 0.f;
        };

        ITransport* transport() const;
        bool        ensureSocket(uint16_t port);
        void        closeOwned();
        void        resetToIdle();
        void        logAddress(const char* prefix, const Address& addr) const;
        bool        allowPacket(const Address& src);
        void        warnDrop(const char* why);

        Peer*       findPeerByAddr(const Address& addr);
        const Peer* findPeerByAddr(const Address& addr) const;
        Peer*       findPeerById(ClientId id);
        const Peer* findPeerById(ClientId id) const;
        ClientId    allocateClientId() const;
        void        erasePeer(Peer* peer);
        void        notifyPeer(const Peer& peer, NetPeerEvent event);

        bool flushPeer(Peer& peer);
        bool sendRawUnreliable(const Address& dest, uint8_t opcode, const void* payload, uint32_t len, uint32_t token);
        bool sendConnectRequest();
        bool sendAccept(Peer& peer);
        bool sendReject(const Address& dest, ConnectRejectReason reason);
        bool sendDisconnectBurst(Peer& peer, uint8_t reason);
        bool sendHeartbeat(Peer& peer, uint32_t tick);

        void handleDatagram(const Address& src, const uint8_t* data, uint32_t size);
        void handleConnectRequest(const Address& src, const uint8_t* payload, uint32_t size);
        void handleJoinPacket(Peer& peer, const uint8_t* data, uint32_t size);
        void handleConnectedPacket(Peer& peer, const uint8_t* data, uint32_t size);
        void onConnectAccept(Peer& peer, const uint8_t* payload, uint32_t size);
        void onConnectReject(const uint8_t* payload, uint32_t size);
        void dropPeer(Peer& peer, bool notifyLeft, bool sendDisconnect, uint8_t disconnectReason);
        void failJoin(const char* why);
        void checkPeerTimeouts();
        void checkPendingOverflow();
        void retryAccepts(float dt);

        NetRole     m_role     = NetRole::Idle;
        ClientId    m_localId  = ClientId::Invalid;
        ITransport* m_injected = nullptr;

        std::unique_ptr<UdpSocket>        m_owned;
        std::vector<std::unique_ptr<Peer>> m_peers;
        std::vector<RateSlot>              m_rates;

        NetPeerFn m_peerFn   = nullptr;
        void*     m_peerUser = nullptr;

        uint32_t m_sessionToken = 0;
        uint32_t m_serverTick   = 0;
        uint32_t m_echoTick     = 0;
        uint16_t m_rawSeq       = 0;
        uint8_t  m_sceneMode    = 0;
        uint8_t  m_wantsPawn    = 1;
        char     m_name[32]{};
        UUID     m_playerId{ 0ull };
        Address  m_serverAddr{};

        float m_now               = 0.f;
        float m_joinTimeoutAccum  = 0.f;
        float m_joinRetryAccum    = 0.f;
        float m_netAccum          = 0.f;
        float m_lastMagicWarnSec  = -1.f;
        float m_lastDropWarnSec   = -1.f;

        uint64_t m_packetsIn       = 0;
        uint64_t m_packetsOut      = 0;
        uint64_t m_bytesIn         = 0;
        uint64_t m_bytesOut        = 0;
        uint64_t m_packetsDropped  = 0;
        uint64_t m_reliableResends = 0;
    };

} // namespace Dark

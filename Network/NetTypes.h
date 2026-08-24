#pragma once

#include "Core/UUID.h"

#include <cstdint>

namespace Dark
{

    using NetId = uint32_t;
    constexpr NetId NULL_NET_ID = 0;

    struct Address
    {
        uint32_t ipv4 = 0; // host byte order
        uint16_t port = 0; // host byte order
        bool operator==(const Address&) const = default;
    };

    // Dotted-quad, optional ":port". "127.0.0.1" leaves port unchanged.
    bool parseIPv4(const char* s, Address& out);

    enum class ClientId : uint8_t
    {
        Host    = 0,
        Invalid = 0xFF // remotes 1..7
    };

    enum class NetRole : uint8_t
    {
        Idle = 0,
        Joining,
        Host,
        Client
    };

    enum class ConnectRejectReason : uint8_t
    {
        Version = 1,
        Full    = 2,
        Mode    = 3
    };

    enum class NetPrefab : uint8_t
    {
        Unknown    = 0,
        Cube       = 1,
        Sphere     = 2,
        PlayerPawn = 3,
        Platform   = 4,
        Coin       = 5,
        Player2D   = 6
    };

    enum class NetPeerEvent : uint8_t
    {
        Joined = 0,
        Left
    };

    struct NetPeerInfo
    {
        ClientId id        = ClientId::Invalid;
        Address  addr{};
        UUID     playerId{ 0ull };
        bool     wantsPawn = false;
    };

    struct NetSessionInfo
    {
        Address address{}; // ipv4 from recvfrom; port = Beacon.hostPort (game port)
        char    name[32]{};
        uint8_t sceneMode = 0;
        uint8_t peerCount = 0;
        float   ageSec    = 0.f;
    };

    using NetPeerFn = void (*)(const NetPeerInfo& info, NetPeerEvent event, void* user);

    constexpr uint32_t kNetMagic            = 0x314E4544u;
    constexpr uint8_t  kNetProtocolVersion  = 1;
    constexpr uint8_t  kNetHeaderSize       = 24;
    constexpr uint16_t kNetDefaultPort      = 26160;
    constexpr uint16_t kNetBeaconPort       = 26161;
    constexpr uint32_t kNetMaxClients       = 8;
    constexpr float    kNetTickHz           = 20.0f;
    constexpr float    kNetJoinTimeoutSec   = 5.0f;
    constexpr float    kNetPeerTimeoutSec   = 2.0f;
    constexpr float    kNetConnectRetrySec  = 0.1f; // 10 Hz
    constexpr float    kNetAcceptRetrySec   = 0.1f; // 10 Hz
    constexpr uint32_t kNetMaxPktPerSec     = 200;
    constexpr uint32_t kNetRecvBudget       = 32;
    constexpr uint8_t  kNetDisconnectBurst   = 3;
    constexpr uint8_t  kNetDisconnectUser    = 0;
    constexpr uint8_t  kNetDisconnectTimeout = 1;
    constexpr uint32_t kNetMaxReplicated     = 32;
    constexpr uint32_t kNetInterpDelayTicks  = 2;
    constexpr float    kNetPawnMaxSpeed       = 20.0f;
    constexpr float    kNetBeaconIntervalSec  = 1.0f;
    constexpr float    kNetSessionAgeOutSec   = 3.0f;

} // namespace Dark

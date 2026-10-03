#include <gtest/gtest.h>

#include "Save/SaveSystem.h"

using namespace Dark;

namespace
{
    struct Gate
    {
        int      role  = 0;
        uint32_t peers = 0;
        bool     allow = true;
    };

    int netRole(void* user) { return static_cast<Gate*>(user)->role; }
    uint32_t peerCount(void* user) { return static_cast<Gate*>(user)->peers; }
    bool canSave(void* user, Save::SaveResult& why)
    {
        if (static_cast<Gate*>(user)->allow)
        {
            why = Save::SaveResult::Ok;
            return true;
        }
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
}

TEST(SaveNetGate, ClientAndJoiningCannotSaveOrLoad)
{
    Gate gate;
    gate.role = 3;
    Save::SaveHost host{};
    host.id         = "sandbox";
    host.netRole    = netRole;
    host.peerCount  = peerCount;
    host.canSaveNow = canSave;
    host.user       = &gate;
    Save::SaveSystem save;
    save.setHost(host);
    EXPECT_EQ(save.requestSave(Save::SaveKind::Quick, "Quick Save"), Save::SaveResult::NotHost);
    EXPECT_EQ(save.requestLoadName("quick_2026-10-03_18-04-40_9f03.json"), Save::SaveResult::NotHost);
    gate.role = 1;
    EXPECT_EQ(save.requestSave(Save::SaveKind::Quick, "Quick Save"), Save::SaveResult::NotHost);
}

TEST(SaveNetGate, HostWithPeerCannotLoad)
{
    Gate gate;
    gate.role = 2;
    gate.peers = 1;
    Save::SaveHost host{};
    host.id        = "sandbox";
    host.netRole   = netRole;
    host.peerCount = peerCount;
    host.user      = &gate;
    Save::SaveSystem save;
    save.setHost(host);
    EXPECT_EQ(save.requestLoadName("quick_2026-10-03_18-04-40_9f03.json"), Save::SaveResult::NotHost);
}

TEST(SaveNetGate, UnsafeMomentDoesNotQueue)
{
    Gate gate;
    gate.allow = false;
    Save::SaveHost host{};
    host.id         = "sandbox";
    host.netRole    = netRole;
    host.canSaveNow = canSave;
    host.user       = &gate;
    Save::SaveSystem save;
    save.setHost(host);
    EXPECT_EQ(save.requestSave(Save::SaveKind::Quick, "Quick Save"), Save::SaveResult::UnsafeMoment);
    EXPECT_FALSE(save.busy());
}

#pragma once

#include "AI/HsmState.h"

#include <unordered_map>
#include <vector>

namespace Dark::AI
{

    // Hierarchical state machine runtime.
    //
    // Event flow (propagation):
    //   1. Leaf-first: walk active path from leaf toward root.
    //   2. At each state, run onEvent handlers (state then base chain).
    //   3. Then try transitions on that state (state then base chain).
    //   4. Stop when a transition fires or a handler sets context.consumed.
    //
    // Shared transitions:
    //   - Transitions registered on a composite apply while any descendant is active
    //     (via bubbling).
    //   - Machine-level addSharedTransition() attaches one edge to many source states.
    //
    // History: composites with Shallow/Deep history restore the last active child / path.
    class HsmMachine
    {
    public:
        HsmMachine() = default;

        void setRoot(HsmState* root);
        void setOwner(void* owner)
        {
            m_owner = owner;
        }
        void* owner() const
        {
            return m_owner;
        }

        // Enter the initial configuration (root + defaults / history). Returns false on guard failure.
        bool start();
        void stop(); // exit entire active configuration
        bool isRunning() const
        {
            return m_running;
        }

        // Dispatch an event. Returns true if a transition fired or the event was consumed.
        bool processEvent(const HsmEvent& event);

        // Convenience: process kHsmEventTick with dt.
        bool tick(float dt);

        // ── Shared transitions (one definition, many sources) ────────────────────
        // Adds the same transition to every source state (shared definition).
        bool addSharedTransition(const std::vector<HsmState*>& sources, HsmEventId eventId, HsmState* target, HsmGuard guard = {}, HsmAction action = {},
                                 HsmTransitionKind kind = HsmTransitionKind::External);

        // ── Configuration queries ────────────────────────────────────────────────
        HsmState* currentLeaf() const
        {
            return m_leaf;
        }
        bool isIn(const HsmState* state) const;
        // Active path root -> ... -> leaf
        const std::vector<HsmState*>& activePath() const
        {
            return m_activePath;
        }

        // Clear shallow/deep history for a composite (or all if null).
        void clearHistory(HsmState* composite = nullptr);

    private:
        struct HistoryRecord
        {
            HsmState*              shallowChild = nullptr;
            std::vector<HsmState*> deepPath; // from composite's child down to leaf
        };

        bool enterInitialConfiguration();
        bool transitionTo(const HsmTransition& t, HsmState* matchedSource, const HsmEvent& event);

        HsmState* leastCommonAncestor(HsmState* a, HsmState* b) const;
        void      rebuildActivePath();
        void      recordHistoryUpTo(HsmState* lcca);

        bool checkExitGuards(const std::vector<HsmState*>& exitChain, HsmContext& ctx) const;
        bool checkEnterGuards(const std::vector<HsmState*>& enterChain, HsmContext& ctx) const;

        void runExitChain(const std::vector<HsmState*>& exitChain, HsmContext& ctx);
        void runEnterChain(const std::vector<HsmState*>& enterChain, HsmContext& ctx);

        void collectEnterPath(HsmState* fromExclusive, HsmState* toInclusive, std::vector<HsmState*>& out) const;
        void collectExitPath(HsmState* fromInclusive, HsmState* toExclusive, std::vector<HsmState*>& out) const;

        // Resolve default or history entry from a composite down to a leaf.
        bool resolveEntryLeaf(HsmState* start, HsmHistory historyMode, std::vector<HsmState*>& enterTail);

        // Transitions for state including inheritance chain (derived first).
        void collectTransitions(HsmState* state, std::vector<const HsmTransition*>& out) const;
        void collectHandlers(HsmState* state, std::vector<const HsmAction*>& out) const;
        void collectEnterActions(HsmState* state, std::vector<const HsmAction*>& out) const;
        void collectExitActions(HsmState* state, std::vector<const HsmAction*>& out) const;

        bool tryGuards(const std::vector<HsmGuard>& guards, const HsmContext& ctx) const;

        HsmState* m_root    = nullptr;
        HsmState* m_leaf    = nullptr;
        void*     m_owner   = nullptr;
        bool      m_running = false;

        std::vector<HsmState*>                       m_activePath; // root -> leaf
        std::unordered_map<HsmState*, HistoryRecord> m_history;
    };

} // namespace Dark::AI

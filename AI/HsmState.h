#pragma once

#include "AI/HsmTypes.h"

#include <string>
#include <vector>

namespace Dark::AI
{

    // Node in a hierarchical state tree.
    //
    // Features:
    //  - Nesting via parent/children + initial child
    //  - Entry / exit actions (nested on transition paths)
    //  - Enter / exit guards (conditionals that can block a transition)
    //  - Local transitions + composite-level transitions (shared among descendants)
    //  - Shallow / deep history
    //  - Behavior inheritance via setBase() (transitions, handlers, optional entry/exit)
    class HsmState
    {
    public:
        explicit HsmState(std::string_view name);
        ~HsmState() = default;

        HsmState(const HsmState&)            = delete;
        HsmState& operator=(const HsmState&) = delete;

        // ── Structure ────────────────────────────────────────────────────────────
        HsmState& setParent(HsmState* parent);
        HsmState& setInitial(HsmState* child); // default substate when entered without history
        HsmState& setHistory(HsmHistory history);
        HsmState& setBase(HsmState* base); // behavior inheritance parent
        HsmState& setInheritEntryExit(bool enabled);

        // ── Actions / guards ─────────────────────────────────────────────────────
        HsmState& onEnter(HsmAction action);
        HsmState& onExit(HsmAction action);
        HsmState& onEvent(HsmAction handler); // called when event reaches this state (leaf-first)
        HsmState& enterGuard(HsmGuard guard); // must return true to enter
        HsmState& exitGuard(HsmGuard guard);  // must return true to exit

        // ── Transitions owned by this state ──────────────────────────────────────
        // If this is a composite, these are shared among all active descendants
        // (evaluated when the event bubbles to this state).
        HsmState& addTransition(HsmEventId eventId, HsmState* target, HsmGuard guard = {}, HsmAction action = {}, HsmTransitionKind kind = HsmTransitionKind::External);

        HsmState& addInternalTransition(HsmEventId eventId, HsmGuard guard = {}, HsmAction action = {});

        // ── Queries ──────────────────────────────────────────────────────────────
        const std::string& name() const
        {
            return m_name;
        }
        HsmState* parent() const
        {
            return m_parent;
        }
        HsmState* initial() const
        {
            return m_initial;
        }
        HsmState* base() const
        {
            return m_base;
        }
        HsmHistory history() const
        {
            return m_history;
        }
        bool inheritEntryExit() const
        {
            return m_inheritEntryExit;
        }
        bool isComposite() const
        {
            return !m_children.empty();
        }
        bool isAncestorOf(const HsmState* other) const;
        int  depth() const;

        const std::vector<HsmState*>& children() const
        {
            return m_children;
        }
        const std::vector<HsmTransition>& transitions() const
        {
            return m_transitions;
        }
        const std::vector<HsmAction>& enterActions() const
        {
            return m_enter;
        }
        const std::vector<HsmAction>& exitActions() const
        {
            return m_exit;
        }
        const std::vector<HsmAction>& eventHandlers() const
        {
            return m_onEvent;
        }
        const std::vector<HsmGuard>& enterGuards() const
        {
            return m_enterGuards;
        }
        const std::vector<HsmGuard>& exitGuards() const
        {
            return m_exitGuards;
        }

    private:
        friend class HsmMachine;

        std::string                m_name;
        HsmState*                  m_parent           = nullptr;
        HsmState*                  m_initial          = nullptr;
        HsmState*                  m_base             = nullptr;
        HsmHistory                 m_history          = HsmHistory::None;
        bool                       m_inheritEntryExit = true;
        std::vector<HsmState*>     m_children;
        std::vector<HsmTransition> m_transitions;
        std::vector<HsmAction>     m_enter;
        std::vector<HsmAction>     m_exit;
        std::vector<HsmAction>     m_onEvent;
        std::vector<HsmGuard>      m_enterGuards;
        std::vector<HsmGuard>      m_exitGuards;
    };

} // namespace Dark::AI

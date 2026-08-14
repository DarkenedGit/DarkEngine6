#include "AI/HsmState.h"
#include "Core/Log.h"

namespace Dark::AI
{

    HsmState::HsmState(std::string_view name) : m_name(name)
    {
    }

    HsmState& HsmState::setParent(HsmState* parent)
    {
        if (m_parent == parent)
            return *this;

        if (m_parent)
        {
            auto& siblings = m_parent->m_children;
            for (size_t i = 0; i < siblings.size(); ++i)
            {
                if (siblings[i] == this)
                {
                    siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            if (m_parent->m_initial == this)
                m_parent->m_initial = nullptr;
        }

        m_parent = parent;
        if (m_parent)
        {
            m_parent->m_children.push_back(this);
            if (!m_parent->m_initial)
                m_parent->m_initial = this;
        }
        return *this;
    }

    HsmState& HsmState::setInitial(HsmState* child)
    {
        if (child && child->m_parent != this)
        {
            DE_LOG_ERROR("HsmState '{}': initial child '{}' is not a direct child", m_name, child ? child->m_name : std::string("(null)"));
            return *this;
        }
        m_initial = child;
        return *this;
    }

    HsmState& HsmState::setHistory(HsmHistory history)
    {
        m_history = history;
        return *this;
    }

    HsmState& HsmState::setBase(HsmState* base)
    {
        // Prevent trivial cycles (base == this).
        if (base == this)
        {
            DE_LOG_ERROR("HsmState '{}': cannot set base to self", m_name);
            return *this;
        }
        m_base = base;
        return *this;
    }

    HsmState& HsmState::setInheritEntryExit(bool enabled)
    {
        m_inheritEntryExit = enabled;
        return *this;
    }

    HsmState& HsmState::onEnter(HsmAction action)
    {
        if (action)
            m_enter.push_back(std::move(action));
        return *this;
    }

    HsmState& HsmState::onExit(HsmAction action)
    {
        if (action)
            m_exit.push_back(std::move(action));
        return *this;
    }

    HsmState& HsmState::onEvent(HsmAction handler)
    {
        if (handler)
            m_onEvent.push_back(std::move(handler));
        return *this;
    }

    HsmState& HsmState::enterGuard(HsmGuard guard)
    {
        if (guard)
            m_enterGuards.push_back(std::move(guard));
        return *this;
    }

    HsmState& HsmState::exitGuard(HsmGuard guard)
    {
        if (guard)
            m_exitGuards.push_back(std::move(guard));
        return *this;
    }

    HsmState& HsmState::addTransition(HsmEventId eventId, HsmState* target, HsmGuard guard, HsmAction action, HsmTransitionKind kind)
    {
        HsmTransition t{};
        t.eventId = eventId;
        t.source  = this;
        t.target  = target;
        t.guard   = std::move(guard);
        t.action  = std::move(action);
        t.kind    = kind;
        m_transitions.push_back(std::move(t));
        return *this;
    }

    HsmState& HsmState::addInternalTransition(HsmEventId eventId, HsmGuard guard, HsmAction action)
    {
        return addTransition(eventId, this, std::move(guard), std::move(action), HsmTransitionKind::Internal);
    }

    bool HsmState::isAncestorOf(const HsmState* other) const
    {
        if (!other)
            return false;
        for (const HsmState* p = other->m_parent; p; p = p->m_parent)
        {
            if (p == this)
                return true;
        }
        return false;
    }

    int HsmState::depth() const
    {
        int d = 0;
        for (const HsmState* p = m_parent; p; p = p->m_parent)
            ++d;
        return d;
    }

} // namespace Dark::AI

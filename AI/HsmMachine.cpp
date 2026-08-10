#include "AI/HsmMachine.h"
#include "Core/Log.h"

#include <algorithm>

namespace Dark::AI
{

void* HsmContext::owner() const
{
    return machine.owner();
}

void HsmMachine::setRoot(HsmState* root)
{
    if (m_running)
    {
        DE_LOG_ERROR("HsmMachine::setRoot: stop the machine before changing root");
        return;
    }
    m_root = root;
    m_leaf = nullptr;
    m_activePath.clear();
    m_history.clear();
}

bool HsmMachine::start()
{
    if (!m_root)
    {
        DE_LOG_ERROR("HsmMachine::start: root is null");
        return false;
    }
    if (m_running)
        return true;

    m_activePath.clear();
    m_leaf = nullptr;

    if (!enterInitialConfiguration())
        return false;

    m_running = true;
    return true;
}

void HsmMachine::stop()
{
    if (!m_running)
        return;

    HsmEvent none{};
    HsmContext ctx{*this, none, nullptr, nullptr, false};

    // Exit leaf -> root.
    std::vector<HsmState*> exitChain = m_activePath;
    std::reverse(exitChain.begin(), exitChain.end());
    runExitChain(exitChain, ctx);

    m_activePath.clear();
    m_leaf    = nullptr;
    m_running = false;
}

bool HsmMachine::isIn(const HsmState* state) const
{
    if (!state)
        return false;
    for (HsmState* s : m_activePath)
    {
        if (s == state)
            return true;
    }
    return false;
}

bool HsmMachine::tick(float dt)
{
    HsmEvent e{};
    e.id        = kHsmEventTick;
    e.deltaTime = dt;
    return processEvent(e);
}

bool HsmMachine::addSharedTransition(const std::vector<HsmState*>& sources,
                                     HsmEventId eventId,
                                     HsmState* target,
                                     HsmGuard guard,
                                     HsmAction action,
                                     HsmTransitionKind kind)
{
    if (sources.empty())
    {
        DE_LOG_ERROR("HsmMachine::addSharedTransition: no sources");
        return false;
    }

    for (HsmState* src : sources)
    {
        if (!src)
        {
            DE_LOG_ERROR("HsmMachine::addSharedTransition: null source");
            return false;
        }
        // Each source gets a copy of the same logical edge (shared definition).
        src->addTransition(eventId, target, guard, action, kind);
    }
    return true;
}

void HsmMachine::clearHistory(HsmState* composite)
{
    if (!composite)
    {
        m_history.clear();
        return;
    }
    m_history.erase(composite);
}

// ── Start configuration ──────────────────────────────────────────────────────

bool HsmMachine::enterInitialConfiguration()
{
    std::vector<HsmState*> enterChain;
    enterChain.push_back(m_root);

    std::vector<HsmState*> tail;
    if (!resolveEntryLeaf(m_root, m_root->history(), tail))
        return false;
    enterChain.insert(enterChain.end(), tail.begin(), tail.end());

    HsmEvent none{};
    HsmContext ctx{*this, none, nullptr, nullptr, false};

    if (!checkEnterGuards(enterChain, ctx))
    {
        DE_LOG_ERROR("HsmMachine::start: enter guard failed");
        return false;
    }

    runEnterChain(enterChain, ctx);
    rebuildActivePath();
    return m_leaf != nullptr;
}

// ── Event processing ─────────────────────────────────────────────────────────

bool HsmMachine::processEvent(const HsmEvent& event)
{
    if (!m_running || !m_leaf)
        return false;

    // Snapshot path leaf -> root for stable bubbling.
    std::vector<HsmState*> bubble = m_activePath;
    std::reverse(bubble.begin(), bubble.end());

    for (HsmState* state : bubble)
    {
        HsmContext ctx{*this, event, state, nullptr, false};

        // 1) Event handlers (this state, then bases).
        std::vector<const HsmAction*> handlers;
        collectHandlers(state, handlers);
        for (const HsmAction* h : handlers)
        {
            if (*h)
                (*h)(ctx);
            if (ctx.consumed)
                return true;
        }

        // 2) Transitions (this state, then bases). First matching guard wins.
        std::vector<const HsmTransition*> transitions;
        collectTransitions(state, transitions);
        for (const HsmTransition* t : transitions)
        {
            if (t->eventId != event.id)
                continue;

            ctx.target = t->target;
            if (t->guard && !t->guard(ctx))
                continue;

            if (transitionTo(*t, state, event))
            {
                ctx.consumed = true;
                return true;
            }
            // Guard failure on exit/enter path: try next transition.
        }
    }

    return false;
}

bool HsmMachine::transitionTo(const HsmTransition& t, HsmState* matchedSource, const HsmEvent& event)
{
    HsmContext ctx{*this, event, matchedSource, t.target, false};

    if (t.kind == HsmTransitionKind::Internal)
    {
        if (t.action)
            t.action(ctx);
        return true;
    }

    HsmState* target = t.target;
    if (!target)
    {
        DE_LOG_ERROR("HsmMachine: external/local transition missing target");
        return false;
    }

    // Local transitions: if target is inside matchedSource (composite), LCCA is matchedSource.
    HsmState* lcca = nullptr;
    if (t.kind == HsmTransitionKind::Local && matchedSource && matchedSource->isAncestorOf(target))
        lcca = matchedSource;
    else
        lcca = leastCommonAncestor(m_leaf, target);

    // Exit chain: leaf .. child-of-lcca (exclude lcca).
    std::vector<HsmState*> exitChain;
    collectExitPath(m_leaf, lcca, exitChain);

    // Enter path from lcca toward target, then default/history to leaf.
    std::vector<HsmState*> enterMain;
    collectEnterPath(lcca, target, enterMain);

    // If target is composite, continue into history / initial children.
    std::vector<HsmState*> enterTail;
    HsmHistory histMode = HsmHistory::None;
    if (t.useTargetHistory)
    {
        if (t.forceHistory != HsmHistory::None)
            histMode = t.forceHistory;
        else
            histMode = target->history();
    }

    if (target->isComposite() || histMode != HsmHistory::None)
    {
        if (!resolveEntryLeaf(target, histMode, enterTail))
            return false;
    }

    std::vector<HsmState*> enterChain = enterMain;
    enterChain.insert(enterChain.end(), enterTail.begin(), enterTail.end());

    if (!checkExitGuards(exitChain, ctx))
        return false;
    if (!checkEnterGuards(enterChain, ctx))
        return false;

    // Record history for composites we are leaving (before exits).
    recordHistoryUpTo(lcca);

    runExitChain(exitChain, ctx);

    if (t.action)
        t.action(ctx);

    runEnterChain(enterChain, ctx);
    rebuildActivePath();
    return true;
}

// ── Paths ────────────────────────────────────────────────────────────────────

HsmState* HsmMachine::leastCommonAncestor(HsmState* a, HsmState* b) const
{
    if (!a || !b)
        return m_root;

    // Mark ancestors of a.
    std::vector<HsmState*> aChain;
    for (HsmState* s = a; s; s = s->parent())
        aChain.push_back(s);

    for (HsmState* s = b; s; s = s->parent())
    {
        for (HsmState* p : aChain)
        {
            if (p == s)
                return s;
        }
    }
    return m_root;
}

void HsmMachine::collectExitPath(HsmState* fromInclusive, HsmState* toExclusive, std::vector<HsmState*>& out) const
{
    out.clear();
    for (HsmState* s = fromInclusive; s && s != toExclusive; s = s->parent())
        out.push_back(s);
}

void HsmMachine::collectEnterPath(HsmState* fromExclusive, HsmState* toInclusive, std::vector<HsmState*>& out) const
{
    out.clear();
    if (!toInclusive)
        return;

    // Build path toInclusive -> ... -> root, then reverse until past fromExclusive.
    std::vector<HsmState*> stack;
    for (HsmState* s = toInclusive; s; s = s->parent())
    {
        if (s == fromExclusive)
            break;
        stack.push_back(s);
    }
    std::reverse(stack.begin(), stack.end());
    out = std::move(stack);
}

bool HsmMachine::resolveEntryLeaf(HsmState* start, HsmHistory historyMode, std::vector<HsmState*>& enterTail)
{
    enterTail.clear();
    if (!start)
        return false;

    // If start is a leaf, nothing below it.
    if (!start->isComposite())
        return true;

    HsmState* cur = start;

    // First step under start.
    if (historyMode == HsmHistory::Shallow || historyMode == HsmHistory::Deep)
    {
        const auto it = m_history.find(start);
        if (it != m_history.end() && it->second.shallowChild)
        {
            if (historyMode == HsmHistory::Deep && !it->second.deepPath.empty())
            {
                enterTail = it->second.deepPath;
                return true;
            }
            cur = it->second.shallowChild;
            enterTail.push_back(cur);
        }
        else if (start->initial())
        {
            cur = start->initial();
            enterTail.push_back(cur);
        }
        else
        {
            DE_LOG_ERROR("HsmMachine: composite '{}' has no initial child and no history",
                         start->name());
            return false;
        }
    }
    else
    {
        if (!start->initial())
        {
            // Composite with no children path required — treat as leaf-like.
            if (start->children().empty())
                return true;
            DE_LOG_ERROR("HsmMachine: composite '{}' has no initial child", start->name());
            return false;
        }
        cur = start->initial();
        enterTail.push_back(cur);
    }

    // Descend via initial children until leaf. (Deep history already returned.)
    while (cur && cur->isComposite())
    {
        if (!cur->initial())
        {
            DE_LOG_ERROR("HsmMachine: composite '{}' has no initial child", cur->name());
            return false;
        }
        cur = cur->initial();
        enterTail.push_back(cur);
    }
    return true;
}

void HsmMachine::rebuildActivePath()
{
    // m_leaf is set by runEnterChain / transition logic; rebuild path from it.
    m_activePath.clear();
    if (!m_leaf)
    {
        if (m_root && !m_root->isComposite())
            m_leaf = m_root;
        else
            return;
    }

    m_activePath.reserve(8);
    for (HsmState* s = m_leaf; s; s = s->parent())
        m_activePath.push_back(s);
    std::reverse(m_activePath.begin(), m_activePath.end());
}

void HsmMachine::recordHistoryUpTo(HsmState* lcca)
{
    // For each composite on the active path whose active child is leaving,
    // remember shallow child and (optionally) the deep path to the leaf.
    if (m_activePath.empty())
        return;

    for (size_t i = 0; i + 1 < m_activePath.size(); ++i)
    {
        HsmState* composite = m_activePath[i];
        HsmState* child     = m_activePath[i + 1];
        if (composite->history() == HsmHistory::None)
            continue;

        // Child leaves the configuration if it is not lcca and not an ancestor of lcca.
        const bool childExits = (child != lcca) && !child->isAncestorOf(lcca);
        if (!childExits)
            continue;

        HistoryRecord& rec = m_history[composite];
        rec.shallowChild   = child;
        if (composite->history() == HsmHistory::Deep)
        {
            rec.deepPath.clear();
            for (size_t j = i + 1; j < m_activePath.size(); ++j)
                rec.deepPath.push_back(m_activePath[j]);
        }
    }
}

// ── Guards / actions ─────────────────────────────────────────────────────────

bool HsmMachine::tryGuards(const std::vector<HsmGuard>& guards, const HsmContext& ctx) const
{
    for (const HsmGuard& g : guards)
    {
        if (g && !g(ctx))
            return false;
    }
    return true;
}

bool HsmMachine::checkExitGuards(const std::vector<HsmState*>& exitChain, HsmContext& ctx) const
{
    for (HsmState* s : exitChain)
    {
        ctx.source = s;
        if (!tryGuards(s->exitGuards(), ctx))
            return false;
        // Inherited bases: exit guards on base as well when inheritEntryExit.
        if (s->inheritEntryExit())
        {
            for (HsmState* b = s->base(); b; b = b->base())
            {
                if (!tryGuards(b->exitGuards(), ctx))
                    return false;
            }
        }
    }
    return true;
}

bool HsmMachine::checkEnterGuards(const std::vector<HsmState*>& enterChain, HsmContext& ctx) const
{
    for (HsmState* s : enterChain)
    {
        ctx.target = s;
        if (s->inheritEntryExit())
        {
            // Bases first (outer behavior).
            std::vector<HsmState*> bases;
            for (HsmState* b = s->base(); b; b = b->base())
                bases.push_back(b);
            std::reverse(bases.begin(), bases.end());
            for (HsmState* b : bases)
            {
                if (!tryGuards(b->enterGuards(), ctx))
                    return false;
            }
        }
        if (!tryGuards(s->enterGuards(), ctx))
            return false;
    }
    return true;
}

void HsmMachine::runExitChain(const std::vector<HsmState*>& exitChain, HsmContext& ctx)
{
    for (HsmState* s : exitChain)
    {
        ctx.source = s;
        // Derived exit, then bases (toward super-state).
        std::vector<const HsmAction*> actions;
        collectExitActions(s, actions);
        for (const HsmAction* a : actions)
        {
            if (*a)
                (*a)(ctx);
        }
        if (m_leaf == s)
            m_leaf = s->parent();
    }
}

void HsmMachine::runEnterChain(const std::vector<HsmState*>& enterChain, HsmContext& ctx)
{
    for (HsmState* s : enterChain)
    {
        ctx.target = s;
        std::vector<const HsmAction*> actions;
        collectEnterActions(s, actions);
        for (const HsmAction* a : actions)
        {
            if (*a)
                (*a)(ctx);
        }
        m_leaf = s;
    }
}

void HsmMachine::collectTransitions(HsmState* state, std::vector<const HsmTransition*>& out) const
{
    out.clear();
    for (HsmState* s = state; s; s = s->base())
    {
        for (const HsmTransition& t : s->transitions())
            out.push_back(&t);
    }
}

void HsmMachine::collectHandlers(HsmState* state, std::vector<const HsmAction*>& out) const
{
    out.clear();
    for (HsmState* s = state; s; s = s->base())
    {
        for (const HsmAction& h : s->eventHandlers())
            out.push_back(&h);
    }
}

void HsmMachine::collectEnterActions(HsmState* state, std::vector<const HsmAction*>& out) const
{
    out.clear();
    if (!state)
        return;

    if (state->inheritEntryExit())
    {
        std::vector<HsmState*> bases;
        for (HsmState* b = state->base(); b; b = b->base())
            bases.push_back(b);
        std::reverse(bases.begin(), bases.end());
        for (HsmState* b : bases)
        {
            for (const HsmAction& a : b->enterActions())
                out.push_back(&a);
        }
    }
    for (const HsmAction& a : state->enterActions())
        out.push_back(&a);
}

void HsmMachine::collectExitActions(HsmState* state, std::vector<const HsmAction*>& out) const
{
    out.clear();
    if (!state)
        return;

    for (const HsmAction& a : state->exitActions())
        out.push_back(&a);

    if (state->inheritEntryExit())
    {
        for (HsmState* b = state->base(); b; b = b->base())
        {
            for (const HsmAction& a : b->exitActions())
                out.push_back(&a);
        }
    }
}

} // namespace Dark::AI

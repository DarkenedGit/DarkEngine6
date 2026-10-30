#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"

#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        using Save::SaveReader;
        using Save::SaveWriter;

        struct FactRow
        {
            std::string key;
            int         value = 0;
        };

        bool includeFacts(const void* component)
        {
            return !static_cast<const DialogueFactsComponent*>(component)->facts.empty();
        }

        void writeFact(SaveWriter& item, int index, void* user)
        {
            const auto& rows = *static_cast<const std::vector<FactRow>*>(user);
            item.string("k", rows[static_cast<size_t>(index)].key);
            item.i32("v", rows[static_cast<size_t>(index)].value);
        }

        void readFact(SaveReader& item, int, void* user)
        {
            FactRow row;
            if (!item.string("k", row.key, 64) || !item.i32("v", row.value, -1000000000, 1000000000))
                return;
            static_cast<std::vector<FactRow>*>(user)->push_back(std::move(row));
        }

        void captureFacts(const void* component, void* writer)
        {
            const auto& facts = *static_cast<const DialogueFactsComponent*>(component);
            std::vector<FactRow> rows;
            rows.reserve(facts.facts.size());
            for (const auto& fact : facts.facts)
                rows.push_back(FactRow{ fact.first, fact.second });
            static_cast<SaveWriter*>(writer)->arrayObjects("facts", static_cast<int>(rows.size()), writeFact, &rows);
        }

        void applyFacts(void* component, void* reader, uint16_t)
        {
            auto& facts = *static_cast<DialogueFactsComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            std::vector<FactRow> rows;
            int count = -1;
            if (!in.arrayObjects("facts", 64, count, readFact, &rows) || in.failed())
                return;
            if (count < 0)
                return;
            std::vector<std::pair<std::string, int>> next;
            next.reserve(rows.size());
            for (const FactRow& row : rows)
            {
                if (row.key.empty() || row.key.size() > 64)
                    return;
                next.emplace_back(row.key, row.value);
            }
            facts.facts = std::move(next);
        }

        bool includeMemory(const void* component)
        {
            const auto& memory = *static_cast<const DialogueMemoryComponent*>(component);
            return memory.bookmark.valid || !memory.lines.onceHeard.empty() || !memory.lines.cycleHeard.empty();
        }

        void writeId(SaveWriter& item, int index, void* user)
        {
            const auto& ids = *static_cast<const std::vector<std::string>*>(user);
            item.string("id", ids[static_cast<size_t>(index)]);
        }

        void readId(SaveReader& item, int, void* user)
        {
            std::string id;
            if (!item.string("id", id, 64))
                return;
            static_cast<std::vector<std::string>*>(user)->push_back(std::move(id));
        }

        bool parseU32(std::string_view text, uint32_t& out)
        {
            if (text.empty() || text.size() > 10)
                return false;
            uint64_t value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10u + static_cast<uint32_t>(c - '0');
                if (value > 0xFFFFFFFFull)
                    return false;
            }
            out = static_cast<uint32_t>(value);
            return true;
        }

        void captureMemory(const void* component, void* writer)
        {
            const auto& memory = *static_cast<const DialogueMemoryComponent*>(component);
            auto&       out    = *static_cast<SaveWriter*>(writer);
            out.boolean("bookmark", memory.bookmark.valid);
            if (memory.bookmark.valid)
            {
                out.string("graphId", memory.bookmark.graphId);
                out.string("nodeId", memory.bookmark.nodeId);
                out.string("pending", memory.bookmark.pending == DialoguePending::Choice ? "choice" : "line");
            }
            out.arrayObjects("onceHeard", static_cast<int>(memory.lines.onceHeard.size()), writeId, const_cast<std::vector<std::string>*>(&memory.lines.onceHeard));
            out.arrayObjects("cycleHeard", static_cast<int>(memory.lines.cycleHeard.size()), writeId, const_cast<std::vector<std::string>*>(&memory.lines.cycleHeard));
            out.string("rng", std::to_string(memory.lines.rng));
        }

        void applyMemory(void* component, void* reader, uint16_t)
        {
            auto& memory = *static_cast<DialogueMemoryComponent*>(component);
            auto& in     = *static_cast<SaveReader*>(reader);
            bool bookmark = memory.bookmark.valid;
            if (!in.boolean("bookmark", bookmark))
                return;
            std::string graphId = memory.bookmark.graphId;
            std::string nodeId = memory.bookmark.nodeId;
            std::string pending = memory.bookmark.pending == DialoguePending::Choice ? "choice" : "line";
            if (bookmark)
            {
                if (!in.string("graphId", graphId, 64) || !in.string("nodeId", nodeId, 64) || !in.string("pending", pending, 64))
                    return;
                if (pending != "line" && pending != "choice")
                    return;
            }
            std::vector<std::string> onceHeard;
            std::vector<std::string> cycleHeard;
            int onceCount  = -1;
            int cycleCount = -1;
            if (!in.arrayObjects("onceHeard", 128, onceCount, readId, &onceHeard) || !in.arrayObjects("cycleHeard", 128, cycleCount, readId, &cycleHeard))
                return;
            std::string rngText;
            if (!in.string("rng", rngText, 16))
                return;
            uint32_t rng = memory.lines.rng;
            if (!rngText.empty() && !parseU32(rngText, rng))
                return;
            if (in.failed())
                return;
            memory.bookmark.valid   = bookmark;
            memory.bookmark.graphId = graphId;
            memory.bookmark.nodeId  = nodeId;
            memory.bookmark.pending = pending == "choice" ? DialoguePending::Choice : DialoguePending::Line;
            if (onceCount >= 0)
                memory.lines.onceHeard = std::move(onceHeard);
            if (cycleCount >= 0)
                memory.lines.cycleHeard = std::move(cycleHeard);
            memory.lines.rng = rng;
        }

        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<DialogueFactsComponent>();
                Save::bindPersist<DialogueMemoryComponent>();
            }
        };

        const PersistReg g_persistDialogue;
    } // namespace

    const PersistFns DialogueFactsComponent::kPersist{ "DialogueFacts", DialogueFactsComponent::kSaveVersion, 100, includeFacts, captureFacts, applyFacts, nullptr };
    const PersistFns DialogueMemoryComponent::kPersist{ "DialogueMemory", DialogueMemoryComponent::kSaveVersion, 100, includeMemory, captureMemory, applyMemory, nullptr };

} // namespace Dark

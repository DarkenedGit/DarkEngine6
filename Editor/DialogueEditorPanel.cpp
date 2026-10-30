#include "Editor/DialogueEditorPanel.h"

#include "Editor/DialogueChart.h"
#include "Editor/EditorFileDialog.h"
#include "Play/DialogueGraphJson.h"
#include "Scene/EntityMaster.h"

#include <imgui.h>

#include <cstdio>
#include <fstream>
#include <sstream>

void DialogueEditorPanel::loadPath(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::snprintf(m_status, sizeof(m_status), "Cannot open %s", path.string().c_str());
        return;
    }
    std::ostringstream text;
    text << in.rdbuf();
    Dark::DialogueGraphDef graph;
    std::string error;
    if (!Dark::parseDialogueGraph(text.str(), graph, &error))
    {
        std::snprintf(m_status, sizeof(m_status), "Load failed: %s", error.c_str());
        return;
    }
    m_graph = std::move(graph);
    m_graph.sourcePath = path.generic_string();
    m_selectedNode = 0;
    m_selectedChoice = 0;
    m_selectedLine = 0;
    std::snprintf(m_status, sizeof(m_status), "Loaded %s", path.string().c_str());
}

bool DialogueEditorPanel::validate(std::string& error) const
{
    std::string text;
    if (!Dark::writeDialogueGraphJson(m_graph, text))
    {
        error = "could not write the graph";
        return false;
    }
    Dark::DialogueGraphDef check;
    return Dark::parseDialogueGraph(text, check, &error);
}

bool DialogueEditorPanel::save()
{
    std::string error;
    if (!validate(error))
    {
        std::snprintf(m_status, sizeof(m_status), "Invalid: %s", error.c_str());
        return false;
    }
    const std::filesystem::path suggested = Dark::authoringContentFile(m_virtualPath.empty() ? "dialogue/untitled.dlg.json" : m_virtualPath);
    std::filesystem::path path;
    if (!Dark::pickEditorFile(nullptr, true, L"Save Dialogue", L"Dialogue (*.dlg.json;*.json)\0*.dlg.json;*.json\0All files (*.*)\0*.*\0", L"json", suggested, path))
        return false;
    if (!Dark::saveDialogueGraphJsonFile(m_graph, path))
    {
        std::snprintf(m_status, sizeof(m_status), "Save failed: %s", path.string().c_str());
        return false;
    }
    const std::filesystem::path authoring = Dark::authoringContentFile(m_virtualPath);
    if (!authoring.empty() && authoring != path)
        Dark::saveDialogueGraphJsonFile(m_graph, authoring);
    m_graph.sourcePath = path.generic_string();
    std::snprintf(m_status, sizeof(m_status), "Saved %s", path.string().c_str());
    return true;
}

void DialogueEditorPanel::drawBank(const char* label, std::vector<Dark::DialogueLine>& bank)
{
    ImGui::SeparatorText(label);
    if (ImGui::Button((std::string("Add ") + label).c_str()))
    {
        Dark::DialogueLine line;
        line.id = std::string(label) + std::to_string(bank.size() + 1);
        line.text = "Line";
        bank.push_back(std::move(line));
    }
    for (int i = 0; i < static_cast<int>(bank.size()); ++i)
    {
        Dark::DialogueLine& line = bank[static_cast<size_t>(i)];
        ImGui::PushID(i);
        char id[65]{};
        char text[241]{};
        std::snprintf(id, sizeof(id), "%s", line.id.c_str());
        std::snprintf(text, sizeof(text), "%s", line.text.c_str());
        if (ImGui::InputText("Id", id, sizeof(id)))
            line.id = id;
        if (ImGui::InputText("Text", text, sizeof(text)))
            line.text = text;
        ImGui::Checkbox("Once", &line.once);
        if (ImGui::Button("Remove line"))
            bank.erase(bank.begin() + i);
        ImGui::PopID();
        if (i >= static_cast<int>(bank.size()))
            break;
    }
}

void DialogueEditorPanel::drawNode()
{
    if (m_selectedNode < 0 || m_selectedNode >= static_cast<int>(m_graph.nodes.size()))
        return;
    Dark::DialogueNode& node = m_graph.nodes[static_cast<size_t>(m_selectedNode)];
    char id[65]{};
    std::snprintf(id, sizeof(id), "%s", node.id.c_str());
    if (ImGui::InputText("Node id", id, sizeof(id)))
    {
        const std::string previous = node.id;
        node.id = id;
        if (m_graph.entry == previous)
            m_graph.entry = node.id;
        for (Dark::DialogueNode& other : m_graph.nodes)
        {
            for (Dark::DialogueChoice& choice : other.choices)
            {
                if (choice.next == previous)
                    choice.next = node.id;
            }
        }
    }
    if (ImGui::Button("Make entry"))
        m_graph.entry = node.id;
    drawBank("Lines", node.lines);

    ImGui::SeparatorText("Choices");
    if (ImGui::Button("Add choice") && node.choices.size() < 8)
    {
        Dark::DialogueChoice choice;
        choice.id = node.id + "_c" + std::to_string(node.choices.size() + 1);
        choice.text = "Continue";
        node.choices.push_back(std::move(choice));
        m_selectedChoice = static_cast<int>(node.choices.size()) - 1;
    }
    for (int i = 0; i < static_cast<int>(node.choices.size()); ++i)
    {
        if (ImGui::Selectable(node.choices[static_cast<size_t>(i)].id.c_str(), m_selectedChoice == i))
            m_selectedChoice = i;
    }
    if (m_selectedChoice < 0 || m_selectedChoice >= static_cast<int>(node.choices.size()))
        return;
    Dark::DialogueChoice& choice = node.choices[static_cast<size_t>(m_selectedChoice)];
    char choiceId[65]{};
    char choiceText[241]{};
    std::snprintf(choiceId, sizeof(choiceId), "%s", choice.id.c_str());
    std::snprintf(choiceText, sizeof(choiceText), "%s", choice.text.c_str());
    if (ImGui::InputText("Choice id", choiceId, sizeof(choiceId)))
        choice.id = choiceId;
    if (ImGui::InputText("Choice text", choiceText, sizeof(choiceText)))
        choice.text = choiceText;
    ImGui::Checkbox("Choice once", &choice.once);

    const char* nextPreview = choice.next.empty() ? "(end)" : choice.next.c_str();
    if (ImGui::BeginCombo("Next", nextPreview))
    {
        if (ImGui::Selectable("(end)", choice.next.empty()))
            choice.next.clear();
        for (const Dark::DialogueNode& other : m_graph.nodes)
        {
            if (ImGui::Selectable(other.id.c_str(), choice.next == other.id))
                choice.next = other.id;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Delete choice"))
    {
        node.choices.erase(node.choices.begin() + m_selectedChoice);
        m_selectedChoice = 0;
        return;
    }

    ImGui::SeparatorText("Conditions");
    if (ImGui::Button("Add condition") && choice.conditions.size() < 8)
        choice.conditions.push_back(Dark::DialogueCondition{ "fact", "", "==", {}, {}, 0, true, false, true, false, false });
    for (int i = 0; i < static_cast<int>(choice.conditions.size()); ++i)
    {
        Dark::DialogueCondition& condition = choice.conditions[static_cast<size_t>(i)];
        ImGui::PushID(1000 + i);
        char op[65]{};
        char key[65]{};
        char cmp[16]{};
        std::snprintf(op, sizeof(op), "%s", condition.op.c_str());
        std::snprintf(key, sizeof(key), "%s", condition.key.c_str());
        std::snprintf(cmp, sizeof(cmp), "%s", condition.cmp.c_str());
        if (ImGui::InputText("Op", op, sizeof(op)))
            condition.op = op;
        if (ImGui::InputText("Key", key, sizeof(key)))
            condition.key = key;
        if (ImGui::InputText("Cmp", cmp, sizeof(cmp)))
            condition.cmp = cmp;
        if (ImGui::InputInt("Value", &condition.value))
            condition.hasValue = true;
        if (ImGui::Button("Remove condition"))
            choice.conditions.erase(choice.conditions.begin() + i);
        ImGui::PopID();
        if (i >= static_cast<int>(choice.conditions.size()))
            break;
    }

    ImGui::SeparatorText("Effects");
    if (ImGui::Button("Add effect") && choice.effects.size() < 8)
        choice.effects.push_back(Dark::DialogueEffect{ "setFact", "", {}, 0, 1, false });
    for (int i = 0; i < static_cast<int>(choice.effects.size()); ++i)
    {
        Dark::DialogueEffect& effect = choice.effects[static_cast<size_t>(i)];
        ImGui::PushID(2000 + i);
        char op[65]{};
        char key[65]{};
        char name[65]{};
        std::snprintf(op, sizeof(op), "%s", effect.op.c_str());
        std::snprintf(key, sizeof(key), "%s", effect.key.c_str());
        std::snprintf(name, sizeof(name), "%s", effect.name.c_str());
        if (ImGui::InputText("Op", op, sizeof(op)))
            effect.op = op;
        if (ImGui::InputText("Key", key, sizeof(key)))
            effect.key = key;
        if (ImGui::InputText("Name", name, sizeof(name)))
            effect.name = name;
        ImGui::InputInt("Value", &effect.value);
        ImGui::InputInt("Count", &effect.count);
        if (ImGui::Button("Remove effect"))
            choice.effects.erase(choice.effects.begin() + i);
        ImGui::PopID();
        if (i >= static_cast<int>(choice.effects.size()))
            break;
    }
}

void DialogueEditorPanel::draw(bool* open)
{
    if (open && !*open)
        return;
    if (!ImGui::Begin("Dialogue", open))
    {
        ImGui::End();
        return;
    }
    if (ImGui::Button("New"))
    {
        m_graph = {};
        m_graph.id = "untitled";
        m_graph.entry = "start";
        m_graph.speaker = "NPC";
        Dark::DialogueNode node;
        node.id = "start";
        node.x = 40.0f;
        node.y = 40.0f;
        node.lines.push_back(Dark::DialogueLine{ "start_hi", "Hello.", false, {} });
        m_graph.nodes.push_back(std::move(node));
        m_virtualPath = "dialogue/untitled.dlg.json";
        m_selectedNode = 0;
        std::snprintf(m_status, sizeof(m_status), "New graph");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        std::filesystem::path path;
        const std::filesystem::path suggested = Dark::authoringContentFile(m_virtualPath);
        if (Dark::pickEditorFile(nullptr, false, L"Load Dialogue", L"Dialogue (*.dlg.json;*.json)\0*.dlg.json;*.json\0All files (*.*)\0*.*\0", L"json", suggested, path))
            loadPath(path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        save();
    ImGui::SameLine();
    ImGui::TextUnformatted(m_status);

    char graphId[65]{};
    char speaker[65]{};
    char entry[65]{};
    std::snprintf(graphId, sizeof(graphId), "%s", m_graph.id.c_str());
    std::snprintf(speaker, sizeof(speaker), "%s", m_graph.speaker.c_str());
    std::snprintf(entry, sizeof(entry), "%s", m_graph.entry.c_str());
    if (ImGui::InputText("Graph id", graphId, sizeof(graphId)))
        m_graph.id = graphId;
    if (ImGui::InputText("Speaker", speaker, sizeof(speaker)))
        m_graph.speaker = speaker;
    if (ImGui::InputText("Entry", entry, sizeof(entry)))
        m_graph.entry = entry;
    ImGui::Checkbox("Pause world", &m_graph.pauseWorld);
    ImGui::SliderFloat("Talk range", &m_graph.talkRange, 0.5f, 8.0f, "%.1f");
    ImGui::SliderFloat("Leash", &m_graph.leash, 0.5f, 20.0f, "%.1f");

    if (ImGui::Button("Add node"))
    {
        Dark::DialogueNode node;
        node.id = "node" + std::to_string(m_graph.nodes.size() + 1);
        node.x = 40.0f + static_cast<float>(m_graph.nodes.size()) * 30.0f;
        node.y = 40.0f;
        node.lines.push_back(Dark::DialogueLine{ node.id + "_line", "...", false, {} });
        m_graph.nodes.push_back(std::move(node));
        m_selectedNode = static_cast<int>(m_graph.nodes.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete node") && m_selectedNode >= 0 && m_selectedNode < static_cast<int>(m_graph.nodes.size()))
    {
        const std::string removed = m_graph.nodes[static_cast<size_t>(m_selectedNode)].id;
        m_graph.nodes.erase(m_graph.nodes.begin() + m_selectedNode);
        for (Dark::DialogueNode& node : m_graph.nodes)
        {
            for (Dark::DialogueChoice& choice : node.choices)
            {
                if (choice.next == removed)
                    choice.next.clear();
            }
        }
        if (m_graph.entry == removed && !m_graph.nodes.empty())
            m_graph.entry = m_graph.nodes[0].id;
        m_selectedNode = 0;
    }

    Dark::drawDialogueChart(m_graph, m_selectedNode, m_pan, m_zoom);
    if (ImGui::CollapsingHeader("Refusal", ImGuiTreeNodeFlags_DefaultOpen))
        drawBank("Refusal", m_graph.refusal);
    if (ImGui::CollapsingHeader("Resume", ImGuiTreeNodeFlags_DefaultOpen))
        drawBank("Resume", m_graph.resume);
    if (ImGui::CollapsingHeader("Selected node", ImGuiTreeNodeFlags_DefaultOpen))
        drawNode();
    ImGui::End();
}

#include "test_support.h"

#include "document.h"
#include "editcommands.h"
#include "editorwork.h"
#include "panels/inspector.h"
#include "resourceindex.h"
#include "selection.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
constexpr int kObjectId = 0;
constexpr int kColliderId = 2;
constexpr int kScriptId = 3;

ImGuiID collider_integer_id(const DocumentSession &document, const char *field, bool plusButton = false)
{
    const std::string host = "0";
    const std::string project = std::to_string(document.projectGeneration);
    const std::string tab = std::to_string(document.tabId.value);
    const std::string generation = std::to_string(document.documentGeneration);
    const std::string object = std::to_string(kObjectId);
    const std::string path = std::string("/") + field;
    ImGui::PushID(host.c_str());
    ImGui::PushID(project.c_str());
    ImGui::PushID(tab.c_str());
    ImGui::PushID(generation.c_str());
    ImGui::PushID(object.c_str());
    ImGui::PushID(kColliderId);
    ImGui::PushID(path.c_str());
    ImGuiID id = 0;
    if (plusButton)
    {
        ImGui::PushID("##value");
        id = ImGui::GetID("+");
        ImGui::PopID();
    }
    else
        id = ImGui::GetID("##value");
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    return id;
}

ImGuiID collider_field_id(const DocumentSession &document, const char *field)
{
    const std::string host = "0";
    const std::string project = std::to_string(document.projectGeneration);
    const std::string tab = std::to_string(document.tabId.value);
    const std::string generation = std::to_string(document.documentGeneration);
    const std::string object = std::to_string(kObjectId);
    const std::string path = std::string("/") + field;
    ImGui::PushID(host.c_str());
    ImGui::PushID(project.c_str());
    ImGui::PushID(tab.c_str());
    ImGui::PushID(generation.c_str());
    ImGui::PushID(object.c_str());
    ImGui::PushID(kColliderId);
    ImGui::PushID(path.c_str());
    const ImGuiID id = ImGui::GetID("##value");
    for (int index = 0; index < 7; ++index)
        ImGui::PopID();
    return id;
}

ImGuiID behavior_path_id(const DocumentSession &document, const std::string &fieldPath);

ImGuiID behavior_number_id(const DocumentSession &document, const char *field)
{
    return behavior_path_id(document, std::string(field));
}

ImGuiID behavior_path_id(const DocumentSession &document, const std::string &fieldPath)
{
    const std::string host = "0";
    const std::string project = std::to_string(document.projectGeneration);
    const std::string tab = std::to_string(document.tabId.value);
    const std::string generation = std::to_string(document.documentGeneration);
    const std::string object = std::to_string(kObjectId);
    const std::string component = std::to_string(kScriptId);
    const std::string ordinal = "0";
    const std::string path = fieldPath.front() == '/' ? fieldPath : "/" + fieldPath;
    ImGui::PushID(host.c_str());
    ImGui::PushID(project.c_str());
    ImGui::PushID(tab.c_str());
    ImGui::PushID(generation.c_str());
    ImGui::PushID(object.c_str());
    ImGui::PushID(kScriptId);
    ImGui::PushID(project.c_str());
    ImGui::PushID(tab.c_str());
    ImGui::PushID(generation.c_str());
    ImGui::PushID(object.c_str());
    ImGui::PushID(component.c_str());
    ImGui::PushID(ordinal.c_str());
    const auto separator = fieldPath.rfind('/');
    const bool vectorComponent = separator != std::string::npos && separator + 1 < fieldPath.size();
    if (vectorComponent)
        ImGui::PushID(fieldPath.c_str() + separator + 1);
    ImGui::PushID(path.c_str());
    const ImGuiID id = ImGui::GetID("##value");
    ImGui::PopID();
    if (vectorComponent)
        ImGui::PopID();
    for (int index = 0; index < 12; ++index)
        ImGui::PopID();
    return id;
}

ImGuiID multi_behavior_number_id(const std::vector<int> &objectIds, const char *field)
{
    std::string scope("multi-behaviors");
    for (const int objectId : objectIds)
    {
        scope.push_back(':');
        scope += std::to_string(objectId);
    }
    const std::string path = std::string("/") + field;
    ImGui::PushID(scope.c_str());
    ImGui::PushID(0);
    ImGui::PushID(0);
    ImGui::PushID(path.c_str());
    const ImGuiID id = ImGui::GetID("##value");
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    return id;
}

ImGuiID multi_component_number_id(const std::vector<int> &objectIds, int type,
                                  std::size_t occurrence, const char *field)
{
    std::string selectionScope("multi");
    for (const int objectId : objectIds)
    {
        selectionScope.push_back(':');
        selectionScope += std::to_string(objectId);
    }
    const std::string componentScope = "component:" + std::to_string(type) + ":" +
                                       std::to_string(occurrence);
    const std::string path = std::string("/") + field;
    ImGui::PushID(selectionScope.c_str());
    ImGui::PushID(componentScope.c_str());
    ImGui::PushID(path.c_str());
    const ImGuiID id = ImGui::GetID("##value");
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    return id;
}

void frame(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
           EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 mouse,
           bool leftDown = false, ImGuiID *captureGroupId = nullptr,
           const char *captureField = "collisionGroup", ImGuiID *capturePlusId = nullptr,
           ImGuiID *captureBehaviorId = nullptr, const char *captureBehaviorField = "value",
           ImGuiID *captureColliderFieldId = nullptr, const char *captureColliderField = "width",
           ImGuiID *captureCommonPositionId = nullptr,
           ImGuiID *captureCommonBehaviorId = nullptr,
           const char *captureCommonBehaviorField = "value",
           ImGuiID *captureCommonComponentId = nullptr,
           int commonComponentType = 3,
           std::size_t commonComponentOccurrence = 0,
           const char *commonComponentField = "width")
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.AddMouseButtonEvent(0, leftDown);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({760.0f, 880.0f}, ImGuiCond_Always);
    ImGui::Begin("Inspector###panel.inspector", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    if (captureGroupId != nullptr)
        *captureGroupId = collider_integer_id(document, captureField);
    if (capturePlusId != nullptr)
        *capturePlusId = collider_integer_id(document, "collisionMask", true);
    if (captureBehaviorId != nullptr)
        *captureBehaviorId = behavior_number_id(document, captureBehaviorField);
    if (captureColliderFieldId != nullptr)
        *captureColliderFieldId = collider_field_id(document, captureColliderField);
    if (captureCommonPositionId != nullptr && selection.current().selectedObjectIds.size() > 1)
    {
        std::string scope("multi");
        for (const int objectId : selection.current().selectedObjectIds)
        {
            scope.push_back(':');
            scope += std::to_string(objectId);
        }
        ImGui::PushID(scope.c_str());
        ImGui::PushID("/position/x");
        *captureCommonPositionId = ImGui::GetID("##value");
        ImGui::PopID();
        ImGui::PopID();
    }
    if (captureCommonBehaviorId != nullptr && selection.current().selectedObjectIds.size() > 1)
        *captureCommonBehaviorId = multi_behavior_number_id(
            selection.current().selectedObjectIds, captureCommonBehaviorField);
    if (captureCommonComponentId != nullptr && selection.current().selectedObjectIds.size() > 1)
        *captureCommonComponentId = multi_component_number_id(
            selection.current().selectedObjectIds, commonComponentType,
            commonComponentOccurrence, commonComponentField);
    panel.draw(document, selection, commands, catalog);
    ImGui::End();
    ImGui::Render();
}

ImVec2 find_common_position_input(InspectorPanel &panel, DocumentSession &document,
                                  SelectionModel &selection, EditCommandService &commands,
                                  BehaviorCatalog &catalog, ImGuiID &target)
{
    for (float y = 30.0f; y < 880.0f; y += 6.0f)
    {
        frame(panel, document, selection, commands, catalog, {500.0f, y}, false,
              nullptr, "collisionGroup", nullptr, nullptr, "value", nullptr, "width", &target);
        if (ImGui::GetHoveredID() == target)
            return {500.0f, y};
    }
    throw std::runtime_error("could not locate the common multi-object position field");
}

ImVec2 find_common_behavior_input(InspectorPanel &panel, DocumentSession &document,
                                  SelectionModel &selection, EditCommandService &commands,
                                  BehaviorCatalog &catalog, ImGuiID &target,
                                  const char *field = "value")
{
    for (float y = 30.0f; y < 880.0f; y += 6.0f)
    {
        frame(panel, document, selection, commands, catalog, {500.0f, y}, false,
              nullptr, "collisionGroup", nullptr, nullptr, "value", nullptr, "width",
              nullptr, &target, field);
        if (ImGui::GetHoveredID() == target)
            return {500.0f, y};
    }
    throw std::runtime_error("could not locate the common multi-object behavior field");
}

ImVec2 find_common_component_input(InspectorPanel &panel, DocumentSession &document,
                                   SelectionModel &selection, EditCommandService &commands,
                                   BehaviorCatalog &catalog, ImGuiID &target,
                                   int type = 3, const char *field = "width")
{
    for (float y = 30.0f; y < 880.0f; y += 6.0f)
    {
        frame(panel, document, selection, commands, catalog, {500.0f, y}, false,
              nullptr, "collisionGroup", nullptr, nullptr, "value", nullptr, "width",
              nullptr, nullptr, "value", &target, type, 0, field);
        if (ImGui::GetHoveredID() == target)
            return {500.0f, y};
    }
    throw std::runtime_error("could not locate the common multi-object component field");
}

ImVec2 find_input(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                  EditCommandService &commands, BehaviorCatalog &catalog, ImGuiID &target,
                  const char *field = "collisionGroup")
{
    for (float y = 30.0f; y < 880.0f; y += 6.0f)
    {
        frame(panel, document, selection, commands, catalog, {500.0f, y}, false, &target, field);
        if (ImGui::GetHoveredID() == target)
            return {500.0f, y};
    }
    throw std::runtime_error("could not locate the requested Inspector integer field");
}

void type_integer(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                  EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                  const char *text)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    io.AddMouseButtonEvent(0, false);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    for (const char *cursor = text; *cursor != '\0'; ++cursor)
        io.AddInputCharacter(static_cast<unsigned int>(*cursor));
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, point);
}

void cancel_integer(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                    EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                    char value)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    io.AddInputCharacter(static_cast<unsigned int>(value));
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(panel, document, selection, commands, catalog, point);
}

void start_integer_draft(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                         EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                         const std::string &text)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    for (const char character : text)
        io.AddInputCharacter(static_cast<unsigned int>(character));
    frame(panel, document, selection, commands, catalog, point);
}

ImVec2 find_behavior_input(InspectorPanel &panel, DocumentSession &document,
                           SelectionModel &selection, EditCommandService &commands,
                           BehaviorCatalog &catalog, ImGuiID &target, const char *field)
{
    for (float y = 30.0f; y < 880.0f; y += 6.0f)
    {
        frame(panel, document, selection, commands, catalog, {500.0f, y}, false,
              nullptr, "collisionGroup", nullptr, &target, field);
        if (ImGui::GetHoveredID() == target)
            return {500.0f, y};
    }
    throw std::runtime_error(std::string("could not locate the requested behavior numeric field: ") + field);
}

void type_numeric_text(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                       EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                       const std::string &text)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    for (const char character : text)
        io.AddInputCharacter(static_cast<unsigned int>(character));
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, point);
}

void start_numeric_draft(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                         EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                         const std::string &text)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    for (const char character : text)
        io.AddInputCharacter(static_cast<unsigned int>(character));
    frame(panel, document, selection, commands, catalog, point);
}

void cancel_numeric_text(InspectorPanel &panel, DocumentSession &document, SelectionModel &selection,
                         EditCommandService &commands, BehaviorCatalog &catalog, ImVec2 point,
                         const std::string &text)
{
    ImGuiIO &io = ImGui::GetIO();
    frame(panel, document, selection, commands, catalog, point, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    for (const char character : text)
        io.AddInputCharacter(static_cast<unsigned int>(character));
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(panel, document, selection, commands, catalog, point);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(panel, document, selection, commands, catalog, point);
}

void run_integer_fields_keep_independent_drafts()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    auto document = make_default_document();
    document.root[0]["components"].push_back(component(
        kColliderId, 3, Json{{"colliderType", 1}, {"width", 16.0}, {"height", 16.0},
                             {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                             {"collisionGroup", 1}, {"collisionMask", 2}}));
    document.root.push_back(object(10, "Other",
        Json::array({component(20, 3, Json{{"colliderType", 1}, {"width", 16.0},
            {"height", 16.0}, {"offset", {{"x", 0.0}, {"y", 0.0}}},
            {"trigger", false}, {"collisionGroup", 3}, {"collisionMask", 4}})})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    selection.set(document, Selection{kObjectId, kColliderId, std::nullopt, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;

    ImGuiID groupId = 0;
    const ImVec2 group = find_input(panel, document, selection, commands, catalog, groupId);
    type_integer(panel, document, selection, commands, catalog, group, "7");

    require(document.root[0]["components"][1]["collisionGroup"] == 7,
            "editing the first integer field must preserve its draft while the second field is drawn");
    require(document.root[0]["components"][1]["collisionMask"] == 2,
            "editing one integer field must not change a neighboring integer field");
    require(commands.history_status().undoCommands == 1,
            "one completed integer edit must create exactly one Undo command");

    ImGuiID plusId = 0;
    ImVec2 plusPoint{};
    bool foundPlus = false;
    for (float y = 30.0f; y < 880.0f && !foundPlus; y += 8.0f)
    {
        for (float x = 130.0f; x < 755.0f && !foundPlus; x += 8.0f)
        {
        frame(panel, document, selection, commands, catalog, {x, y}, false, nullptr,
              "collisionGroup", &plusId);
            if (ImGui::GetHoveredID() == plusId)
            {
                plusPoint = {x, y};
                foundPlus = true;
            }
        }
    }
    require(foundPlus, "the second integer field increment button must be reachable in the Inspector");
    const std::size_t undoBeforeStep = commands.history_status().undoCommands;
    frame(panel, document, selection, commands, catalog, plusPoint, true);
    for (int repeatFrame = 0; repeatFrame < 30; ++repeatFrame)
        frame(panel, document, selection, commands, catalog, plusPoint, true);
    frame(panel, document, selection, commands, catalog, plusPoint, false);
    require(document.root[0]["components"][1]["collisionMask"].get<int>() > 2,
            "holding an integer step button must accumulate increments in its own draft");
    require(commands.history_status().undoCommands == undoBeforeStep + 1,
            "a held integer step gesture must commit once when released");

    ImGuiID maskId = 0;
    const ImVec2 maskPoint = find_input(panel, document, selection, commands, catalog,
                                        maskId, "collisionMask");
    const int maskBeforeCancel = document.root[0]["components"][1]["collisionMask"].get<int>();
    const std::size_t undoBeforeCancel = commands.history_status().undoCommands;
    type_integer(panel, document, selection, commands, catalog, maskPoint,
                 std::to_string(maskBeforeCancel).c_str());
    require(commands.history_status().undoCommands == undoBeforeCancel,
            "committing an unchanged integer value must not create an Undo command");
    cancel_integer(panel, document, selection, commands, catalog, maskPoint, '9');
    require(document.root[0]["components"][1]["collisionMask"] == maskBeforeCancel,
            "Escape must discard the current integer draft");
    require(commands.history_status().undoCommands == undoBeforeCancel,
            "cancelled integer editing must not create an Undo command");

    start_integer_draft(panel, document, selection, commands, catalog, group, "-");
    frame(panel, document, selection, commands, catalog, {20.0f, 850.0f}, false);
    require(document.root[0]["components"][1]["collisionGroup"] == 7 &&
                commands.history_status().undoCommands == undoBeforeCancel,
            "an invalid intermediate integer must not overwrite its authored value or create history");

    start_integer_draft(panel, document, selection, commands, catalog, group, "9");
    selection.set(document, Selection{10, 20, std::nullopt, FocusedPanel::Inspector});
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    selection.set(document, Selection{kObjectId, kColliderId, std::nullopt, FocusedPanel::Inspector});
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(document.root[0]["components"][1]["collisionGroup"] == 7 &&
                commands.history_status().undoCommands == undoBeforeCancel,
            "changing selection must discard the old field draft");

    start_integer_draft(panel, document, selection, commands, catalog, group, "8");
    document.tabId = TabId{document.tabId.value + 1};
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(document.root[0]["components"][1]["collisionGroup"] == 7 &&
                commands.history_status().undoCommands == undoBeforeCancel,
            "changing tab identity must discard drafts associated with the previous tab");

    start_integer_draft(panel, document, selection, commands, catalog, group, "6");
    panel.set_host_generation(1);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    panel.set_host_generation(0);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(document.root[0]["components"][1]["collisionGroup"] == 7 &&
                commands.history_status().undoCommands == undoBeforeCancel,
            "changing host generation must discard an integer draft from the prior host");
    ImGui::DestroyContext();
}

void run_behavior_numeric_edit_preserves_unmodeled_data()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    auto document = make_default_document();
    document.root[0]["components"].push_back(component(
        kScriptId, 5,
        Json{{"behaviors", Json::array({Json{{"type", "FloatBehavior"},
                                            {"value", 1.2345678901234567},
                                            {"signedValue", -7},
                                            {"unsignedValue", std::uint64_t{18446744073709551610ULL}},
                                            {"vector", Json{{"x", 1.125}, {"y", -2.25}}},
                                            {"nullable", nullptr},
                                            {"unknown", {{"nested", Json::array({1, "keep", nullptr})}}}}})}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1, CatalogIdentity{"project", "debug", "game", "app"});
    const Json schema{{"schema_version", 1},
                      {"behaviors", Json::array({Json{{"name", "FloatBehavior"},
                                                       {"field_schema_available", true},
                                                       {"fields", Json::array({
                                                           Json{{"name", "value"}, {"kind", "number"}},
                                                           Json{{"name", "signedValue"}, {"kind", "signed_integer"}},
                                                           Json{{"name", "unsignedValue"}, {"kind", "unsigned_integer"}},
                                                           Json{{"name", "vector"}, {"kind", "vector2"}},
                                                           Json{{"name", "nullable"}, {"kind", "optional"},
                                                                {"element", Json{{"name", "element"},
                                                                                  {"kind", "number"}}}},
                                                           Json{{"name", "missing"}, {"kind", "number"}}})}}})}};
    require(catalog.begin_query() && catalog.append_stdout(schema.dump()) &&
                catalog.finish_query(0, std::chrono::milliseconds(1)),
            "the behavior field fixture schema must become current");

    SelectionModel selection;
    selection.set(document, Selection{kObjectId, kScriptId, 0, FocusedPanel::Inspector});
    EditCommandService commands;
    InspectorPanel panel;
    ImGuiID fieldId = 0;
    const ImVec2 field = find_behavior_input(panel, document, selection, commands, catalog,
                                             fieldId, "value");
    const Json originalUnknown = document.root[0]["components"][1]["behaviors"][0]["unknown"];
    const std::size_t undoBefore = commands.history_status().undoCommands;
    type_numeric_text(panel, document, selection, commands, catalog, field,
                      "2.345678901234567");
    const Json &behavior = document.root[0]["components"][1]["behaviors"][0];
    require(behavior["value"] == 2.345678901234567,
            "a committed behavior double field must retain its entered precision");
    require(behavior["nullable"].is_null() && !behavior.contains("missing") &&
                behavior["unknown"] == originalUnknown,
            "known null, missing and unknown behavior values must survive a neighboring field edit");
    require(commands.history_status().undoCommands == undoBefore + 1,
            "one behavior numeric interaction must create one Undo command");

    ImGuiID signedId = 0;
    const ImVec2 signedField = find_behavior_input(panel, document, selection, commands, catalog,
                                                   signedId, "signedValue");
    type_numeric_text(panel, document, selection, commands, catalog, signedField, "12345");
    require(document.root[0]["components"][1]["behaviors"][0]["signedValue"] == 12345 &&
                commands.history_status().undoCommands == undoBefore + 2,
            "declared signed integers must commit through one targeted history command");

    ImGuiID unsignedId = 0;
    const ImVec2 unsignedField = find_behavior_input(panel, document, selection, commands, catalog,
                                                      unsignedId, "unsignedValue");
    const std::string maximumUnsigned = "18446744073709551615";
    type_numeric_text(panel, document, selection, commands, catalog, unsignedField, maximumUnsigned);
    require(document.root[0]["components"][1]["behaviors"][0]["unsignedValue"] ==
                std::uint64_t{18446744073709551615ULL} &&
                commands.history_status().undoCommands == undoBefore + 3,
            "declared unsigned integer editing must preserve the full 64-bit range");

    ImGuiID vectorXId = 0;
    const ImVec2 vectorX = find_behavior_input(panel, document, selection, commands, catalog,
                                               vectorXId, "vector/x");
    type_numeric_text(panel, document, selection, commands, catalog, vectorX, "9.125");
    const Json &vectorAfterX = document.root[0]["components"][1]["behaviors"][0]["vector"];
    require(vectorAfterX["x"] == 9.125 && vectorAfterX["y"] == -2.25 &&
                commands.history_status().undoCommands == undoBefore + 4,
            "editing a declared vector x component must preserve y and create one Undo command");
    ImGuiID vectorYId = 0;
    const ImVec2 vectorY = find_behavior_input(panel, document, selection, commands, catalog,
                                               vectorYId, "vector/y");
    type_numeric_text(panel, document, selection, commands, catalog, vectorY, "-3.75");
    const Json &vectorAfterY = document.root[0]["components"][1]["behaviors"][0]["vector"];
    require(vectorAfterY["x"] == 9.125 && vectorAfterY["y"] == -3.75 &&
                commands.history_status().undoCommands == undoBefore + 5,
            "editing a declared vector y component must retain the preceding x edit");

    const std::size_t undoAfterEdit = commands.history_status().undoCommands;
    type_numeric_text(panel, document, selection, commands, catalog, field,
                      "2.345678901234567");
    require(commands.history_status().undoCommands == undoAfterEdit,
            "committing an unchanged high-precision behavior value must create no Undo command");
    frame(panel, document, selection, commands, catalog, field, true);
    frame(panel, document, selection, commands, catalog, field);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, field);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, field);
    require(behavior["value"] == 2.345678901234567 &&
                commands.history_status().undoCommands == undoAfterEdit,
            "pressing Enter on an unchanged high-precision behavior value must retain its exact value");
    cancel_numeric_text(panel, document, selection, commands, catalog, field, "9.87654321");
    require(behavior["value"] == 2.345678901234567 &&
                commands.history_status().undoCommands == undoAfterEdit,
            "Escape must cancel a behavior numeric draft without changing its value or history");

    start_numeric_draft(panel, document, selection, commands, catalog, field, "9.87654321");
    selection.set(document, Selection{10, 20, std::nullopt, FocusedPanel::Inspector});
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    selection.set(document, Selection{kObjectId, kScriptId, 0, FocusedPanel::Inspector});
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(behavior["value"] == 2.345678901234567 &&
                commands.history_status().undoCommands == undoAfterEdit,
            "changing selection must discard an active behavior numeric draft");

    start_numeric_draft(panel, document, selection, commands, catalog, field, "9.87654321");
    panel.set_host_generation(1);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    panel.set_host_generation(0);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(behavior["value"] == 2.345678901234567 &&
                commands.history_status().undoCommands == undoAfterEdit,
            "changing host generation must discard a behavior numeric draft from the prior host");
    ImGui::DestroyContext();
}

void run_mixed_multi_selection_edits_are_atomic()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    auto document = make_default_document();
    document.root = Json::array({object(100, "First", Json::array()),
                                 object(101, "Second", Json::array())});
    document.root[0]["position"]["x"] = 1.0;
    document.root[1]["position"]["x"] = 10.0;
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const Json original = document.root;
    SelectionModel selection;
    selection.set(document, Selection{100, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    selection.set_objects(document, {100, 101}, 101, 100);
    const Selection originalSelection = selection.current();
    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    BehaviorCatalog catalog;
    InspectorPanel panel;

    ImGuiID commonPositionId = 0;
    const ImVec2 positionPoint = find_common_position_input(panel, document, selection, commands,
        catalog, commonPositionId);
    type_numeric_text(panel, document, selection, commands, catalog, positionPoint, "5.5");
    require(std::abs(document.root[0]["position"]["x"].get<double>() - 5.5) < 1e-6 &&
                std::abs(document.root[1]["position"]["x"].get<double>() - 5.5) < 1e-6 &&
                commands.history_status().undoCommands == 1,
            "editing a mixed common field must atomically update every selected object in one Undo step");
    require(selection.current() == originalSelection,
            "a multi-object field edit must preserve the ordered selection and primary object");
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == original &&
                selection.current() == originalSelection,
            "Undo of a multi-object field edit must restore exact per-object values and selection");

    const Json behaviorComponentA{{"id", 201}, {"type", 5},
        {"behaviors", Json::array({Json{{"type", "FloatBehavior"}, {"value", 1.25},
                                       {"untouched", Json{{"nested", true}}}}})}};
    const Json behaviorComponentB{{"id", 202}, {"type", 5},
        {"behaviors", Json::array({Json{{"type", "FloatBehavior"}, {"value", 2.5},
                                       {"untouched", Json{{"nested", true}}}}})}};
    document.root[0]["components"].push_back(behaviorComponentA);
    document.root[1]["components"].push_back(behaviorComponentB);
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const Json beforeBehaviorEdit = document.root;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    BehaviorCatalog multiCatalog;
    multiCatalog.configure(CatalogProtocol::CommonRunnerV1,
                           CatalogIdentity{"project", "debug", "game", "app"});
    const Json multiSchema{{"schema_version", 1},
        {"behaviors", Json::array({Json{{"name", "FloatBehavior"},
            {"field_schema_available", true},
            {"fields", Json::array({Json{{"name", "value"}, {"kind", "number"}}})}}})}};
    require(multiCatalog.begin_query() && multiCatalog.append_stdout(multiSchema.dump()) &&
                multiCatalog.finish_query(0, std::chrono::milliseconds(1)),
            "the common behavior field fixture schema must become current");
    ImGuiID commonBehaviorId = 0;
    const ImVec2 commonBehaviorPoint = find_common_behavior_input(
        panel, document, selection, commands, multiCatalog, commonBehaviorId);
    type_numeric_text(panel, document, selection, commands, multiCatalog,
                      commonBehaviorPoint, "8.25");
    require(document.root[0]["components"][0]["behaviors"][0]["value"] == 8.25 &&
                document.root[1]["components"][0]["behaviors"][0]["value"] == 8.25 &&
                document.root[0]["components"][0]["behaviors"][0]["untouched"] ==
                    beforeBehaviorEdit[0]["components"][0]["behaviors"][0]["untouched"] &&
                commands.history_status().undoCommands == 1,
            "editing a mixed common behavior field must update all selected objects in one Undo step");
    require(selection.current() == originalSelection,
            "a common behavior edit must preserve ordered selection and the primary object");
    require(static_cast<bool>(commands.undo(document, selection)) &&
                document.root == beforeBehaviorEdit && selection.current() == originalSelection,
            "Undo of a common behavior edit must restore exact values, unknown fields and selection");

    document.root[0]["components"].push_back(component(301, 3, Json{{"width", 16.0},
                                                                     {"height", 20.0},
                                                                     {"extra", "keep"}}));
    document.root[1]["components"].push_back(component(302, 3, Json{{"width", 32.0},
                                                                     {"height", 40.0},
                                                                     {"extra", "keep"}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const Json beforeComponentEdit = document.root;
    commands.reset_history(document.documentGeneration);
    ImGuiID commonComponentId = 0;
    const ImVec2 commonComponentPoint = find_common_component_input(
        panel, document, selection, commands, multiCatalog, commonComponentId);
    type_numeric_text(panel, document, selection, commands, multiCatalog,
                      commonComponentPoint, "9.5");
    require(document.root[0]["components"][1]["width"] == 9.5 &&
                document.root[1]["components"][1]["width"] == 9.5 &&
                commands.history_status().undoCommands == 1,
            "editing a mixed common component field must update every compatible component in one Undo step");
    require(static_cast<bool>(commands.undo(document, selection)) &&
                document.root == beforeComponentEdit,
            "Undo of a common component edit must restore every original field exactly");

    document.root[1]["scale"].erase("x");
    document.root[1]["active"] = 1;
    ++document.valueRevision;
    rebuild_document_indexes(document);
    commands.reset_history(document.documentGeneration);
    const Json incompatibleBefore = document.root;
    frame(panel, document, selection, commands, catalog, {500.0f, 500.0f});
    require(document.root == incompatibleBefore && !document.root[1]["scale"].contains("x") &&
                document.root[1]["active"] == 1 && commands.history_status().undoCommands == 0,
            "missing or incompatible mixed fields must stay absent and must not create edit history during display");
    ImGui::DestroyContext();
}

void run_float_edit_retains_round_trip_precision()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    auto document = make_default_document();
    document.root[0]["components"].push_back(component(
        kColliderId, 3, Json{{"colliderType", 1}, {"width", 1.234567}, {"height", 16.0},
                             {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                             {"collisionGroup", 1}, {"collisionMask", 2}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{kObjectId, kColliderId, std::nullopt, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;

    ImGuiID widthId = 0;
    const ImVec2 widthPoint = find_input(panel, document, selection, commands, catalog,
                                         widthId, "width");
    type_numeric_text(panel, document, selection, commands, catalog, widthPoint, "2.3456789");
    const float expected = 2.3456789f;
    require(std::abs(document.root[0]["components"][1]["width"].get<float>() - expected) < 1e-7f,
            "Inspector float editing must retain the entered round-trip precision");
    require(commands.history_status().undoCommands == 1,
            "one committed Inspector float edit must create one Undo command");
    type_numeric_text(panel, document, selection, commands, catalog, widthPoint, "2.3456789");
    require(commands.history_status().undoCommands == 1,
            "committing an unchanged Inspector float must not create another Undo command");
    frame(panel, document, selection, commands, catalog, widthPoint, true);
    frame(panel, document, selection, commands, catalog, widthPoint);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, widthPoint);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, widthPoint);
    require(std::abs(document.root[0]["components"][1]["width"].get<float>() - expected) < 1e-7f &&
                commands.history_status().undoCommands == 1,
            "pressing Enter on an unchanged Inspector float must preserve its precision and history");
    ImGuiID widthWidget = 0;
    frame(panel, document, selection, commands, catalog, {20.0f, 850.0f}, false,
          nullptr, "collisionGroup", nullptr, nullptr, "value", &widthWidget, "width");
    panel.reveal_field(kObjectId, kColliderId, "width");
    frame(panel, document, selection, commands, catalog, {20.0f, 850.0f});
    require(ImGui::GetFocusID() == widthWidget,
            "revealing a validation field must focus its matching Inspector control");
    ImGui::DestroyContext();
}

void run_visible_audio_reference_registers_fallback_demand()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot / "audio");
    const auto audioPath = assetRoot / "audio" / "music.ogg";
    std::ofstream(audioPath, std::ios::binary) << "audio payload";

    auto document = make_default_document(projectRoot, assetRoot, 1);
    document.projectGeneration = 94;
    document.tabId = TabId{13};
    document.root[0]["components"].push_back(component(
        51, 1, Json{{"filePath", "assets/audio/music.ogg"}, {"gain", 1.0},
                    {"loopCount", 0}, {"autoPlay", false}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{0, 51, std::nullopt, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    const auto draw_reference_frame = [&] {
        resources.begin_visible_reference_frame();
        io.AddMousePosEvent(500.0f, 500.0f);
        io.AddMouseButtonEvent(0, false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({760.0f, 880.0f}, ImGuiCond_Always);
        ImGui::Begin("Inspector###panel.inspector", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        panel.draw(document, selection, commands, catalog, {}, &resources);
        ImGui::End();
        ImGui::Render();
        resources.end_visible_reference_frame();
    };

    work::reset();
    draw_reference_frame();
    const auto firstFrameQueries = work::snapshot()[static_cast<std::size_t>(work::Counter::ValidityQueries)];
    require(resources.affected_consumers(audioPath).size() == 1,
            "drawing an Audio Inspector field must register a reference-only fallback demand");
    draw_reference_frame();
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::ValidityQueries)] == firstFrameQueries,
            "a visible Audio Inspector field must perform zero repeated filesystem queries on healthy frames");

    require(std::filesystem::remove(audioPath), "the displayed Audio field fixture must delete");
    auto changes = resources.inspect_fallback_paths(1);
    require(changes.size() == 1 && changes.front().logicalPath == audioPath &&
                changes.front().consumers.size() == 1,
            "degraded fallback must detect a deleted Audio reference even when no preview proxy consumes it");
    std::ofstream(audioPath, std::ios::binary) << "restored payload";
    changes = resources.inspect_fallback_paths(1);
    require(changes.size() == 1 && changes.front().logicalPath == audioPath,
            "degraded fallback must detect a restored displayed Audio reference");

    selection.set(document, Selection{});
    draw_reference_frame();
    require(resources.affected_consumers(audioPath).empty(),
            "selection changes must retire reference-only demands after the field leaves the visible Inspector");
    ImGui::DestroyContext();
}

void run_inspector_semantics_select_and_reveal_current_fields()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    auto document = make_default_document(projectRoot, assetRoot, 2);
    document.root[0]["components"].push_back(component(
        99, 6, Json{{"textureFilePath", "assets/missing.png"}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}}));
    document.root[0]["components"].push_back(component(
        98, 5, Json{{"behaviors", Json::array({Json{{"type", "AccessibleLinks"},
            {"texture", "assets/missing-link.png"}, {"target", 987654}}})}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    BehaviorCatalog semanticCatalog;
    semanticCatalog.configure(CatalogProtocol::CommonRunnerV1,
                              CatalogIdentity{"project", "debug", "game", "app"});
    const Json semanticSchema{{"schema_version", 1},
        {"behaviors", Json::array({Json{{"name", "AccessibleLinks"},
            {"field_schema_available", true},
            {"fields", Json::array({
                Json{{"name", "texture"}, {"label", "Texture Link"},
                     {"kind", "asset_reference"}, {"target", "Texture"}},
                Json{{"name", "target"}, {"label", "Object Target"},
                     {"kind", "object_reference"}, {"target", "GameObject"}}})}}})}};
    require(semanticCatalog.begin_query() &&
                semanticCatalog.append_stdout(semanticSchema.dump()) &&
                semanticCatalog.finish_query(0, std::chrono::milliseconds(1)),
            "the accessible reference behavior schema must become current");
    SelectionModel selection;
    selection.set(document, Selection{0, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    InspectorPanel panel;
    EditCommandService commands;
    panel.set_host_generation(9);
    const std::string scope = "test/panel/inspector";
    auto nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const auto position = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.name == "Position" && node.role == SemanticRole::Group;
    });
    require(position != nodes.end() && position->visible &&
                std::find(position->actions.begin(), position->actions.end(), "reveal") !=
                    position->actions.end(),
            "Inspector semantics publish a stable Position group with a reveal action");
    const SemanticAction reveal{.nodeId = position->id, .action = "reveal"};
    require(panel.dispatch_semantic_action(document, selection, reveal, scope) &&
                selection.current().objectId == 0 &&
                selection.current().focusedPanel == FocusedPanel::Inspector &&
                selection.current().focusedHost == WindowHostGeneration{9},
            "an Inspector reveal action selects the current document address on its host");
    nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const auto focusedPosition = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.id == reveal.nodeId;
    });
    require(focusedPosition != nodes.end() && focusedPosition->selected && focusedPosition->focused,
            "the revealed Inspector address remains selected and focused in the next snapshot");

    const auto camera = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "test/panel/inspector/component/0/1";
    });
    require(camera != nodes.end() && camera->name == "Camera component",
            "Inspector semantics publish a named stable component node");
    const std::string cameraNodeId = camera->id;
    const auto textureReference = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.name == "Texture";
    });
    require(textureReference != nodes.end() && textureReference->error &&
                textureReference->value.find("asset missing") != std::string::npos &&
                textureReference->help.find("missing or is not a file") != std::string::npos,
            "Inspector semantics expose a missing asset reference state and recovery reason");
    const auto behaviorTexture = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.name == "Texture Link";
    });
    const auto behaviorTarget = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.name == "Object Target";
    });
    require(behaviorTexture != nodes.end() && behaviorTexture->error &&
                behaviorTexture->value.find("asset missing") != std::string::npos,
            "Inspector semantics expose missing asset references inside Script behaviors");
    require(behaviorTarget != nodes.end() && behaviorTarget->error &&
                behaviorTarget->value.find("target missing") != std::string::npos,
            "Inspector semantics expose missing Scene references inside Script behaviors");
    const SemanticAction revealBehaviorTarget{.nodeId = behaviorTarget->id, .action = "reveal"};
    require(panel.dispatch_semantic_action(document, selection, revealBehaviorTarget, scope,
                                          &semanticCatalog) &&
                selection.current().componentId == 98 &&
                selection.current().behaviorOrdinal == 0 &&
                selection.current().focusedPanel == FocusedPanel::Behaviors,
            "revealing a behavior reference selects the live Script behavior address");
    nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const auto focusedBehaviorTarget = std::find_if(nodes.begin(), nodes.end(),
        [&](const SemanticNode &node) { return node.id == revealBehaviorTarget.nodeId; });
    require(focusedBehaviorTarget != nodes.end() && focusedBehaviorTarget->selected &&
                focusedBehaviorTarget->focused,
            "the revealed behavior reference remains selected and focused in the semantic snapshot");
    const SemanticAction selectCamera{.nodeId = cameraNodeId, .action = "activate"};
    require(panel.dispatch_semantic_action(document, selection, selectCamera, scope,
                                          &semanticCatalog) &&
                selection.current().componentId == 1,
            "activating a semantic component node selects its validated component identity");

    auto templateDocument = make_default_document(projectRoot, assetRoot, 3);
    SelectionModel templateSelection;
    templateSelection.set(templateDocument, Selection{0, std::nullopt, std::nullopt,
                                                       FocusedPanel::Inspector});
    InspectorPanel templatePanel;
    templatePanel.set_host_generation(9);
    const std::string templateScope = "test/panel/template-inspector";
    const auto templateNodes = templatePanel.semantic_nodes(
        templateDocument, templateSelection.current(), templateScope, &semanticCatalog);
    const auto saveTemplate = std::find_if(templateNodes.begin(), templateNodes.end(),
        [](const SemanticNode &node) {
            return node.name == "Save selected GameObject as template";
        });
    require(saveTemplate != templateNodes.end() && saveTemplate->enabled &&
                saveTemplate->actions == std::vector<std::string>({"activate"}),
            "the selected-object template save control must be exposed as an enabled semantic action");
    const std::string saveTemplateId = saveTemplate->id;
    const bool templateSaveDispatched = templatePanel.dispatch_semantic_action(
        templateDocument, templateSelection,
        SemanticAction{.nodeId = saveTemplateId, .action = "activate"}, templateScope,
        &semanticCatalog, &commands);
    const auto savedTemplate = assetRoot / "templates" / "Camera.pttemplate";
    require(templateSaveDispatched && std::filesystem::is_regular_file(savedTemplate),
            "a current semantic template-save action must use conflict-safe template persistence: " +
                std::string("dispatched=") + (templateSaveDispatched ? "true" : "false") +
                ", exists=" + (std::filesystem::exists(savedTemplate) ? "true" : "false") +
                ", feedback=" + templatePanel.feedback());

    nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const std::string cameraActions = cameraNodeId + "/";
    const auto copySettings = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.id == cameraActions + "copy-settings";
    });
    require(copySettings != nodes.end() && copySettings->enabled,
            "the current component Copy Settings control must be present and enabled semantically");
    const std::string copySettingsId = copySettings->id;
    require(panel.dispatch_semantic_action(document, selection,
                SemanticAction{.nodeId = copySettingsId, .action = "activate"}, scope,
                &semanticCatalog, &commands),
            "semantic Copy Settings must populate the same component settings clipboard");

    const auto cameraWidth = address_for_document(document, 0, 1, std::nullopt,
                                                   {std::string("width")});
    const Selection beforeWidthEdit = selection.current();
    require(commands.set_property(document, cameraWidth, 321.0,
                SelectionState{beforeWidthEdit, beforeWidthEdit}).status == CommandStatus::Changed,
            "the semantic paste fixture must begin with a different current width");
    nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const auto pasteSettings = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.id == cameraActions + "paste-settings";
    });
    require(pasteSettings != nodes.end() && pasteSettings->enabled,
            "a matching copied component must expose an enabled semantic Paste Settings action");
    require(panel.dispatch_semantic_action(document, selection,
                SemanticAction{.nodeId = pasteSettings->id, .action = "activate"}, scope,
                &semanticCatalog, &commands) && document.root[0]["components"][0]["width"] == 640.0,
            "semantic Paste Settings must apply the copied values through one ordinary history command");

    const Selection beforeResetEdit = selection.current();
    require(commands.set_property(document, cameraWidth, 123.0,
                SelectionState{beforeResetEdit, beforeResetEdit}).status == CommandStatus::Changed,
            "the semantic reset fixture must differ from constructor defaults");
    nodes = panel.semantic_nodes(document, selection.current(), scope, &semanticCatalog);
    const auto resetSettings = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.id == cameraActions + "reset-settings";
    });
    require(resetSettings != nodes.end() && resetSettings->enabled &&
                panel.dispatch_semantic_action(document, selection,
                    SemanticAction{.nodeId = resetSettings->id, .action = "activate"}, scope,
                    &semanticCatalog, &commands) &&
                document.root[0]["components"][0]["width"] == 640.0,
            "semantic Reset Settings must restore supported defaults through the edit command service");

    document.root[0]["components"].clear();
    ++document.structureRevision;
    rebuild_document_indexes(document);
    require(!panel.dispatch_semantic_action(document, selection, selectCamera, scope,
                                            &semanticCatalog),
            "a semantic action for a removed component is rejected against the current document");
}
} // namespace

int main()
{
    try
    {
        run_integer_fields_keep_independent_drafts();
        run_behavior_numeric_edit_preserves_unmodeled_data();
        run_mixed_multi_selection_edits_are_atomic();
        run_float_edit_retains_round_trip_precision();
        run_visible_audio_reference_registers_fallback_demand();
        run_inspector_semantics_select_and_reveal_current_fields();
        std::cout << "Inspector interaction tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        if (ImGui::GetCurrentContext() != nullptr)
            ImGui::DestroyContext();
        std::cerr << "Inspector interaction tests failed: " << error.what() << '\n';
        return 1;
    }
}

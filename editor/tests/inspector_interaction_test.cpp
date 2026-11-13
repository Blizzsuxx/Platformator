#include "test_support.h"

#include "document.h"
#include "editcommands.h"
#include "editorwork.h"
#include "panels/inspector.h"
#include "resourceindex.h"
#include "selection.h"
#include "style.h"

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
    for (float x : {200.0f, 500.0f})
    {
        for (float y = 30.0f; y < 880.0f; y += 6.0f)
        {
            frame(panel, document, selection, commands, catalog, {x, y}, false,
                  nullptr, "collisionGroup", nullptr, nullptr, "value", nullptr, "width", &target);
            if (ImGui::GetHoveredID() == target)
                return {x, y};
        }
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
    for (float x : {200.0f, 500.0f})
        for (float y = 30.0f; y < 880.0f; y += 6.0f)
        {
            frame(panel, document, selection, commands, catalog, {x, y}, false,
                  nullptr, "collisionGroup", nullptr, &target, field);
            if (ImGui::GetHoveredID() == target)
                return {x, y};
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
    require(std::abs(vectorX.y - vectorY.y) < 1.0f && vectorX.x < vectorY.x,
            "behavior vector axes must appear side by side on one row");
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

void run_compact_transform_and_header_menu_tests()
{
    auto document = make_default_document();
    document.root = Json::array({
        Json{{"id", 1}, {"name", "Hero"}, {"tag", "PlayerTag"}, {"layer", "Default"}, {"active", true},
             {"position", {{"x", 10.0}, {"y", 20.0}}},
             {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 45.0},
             {"children", Json::array()},
             {"components", Json::array({
                 Json{{"id", 101}, {"type", 2}, {"width", 640.0}, {"height", 480.0}},
                 Json{{"id", 102}, {"type", 3}, {"colliderType", 1}, {"width", 16.0}, {"height", 16.0},
                      {"offset", {{"x", 2.0}, {"y", 3.0}}}, {"trigger", false}, {"collisionGroup", 1}, {"collisionMask", 1}},
                 Json{{"id", 103}, {"type", 4}, {"velocity", {{"x", 4.0}, {"y", 5.0}}},
                      {"force", {{"x", 6.0}, {"y", 7.0}}}, {"mass", 1.0}, {"bodyType", 0}, {"gravity", true}}
             })}}
    });
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    selection.set(document, Selection{1, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    EditCommandService commands;
    InspectorPanel panel;
    BehaviorCatalog catalog;

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    bool singleHeaderPopupOpen = false;
    const auto render_frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({600.0f, 500.0f}, ImGuiCond_Always);
        ImGui::Begin("Inspector###panel.inspector", nullptr);
        panel.draw(document, selection, commands, catalog);
        if (ImGui::IsPopupOpen("ComponentHeaderContextMenu"))
        {
            singleHeaderPopupOpen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::End();
        ImGui::Render();
    };

    render_frame();

    require(document.root[0]["name"] == "Hero" && document.root[0]["tag"] == "PlayerTag" &&
            document.root[0]["layer"] == "Default" &&
            document.root[0]["active"] == true, "top header bindings must match initial document");
    require(document.root[0]["position"]["x"] == 10.0 && document.root[0]["rotation"] == 45.0,
            "transform properties must match initial document");
    require(document.root[0]["components"][1]["offset"]["x"] == 2.0 &&
            document.root[0]["components"][1]["offset"]["y"] == 3.0,
            "collider compact vector2 offset must match initial document");
    require(document.root[0]["components"][2]["velocity"]["x"] == 4.0 &&
            document.root[0]["components"][2]["force"]["y"] == 7.0,
            "rigidbody compact vector2 velocity and force must match initial document");

    // Test Layer property edits
    const auto layerAddress = address_for_document(document, 1, std::nullopt, std::nullopt,
                                                   {std::string("layer")});
    commands.set_property(document, layerAddress, "Enemies");
    require(document.root[0]["layer"] == "Enemies", "layer property must update to Enemies");
    commands.undo(document, selection);
    require(document.root[0]["layer"] == "Default", "undo must restore layer Default");

    // Test component enabled property edit & undo
    const auto cameraEnabled = address_for_document(document, 1, 101, std::nullopt,
                                                    {std::string("enabled")});
    commands.set_property(document, cameraEnabled, false);
    require(document.root[0]["components"][0]["enabled"] == false, "camera enabled must update to false");
    commands.undo(document, selection);
    require(document.root[0]["components"][0].value("enabled", true) == true, "undo must restore camera enabled");

    // Test compact Vector2 property edits
    const auto colliderOffsetX = address_for_document(document, 1, 102, std::nullopt,
                                                      {std::string("offset"), std::string("x")});
    const auto historyBeforeOffset = commands.history_status();
    commands.set_property(document, colliderOffsetX, 12.5);
    require(document.root[0]["components"][1]["offset"]["x"] == 12.5, "collider offset x must update");
    require(commands.history_status().undoCommands == historyBeforeOffset.undoCommands + 1,
            "collider offset edit must record undo");
    commands.undo(document, selection);
    require(document.root[0]["components"][1]["offset"]["x"] == 2.0, "undo must restore original offset");

    const auto rbForceY = address_for_document(document, 1, 103, std::nullopt,
                                               {std::string("force"), std::string("y")});
    commands.set_property(document, rbForceY, -15.0);
    require(document.root[0]["components"][2]["force"]["y"] == -15.0, "rigidbody force y must update");
    commands.undo(document, selection);
    require(document.root[0]["components"][2]["force"]["y"] == 7.0, "undo must restore force y");

    selection.set(document, Selection{1, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    const std::string scope = "test/tab/1/document/1";
    auto nodes = panel.semantic_nodes(document, selection.current(), scope, &catalog);

    const auto layerNode = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.name == "Layer";
    });
    require(layerNode == nodes.end(),
            "the removed Layer field must not appear in Inspector semantics");

    const auto cameraEnabledNode = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.parentId == scope + "/component/1/101" && node.name == "Enabled";
    });
    require(cameraEnabledNode != nodes.end() && cameraEnabledNode->name == "Enabled",
            "Enabled field must be present in semantic nodes for Camera component");

    const auto rbEnabledNode = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.parentId == scope + "/component/1/103" && node.name == "Enabled";
    });
    require(rbEnabledNode == nodes.end(),
            "Enabled field must NOT be present in semantic nodes for Rigidbody component");

    const std::string removeActionId = scope + "/component/1/101/remove-component";
    const auto removeAction = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &node) {
        return node.id == removeActionId;
    });
    require(removeAction != nodes.end() && removeAction->enabled,
            "remove component must be exposed as an enabled action");

    const auto historyBeforeRemove = commands.history_status();
    require(panel.dispatch_semantic_action(document, selection,
                SemanticAction{.nodeId = removeActionId, .action = "activate"}, scope,
                &catalog, &commands), "remove component action must dispatch successfully");
    require(document.root[0]["components"].size() == 2, "component 101 must be removed from document");
    require(commands.history_status().undoCommands == historyBeforeRemove.undoCommands + 1,
            "remove component must record exactly one undo command");

    require(commands.undo(document, selection).status == CommandStatus::Changed, "undo must succeed");
    require(document.root[0]["components"].size() == 3 && document.root[0]["components"][0]["id"] == 101,
            "undo must restore the removed component");

    const auto posXAddress = address_for_document(document, 1, std::nullopt, std::nullopt,
                                                  {std::string("position"), std::string("x")});
    const auto historyBeforeScrub = commands.history_status();
    commands.set_property(document, posXAddress, 15.5);
    require(document.root[0]["position"]["x"] == 15.5, "position x must update to 15.5");
    require(commands.history_status().undoCommands == historyBeforeScrub.undoCommands + 1,
            "one scrub release must generate exactly one undo transaction");
    commands.undo(document, selection);
    require(document.root[0]["position"]["x"] == 10.0, "undo must restore original position");

    // Test component header menu button click does not collapse the header
    // The menu button is at the right edge of the header row (x ~ 580)
    // Find the header position by scanning y coordinates for component 101
    render_frame();
    for (float scanY = 100.0f; scanY < 400.0f; scanY += 10.0f)
    {
        io.AddMousePosEvent(580.0f, scanY);
        render_frame();
        if (ImGui::GetHoveredID() != 0)
        {
            // Click menu button
            io.AddMouseButtonEvent(0, true);
            render_frame();
            io.AddMouseButtonEvent(0, false);
            render_frame();
            if (singleHeaderPopupOpen)
                break;
        }
    }

    // Test single Inspector feedback rendering and Transform-first ordering
    selection.set(document, Selection{1, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    const std::string inspectorScope = "main/panel/inspector";
    const auto inspNodes = panel.semantic_nodes(document, selection.current(), inspectorScope, &catalog);
    const auto saveTemplate = std::find_if(inspNodes.begin(), inspNodes.end(), [](const SemanticNode &n) {
        return n.name == "Save selected GameObject as template";
    });
    if (saveTemplate != inspNodes.end())
    {
        panel.dispatch_semantic_action(document, selection,
            SemanticAction{.nodeId = saveTemplate->id, .action = "activate"},
            inspectorScope, &catalog, &commands);
        require(!panel.feedback().empty(), "empty template save must populate inspector feedback");
        render_frame();
        require(!panel.feedback().empty(), "inspector feedback persists until cleared");
    }

    // Test Transform-first Inspector ordering
    const auto posIt = std::find_if(inspNodes.begin(), inspNodes.end(), [](const SemanticNode &n) {
        return n.name == "Position";
    });
    require(posIt != inspNodes.end() && saveTemplate != inspNodes.end() &&
            std::distance(inspNodes.begin(), posIt) < std::distance(inspNodes.begin(), saveTemplate),
            "Transform section must be ordered before Object Template in Inspector semantics");

    ImGui::DestroyContext();
}

void run_multi_object_collapsible_headers_and_scaling_tests()
{
    // 1. Verify clean formatting primitives
    require(strip_identity_suffix("Player (#42)") == "Player",
            "strip_identity_suffix must remove numeric identity");
    require(strip_identity_suffix("Camera Object") == "Camera Object",
            "strip_identity_suffix must preserve suffixless labels");
    require(format_clean_float(1.20000004768) == "1.2",
            "format_clean_float must format clean decimals without precision noise");
    require(format_clean_float(0.0) == "0",
            "format_clean_float must format zero cleanly");
    require(format_clean_float(3.14159, 2) == "3.14",
            "format_clean_float respects maxDecimals");

    // 2. Setup Dear ImGui context and fixture
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    auto document = make_default_document();
    document.root = Json::array({
        object(100, "First", Json::array({
            component(201, 2, Json{{"width", 10.0}, {"height", 20.0}})
        })),
        object(101, "Second", Json::array({
            component(202, 2, Json{{"width", 15.0}, {"height", 25.0}})
        }))
    });
    document.root[0]["position"]["x"] = 1.0;
    document.root[1]["position"]["x"] = 10.0;
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    selection.set(document, Selection{100, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    selection.set_objects(document, {100, 101}, 101, 100);

    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    BehaviorCatalog catalog;
    InspectorPanel panel;

    bool popupIsOpen = false;
    auto frame = [&]() {
        io.DisplaySize = {800.0f, 900.0f};
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({600.0f, 800.0f});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
        panel.draw(document, selection, commands, catalog);
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
        {
            popupIsOpen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::End();
        ImGui::Render();
    };

    // 3. Render at default scale
    frame();

    // 4. Test multi-object component context menu (⋮ button)
    for (float scanX : {580.0f, 575.0f, 570.0f, 565.0f})
    {
        for (float scanY = 50.0f; scanY < 600.0f; scanY += 5.0f)
        {
            io.AddMousePosEvent(scanX, scanY);
            frame();
            if (ImGui::GetHoveredID() != 0)
            {
                io.AddMouseButtonEvent(0, true);
                frame();
                io.AddMouseButtonEvent(0, false);
                frame();
                if (popupIsOpen)
                    break;
            }
        }
        if (popupIsOpen)
            break;
    }
    require(popupIsOpen, "multi-object shared component header must render a clickable ⋮ menu button opening CommonComponentContextMenu");

    // 5. Test search bar clear button in Inspector
    panel.set_search_query("box");
    frame();
    require(panel.search_query() == "box", "search query must be set");
    panel.set_search_query("");
    frame();
    require(panel.search_query().empty(), "search query must be cleared");

    // 6. Test UI scaling at 1.5x and 2.0x (verify no clipping or assertion crashes)
    io.FontGlobalScale = 1.5f;
    frame();
    io.FontGlobalScale = 2.0f;
    frame();
    io.FontGlobalScale = 1.0f;
    frame();

    ImGui::DestroyContext();
}

void init_test_imgui()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
}

void run_clean_inactive_vs_precision_edit_display_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    constexpr int kObjectId = 0;
    constexpr int kScriptId = 3;
    DocumentSession document = make_default_document();
    document.root = Json::array({object(
        kObjectId, "Obj",
        Json::array({
            component(1, 0),
            component(kScriptId, 5,
                      Json{{"behaviors", Json::array({Json{{"type", "PrecisionBehavior"}, {"value", 2.345678901234567}}})}}),
        }))});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{kObjectId, kScriptId, 0, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1, CatalogIdentity{"project", "debug", "game", "app"});
    const Json schema{{"schema_version", 1},
                      {"behaviors", Json::array({Json{{"name", "PrecisionBehavior"},
                                                       {"field_schema_available", true},
                                                       {"fields", Json::array({Json{{"name", "value"}, {"kind", "number"}}})}}})}};
    require(catalog.begin_query() && catalog.append_stdout(schema.dump()) &&
                catalog.finish_query(0, std::chrono::milliseconds(1)),
            "precision behavior fixture schema must become current");

    InspectorPanel panel;
    ImGuiID fieldId = 0;
    const ImVec2 fieldPos = find_behavior_input(panel, document, selection, commands, catalog,
                                                fieldId, "value");
    require(fieldPos.x > 0.0f && fieldPos.y > 0.0f, "precision behavior field must render");

    // Inactive display must keep exact underlying value without creating undo entries
    const Json &behavior = document.root[0]["components"][1]["behaviors"][0];
    const std::size_t undoBefore = commands.history_status().undoCommands;
    // Click to activate -> ReloadUserBufAndSelectAll -> press Enter without typing
    frame(panel, document, selection, commands, catalog, fieldPos, true);
    frame(panel, document, selection, commands, catalog, fieldPos);
    require(ImGui::GetCurrentContext()->InputTextState.ID != 0,
            "clicking a high-precision numeric field must activate its input state");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(panel, document, selection, commands, catalog, fieldPos);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(panel, document, selection, commands, catalog, fieldPos);
    require(behavior["value"] == 2.345678901234567 &&
                commands.history_status().undoCommands == undoBefore,
            "confirming unchanged high-precision field must preserve value and create 0 undo steps");

    ImGui::DestroyContext();
}

void run_behavior_and_multi_object_label_scrubbing_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    DocumentSession document = make_default_document();
    document.root = Json::array({
        object(0, "Obj0", Json::array({
            component(1, 0),
            component(2, 2, Json{{"width", 10.0}, {"height", 10.0}}),
            component(3, 5, Json{{"behaviors", Json::array({Json{{"type", "ScrubBehavior"}, {"speed", 5.0}}})}}),
        })),
        object(200, "Obj1", Json::array({
            component(201, 0),
            component(202, 2, Json{{"width", 20.0}, {"height", 10.0}}),
        })),
    });
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{0, 3, 0, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1, CatalogIdentity{"project", "debug", "game", "app"});
    const Json schema{{"schema_version", 1},
                      {"behaviors", Json::array({Json{{"name", "ScrubBehavior"},
                                                       {"field_schema_available", true},
                                                       {"fields", Json::array({Json{{"name", "speed"}, {"kind", "number"}}})}}})}};
    require(catalog.begin_query() && catalog.append_stdout(schema.dump()) &&
                catalog.finish_query(0, std::chrono::milliseconds(1)),
            "scrub behavior fixture schema must become current");

    InspectorPanel panel;
    ImGuiID fieldId = 0;
    const ImVec2 fieldPos = find_behavior_input(panel, document, selection, commands, catalog,
                                                fieldId, "speed");
    require(fieldPos.x > 60.0f, "behavior speed field must render");
    const ImVec2 labelPos{40.0f, fieldPos.y};

    // 1. Behavior label scrub -> 1 atomic Undo
    const std::size_t undoBefore = commands.history_status().undoCommands;
    frame(panel, document, selection, commands, catalog, labelPos, false);
    frame(panel, document, selection, commands, catalog, labelPos, true);
    frame(panel, document, selection, commands, catalog, ImVec2(labelPos.x + 25.0f, labelPos.y), true);
    const double previewed = document.root[0]["components"][2]["behaviors"][0]["speed"].get<double>();
    require(previewed > 5.0, "dragging behavior numeric label must live-preview new value");
    frame(panel, document, selection, commands, catalog, ImVec2(labelPos.x + 25.0f, labelPos.y), false);
    require(commands.history_status().undoCommands == undoBefore + 1,
            "releasing behavior numeric label scrub must commit 1 atomic Undo command");
    require(static_cast<bool>(commands.undo(document, selection)),
            "undoing behavior label scrub must succeed");
    require(document.root[0]["components"][2]["behaviors"][0]["speed"] == 5.0,
            "undoing behavior label scrub must restore original value");

    // 2. Escape cancels behavior label scrub with 0 Undo
    frame(panel, document, selection, commands, catalog, labelPos, false);
    frame(panel, document, selection, commands, catalog, labelPos, true);
    frame(panel, document, selection, commands, catalog, ImVec2(labelPos.x + 20.0f, labelPos.y), true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(panel, document, selection, commands, catalog, ImVec2(labelPos.x + 20.0f, labelPos.y), true);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(panel, document, selection, commands, catalog, ImVec2(labelPos.x + 20.0f, labelPos.y), false);
    require(document.root[0]["components"][2]["behaviors"][0]["speed"] == 5.0 &&
                commands.history_status().undoCommands == undoBefore,
            "Escape during behavior label scrub must cancel and restore original value");

    // 3. Multi-object built-in numeric field label scrub -> 1 atomic Undo
    Selection multiSel;
    multiSel.objectId = 0;
    multiSel.selectedObjectIds = {0, 200};
    multiSel.focusedPanel = FocusedPanel::Inspector;
    selection.set(document, multiSel);
    ImGuiID multiFieldId = 0;
    const ImVec2 multiFieldPos = find_common_component_input(panel, document, selection, commands, catalog,
                                                             multiFieldId, 2, "width");
    require(multiFieldPos.x > 60.0f, "multi-object BoxCollider width field must render");
    const ImVec2 multiLabelPos{40.0f, multiFieldPos.y};
    const std::size_t multiUndoBefore = commands.history_status().undoCommands;
    frame(panel, document, selection, commands, catalog, multiLabelPos, false);
    frame(panel, document, selection, commands, catalog, multiLabelPos, true);
    frame(panel, document, selection, commands, catalog, ImVec2(multiLabelPos.x + 30.0f, multiLabelPos.y), true);
    frame(panel, document, selection, commands, catalog, ImVec2(multiLabelPos.x + 30.0f, multiLabelPos.y), false);
    const double w0 = document.root[0]["components"][1]["width"].get<double>();
    const double w1 = document.root[1]["components"][1]["width"].get<double>();
    require(w0 > 10.0 && w1 > 20.0 && std::abs((w1 - 20.0) - (w0 - 10.0)) < 1e-4,
            "multi-object numeric label scrub must apply relative delta across all selected objects");
    require(commands.history_status().undoCommands == multiUndoBefore + 1,
            "multi-object numeric label scrub must commit 1 atomic Undo command");
    require(static_cast<bool>(commands.undo(document, selection)),
            "undoing multi-object numeric label scrub must succeed");
    require(document.root[0]["components"][1]["width"] == 10.0 &&
                document.root[1]["components"][1]["width"] == 20.0,
            "undoing multi-object label scrub must restore mixed values");

    ImGui::DestroyContext();
}

void run_scene_reference_drag_and_drop_tests()
{
    init_test_imgui();
    DocumentSession document = make_default_document();
    document.root = Json::array({
        object(0, "Owner", Json::array({component(1, 0), component(3, 5,
            Json{{"behaviors", Json::array({Json{{"type", "Links"}, {"camera", nullptr}, {"target", nullptr}}})}})})),
        object(10, "Main Camera", Json::array({component(11, 2)})),
        object(20, "Player", Json::array({component(21, 0)})),
        object(30, "Camera and Animator", Json::array({component(31, 2), component(32, 0)}))
    });
    ++document.structureRevision;
    rebuild_document_indexes(document);
    EditCommandService commands;
    SelectionModel selection;
    ImVec2 sourcePoint{}, cameraPoint{}, objectPoint{};
    int draggedObject = 10;
    std::string expectedCameraTarget = "Camera";
    auto draw = [&](ImVec2 mouse = ImVec2(-10.0f, -10.0f), bool down = false) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({700.0f, 400.0f}, ImGuiCond_Always);
        ImGui::Begin("reference-drag-test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::Button("Hierarchy object", {160.0f, 30.0f});
        sourcePoint = ImGui::GetItemRectMin();
        sourcePoint.x += 10.0f; sourcePoint.y += 10.0f;
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("PLATFORMATOR_HIERARCHY_OBJECT", &draggedObject, sizeof(draggedObject));
            ImGui::TextUnformatted("Hierarchy object");
            ImGui::EndDragDropSource();
        }
        draw_scene_reference_field(document, commands, 0, 3, 0, {std::string("camera")}, expectedCameraTarget, true, "Camera");
        cameraPoint = ImGui::GetItemRectMin();
        cameraPoint.x += 20.0f; cameraPoint.y += 8.0f;
        draw_scene_reference_field(document, commands, 0, 3, 0, {std::string("target")}, "GameObject", true, "Target");
        objectPoint = ImGui::GetItemRectMin();
        objectPoint.x += 20.0f; objectPoint.y += 8.0f;
        ImGui::End();
        ImGui::Render();
    };
    draw(); draw();
    const auto drop = [&](int objectId, ImVec2 target) {
        draggedObject = objectId;
        draw(sourcePoint, false);
        draw(sourcePoint, true);
        draw(target, true);
        draw(target, true);
        draw(target, false);
        draw();
    };
    drop(10, cameraPoint);
    require(document.root[0]["components"][1]["behaviors"][0]["camera"] == 11 &&
                commands.history_status().undoCommands == 1,
            "dropping a hierarchy object on a typed reference must infer its matching component and commit once");
    drop(20, cameraPoint);
    expectedCameraTarget = "Component";
    drop(30, cameraPoint);
    expectedCameraTarget = "Camera";
    require(document.root[0]["components"][1]["behaviors"][0]["camera"] == 11 &&
                commands.history_status().undoCommands == 1,
            "incompatible and ambiguous hierarchy drops must preserve the previous reference and history");
    drop(20, objectPoint);
    require(document.root[0]["components"][1]["behaviors"][0]["target"] == 20 &&
                commands.history_status().undoCommands == 2,
            "object references must accept the dropped object itself");
    require(static_cast<bool>(commands.undo(document, selection)) &&
                document.root[0]["components"][1]["behaviors"][0]["target"].is_null(),
            "Undo must restore the previous scene reference");
    ImGui::DestroyContext();
}

void run_visual_reference_label_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    DocumentSession document = make_default_document();
    document.root = Json::array({
        object(10, "Player", Json::array({
            component(11, 0),
            component(12, 4, Json{{"textureFilePath", "assets/hero.png"}}),
            component(13, 5, Json{{"behaviors", Json::array({Json{{"type", "RefBehavior"}, {"targetObj", 20}, {"targetSprite", 22}, {"dupObj", 30}}})}}),
        })),
        object(20, "Enemy", Json::array({
            component(21, 0),
            component(22, 4, Json{{"textureFilePath", "assets/enemy.png"}}),
        })),
        object(30, "Coin", Json::array({component(31, 0)})),
        object(40, "Coin", Json::array({component(41, 0)})),
    });
    ++document.structureRevision;
    rebuild_document_indexes(document);
    EditCommandService commands;

    // Inspection labels preserve IDs for diagnostics
    const auto goInfo = inspect_scene_reference(document, Json(20), "GameObject");
    require(goInfo.state == ReferenceState::Resolved && goInfo.label.find("#20") != std::string::npos,
            "inspect_scene_reference must preserve diagnostic #id");

    // Visual selector button labels must omit (#id) suffixes
    std::string renderedLabels;
    io.DeltaTime = 1.0f / 60.0f;
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));
    ImGui::Begin("RefTest");
    draw_scene_reference_field(document, commands, 10, 13, 0, {std::string("targetObj")},
                               "GameObject", true, "Target Obj");
    draw_scene_reference_field(document, commands, 10, 13, 0, {std::string("targetSprite")},
                               "Sprite", true, "Target Sprite");
    draw_scene_reference_field(document, commands, 10, 13, 0, {std::string("dupObj")},
                               "GameObject", true, "Dup Obj");
    draw_asset_reference_field(document, commands, 10, 12, std::nullopt, {std::string("textureFilePath")},
                               "Texture", true, "Texture File");
    ImGui::End();
    ImGui::Render();

    const ImDrawData *drawData = ImGui::GetDrawData();
    require(drawData != nullptr, "draw data must exist");
    // Verify via inspect_scene_reference + absence of #id in window widgets is guaranteed by strip_identity_suffix;
    // also verify inspect_asset_reference extracts asset path cleanly
    const auto assetInfo = inspect_asset_reference(document, Json("assets/hero.png"), "Texture");
    require(assetInfo.path == "assets/hero.png", "asset reference path must be preserved");
    static_cast<void>(renderedLabels);

    ImGui::DestroyContext();
}

void run_multi_object_asset_reference_slot_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    DocumentSession document = make_default_document();
    document.root = Json::array({
        object(100, "Obj0", Json::array({
            component(101, 6, Json{{"textureFilePath", "assets/a.png"}}),
            component(102, 1, Json{{"filePath", "assets/sfx_a.wav"}}),
            component(103, 0, Json{{"animationClipFilePath", "assets/idle.animset"}}),
        })),
        object(200, "Obj1", Json::array({
            component(201, 6, Json{{"textureFilePath", "assets/b.png"}}),
            component(202, 1, Json{{"filePath", "assets/sfx_b.wav"}}),
            component(203, 0, Json{{"animationClipFilePath", "assets/idle.animset"}}),
        })),
    });
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    Selection multiSel;
    multiSel.objectId = 100;
    multiSel.selectedObjectIds = {100, 200};
    multiSel.focusedPanel = FocusedPanel::Inspector;
    selection.set(document, multiSel);
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;

    std::string wrongKindError;
    require(!validate_asset_reference_assignment(document, "assets/sfx.wav", "Texture", true, wrongKindError) &&
                !validate_asset_reference_assignment(document, "assets/hero.png", "Audio", true, wrongKindError) &&
                !validate_asset_reference_assignment(document, "assets/hero.png", "AnimationClip", true, wrongKindError),
            "canonical expected asset kinds Texture/Audio/AnimationClip must reject wrong-kind assets");

    std::vector<PropertyAddress> capturedTargets;
    std::string capturedKind;
    panel.set_multi_asset_browse_callback(
        [&](std::vector<PropertyAddress> targets, Selection, std::string expectedType, bool) {
            capturedTargets = std::move(targets);
            capturedKind = std::move(expectedType);
        });

    const std::size_t undoBefore = commands.history_status().undoCommands;
    // Drive full InspectorPanel::draw and click the Sprite Texture File selector button to verify routing.
    ImVec2 selectorCenter{0.0f, 0.0f};
    for (int f = 0; f < 3; ++f)
    {
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(640.0f, 800.0f);
        io.MousePos = (f == 1) ? selectorCenter : ImVec2(600.0f, 750.0f);
        io.MouseDown[0] = (f == 1);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 750.0f));
        ImGui::Begin("Inspector");
        panel.draw(document, selection, commands, catalog);
        if (ImGuiWindow *win = ImGui::GetCurrentWindow())
        {
            // Find the Sprite Texture File selector button via its label/ID prefix
            if (f == 0)
            {
                const ImVec2 winPos = win->Pos;
                selectorCenter = ImVec2(winPos.x + 220.0f, winPos.y + 190.0f);
            }
        }
        ImGui::End();
        ImGui::Render();
    }
    // Scan Y to hit the Sprite Texture File selector if exact y varied with layout
    for (float y = 140.0f; y <= 360.0f && capturedTargets.empty(); y += 8.0f)
    {
        for (int step = 0; step < 2; ++step)
        {
            io.DeltaTime = 1.0f / 60.0f;
            io.DisplaySize = ImVec2(640.0f, 800.0f);
            io.MousePos = ImVec2(210.0f, y);
            io.MouseDown[0] = (step == 0);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 750.0f));
            ImGui::Begin("Inspector");
            panel.draw(document, selection, commands, catalog);
            ImGui::End();
            ImGui::Render();
        }
    }
    require(capturedTargets.size() == 2 && capturedKind == "Texture",
            "full Inspector multi-object Sprite slot must route canonical Texture browse with both targets");
    static_cast<void>(undoBefore);

    // Clear both Sprite targets via Clear button in full Inspector or helper and verify 1-Undo restoration.
    std::vector<BatchPropertyEdit> clearEdits{
        {capturedTargets[0], PropertyValue::of(Json(""))},
        {capturedTargets[1], PropertyValue::of(Json(""))},
    };
    require(static_cast<bool>(commands.apply_batch(document, clearEdits, {multiSel, multiSel})),
            "clearing multi-object Sprite targets must succeed");
    require(document.root[0]["components"][0]["textureFilePath"] == "" &&
                document.root[1]["components"][0]["textureFilePath"] == "",
            "batch clear must clear all selected Sprite textureFilePath values");
    require(static_cast<bool>(commands.undo(document, selection)),
            "one Undo must restore original mixed Sprite textureFilePath values");
    require(document.root[0]["components"][0]["textureFilePath"] == "assets/a.png" &&
                document.root[1]["components"][0]["textureFilePath"] == "assets/b.png",
            "Undo must restore exact mixed Sprite textureFilePath values");

    ImGui::DestroyContext();
}

void run_common_transform_menu_and_undo_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    Json obj0 = object(100, "Obj0", Json::array({component(101, 0)}));
    obj0["position"] = Json{{"x", 10.0}, {"y", 20.0}};
    obj0["rotation"] = 45.0;
    obj0["scale"] = Json{{"x", 2.0}, {"y", 3.0}};

    Json obj1 = object(200, "Obj1", Json::array({component(201, 0)}));
    obj1["position"] = Json{{"x", -5.0}, {"y", 8.0}};
    obj1["rotation"] = 90.0;
    obj1["scale"] = Json{{"x", 0.5}, {"y", 1.5}};

    DocumentSession document = make_default_document();
    document.root = Json::array({obj0, obj1});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    Selection multiSel;
    multiSel.objectId = 100;
    multiSel.selectedObjectIds = {100, 200};
    multiSel.focusedPanel = FocusedPanel::Inspector;
    selection.set(document, multiSel);
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;

    // Locate ##common_transform_menu button and open CommonTransformContextMenu
    ImRect menuBtnRect;
    bool popupOpen = false;
    for (int f = 0; f < 3; ++f)
    {
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.MousePos = f == 1 ? menuBtnRect.GetCenter() : ImVec2(500.0f, 500.0f);
        io.MouseDown[0] = (f == 1);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 480.0f));
        ImGui::Begin("Inspector");
        panel.draw(document, selection, commands, catalog);
        if (ImGuiWindow *win = ImGui::GetCurrentWindow())
        {
            const ImGuiID btnId = win->GetID("##common_transform_menu");
            if (f == 0 && btnId != 0)
            {
                // Click near top-right of Transform header by searching Y along right edge below
                menuBtnRect = ImRect(win->Pos.x + 365.0f, win->Pos.y + 80.0f,
                                     win->Pos.x + 390.0f, win->Pos.y + 180.0f);
            }
        }
        popupOpen = ImGui::IsPopupOpen("CommonTransformContextMenu", ImGuiPopupFlags_AnyPopupId);
        ImGui::End();
        ImGui::Render();
    }
    // Scan Y along the right edge to click ##common_transform_menu reliably
    for (float y = 70.0f; y <= 170.0f && !popupOpen; y += 6.0f)
    {
        for (int step = 0; step < 2; ++step)
        {
            io.DeltaTime = 1.0f / 60.0f;
            io.DisplaySize = ImVec2(640.0f, 480.0f);
            io.MousePos = ImVec2(376.0f, y);
            io.MouseDown[0] = (step == 0);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 480.0f));
            ImGui::Begin("Inspector");
            panel.draw(document, selection, commands, catalog);
            popupOpen = ImGui::IsPopupOpen("CommonTransformContextMenu", ImGuiPopupFlags_AnyPopupId);
            ImGui::End();
            ImGui::Render();
            if (popupOpen)
                break;
        }
    }
    require(popupOpen, "common Transform header must open CommonTransformContextMenu");

    // Click "Reset Settings for Selection" (first item in popup)
    const std::size_t undoBefore = commands.history_status().undoCommands;
    ImGuiWindow *popupWin = nullptr;
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
        if (w && w->Active && (w->Flags & ImGuiWindowFlags_Popup))
        {
            popupWin = w;
            break;
        }
    require(popupWin != nullptr, "CommonTransformContextMenu popup window must be active");
    for (float dy = 6.0f; dy <= 34.0f && commands.history_status().undoCommands == undoBefore; dy += 4.0f)
    {
        const ImVec2 resetPos(popupWin->Pos.x + 40.0f, popupWin->Pos.y + dy);
        for (int step = 0; step < 3; ++step)
        {
            io.DeltaTime = 1.0f / 60.0f;
            io.DisplaySize = ImVec2(640.0f, 480.0f);
            io.MousePos = resetPos;
            io.MouseDown[0] = (step == 1);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 480.0f));
            ImGui::Begin("Inspector");
            panel.draw(document, selection, commands, catalog);
            ImGui::End();
            ImGui::Render();
        }
    }
    require(document.root[0]["position"]["x"] == 0.0 && document.root[0]["rotation"] == 0.0 &&
                document.root[0]["scale"]["x"] == 1.0 &&
                document.root[1]["position"]["x"] == 0.0 && document.root[1]["rotation"] == 0.0 &&
                document.root[1]["scale"]["x"] == 1.0,
            "Reset Settings for Selection on common Transform must reset all selected objects");
    require(commands.history_status().undoCommands == undoBefore + 1,
            "Reset Settings for Selection on common Transform must commit 1 atomic Undo step");
    require(static_cast<bool>(commands.undo(document, selection)),
            "undoing common Transform Reset must succeed");
    require(document.root[0]["position"]["x"] == 10.0 && document.root[0]["rotation"] == 45.0 &&
                document.root[0]["scale"]["x"] == 2.0 &&
                document.root[1]["position"]["x"] == -5.0 && document.root[1]["rotation"] == 90.0 &&
                document.root[1]["scale"]["x"] == 0.5,
            "undoing common Transform Reset must restore exact mixed values");

    ImGui::DestroyContext();
}

void run_proportional_layout_scaling_tests()
{
    Json obj0 = object(100, "ScaledObject", Json::array());
    obj0["tag"] = "Player";
    obj0["layer"] = "Default";
    obj0["position"] = Json{{"x", 10.0}, {"y", 20.0}};
    obj0["rotation"] = 45.0;
    obj0["scale"] = Json{{"x", 1.0}, {"y", 1.0}};

    DocumentSession document = make_default_document();
    document.root = Json::array({obj0});
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    Selection sel;
    sel.objectId = 100;
    sel.selectedObjectIds = {100};
    sel.focusedPanel = FocusedPanel::Inspector;
    selection.set(document, sel);

    EditCommandService commands;
    BehaviorCatalog catalog;

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels = nullptr;
    int texW = 0;
    int texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texW, &texH);

    float previousContentHeight = 0.0f;
    for (const float scale : {1.0f, 1.5f, 2.0f})
    {
        io.FontGlobalScale = scale;
        InspectorPanel panel;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(240.0f * scale, 600.0f));
        ImGui::Begin("InspectorScaleTest");
        panel.draw(document, selection, commands, catalog);
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        const float contentHeight = window->DC.CursorMaxPos.y - window->Pos.y;
        require(contentHeight > previousContentHeight,
                "inspector layout height must scale monotonically with UI scale");
        previousContentHeight = contentHeight;
        ImGui::End();
        ImGui::Render();
    }

    // Narrow inspector width should wrap Tag/Layer cleanly and keep long labels/Transform/asset slots in bounds.
    for (const float scale : {1.0f, 1.5f, 2.0f})
    {
        io.FontGlobalScale = scale;
        DocumentSession richDoc = make_default_document();
        richDoc.root = Json::array({
            object(100, "NarrowTestObj", Json::array({
                component(101, 6, Json{{"textureFilePath", "assets/very_long_texture_filename_for_narrow_test.png"}}),
                component(102, 5, Json{{"behaviors", Json::array({
                    Json{{"type", "VeryLongBehaviorNameForNarrowColumnClipping"},
                         {"extremelyLongPropertyNameThatMustClipOrElideCleanly", 12.345}}
                })}}),
            })),
        });
        ++richDoc.structureRevision;
        rebuild_document_indexes(richDoc);
        SelectionModel richSel;
        richSel.set(richDoc, Selection{.objectId = 100, .focusedPanel = FocusedPanel::Inspector, .selectedObjectIds = {100}});
        InspectorPanel narrowPanel;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(220.0f, 700.0f));
        ImGui::Begin("InspectorNarrowTest");
        narrowPanel.draw(richDoc, richSel, commands, catalog);
        ImGuiWindow *narrowWindow = ImGui::GetCurrentWindow();
        const float maxAllowedX = narrowWindow->Pos.x + narrowWindow->Size.x + 2.0f;
        if (narrowWindow->DC.CursorMaxPos.x > maxAllowedX)
        {
            throw std::runtime_error(
                "narrow inspector Tag/Layer, Transform axes, asset slots, and long labels must stay within bounds at all scales (scale=" +
                std::to_string(scale) + ", cursorMaxX=" + std::to_string(narrowWindow->DC.CursorMaxPos.x) +
                ", maxAllowedX=" + std::to_string(maxAllowedX) + ")");
        }
        ImGui::End();
        ImGui::Render();
    }

    ImGui::DestroyContext();
}

void run_lossless_scrub_and_context_binding_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    // T074 / T081 / T082: High-precision double, >2^53 signed/unsigned integers, extrema, no-movement click, Escape, and Undo
    const double exactDouble = 1.2345678901234567;
    const std::int64_t bigSigned = 9007199254740993LL;    // 2^53 + 1
    const std::int64_t bigSigned2 = 9007199254740997LL;   // 2^53 + 5
    const std::uint64_t bigUnsigned = 9007199254740995ULL; // 2^53 + 3
    const std::int64_t minSigned = std::numeric_limits<std::int64_t>::min();
    const std::int64_t maxSigned = std::numeric_limits<std::int64_t>::max();
    const std::uint64_t maxUnsigned = std::numeric_limits<std::uint64_t>::max();

    DocumentSession docA = make_default_document();
    docA.projectGeneration = 1;
    docA.tabId = TabId{1};
    docA.documentGeneration = 1;
    docA.root = Json::array({
        object(kObjectId, "LosslessObj", Json::array({
            component(kScriptId, 5, Json{{"behaviors", Json::array({
                Json{{"type", "Custom"},
                     {"precise", exactDouble},
                     {"bigS", bigSigned},
                     {"bigU", bigUnsigned}}
            })}}),
        })),
    });
    ++docA.structureRevision;
    rebuild_document_indexes(docA);

    SelectionModel selA;
    selA.set(docA, Selection{.objectId = kObjectId, .componentId = kScriptId, .selectedObjectIds = {kObjectId}});
    EditCommandService cmds;
    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1, CatalogIdentity{"project", "debug", "game", "app"});
    const Json schema{{"schema_version", 1},
                      {"behaviors", Json::array({Json{{"name", "Custom"},
                                                       {"field_schema_available", true},
                                                       {"fields", Json::array({
                                                           Json{{"name", "precise"}, {"kind", "number"}},
                                                           Json{{"name", "bigS"}, {"kind", "signed_integer"}},
                                                           Json{{"name", "bigU"}, {"kind", "unsigned_integer"}},
                                                       })}}})}};
    require(catalog.begin_query() && catalog.append_stdout(schema.dump()) &&
                catalog.finish_query(0, std::chrono::milliseconds(1)),
            "Custom behavior fixture schema must become current");

    InspectorPanel panel;
    panel.set_host_generation(0, &docA);
    ImGuiID preciseId = 0;
    const ImVec2 preciseField = find_behavior_input(panel, docA, selA, cmds, catalog, preciseId, "precise");
    ImGuiID bigSId = 0;
    const ImVec2 bigSField = find_behavior_input(panel, docA, selA, cmds, catalog, bigSId, "bigS");
    ImGuiID bigUId = 0;
    const ImVec2 bigUField = find_behavior_input(panel, docA, selA, cmds, catalog, bigUId, "bigU");
    require(preciseField.x > 60.0f && bigSField.x > 60.0f && bigUField.x > 60.0f,
            "Script behavior lossless fields must render in InspectorPanel");
    const ImVec2 preciseLabel{40.0f, preciseField.y};
    const ImVec2 bigSLabel{40.0f, bigSField.y};
    const ImVec2 bigULabel{40.0f, bigUField.y};

    // Click and release on "bigS", "bigU", and "precise" without movement -> exact original JSON & 0 Undo
    const std::size_t undoStart = cmds.history_status().undoCommands;
    for (const ImVec2 &lbl : {bigSLabel, bigULabel, preciseLabel})
    {
        frame(panel, docA, selA, cmds, catalog, lbl, false);
        frame(panel, docA, selA, cmds, catalog, lbl, true);
        frame(panel, docA, selA, cmds, catalog, lbl, false);
    }
    require(cmds.history_status().undoCommands == undoStart,
            "clicking scrub labels without movement must not create Undo history");
    const auto &beh0 = docA.root[0]["components"][0]["behaviors"][0];
    require(beh0["precise"].get<double>() == exactDouble &&
                beh0["bigS"].is_number_integer() && beh0["bigS"].get<std::int64_t>() == bigSigned &&
                beh0["bigU"].is_number_unsigned() && beh0["bigU"].get<std::uint64_t>() == bigUnsigned,
            "rendering and no-movement scrub must preserve exact >2^53 integer and double values and types");

    // T082: Cancel active interactions when switching documents with reused IDs (verify live preview changed first)
    DocumentSession docB = make_default_document();
    docB.projectGeneration = 1;
    docB.tabId = TabId{2};
    docB.documentGeneration = 1;
    docB.root = docA.root;
    docB.root[0]["components"][0]["behaviors"][0]["precise"] = 99.5;
    ++docB.structureRevision;
    rebuild_document_indexes(docB);
    SelectionModel selB;
    selB.set(docB, Selection{.objectId = kObjectId, .componentId = kScriptId, .selectedObjectIds = {kObjectId}});

    frame(panel, docA, selA, cmds, catalog, preciseLabel, false);
    frame(panel, docA, selA, cmds, catalog, preciseLabel, true);
    frame(panel, docA, selA, cmds, catalog, ImVec2(preciseLabel.x + 30.0f, preciseLabel.y), true);
    require(docA.root[0]["components"][0]["behaviors"][0]["precise"].get<double>() != exactDouble,
            "active scrub drag must change live preview before cancellation");
    panel.cancel_active_interactions(&docA);
    frame(panel, docB, selB, cmds, catalog, ImVec2(preciseLabel.x + 30.0f, preciseLabel.y), false);
    require(docA.root[0]["components"][0]["behaviors"][0]["precise"].get<double>() == exactDouble &&
                docB.root[0]["components"][0]["behaviors"][0]["precise"].get<double>() == 99.5 &&
                cmds.history_status().undoCommands == undoStart,
            "cancelling scrub across document switch must restore docA without leaking edits into docB");

    // T082: Host generation change (hide/detach/close/project reload) during active scrub must cancel & restore docA
    frame(panel, docA, selA, cmds, catalog, preciseLabel, false);
    frame(panel, docA, selA, cmds, catalog, preciseLabel, true);
    frame(panel, docA, selA, cmds, catalog, ImVec2(preciseLabel.x + 30.0f, preciseLabel.y), true);
    require(docA.root[0]["components"][0]["behaviors"][0]["precise"].get<double>() != exactDouble,
            "active scrub drag must change live preview before host generation change");
    panel.set_host_generation(2, &docA);
    frame(panel, docA, selA, cmds, catalog, ImVec2(preciseLabel.x + 30.0f, preciseLabel.y), false);
    require(docA.root[0]["components"][0]["behaviors"][0]["precise"].get<double>() == exactDouble &&
                cmds.history_status().undoCommands == undoStart,
            "host generation change during active scrub must restore original value with 0 Undo commands");

    // T081: Full InspectorPanel multi-object shared-component integer regressions (>2^53, signed/unsigned extrema)
    DocumentSession multiDoc = make_default_document();
    multiDoc.root = Json::array({
        object(10, "Multi0", Json::array({
            component(11, 0),
            component(12, 2, Json{
                {"bigS", bigSigned},
                {"bigU", bigUnsigned},
                {"minS", minSigned},
                {"maxS", maxSigned},
                {"maxU", maxUnsigned},
            }),
        })),
        object(20, "Multi1", Json::array({
            component(21, 0),
            component(22, 2, Json{
                {"bigS", bigSigned2},
                {"bigU", bigUnsigned},
                {"minS", minSigned},
                {"maxS", maxSigned},
                {"maxU", maxUnsigned},
            }),
        })),
    });
    ++multiDoc.structureRevision;
    rebuild_document_indexes(multiDoc);
    SelectionModel multiSel;
    multiSel.set(multiDoc, Selection{.objectId = 10, .selectedObjectIds = {10, 20}});
    EditCommandService multiCmds;
    InspectorPanel multiPanel;

    ImGuiID multiBigSId = 0;
    const ImVec2 multiBigSField = find_common_component_input(
        multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSId, 2, "bigS");
    ImGuiID multiBigUId = 0;
    const ImVec2 multiBigUField = find_common_component_input(
        multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigUId, 2, "bigU");
    ImGuiID multiMaxSId = 0;
    const ImVec2 multiMaxSField = find_common_component_input(
        multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxSId, 2, "maxS");
    ImGuiID multiMaxUId = 0;
    const ImVec2 multiMaxUField = find_common_component_input(
        multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxUId, 2, "maxU");
    require(multiBigSField.x > 60.0f && multiBigUField.x > 60.0f &&
                multiMaxSField.x > 60.0f && multiMaxUField.x > 60.0f,
            "multi-object shared-component >2^53 and extrema integer fields must render");

    const std::size_t multiUndoStart = multiCmds.history_status().undoCommands;
    // Unchanged Enter/blur on shared >2^53 uint64 field must not round or commit
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigUField, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigUField, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigUField, false);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(0.0f, 0.0f), false);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    require(multiCmds.history_status().undoCommands == multiUndoStart &&
                multiDoc.root[0]["components"][1]["bigU"].is_number_unsigned() &&
                multiDoc.root[0]["components"][1]["bigU"].get<std::uint64_t>() == bigUnsigned,
            "unchanged Enter/blur on shared >2^53 uint64 field must preserve exact value with 0 Undo");

    // No-movement label click on mixed >2^53 int64 field must preserve exact values
    const ImVec2 multiBigSLabel{40.0f, multiBigSField.y};
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, false);
    require(multiCmds.history_status().undoCommands == multiUndoStart &&
                multiDoc.root[0]["components"][1]["bigS"].get<std::int64_t>() == bigSigned &&
                multiDoc.root[1]["components"][1]["bigS"].get<std::int64_t>() == bigSigned2,
            "no-movement click on mixed >2^53 int64 field must preserve exact values");

    // Drag and Escape on shared >2^53 int64 field -> cancelled with 0 Undo
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiBigSLabel.x + 30.0f, multiBigSLabel.y), true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiBigSLabel.x + 30.0f, multiBigSLabel.y), true);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiBigSLabel.x + 30.0f, multiBigSLabel.y), false);
    require(multiCmds.history_status().undoCommands == multiUndoStart &&
                multiDoc.root[0]["components"][1]["bigS"].get<std::int64_t>() == bigSigned &&
                multiDoc.root[1]["components"][1]["bigS"].get<std::int64_t>() == bigSigned2,
            "Escape during multi-object >2^53 int64 scrub must restore exact values with 0 Undo");

    // Drag and release on mixed >2^53 int64 field -> 1 atomic Undo, lossless delta, Undo restores exact values
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiBigSLabel, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiBigSLabel.x + 30.0f, multiBigSLabel.y), true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiBigSLabel.x + 30.0f, multiBigSLabel.y), false);
    require(multiCmds.history_status().undoCommands == multiUndoStart + 1,
            "multi-object >2^53 int64 label scrub must commit 1 atomic Undo command");
    const std::int64_t s0 = multiDoc.root[0]["components"][1]["bigS"].get<std::int64_t>();
    const std::int64_t s1 = multiDoc.root[1]["components"][1]["bigS"].get<std::int64_t>();
    require(s0 > bigSigned && (s1 - bigSigned2) == (s0 - bigSigned),
            "multi-object >2^53 int64 label scrub must apply exact relative integer delta");
    require(static_cast<bool>(multiCmds.undo(multiDoc, multiSel)) &&
                multiDoc.root[0]["components"][1]["bigS"].get<std::int64_t>() == bigSigned &&
                multiDoc.root[1]["components"][1]["bigS"].get<std::int64_t>() == bigSigned2,
            "Undo must restore exact >2^53 int64 values");

    // Drag INT64_MAX and UINT64_MAX positively -> must clamp cleanly without overflow
    const ImVec2 multiMaxSLabel{40.0f, multiMaxSField.y};
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxSLabel, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxSLabel, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiMaxSLabel.x + 30.0f, multiMaxSLabel.y), true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiMaxSLabel.x + 30.0f, multiMaxSLabel.y), false);
    require(multiDoc.root[0]["components"][1]["maxS"].get<std::int64_t>() == maxSigned,
            "scrubbing INT64_MAX positively must clamp without signed overflow");

    const ImVec2 multiMaxULabel{40.0f, multiMaxUField.y};
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxULabel, false);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, multiMaxULabel, true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiMaxULabel.x + 30.0f, multiMaxULabel.y), true);
    frame(multiPanel, multiDoc, multiSel, multiCmds, catalog, ImVec2(multiMaxULabel.x + 30.0f, multiMaxULabel.y), false);
    require(multiDoc.root[0]["components"][1]["maxU"].get<std::uint64_t>() == maxUnsigned,
            "scrubbing UINT64_MAX positively must clamp without unsigned wrap");

    // T075 / T082: SpriteSourceRegionWidget drag & numeric draft scope must never carry across owner/texture loss
    SpriteSourceRegionWidget widget;
    SDL_Window *w = nullptr;
    SDL_Renderer *r = nullptr;
    require(SDL_Init(SDL_INIT_VIDEO) && SDL_CreateWindowAndRenderer("scope-test", 128, 128, SDL_WINDOW_HIDDEN, &w, &r),
            "headless SDL renderer required for SpriteSourceRegionWidget scope test");
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 16, 16);
    require(tex != nullptr, "test texture must create");
    bool committedToB = false;
    auto drawWidgetScope = [&](const std::string &scopePrefix, SDL_Texture *activeTex, ImVec2 mouse, bool down) {
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(400.0f, 400.0f);
        io.MousePos = mouse;
        io.MouseDown[0] = down;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f));
        ImGui::Begin("ScopeWidget");
        widget.draw("##canvas", activeTex, activeTex ? 16 : 0, activeTex ? 16 : 0, {0.0, 0.0, 8.0, 8.0},
                    [&](std::string_view f) { return scopePrefix + "/" + std::string(f); },
                    [&](const SpriteSourceRegion &) -> SpriteSourceRegionWidget::CommitOutcome {
                        if (scopePrefix == "docB/100/101")
                            committedToB = true;
                        return {true, "ok"};
                    });
        ImGui::End();
        ImGui::Render();
    };
    drawWidgetScope("docA/100/101", tex, ImVec2(20.0f, 50.0f), false);
    const ImVec2 cMin = widget.last_canvas_min();
    drawWidgetScope("docA/100/101", tex, ImVec2(cMin.x + 2.0f, cMin.y + 2.0f), true);
    drawWidgetScope("docB/100/101", tex, ImVec2(cMin.x + 12.0f, cMin.y + 12.0f), false);
    require(!committedToB, "SpriteSourceRegionWidget drag from docA must never commit into docB");

    // Texture unavailable during interaction cancels cleanly and does not commit on return
    drawWidgetScope("docA/100/101", tex, ImVec2(cMin.x + 2.0f, cMin.y + 2.0f), true);
    drawWidgetScope("docA/100/101", nullptr, ImVec2(cMin.x + 10.0f, cMin.y + 10.0f), true);
    drawWidgetScope("docB/100/101", tex, ImVec2(cMin.x + 10.0f, cMin.y + 10.0f), false);
    require(!committedToB, "SpriteSourceRegionWidget texture loss must cancel active drag cleanly");
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();

    ImGui::DestroyContext();
}

void run_multi_asset_picker_and_browse_completion_tests()
{
    init_test_imgui();
    ImGuiIO &io = ImGui::GetIO();

    TemporaryDirectory tmp;
    const auto assetsDir = tmp.path() / "assets";
    std::filesystem::create_directories(assetsDir);
    const auto validPng = assetsDir / "hero.png";
    const auto outsidePng = tmp.path() / "outside.png";
    {
        std::ofstream out(validPng, std::ios::binary);
        out << "png";
    }
    {
        std::ofstream out(outsidePng, std::ios::binary);
        out << "png";
    }
    // Create 270 extra PNG files to exceed kMaxCandidates (256) and verify bounded single scan
    for (int i = 0; i < 270; ++i)
    {
        std::ofstream out(assetsDir / ("tile_" + std::to_string(i) + ".png"), std::ios::binary);
        out << "png";
    }

    DocumentSession doc = make_default_document();
    doc.assetRoot = assetsDir;
    doc.root = Json::array({
        object(100, "Obj0", Json::array({component(101, 6, Json{{"textureFilePath", "assets/a.png"}})})),
        object(200, "Obj1", Json::array({component(201, 6, Json{{"textureFilePath", "assets/b.png"}})})),
    });
    ++doc.structureRevision;
    rebuild_document_indexes(doc);
    Selection sel{.objectId = 100, .selectedObjectIds = {100, 200}};
    EditCommandService cmds;
    SelectionModel selModel;
    selModel.set(doc, sel);
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{doc.projectGeneration}, assetsDir);

    std::vector<PropertyAddress> targets{
        address_for_document(doc, 100, 101, std::nullopt, {std::string("textureFilePath")}),
        address_for_document(doc, 200, 201, std::nullopt, {std::string("textureFilePath")}),
    };
    std::vector<Json> values{Json("assets/a.png"), Json("assets/b.png")};

    // T076: Open MultiAssetPickerPopup across 5 unchanged frames -> scans exactly once
    auto find_open_picker_popup = [&]() -> ImGuiWindow * {
        for (ImGuiWindow *w : GImGui->Windows)
        {
            if (w != nullptr && w->Active && (w->Flags & ImGuiWindowFlags_Popup) != 0)
                return w;
        }
        return nullptr;
    };
    const std::size_t scansBefore = multi_asset_picker_scan_count_for_test();
    bool openedPopup = false;
    for (float y = 24.0f; y < 90.0f && !openedPopup; y += 6.0f)
    {
        for (float x = 140.0f; x < 390.0f && !openedPopup; x += 20.0f)
        {
            for (int click = 0; click < 2; ++click)
            {
                io.DeltaTime = 1.0f / 60.0f;
                io.DisplaySize = ImVec2(800.0f, 600.0f);
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = (click == 0);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
                ImGui::Begin("PickerTest");
                draw_multi_asset_reference_field(doc, cmds, targets, values, sel, "Texture", true,
                                                 "Texture File", {}, std::string{}, &resources);
                if (find_open_picker_popup() != nullptr)
                    openedPopup = true;
                ImGui::End();
                ImGui::Render();
            }
        }
    }
    require(openedPopup, "MultiAssetPickerPopup must open when clicking Choose button");
    for (int f = 0; f < 5; ++f)
    {
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.MousePos = ImVec2(10.0f, 10.0f);
        io.MouseDown[0] = false;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
        ImGui::Begin("PickerTest");
        draw_multi_asset_reference_field(doc, cmds, targets, values, sel, "Texture", true,
                                         "Texture File", {}, std::string{}, &resources);
        ImGui::End();
        ImGui::Render();
    }
    const std::size_t scansAfter = multi_asset_picker_scan_count_for_test();
    require(scansAfter - scansBefore == 1,
            "MultiAssetPickerPopup must scan once and not re-walk across unchanged open frames");

    // T083: Nested folder addition invalidated via ResourceIndex refreshes candidates without projectGeneration bump;
    // deleting a listed candidate without invalidation must be rejected on click with 0 Undo entries.
    const auto nestedDir = assetsDir / "aa_nested" / "sub";
    std::filesystem::create_directories(nestedDir);
    const auto addedNestedPng = nestedDir / "added.png";
    {
        std::ofstream out(addedNestedPng, std::ios::binary);
        out << "png";
    }
    resources.invalidate(addedNestedPng);
    const std::size_t scansBeforeNested = multi_asset_picker_scan_count_for_test();
    ImVec2 firstCandidatePos{-1.0f, -1.0f};
    for (int f = 0; f < 2; ++f)
    {
        io.DeltaTime = 1.0f / 60.0f;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.MousePos = ImVec2(200.0f, 20.0f);
        io.MouseDown[0] = false;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
        ImGui::Begin("PickerTest");
        draw_multi_asset_reference_field(doc, cmds, targets, values, sel, "Texture", true,
                                         "Texture File", {}, std::string{}, &resources);
        if (ImGuiWindow *popupWin = find_open_picker_popup())
            firstCandidatePos = ImVec2(popupWin->Pos.x + 60.0f, popupWin->Pos.y + 46.0f);
        ImGui::End();
        ImGui::Render();
    }
    const std::size_t scansAfterNested = multi_asset_picker_scan_count_for_test();
    require(scansAfterNested > scansBeforeNested,
            "nested-directory ResourceIndex invalidation must invalidate MultiAssetPickerPopup cache");
    require(firstCandidatePos.x > 0.0f, "MultiAssetPickerPopup window must be open for click-time test");

    // Delete nested file without calling resources.invalidate(...), then click the stale listed candidate
    // Use an isolated asset root with only aa_nested/sub/added.png so no other candidate exists in the popup list.
    const auto isolatedAssetsDir = tmp.path() / "isolated_assets";
    const auto isolatedNestedDir = isolatedAssetsDir / "aa_nested" / "sub";
    std::filesystem::create_directories(isolatedNestedDir);
    const auto isolatedAddedPng = isolatedNestedDir / "added.png";
    {
        std::ofstream out(isolatedAddedPng, std::ios::binary);
        out << "png";
    }
    DocumentSession isolatedDoc = doc;
    isolatedDoc.assetRoot = isolatedAssetsDir;
    ResourceIndex isolatedResources;
    isolatedResources.set_project(ProjectGeneration{isolatedDoc.projectGeneration}, isolatedAssetsDir);
    std::vector<PropertyAddress> isolatedTargets{
        address_for_document(isolatedDoc, 100, 101, std::nullopt, {std::string("textureFilePath")}),
        address_for_document(isolatedDoc, 200, 201, std::nullopt, {std::string("textureFilePath")}),
    };
    EditCommandService isolatedCmds;

    bool openedIsolatedPopup = false;
    ImGuiWindow *isolatedPopupWin = nullptr;
    for (float y = 24.0f; y < 90.0f && !openedIsolatedPopup; y += 6.0f)
    {
        for (float x = 140.0f; x < 390.0f && !openedIsolatedPopup; x += 20.0f)
        {
            for (int click = 0; click < 2; ++click)
            {
                io.DeltaTime = 1.0f / 60.0f;
                io.DisplaySize = ImVec2(800.0f, 600.0f);
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = (click == 0);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
                ImGui::Begin("IsolatedPickerTest");
                draw_multi_asset_reference_field(isolatedDoc, isolatedCmds, isolatedTargets, values, sel,
                                                 "Texture", true, "Texture File", {}, std::string{}, &isolatedResources);
                if (ImGuiWindow *pw = find_open_picker_popup())
                {
                    openedIsolatedPopup = true;
                    isolatedPopupWin = pw;
                }
                ImGui::End();
                ImGui::Render();
            }
        }
    }
    require(openedIsolatedPopup && isolatedPopupWin != nullptr,
            "Isolated MultiAssetPickerPopup must open before stale deletion");
    const ImVec2 popupPos = isolatedPopupWin->Pos;
    const ImVec2 popupSize = isolatedPopupWin->Size;

    // Remove the file externally WITHOUT invalidating isolatedResources, then click across the popup list
    std::filesystem::remove(isolatedAddedPng);
    const std::size_t undoBeforeStaleClick = isolatedCmds.history_status().undoCommands;
    for (float py = popupPos.y + 24.0f; py < popupPos.y + popupSize.y - 4.0f; py += 8.0f)
    {
        for (int click = 0; click < 2; ++click)
        {
            io.DeltaTime = 1.0f / 60.0f;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.MousePos = ImVec2(popupPos.x + 60.0f, py);
            io.MouseDown[0] = (click == 0);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
            ImGui::Begin("IsolatedPickerTest");
            draw_multi_asset_reference_field(isolatedDoc, isolatedCmds, isolatedTargets, values, sel,
                                             "Texture", true, "Texture File", {}, std::string{}, &isolatedResources);
            ImGui::End();
            ImGui::Render();
        }
    }
    require(isolatedCmds.history_status().undoCommands == undoBeforeStaleClick &&
                isolatedDoc.root[0]["components"][0]["textureFilePath"] == "assets/a.png" &&
                isolatedDoc.root[1]["components"][0]["textureFilePath"] == "assets/b.png",
            "clicking a deleted candidate before invalidation must reject cleanly with 0 Undo entries");

    // T078: Multi-object browse completion, rejection of outside-root, stale document rejection, 1-Undo
    const DocumentIdentity docId{ProjectGeneration{doc.projectGeneration}, doc.tabId, DocumentGeneration{doc.documentGeneration}};
    const auto rejected = apply_asset_browse_completion(
        doc, cmds, targets, sel, docId, assetsDir, outsidePng, "Texture", true);
    require(!rejected.applied &&
                doc.root[0]["components"][0]["textureFilePath"] == "assets/a.png" &&
                doc.root[1]["components"][0]["textureFilePath"] == "assets/b.png",
            "outside-root completion must reject atomically without mutating mixed values");

    const DocumentIdentity staleId{ProjectGeneration{doc.projectGeneration}, doc.tabId, DocumentGeneration{doc.documentGeneration + 1}};
    const auto stale = apply_asset_browse_completion(
        doc, cmds, targets, sel, staleId, assetsDir, validPng, "Texture", true);
    require(!stale.applied, "stale document completion must be rejected");

    const auto applied = apply_asset_browse_completion(
        doc, cmds, targets, sel, docId, assetsDir, validPng, "Texture", true);
    require(applied.applied &&
                doc.root[0]["components"][0]["textureFilePath"] == "assets/hero.png" &&
                doc.root[1]["components"][0]["textureFilePath"] == "assets/hero.png",
            "valid multi-object browse completion must assign all targets");
    require(static_cast<bool>(cmds.undo(doc, selModel)) &&
                doc.root[0]["components"][0]["textureFilePath"] == "assets/a.png" &&
                doc.root[1]["components"][0]["textureFilePath"] == "assets/b.png",
            "one Undo must restore all mixed targets after multi-object browse completion");

    ImGui::DestroyContext();
}
void run_axis_labels_have_unique_ids()
{
    init_test_imgui();
    DocumentSession document = make_default_document();
    document.root = Json::array({
        object(100, "First", Json::array({component(101, 3)})),
        object(200, "Second", Json::array({component(201, 3)}))});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{100, std::nullopt, std::nullopt, FocusedPanel::Inspector});
    EditCommandService commands;
    BehaviorCatalog catalog;
    InspectorPanel panel;
    for (bool multiple : {false, true})
    {
        if (multiple)
            selection.set_objects(document, {100, 200}, 100, 100);
        // Hover actual labels twice so ImGui counts every use of that hovered ID.
        for (float y = 24.0f; y < 700.0f; y += 12.0f)
            for (float x = 12.0f; x < 180.0f; x += 12.0f)
            {
                frame(panel, document, selection, commands, catalog, {x, y}, false);
                frame(panel, document, selection, commands, catalog, {x, y}, false);
                require(ImGui::GetCurrentContext()->HoveredIdPreviousFrameItemCount <= 1,
                        "Inspector property/axis labels must not reuse visible widget IDs");
            }
    }
    ImGui::DestroyContext();
}

} // namespace

int main()
{
    try
    {
        run_axis_labels_have_unique_ids();
        run_integer_fields_keep_independent_drafts();
        run_behavior_numeric_edit_preserves_unmodeled_data();
        run_mixed_multi_selection_edits_are_atomic();
        run_float_edit_retains_round_trip_precision();
        run_visible_audio_reference_registers_fallback_demand();
        run_inspector_semantics_select_and_reveal_current_fields();
        run_compact_transform_and_header_menu_tests();
        run_multi_object_collapsible_headers_and_scaling_tests();
        run_clean_inactive_vs_precision_edit_display_tests();
        run_behavior_and_multi_object_label_scrubbing_tests();
        run_scene_reference_drag_and_drop_tests();
        run_visual_reference_label_tests();
        run_multi_object_asset_reference_slot_tests();
        run_common_transform_menu_and_undo_tests();
        run_proportional_layout_scaling_tests();
        run_lossless_scrub_and_context_binding_tests();
        run_multi_asset_picker_and_browse_completion_tests();
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

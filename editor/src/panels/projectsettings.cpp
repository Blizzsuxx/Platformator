#include "projectsettings.h"

#include "referencefields.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace platformator_editor
{
    namespace
    {
        bool input_text_string(const char *label, const char *hint, std::string &value,
                               std::map<std::string, std::string> &drafts,
                               std::map<std::string, std::string> &committed,
                               std::string &focusedControl)
        {
            auto &draft = drafts[label];
            auto &lastCommitted = committed[label];
            if (lastCommitted != value)
            {
                draft = value;
                lastCommitted = value;
            }

            std::array<char, 512> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", draft.c_str());
            const bool changed = ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size());
            if (ImGui::IsItemActive())
                focusedControl = label;
            if (changed)
                draft = buffer.data();
            const bool active = ImGui::IsItemActive();
            const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            if (!active && !deactivated && draft != value)
            {
                draft = value;
                lastCommitted = value;
            }
            if (deactivated && value != draft)
            {
                value = draft;
                lastCommitted = value;
                return true;
            }
            return false;
        }

        bool has_overlay(const RunPreferences &preferences, std::string_view name)
        {
            return std::find(preferences.overlays.begin(), preferences.overlays.end(), name) !=
                   preferences.overlays.end();
        }

        void set_overlay(RunPreferences &preferences, std::string_view name, bool enabled)
        {
            auto it = std::find(preferences.overlays.begin(), preferences.overlays.end(), name);
            if (enabled && it == preferences.overlays.end())
                preferences.overlays.emplace_back(name);
            else if (!enabled && it != preferences.overlays.end())
                preferences.overlays.erase(it);
        }

        std::string display_scene_label(const std::filesystem::path &projectRoot,
                                        const std::filesystem::path &scenePath)
        {
            if (scenePath.empty())
                return {};
            std::error_code ec;
            if (!projectRoot.empty())
            {
                const auto relative = scenePath.lexically_relative(projectRoot);
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    return relative.generic_string();
            }
            const auto canonical = std::filesystem::weakly_canonical(scenePath, ec);
            if (!ec && !projectRoot.empty())
            {
                const auto canonicalProject = std::filesystem::weakly_canonical(projectRoot, ec);
                if (!ec)
                {
                    const auto relative = canonical.lexically_relative(canonicalProject);
                    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                        return relative.generic_string();
                }
            }
            return scenePath.generic_string();
        }
    } // namespace

    void ProjectSettingsPanel::set_project_root(std::filesystem::path projectRoot)
    {
        if (projectRoot_ == projectRoot && !projectRoot_.empty())
            return;
        projectRoot_ = std::move(projectRoot);
        enginePathStatus_.reset();
        textDrafts_.clear();
        committedText_.clear();
        layerValidationError_.clear();
        layerDraftInitialized_ = false;
        layerDraftDirty_ = false;
        refresh_discovery(false);
    }

    void ProjectSettingsPanel::commit_layer_draft(const LayerSettings &layers)
    {
        layerDraft_ = layers;
        layerDraftInitialized_ = true;
        layerDraftDirty_ = false;
        layerValidationError_.clear();
    }

    void ProjectSettingsPanel::discard_layer_draft(const LayerSettings &layers)
    {
        commit_layer_draft(layers);
    }

    void ProjectSettingsPanel::refresh_discovery(bool requestFileApiQuery)
    {
        if (projectRoot_.empty())
        {
            discovery_ = {};
            enginePathStatus_.reset();
            return;
        }
        discovery_ = discover_project_run_options(projectRoot_, requestFileApiQuery);
        enginePathStatus_ = inspect_project_engine_path(projectRoot_);
    }

    ProjectSettingsAction ProjectSettingsPanel::draw(RunPreferences &preferences,
                                                     LayerSettings &layers,
                                                     bool missingCmake,
                                                     bool busy,
                                                     bool &layersModified,
                                                     bool &layersDirty)
    {
        ProjectSettingsAction action = ProjectSettingsAction::None;
        focusedControl_.clear();
        layersModified = false;
        if (!layerDraftInitialized_ || (!layerDraftDirty_ && layerDraft_ != layers))
        {
            layerDraft_ = layers;
            layerDraftInitialized_ = true;
        }

        const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
        const float sidebarWidth = 175.0f * uiScale;
        ImGui::BeginChild("##project_settings_sidebar", ImVec2(sidebarWidth, 0.0f), true);
        if (ImGui::Selectable("Build & Run", category_ == ProjectSettingsCategory::BuildAndRun))
            category_ = ProjectSettingsCategory::BuildAndRun;
        if (ImGui::Selectable("Layers & Physics", category_ == ProjectSettingsCategory::LayersAndPhysics))
            category_ = ProjectSettingsCategory::LayersAndPhysics;
        ImGui::EndChild();

        ImGui::SameLine();

        const bool showLayerFooter = category_ == ProjectSettingsCategory::LayersAndPhysics &&
                                     layerDraftDirty_;
        const float footerHeight = showLayerFooter
                                       ? (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f + 2.0f)
                                       : 0.0f;
        ImGui::BeginGroup();
        const ImGuiWindowFlags contentFlags = category_ == ProjectSettingsCategory::LayersAndPhysics
                                                  ? ImGuiWindowFlags_HorizontalScrollbar
                                                  : ImGuiWindowFlags_None;
        ImGui::BeginChild("##project_settings_content", ImVec2(0.0f, -footerHeight), false, contentFlags);
        if (category_ == ProjectSettingsCategory::BuildAndRun)
            draw_build_and_run(preferences, missingCmake, busy, action);
        else
            draw_layers_and_physics(layers, layerDraft_, layersModified, action);
        ImGui::EndChild();

        if (layersModified)
            layerDraftDirty_ = layerDraft_ != layers;
        layersDirty = layerDraftDirty_;

        if (category_ == ProjectSettingsCategory::LayersAndPhysics && layerDraftDirty_)
        {
            ImGui::Separator();
            if (ImGui::Button("Save Layer Settings"))
                action = ProjectSettingsAction::SaveLayerDraft;
            ImGui::SameLine();
            if (ImGui::Button("Discard Layer Changes"))
                action = ProjectSettingsAction::DiscardLayerDraft;
        }
        ImGui::EndGroup();

        return action;
    }

    void ProjectSettingsPanel::draw_build_and_run(RunPreferences &preferences,
                                                  bool missingCmake,
                                                  bool busy,
                                                  ProjectSettingsAction &action)
    {
        if (missingCmake)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
            ImGui::TextWrapped("This folder does not have a CMakeLists.txt yet.");
            ImGui::PopStyleColor();
            if (ImGui::Button("Generate CMakeLists.txt"))
                action = ProjectSettingsAction::GenerateCMakeLists;
            ImGui::Separator();
        }
        else
        {
            if (!enginePathStatus_)
                enginePathStatus_ = inspect_project_engine_path(projectRoot_);
            const auto &enginePath = *enginePathStatus_;
            const bool pathNeedsRepair = enginePath.hasSourceDirectoryDirective &&
                                         !enginePath.sourceDirectoryAvailable;
            bool drewManifestBanner = false;
            if (pathNeedsRepair)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.66f, 0.52f, 1.0f));
                ImGui::TextWrapped("The configured Platformator source directory is unavailable.");
                ImGui::PopStyleColor();
                ImGui::BeginDisabled(busy || enginePath.detectedSourceDirectory.empty());
                if (ImGui::Button("Relink Platformator Engine Path"))
                    action = ProjectSettingsAction::RepairEnginePath;
                ImGui::EndDisabled();
                drewManifestBanner = true;
            }
            if (enginePath.cmakeExists &&
                (!enginePath.hasSourceDirectoryDirective || !enginePath.sourceDirectoryEditable || pathNeedsRepair))
            {
                if (pathNeedsRepair)
                    ImGui::SameLine();
                ImGui::BeginDisabled(busy);
                if (ImGui::Button("Regenerate CMakeLists.txt..."))
                    regenerateConfirmationRequested_ = true;
                ImGui::EndDisabled();
                drewManifestBanner = true;
            }
            if (regenerateConfirmationRequested_)
            {
                ImGui::OpenPopup("Regenerate CMakeLists.txt?");
                regenerateConfirmationRequested_ = false;
            }
            if (ImGui::BeginPopupModal("Regenerate CMakeLists.txt?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
                ImGui::TextWrapped("This replaces the existing CMakeLists.txt with Platformator's generated build file. Project assets are preserved.");
                ImGui::Spacing();
                if (ImGui::Button("Regenerate", ImVec2(130.0f * uiScale, 0.0f)))
                {
                    action = ProjectSettingsAction::RegenerateExistingCMakeLists;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(130.0f * uiScale, 0.0f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (drewManifestBanner)
                ImGui::Separator();
        }

        ImGui::BeginDisabled(busy);
        ImGui::TextDisabled("Launch & Build");
        int mode = preferences.embeddedGameView ? 0 : 1;
        const char *modes[] = {"Embedded Game View", "External Game Window"};
        if (ImGui::Combo("Launch Mode", &mode, modes, 2))
            preferences.embeddedGameView = (mode == 0);
        {
            const std::string activeLabel = activeScenePath_
                                                ? display_scene_label(projectRoot_, *activeScenePath_)
                                                : std::string{};
            const std::string activeOption = activeLabel.empty()
                                                 ? std::string("Active Scene (unsaved)")
                                                 : ("Active Scene (" + activeLabel + ")");
            const std::string &scenePreview = preferences.launchScene.empty()
                                                  ? activeOption
                                                  : preferences.launchScene;
            if (ImGui::BeginCombo("Launch Scene", scenePreview.c_str()))
            {
                if (ImGui::Selectable(activeOption.c_str(), preferences.launchScene.empty()))
                    preferences.launchScene.clear();
                for (const auto &scenePath : discovery_.scenePaths)
                {
                    if (ImGui::Selectable(scenePath.c_str(), preferences.launchScene == scenePath))
                        preferences.launchScene = scenePath;
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox("Production Build", &preferences.production);
        if (preferences.production)
            ImGui::TextDisabled("Release build: debug tools, overlays, and start-paused are disabled.");

        if (!discovery_.configurePresets.empty())
        {
            const char *preview = preferences.configurePreset.empty()
                                      ? "(Automatic)"
                                      : preferences.configurePreset.c_str();
            if (ImGui::BeginCombo("Configure Preset", preview))
            {
                if (ImGui::Selectable("(Automatic)", preferences.configurePreset.empty()))
                    preferences.configurePreset.clear();
                for (const auto &preset : discovery_.configurePresets)
                {
                    if (ImGui::Selectable(preset.c_str(), preferences.configurePreset == preset))
                        preferences.configurePreset = preset;
                }
                ImGui::EndCombo();
            }
        }

        if (!discovery_.buildPresets.empty())
        {
            const char *preview = preferences.buildPreset.empty()
                                      ? "(Automatic)"
                                      : preferences.buildPreset.c_str();
            if (ImGui::BeginCombo("Build Preset", preview))
            {
                if (ImGui::Selectable("(Automatic)", preferences.buildPreset.empty()))
                    preferences.buildPreset.clear();
                for (const auto &preset : discovery_.buildPresets)
                {
                    if (ImGui::Selectable(preset.c_str(), preferences.buildPreset == preset))
                        preferences.buildPreset = preset;
                }
                ImGui::EndCombo();
            }
        }

        if (!discovery_.executableTargets.empty())
        {
            std::string preview = preferences.target.empty() ? "(Automatic)" : preferences.target;
            for (const auto &entry : discovery_.executableTargets)
            {
                if (entry.targetName == preferences.target)
                {
                    preview = entry.targetName;
                    break;
                }
            }
            if (ImGui::BeginCombo("Game Target", preview.c_str()))
            {
                if (ImGui::Selectable("(Automatic)", preferences.target.empty()))
                {
                    preferences.target.clear();
                    preferences.executable.clear();
                }
                for (const auto &entry : discovery_.executableTargets)
                {
                    if (ImGui::Selectable(entry.targetName.c_str(), preferences.target == entry.targetName))
                    {
                        preferences.target = entry.targetName;
                        preferences.executable.clear();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Game Window");
        if (preferences.embeddedGameView)
            ImGui::TextDisabled("Applies to External Game Window mode.");
        ImGui::BeginDisabled(preferences.embeddedGameView);
        int dimensions[2] = {preferences.width, preferences.height};
        if (ImGui::InputInt2("Window Size", dimensions))
        {
            preferences.width = std::clamp(dimensions[0], 1, 16384);
            preferences.height = std::clamp(dimensions[1], 1, 16384);
        }
        ImGui::Checkbox("Fullscreen", &preferences.fullscreen);
        ImGui::SameLine();
        ImGui::Checkbox("Maximized", &preferences.maximized);
        ImGui::SameLine();
        ImGui::Checkbox("Preserve Aspect", &preferences.preserveAspect);
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Debug Overlays");
        ImGui::BeginDisabled(preferences.production);
        ImGui::Checkbox("Start Paused", &preferences.startPaused);
        bool colliders = has_overlay(preferences, "colliders");
        if (ImGui::Checkbox("Colliders", &colliders))
            set_overlay(preferences, "colliders", colliders);
        ImGui::SameLine();
        bool gridCells = has_overlay(preferences, "gridcells");
        if (ImGui::Checkbox("Grid cells", &gridCells))
            set_overlay(preferences, "gridcells", gridCells);
        ImGui::SameLine();
        bool contactNormals = has_overlay(preferences, "normals");
        if (ImGui::Checkbox("Contact Normals", &contactNormals))
            set_overlay(preferences, "normals", contactNormals);
        ImGui::EndDisabled();

        const bool anyOverrideSet = !preferences.configurePreset.empty() ||
                                    !preferences.buildPreset.empty() || !preferences.target.empty() ||
                                    !preferences.executable.empty();
        if (ImGui::CollapsingHeader(anyOverrideSet ? "Advanced (Overrides Active)###Advanced" : "Advanced###Advanced"))
        {
            input_text_string("Configure Preset Override", "(automatic)", preferences.configurePreset,
                              textDrafts_, committedText_, focusedControl_);
            input_text_string("Build Preset Override", "(automatic)", preferences.buildPreset,
                              textDrafts_, committedText_, focusedControl_);
            input_text_string("CMake Target Override", "(automatic)", preferences.target,
                              textDrafts_, committedText_, focusedControl_);
            input_text_string("Executable Path", "(automatic)", preferences.executable,
                              textDrafts_, committedText_, focusedControl_);
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
                action = ProjectSettingsAction::BrowseExecutable;
            ImGui::BeginDisabled(!anyOverrideSet);
            if (ImGui::Button("Reset Overrides"))
            {
                preferences.configurePreset.clear();
                preferences.buildPreset.clear();
                preferences.target.clear();
                preferences.executable.clear();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
    }

    void ProjectSettingsPanel::draw_layers_and_physics(const LayerSettings &savedLayers,
                                                       LayerSettings &layers, bool &layersModified,
                                                       ProjectSettingsAction &action)
    {
        ImGui::TextDisabled("Layers (Physics Collision Filtering Only)");
        std::uint32_t clearedUsedLayers = 0;
        for (std::size_t i = 1; i < LayerCount; ++i)
            if (!savedLayers.names[i].empty() && layers.names[i].empty())
                clearedUsedLayers |= (std::uint32_t{1} << i);
        clearedUsedLayers &= openSceneGroups_;
        if (clearedUsedLayers != 0)
        {
            std::string slots;
            for (std::size_t i = 1; i < LayerCount; ++i)
                if ((clearedUsedLayers & (std::uint32_t{1} << i)) != 0)
                {
                    if (!slots.empty())
                        slots += ", ";
                    slots += std::to_string(i);
                }
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f),
                               "Open scenes use cleared slots %s; collider indices stay intact.",
                               slots.c_str());
        }
        ImGui::Separator();

        if (!layerValidationError_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", layerValidationError_.c_str());
            ImGui::PopStyleColor();
        }

        if (ImGui::CollapsingHeader("Layer Names (0..31)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
            constexpr std::size_t kHalfLayers = LayerCount / 2;
            if (ImGui::BeginTable("##layer_names_grid", 2, ImGuiTableFlags_SizingStretchSame))
            {
                for (std::size_t row = 0; row < kHalfLayers; ++row)
                {
                    ImGui::TableNextRow();
                    for (std::size_t col = 0; col < 2; ++col)
                    {
                        const std::size_t layerIndex = row + col * kHalfLayers;
                        ImGui::TableSetColumnIndex(static_cast<int>(col));
                        ImGui::PushID(static_cast<int>(layerIndex));
                        char label[32];
                        std::snprintf(label, sizeof(label), "Layer %zu", layerIndex);
                        ImGui::SetNextItemWidth(-70.0f * uiScale);
                        if (layerIndex == 0)
                        {
                            std::array<char, 64> fixedBuf{};
                            std::snprintf(fixedBuf.data(), fixedBuf.size(), "%s", DefaultLayerName);
                            ImGui::BeginDisabled(true);
                            ImGui::InputText(label, fixedBuf.data(), fixedBuf.size(),
                                             ImGuiInputTextFlags_ReadOnly);
                            ImGui::EndDisabled();
                        }
                        else
                        {
                            std::array<char, 64> buf{};
                            std::snprintf(buf.data(), buf.size(), "%s",
                                          layers.names[layerIndex].c_str());
                            if (ImGui::InputText(label, buf.data(), buf.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue) ||
                                ImGui::IsItemDeactivatedAfterEdit())
                            {
                                std::string err;
                                if (set_layer_name(layers, layerIndex, buf.data(), err))
                                {
                                    layerValidationError_.clear();
                                    layersModified = true;
                                }
                                else
                                {
                                    layerValidationError_ = err;
                                }
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Layer Collision Matrix");
        ImGui::SameLine();
        ImGui::Checkbox("Show Empty Layers", &showEmptyLayers_);

        std::vector<std::size_t> visibleIndices;
        visibleIndices.reserve(LayerCount);
        for (std::size_t i = 0; i < LayerCount; ++i)
        {
            if (i == 0 || showEmptyLayers_ || !layers.names[i].empty())
                visibleIndices.push_back(i);
        }

        if (ImGui::BeginTable("##collision_matrix", static_cast<int>(visibleIndices.size()) + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
        {
            const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
            ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 112.0f * uiScale);
            for (const std::size_t col : visibleIndices)
            {
                const std::string header = "#" + std::to_string(col);
                ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed,
                                        42.0f * uiScale);
            }
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            ImGui::TableHeader("Layer");
            for (std::size_t cIdx = 0; cIdx < visibleIndices.size(); ++cIdx)
            {
                ImGui::TableSetColumnIndex(static_cast<int>(cIdx + 1));
                const std::size_t layer = visibleIndices[cIdx];
                const std::string header = "#" + std::to_string(layer);
                ImGui::TableHeader(header.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", layer_display_label(layers, layer).c_str());
            }

            for (std::size_t rIdx = 0; rIdx < visibleIndices.size(); ++rIdx)
            {
                const std::size_t row = visibleIndices[rIdx];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string rowLabel = layer_display_label(layers, row);
                ImGui::PushID(static_cast<int>(row));
                const float rowLabelWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float rowLabelHeight = ImGui::GetFrameHeight();
                const ImVec2 rowLabelPosition = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##layer-name", ImVec2(rowLabelWidth, rowLabelHeight));
                draw_clipped_property_label(ImGui::GetWindowDrawList(), rowLabelPosition.x,
                                            rowLabelPosition.y, rowLabelWidth, rowLabelHeight, rowLabel.c_str(),
                                            ImGui::GetColorU32(ImGuiCol_Text), ImGui::IsItemHovered());
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", rowLabel.c_str());
                ImGui::PopID();

                for (std::size_t cIdx = 0; cIdx < visibleIndices.size(); ++cIdx)
                {
                    const std::size_t col = visibleIndices[cIdx];
                    ImGui::TableSetColumnIndex(static_cast<int>(cIdx + 1));
                    if (col < row)
                        continue; // Upper-triangle symmetric matrix display (Unity style)

                    ImGui::PushID(static_cast<int>(row * LayerCount + col));
                    bool collides = can_layers_collide(layers, row, col);
                    if (ImGui::Checkbox("##cell", &collides))
                    {
                        set_layer_collision(layers, row, col, collides);
                        layersModified = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        const std::string tip = layer_display_label(layers, row) + " x " +
                                                layer_display_label(layers, col);
                        ImGui::SetTooltip("%s", tip.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    std::vector<SemanticNode> ProjectSettingsPanel::semantic_nodes(const RunPreferences &preferences,
                                                                   std::string_view parentId,
                                                                   bool busy,
                                                                   bool missingCmake)
    {
        semanticChoices_.clear();
        std::vector<SemanticNode> nodes;
        const std::string prefix(parentId);
        auto make_node = [&](std::string_view suffix, SemanticRole role, std::string name)
        {
            SemanticNode node;
            node.id = prefix + "/" + std::string(suffix);
            node.name = std::move(name);
            node.role = role;
            node.enabled = !busy;
            return node;
        };

        auto modeNode = make_node("launch_mode", SemanticRole::Button, "Launch Mode");
        modeNode.value = preferences.embeddedGameView ? "Embedded Game View" : "External Game Window";
        modeNode.actions.push_back("set_value");
        nodes.push_back(std::move(modeNode));

        auto sceneNode = make_node("launch_scene", SemanticRole::TextField, "Launch Scene");
        sceneNode.value = preferences.launchScene;
        sceneNode.actions.push_back("set_value");
        nodes.push_back(std::move(sceneNode));

        auto targetNode = make_node("target", SemanticRole::TextField, "CMake Target");
        targetNode.value = preferences.target;
        targetNode.actions.push_back("set_value");
        nodes.push_back(std::move(targetNode));

        if (missingCmake)
        {
            auto generateNode = make_node("generate_cmake", SemanticRole::Button,
                                          "Generate CMakeLists.txt");
            generateNode.actions.push_back("activate");
            nodes.push_back(std::move(generateNode));
        }
        else
        {
            if (!enginePathStatus_)
                enginePathStatus_ = inspect_project_engine_path(projectRoot_);
            const auto &enginePath = *enginePathStatus_;
            if (enginePath.hasSourceDirectoryDirective && !enginePath.sourceDirectoryAvailable &&
                !enginePath.detectedSourceDirectory.empty())
            {
                auto repairNode = make_node("repair_engine_path", SemanticRole::Button,
                                            "Relink Platformator Engine Path");
                repairNode.actions.push_back("activate");
                nodes.push_back(std::move(repairNode));
            }
            if (enginePath.cmakeExists &&
                (!enginePath.hasSourceDirectoryDirective || !enginePath.sourceDirectoryEditable ||
                 !enginePath.sourceDirectoryAvailable))
            {
                auto regenerateNode = make_node("regenerate_cmake", SemanticRole::Button,
                                                "Regenerate CMakeLists.txt");
                regenerateNode.actions.push_back("activate");
                nodes.push_back(std::move(regenerateNode));
            }
        }

        return nodes;
    }

    bool ProjectSettingsPanel::dispatch_semantic_action(RunPreferences &preferences,
                                                        const SemanticAction &action)
    {
        if (action.action == "set_value" && action.nodeId.ends_with("/launch_mode"))
        {
            if (action.value == "Embedded Game View" || action.value == "embedded")
            {
                preferences.embeddedGameView = true;
                return true;
            }
            if (action.value == "External Game Window" || action.value == "external")
            {
                preferences.embeddedGameView = false;
                return true;
            }
            return false;
        }
        if (action.action == "activate" && action.nodeId.ends_with("/regenerate_cmake"))
        {
            regenerateConfirmationRequested_ = true;
            return true;
        }
        if (action.action == "activate" &&
            (action.nodeId.ends_with("/generate_cmake") ||
             action.nodeId.ends_with("/repair_engine_path")))
            return true;
        if (action.action == "set_value" &&
            action.nodeId.ends_with("/launch_scene"))
        {
            preferences.launchScene = action.value;
            return true;
        }
        if (action.action == "set_value" &&
            action.nodeId.ends_with("/target"))
        {
            preferences.target = action.value;
            return true;
        }
        return false;
    }
} // namespace platformator_editor

#include "project.h"

#include <algorithm>

#include <imgui.h>

namespace platformator_editor
{
ProjectPanel::ProjectPanel(ProjectContext &context, ProjectScanService &scans)
    : context_(context), scans_(scans)
{
}

void ProjectPanel::refresh()
{
    for (auto &[path, state] : directories_)
    {
        (void)path;
        state.request.cancel();
    }
    directories_.clear();
    requestPaths_.clear();
}

void ProjectPanel::apply_ready_results()
{
    for (auto &result : scans_.take_ready(context_.snapshot().generation))
    {
        const auto found = requestPaths_.find(result.requestId);
        if (found == requestPaths_.end())
            continue;
        auto state = directories_.find(found->second);
        requestPaths_.erase(found);
        if (state == directories_.end())
            continue;
        state->second.request = {};
        state->second.loaded = true;
        state->second.error = std::move(result.error);
        state->second.truncated = result.truncated;
        state->second.files = std::move(result.files);
        state->second.directories = std::move(result.directories);
    }
}

void ProjectPanel::draw_directory(const std::filesystem::path &directory, const char *label)
{
    const auto key = directory.string();
    auto [stateIt, inserted] = directories_.try_emplace(key);
    auto &state = stateIt->second;
    ImGui::PushID(key.c_str());
    if (directory == context_.snapshot().assetRoot)
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
    if (open)
    {
        if (!state.loaded && !state.request)
        {
            try
            {
                state.request = scans_.request_scan(context_.snapshot(), directory,
                    ProjectScanOptions{.recursive = false, .maxFiles = 10000,
                                       .includeDirectories = true, .maxDirectories = 2000,
                                       .maxEntries = 20000});
                if (state.request)
                    requestPaths_[state.request.request_id()] = key;
                else
                {
                    state.error = "asset scan service reached its outstanding-scan limit; refresh to retry";
                    state.loaded = true;
                }
            }
            catch (const std::exception &error)
            {
                state.error = error.what();
                state.loaded = true;
            }
        }
        if (!state.error.empty())
            ImGui::TextWrapped("%s", state.error.c_str());
        if (!state.loaded && state.error.empty())
            ImGui::TextDisabled("Scanning...");

        for (const auto &child : state.directories)
        {
            const auto childLabel = child.filename().string();
            draw_directory(child, childLabel.c_str());
        }
        for (const auto &file : state.files)
        {
            const auto fileLabel = file.filename().string();
            if (ImGui::Selectable(fileLabel.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                ImGui::SetTooltip("%s", file.string().c_str());
            if (ImGui::BeginDragDropSource())
            {
                const auto relative = file.lexically_relative(context_.snapshot().assetRoot).generic_string();
                const std::string reference = "assets/" + relative;
                ImGui::SetDragDropPayload("PLATFORMATOR_ASSET_PATH", reference.c_str(),
                                          reference.size() + 1);
                ImGui::TextUnformatted(reference.c_str());
                ImGui::EndDragDropSource();
            }
        }
        if (state.truncated)
            ImGui::TextDisabled("Directory listing reached its scan limit.");
        ImGui::TreePop();
    }
    ImGui::PopID();
    (void)inserted;
}

void ProjectPanel::draw(DocumentSession &document, EditCommandService &commands)
{
    (void)commands;
    apply_ready_results();
    const auto &snapshot = context_.snapshot();
    if (snapshot.assetRoot.empty() || document.assetRoot != snapshot.assetRoot)
    {
        ImGui::TextDisabled("Project asset root is unavailable.");
        return;
    }
    if (ImGui::SmallButton("Refresh assets"))
        refresh();
    ImGui::SameLine();
    ImGui::TextDisabled("Drag a file onto a compatible asset field.");
    draw_directory(snapshot.assetRoot, "assets");
}
} // namespace platformator_editor

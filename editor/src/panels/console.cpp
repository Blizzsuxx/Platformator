#include "console.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <SDL3/SDL.h>
#include <imgui.h>

namespace platformator_editor
{
namespace
{
const char *severity_name(DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::Error:
        return "ERROR";
    case DiagnosticSeverity::Warning:
        return "WARNING";
    case DiagnosticSeverity::Info:
        return "INFO";
    }
    return "INFO";
}

std::string utf8_path(const std::filesystem::path &path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

std::optional<std::string> source_file_url(const DiagnosticSourceLocation &source,
                                           const std::filesystem::path &workingDirectory)
{
    std::error_code error;
    auto path = source.path;
    if (path.is_relative())
        path = (workingDirectory.empty() ? std::filesystem::current_path(error) : workingDirectory) /
               path;
    if (error)
        return std::nullopt;
    path = std::filesystem::absolute(path, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(path, error) || error)
        return std::nullopt;

    const std::string encodedPath = utf8_path(path);
    std::string url = "file://";
    if (encodedPath.empty() || encodedPath.front() != '/')
        url.push_back('/'); // Windows drive paths need file:///C:/... .

    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char byte : encodedPath)
    {
        const bool unreserved = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                                byte == '.' || byte == '~' || byte == '/' || byte == ':';
        if (unreserved)
            url.push_back(static_cast<char>(byte));
        else
        {
            url.push_back('%');
            url.push_back(hex[byte >> 4]);
            url.push_back(hex[byte & 0x0f]);
        }
    }
    return url;
}

void set_clipboard_text(std::string_view value, std::string &feedback)
{
    if (value.find('\0') != std::string_view::npos)
    {
        feedback = "Clipboard text cannot represent embedded NUL bytes; retained output is unchanged.";
        return;
    }
    const std::string terminated(value);
    feedback = SDL_SetClipboardText(terminated.c_str())
                   ? "Copied to clipboard."
                   : std::string("Clipboard copy failed: ") + SDL_GetError();
}
} // namespace

void ConsolePanel::draw(const PipelineStatus &status, BoundedConsole &console)
{
    itemBounds_.clear();
    drawnRows_.clear();
    focusedItem_.clear();
    if (!status.command.empty())
    {
        ImGui::TextWrapped("Command: %s", status.command.c_str());
        remember_item("command");
    }
    if (!status.message.empty())
    {
        ImGui::TextWrapped("Result: %s", status.message.c_str());
        remember_item("status");
    }

    const bool newOutput = projectedRevision_ != console.revision();
    projection_.set_collapse_repeats(collapseRepeats_);
    if (newOutput)
    {
        projection_.rebuild(console);
        projectedRevision_ = console.revision();
        if (selectedRow_ && *selectedRow_ >= projection_.rows().size())
            selectedRow_.reset();
    }
    projection_.set_search(search_.data());

    if (ImGui::Button("Copy All"))
        set_clipboard_text(projection_.copy_all(console), feedback_);
    remember_item("copy-all");
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        console.clear();
        projection_.clear();
        selectedRow_.reset();
        projectedRevision_ = console.revision();
        feedback_ = "Console output cleared.";
    }
    remember_item("clear");
    ImGui::SameLine();
    ImGui::Checkbox("Collapse repeats", &collapseRepeats_);
    remember_item("collapse-repeats");
    if (ImGui::IsItemEdited())
    {
        projection_.set_collapse_repeats(collapseRepeats_);
        projection_.rebuild(console);
        projectedRevision_ = console.revision();
        if (selectedRow_ && *selectedRow_ >= projection_.rows().size())
            selectedRow_.reset();
    }
    ImGui::SameLine();
    bool follow = projection_.auto_follow();
    if (ImGui::Checkbox("Follow", &follow))
        projection_.set_auto_follow(follow);
    remember_item("follow");
    ImGui::SameLine();
    if (ImGui::Button("Copy selected") && selectedRow_)
        set_clipboard_text(projection_.copy_row(console, *selectedRow_), feedback_);
    remember_item("copy-selected");

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputText("Search", search_.data(), search_.size()))
        projection_.set_search(search_.data());
    remember_item("search");
    ImGui::SameLine();
    constexpr const char *filterNames[] = {"All severities", "Errors", "Warnings", "Info"};
    const bool severityOpen = ImGui::BeginCombo("Severity", filterNames[severityFilter_]);
    remember_item("severity");
    if (severityOpen)
    {
        for (int index = 0; index < 4; ++index)
        {
            const bool selected = severityFilter_ == index;
            if (ImGui::Selectable(filterNames[index], selected))
            {
                severityFilter_ = index;
                projection_.set_severity_filter(
                    index == 0 ? std::nullopt
                               : std::optional<DiagnosticSeverity>(
                                     index == 1 ? DiagnosticSeverity::Error
                                                : index == 2 ? DiagnosticSeverity::Warning
                                                             : DiagnosticSeverity::Info));
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (console.truncated())
    {
        ImGui::TextDisabled("Older output was removed at the 1 MiB retention limit. All retained bytes remain searchable and copyable.");
        remember_item("output");
    }
    else
    {
        ImGui::TextDisabled("%zu bytes retained (1 MiB limit).", console.text().size());
        remember_item("output");
    }
    if (projection_.omitted_rows() != 0)
        ImGui::TextDisabled("%zu additional rows omitted from the index; retained raw output is still available with Copy All.",
                            projection_.omitted_rows());

    const auto visibleRows = projection_.visible_rows(console.text());
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float listHeight = std::max(48.0f, availableHeight * 0.62f);
    const bool drawRows = ImGui::BeginChild("ConsoleRows", ImVec2(0.0f, listHeight),
                                            ImGuiChildFlags_Borders);
    bool wasAtBottom = true;
    if (drawRows)
    {
        const float previousScroll = ImGui::GetScrollY();
        const float previousMaximum = ImGui::GetScrollMaxY();
        wasAtBottom = previousScroll >= previousMaximum - 1.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleRows.size()));
        while (clipper.Step())
        {
            for (int displayIndex = clipper.DisplayStart; displayIndex < clipper.DisplayEnd;
                 ++displayIndex)
            {
                const std::size_t rowIndex = visibleRows[static_cast<std::size_t>(displayIndex)];
                const auto &row = projection_.rows()[rowIndex];
                const auto line = std::string_view(console.text()).substr(row.byteOffset,
                                                                         row.byteLength);
                std::string label = severity_name(row.severity);
                label.append("  ");
                label.append(line);
                if (row.repeatCount > 1)
                {
                    label.append("  (repeated ");
                    label.append(std::to_string(row.repeatCount));
                    label.push_back('x');
                    label.push_back(')');
                }
                label.append("###console-row-");
                label.append(std::to_string(rowIndex));
                if (ImGui::Selectable(label.c_str(), selectedRow_ == rowIndex,
                                      ImGuiSelectableFlags_AllowDoubleClick))
                    selectedRow_ = rowIndex;
                remember_item("row/" + std::to_string(rowIndex));
                drawnRows_.push_back(rowIndex);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    selectedRow_ = rowIndex;
            }
        }
        if (newOutput && projection_.auto_follow() && wasAtBottom)
            ImGui::SetScrollHereY(1.0f);
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f)
            projection_.user_scrolled_away();
        if (ImGui::GetScrollY() + 1.0f < ImGui::GetScrollMaxY() &&
            ImGui::GetScrollY() < previousScroll && ImGui::IsWindowHovered())
            projection_.user_scrolled_away();
    }
    ImGui::EndChild();

    if (!projection_.auto_follow())
    {
        if (ImGui::SmallButton("Return to bottom"))
            projection_.return_to_bottom();
        remember_item("return-to-bottom");
        ImGui::SameLine();
        ImGui::TextDisabled("Follow is paused while browsing older rows.");
    }

    if (selectedRow_ && *selectedRow_ < projection_.rows().size())
    {
        const auto &row = projection_.rows()[*selectedRow_];
        const auto line = std::string_view(console.text()).substr(row.byteOffset, row.byteLength);
        ImGui::Separator();
        ImGui::Text("%s%s", severity_name(row.severity),
                    row.repeatCount > 1 ? " (repeated)" : "");
        if (row.source)
        {
            const std::string path = utf8_path(row.source->path);
            ImGui::Text("Source: %s:%zu", path.c_str(), row.source->line);
            if (row.source->column)
                ImGui::SameLine(), ImGui::Text(":%zu", *row.source->column);
            const auto url = source_file_url(*row.source, status.workingDirectory);
            ImGui::SameLine();
            if (!url)
                ImGui::BeginDisabled();
            if (ImGui::SmallButton("Open source"))
            {
                if (!SDL_OpenURL(url->c_str()))
                    feedback_ = std::string("Could not open source file: ") + SDL_GetError();
                else
                    feedback_ = "Opened source file with the operating system association.";
            }
            remember_item("open-source");
            if (!url)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("Source file is not available.");
            }
        }
        ImGui::BeginChild("ConsoleDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line.data(), line.data() + line.size());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    if (!feedback_.empty())
        ImGui::TextDisabled("%s", feedback_.c_str());
}

void ConsolePanel::remember_item(std::string id)
{
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    itemBounds_.insert_or_assign(id, SemanticBounds{
        minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y});
    if (ImGui::IsItemFocused())
        focusedItem_ = std::move(id);
}

std::vector<SemanticNode> ConsolePanel::semantic_nodes(const PipelineStatus &status,
                                                        const BoundedConsole &console,
                                                        const std::string &parentId) const
{
    const auto make_node = [&](std::string suffix, SemanticRole role, std::string name) {
        SemanticNode node;
        node.id = parentId + "/" + suffix;
        node.parentId = parentId;
        node.role = role;
        node.name = std::move(name);
        const auto bounds = itemBounds_.find(suffix);
        if (bounds != itemBounds_.end())
            node.bounds = bounds->second;
        node.focused = focusedItem_ == suffix;
        return node;
    };
    const auto add_button = [&](std::vector<SemanticNode> &nodes, std::string suffix,
                                std::string name, bool enabled) {
        auto node = make_node(suffix, SemanticRole::Button, std::move(name));
        node.enabled = enabled;
        node.visible = true;
        node.actions = {"activate"};
        nodes.push_back(std::move(node));
    };

    std::vector<SemanticNode> nodes;
    nodes.reserve(12 + drawnRows_.size());
    auto root = make_node("", SemanticRole::Window, "Console");
    root.id = parentId;
    root.parentId.clear();
    root.visible = true;
    root.expanded = true;
    nodes.push_back(std::move(root));

    auto statusNode = make_node("status", SemanticRole::Text, "Build and game status");
    if (!statusNode.bounds)
    {
        const auto commandBounds = itemBounds_.find("command");
        if (commandBounds != itemBounds_.end())
            statusNode.bounds = commandBounds->second;
    }
    switch (status.stage)
    {
    case PipelineStage::Idle: statusNode.value = "Idle"; break;
    case PipelineStage::Configuring: statusNode.value = "Configuring"; break;
    case PipelineStage::Building: statusNode.value = "Building"; break;
    case PipelineStage::QueryingCatalog: statusNode.value = "Verifying behavior catalog"; break;
    case PipelineStage::Launching: statusNode.value = "Launching"; break;
    case PipelineStage::Running: statusNode.value = "Running"; break;
    }
    if (!status.message.empty())
    {
        statusNode.value += ": ";
        statusNode.value.append(status.message.substr(0, 512));
        if (status.message.size() > 512)
            statusNode.value += "...";
    }
    if (!status.command.empty())
    {
        statusNode.help = "Command: ";
        statusNode.help.append(status.command.substr(0, 512));
        if (status.command.size() > 512)
            statusNode.help += "...";
    }
    statusNode.visible = true;
    nodes.push_back(std::move(statusNode));

    auto outputNode = make_node("output", SemanticRole::Text, "Retained Console output");
    outputNode.value = std::to_string(console.text().size()) + " bytes retained";
    if (console.truncated())
        outputNode.value += "; older bytes were truncated";
    outputNode.help = "Copy All includes every retained byte, regardless of the current row filters.";
    outputNode.visible = true;
    nodes.push_back(std::move(outputNode));

    add_button(nodes, "copy-all", "Copy all retained Console output",
               !console.text().empty());
    add_button(nodes, "clear", "Clear Console", !console.text().empty());
    add_button(nodes, "copy-selected", "Copy selected Console row",
               selectedRow_ && *selectedRow_ < projection_.rows().size());

    auto collapse = make_node("collapse-repeats", SemanticRole::CheckBox,
                              "Collapse repeated Console rows");
    collapse.value = collapseRepeats_ ? "on" : "off";
    collapse.selected = collapseRepeats_;
    collapse.visible = true;
    collapse.actions = {"toggle"};
    nodes.push_back(std::move(collapse));

    auto follow = make_node("follow", SemanticRole::CheckBox, "Follow newest Console output");
    follow.value = projection_.auto_follow() ? "on" : "off";
    follow.selected = projection_.auto_follow();
    follow.visible = true;
    follow.actions = {"toggle"};
    nodes.push_back(std::move(follow));

    auto searchNode = make_node("search", SemanticRole::TextField, "Search Console output");
    searchNode.value = search_.data();
    searchNode.visible = true;
    searchNode.enabled = true;
    searchNode.actions = {"set_value"};
    nodes.push_back(std::move(searchNode));

    constexpr const char *filterNames[] = {"All severities", "Errors", "Warnings", "Info"};
    auto severity = make_node("severity", SemanticRole::Button, "Filter Console severity");
    severity.value = filterNames[std::clamp(severityFilter_, 0, 3)];
    severity.visible = true;
    severity.actions = {"activate"};
    nodes.push_back(std::move(severity));

    if (!projection_.auto_follow())
        add_button(nodes, "return-to-bottom", "Return to newest Console output", true);

    for (const std::size_t rowIndex : drawnRows_)
    {
        if (rowIndex >= projection_.rows().size())
            continue;
        const auto &row = projection_.rows()[rowIndex];
        const auto raw = std::string_view(console.text());
        if (row.byteOffset > raw.size() || row.byteLength > raw.size() - row.byteOffset)
            continue;
        auto node = make_node("row/" + std::to_string(rowIndex), SemanticRole::ListItem,
                              std::string(severity_name(row.severity)) + " Console row");
        constexpr std::size_t MaximumAccessibleRowBytes = 384;
        const auto text = raw.substr(row.byteOffset, row.byteLength);
        node.value.assign(text.substr(0, MaximumAccessibleRowBytes));
        if (text.size() > MaximumAccessibleRowBytes)
            node.value += "...";
        if (row.repeatCount > 1)
            node.help = "Repeated " + std::to_string(row.repeatCount) + " times.";
        if (row.source)
        {
            node.help += node.help.empty() ? "Source: " : " ";
            node.help += utf8_path(row.source->path) + ":" + std::to_string(row.source->line);
        }
        node.visible = true;
        node.selected = selectedRow_ == rowIndex;
        node.actions = {"select"};
        if (row.source && source_file_url(*row.source, status.workingDirectory))
            node.actions.push_back("open-source");
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool ConsolePanel::dispatch_semantic_action(const SemanticAction &action,
                                            const PipelineStatus &status,
                                            BoundedConsole &console)
{
    constexpr std::string_view prefix = "main/panel/console/";
    if (!action.nodeId.starts_with(prefix))
        return false;
    const std::string base(prefix);
    if (action.nodeId == base + "search" && action.action == "set_value")
    {
        if (action.value.size() >= search_.size())
            return false;
        search_.fill('\0');
        std::memcpy(search_.data(), action.value.data(), action.value.size());
        projection_.set_search(action.value);
        return true;
    }
    if (projectedRevision_ != console.revision())
    {
        projection_.set_collapse_repeats(collapseRepeats_);
        projection_.rebuild(console);
        projectedRevision_ = console.revision();
        if (selectedRow_ && *selectedRow_ >= projection_.rows().size())
            selectedRow_.reset();
        if (action.nodeId.starts_with(base + "row/") || action.nodeId == base + "copy-selected")
            return false;
    }
    if (action.action == "activate" && action.nodeId == base + "copy-all")
    {
        set_clipboard_text(projection_.copy_all(console), feedback_);
        return true;
    }
    if (action.action == "activate" && action.nodeId == base + "clear")
    {
        console.clear();
        projection_.clear();
        selectedRow_.reset();
        projectedRevision_ = console.revision();
        feedback_ = "Console output cleared.";
        return true;
    }
    if (action.action == "activate" && action.nodeId == base + "copy-selected")
    {
        if (!selectedRow_ || *selectedRow_ >= projection_.rows().size())
            return false;
        set_clipboard_text(projection_.copy_row(console, *selectedRow_), feedback_);
        return true;
    }
    if (action.nodeId == base + "collapse-repeats" && action.action == "toggle")
    {
        collapseRepeats_ = !collapseRepeats_;
        projection_.set_collapse_repeats(collapseRepeats_);
        projection_.rebuild(console);
        projectedRevision_ = console.revision();
        if (selectedRow_ && *selectedRow_ >= projection_.rows().size())
            selectedRow_.reset();
        return true;
    }
    if (action.nodeId == base + "follow" && action.action == "toggle")
    {
        if (projection_.auto_follow())
            projection_.user_scrolled_away();
        else
            projection_.return_to_bottom();
        return true;
    }
    if (action.nodeId == base + "return-to-bottom" && action.action == "activate")
    {
        projection_.return_to_bottom();
        return true;
    }
    if (action.nodeId == base + "severity" && action.action == "activate")
    {
        severityFilter_ = (severityFilter_ + 1) % 4;
        projection_.set_severity_filter(severityFilter_ == 0 ? std::nullopt
            : std::optional<DiagnosticSeverity>(severityFilter_ == 1 ? DiagnosticSeverity::Error
                : severityFilter_ == 2 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info));
        return true;
    }
    constexpr std::string_view rowPrefix = "row/";
    const auto rowMarker = base + std::string(rowPrefix);
    if (action.nodeId.starts_with(rowMarker))
    {
        const std::string_view indexText(action.nodeId.data() + rowMarker.size(),
                                         action.nodeId.size() - rowMarker.size());
        std::size_t rowIndex = 0;
        const auto parsed = std::from_chars(indexText.data(), indexText.data() + indexText.size(), rowIndex);
        if (parsed.ec != std::errc{} || parsed.ptr != indexText.data() + indexText.size() ||
            rowIndex >= projection_.rows().size())
            return false;
        if (action.action == "select")
        {
            selectedRow_ = rowIndex;
            return true;
        }
        if (action.action == "open-source")
        {
            const auto &row = projection_.rows()[rowIndex];
            if (!row.source)
                return false;
            const auto url = source_file_url(*row.source, status.workingDirectory);
            if (!url)
                return false;
            if (!SDL_OpenURL(url->c_str()))
                feedback_ = std::string("Could not open source file: ") + SDL_GetError();
            else
                feedback_ = "Opened source file with the operating system association.";
            selectedRow_ = rowIndex;
            return true;
        }
    }
    return false;
}
} // namespace platformator_editor

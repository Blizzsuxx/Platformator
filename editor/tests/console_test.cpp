#include "diagnostics.h"
#include "panels/console.h"
#include "test_support.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace platformator_editor;
using platformator_editor_test::require;

namespace
{
void test_copy_search_severity_selection_and_repeats()
{
    BoundedConsole raw;
    const std::string transcript =
        "src/scene.cpp:12:4: error: missing field\n"
        "src/scene.cpp:12:4: error: missing field\n"
        "src/scene.cpp:20: warning: old warning\n"
        "build complete\n";
    raw.append(transcript);

    ConsoleProjection view;
    view.rebuild(raw);
    require(view.copy_all(raw) == transcript,
            "Copy All must return exact retained bytes regardless of derived rows");
    require(view.rows().size() == 3 && view.rows()[0].repeatCount == 2,
            "adjacent duplicate messages must collapse into one row with a repeat count");
    require(view.copy_row(raw, 0) == "src/scene.cpp:12:4: error: missing field",
            "selecting a row must address its exact source text without changing raw output");

    view.set_search("MISSING FIELD");
    auto matches = view.visible_rows(raw.text());
    require(matches.size() == 1 && matches.front() == 0,
            "case-insensitive search must filter derived rows only");
    view.set_severity_filter(DiagnosticSeverity::Warning);
    require(view.visible_rows(raw.text()).empty(),
            "severity filtering must hide nonmatching rows without changing Copy All");
    view.set_severity_filter(std::nullopt);
    view.set_search("");
    view.set_collapse_repeats(false);
    view.rebuild(raw);
    require(view.rows().size() == 4 && view.rows()[0].repeatCount == 1,
            "disabling repeat collapse must expose each repeated raw line separately");
    require(view.copy_all(raw) == transcript,
            "search, filters and repeat collapse must never rewrite retained bytes");
}

void test_older_retained_rows_and_safe_source_locations()
{
    BoundedConsole raw;
    std::string transcript = "older actionable output\n";
    transcript.append(70U * 1024U, 'x');
    transcript.append("\nC:/project with spaces/source.cpp(17,3): error C1234: bad value\n");
    raw.append(transcript);

    ConsoleProjection view;
    view.rebuild(raw);
    view.set_search("older actionable");
    const auto older = view.visible_rows(raw.text());
    require(older.size() == 1 && view.copy_row(raw, older.front()) == "older actionable output",
            "older retained output must remain searchable and selectable outside the display tail");
    view.set_search("");
    const auto &rows = view.rows();
    bool foundMsvc = false;
    for (const auto &row : rows)
    {
        if (!row.source || row.source->line != 17 || row.source->column != 3)
            continue;
        foundMsvc = row.source->path == std::filesystem::path("C:/project with spaces/source.cpp");
    }
    require(foundMsvc, "MSVC source paths with spaces and drive colons must parse without shell handling");

    const auto gcc = parse_source_location("assets/src/main.cpp:42:8: warning: unused value");
    require(gcc && gcc->path == "assets/src/main.cpp" && gcc->line == 42 && gcc->column == 8,
            "GCC/Clang path:line:column locations must parse");
    const auto unicode = parse_source_location(
        "assets/caf\xC3\xA9 source/main.cpp:7:2: error: invalid");
    require(unicode && unicode->path == std::filesystem::path(u8"assets/café source/main.cpp") &&
                unicode->line == 7 && unicode->column == 2,
            "UTF-8 source paths containing spaces must remain actionable");
    const auto malformed = parse_source_location("-fplugin=$(touch sentinel):0: error: invalid");
    require(!malformed, "malformed line numbers and shell-like text must not become actionable paths");
    require(parse_source_location("C:/src/file.cpp(9): error C1: bad").has_value(),
            "MSVC single-line locations must parse");
}

void test_truncation_clear_and_auto_follow()
{
    BoundedConsole raw;
    std::string oversized(BoundedConsole::maximumBytes + 4096, 'o');
    raw.append(oversized);
    ConsoleProjection view;
    view.rebuild(raw);
    require(raw.truncated() && raw.text().size() <= BoundedConsole::maximumBytes &&
                raw.text().find("[console truncated at 1 MiB]") != std::string::npos,
            "retention truncation must stay bounded and explain which bytes were removed");
    require(view.copy_all(raw) == raw.text(),
            "Copy All must include every retained byte, including the truncation marker");
    require(view.auto_follow(), "auto-follow must begin enabled");
    view.user_scrolled_away();
    require(!view.auto_follow(), "scrolling away must stop automatic following");
    view.return_to_bottom();
    require(view.auto_follow(), "returning to the bottom may resume automatic following");

    raw.clear();
    view.clear();
    view.rebuild(raw);
    require(raw.text().empty() && !raw.truncated() && view.rows().empty() &&
                view.copy_all(raw).empty(),
            "Clear must remove raw retention and every derived row");
}

void test_console_revision_tracks_visible_mutations()
{
    BoundedConsole raw;
    const auto initial = raw.revision();
    raw.append("first");
    const auto appended = raw.revision();
    require(appended > initial, "appending output must invalidate a cached projection");
    raw.append("");
    require(raw.revision() == appended, "an empty append must not invalidate the projection");
    raw.clear();
    require(raw.revision() > appended, "clearing output must invalidate a cached projection");
}

void test_console_semantics_and_owner_actions()
{
    BoundedConsole raw;
    raw.append("src/main.cpp:7:2: error: broken value\nwarning: check input\nready\n");
    PipelineStatus status;
    status.stage = PipelineStage::Building;
    status.message = "Configure and build";
    ConsolePanel panel;

    ImGuiContext *context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    require(io.Fonts->Build(), "headless Console semantics require a built ImGui font atlas");
    ImGui::NewFrame();
    ImGui::Begin("Console semantic test");
    panel.draw(status, raw);
    auto nodes = panel.semantic_nodes(status, raw, "main/panel/console");
    ImGui::End();
    ImGui::Render();

    const auto find = [&](std::string_view id) -> const SemanticNode * {
        const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &candidate) {
            return candidate.id == id;
        });
        return node == nodes.end() ? nullptr : &*node;
    };
    require(find("main/panel/console/status") != nullptr &&
                find("main/panel/console/status")->value.starts_with("Building: Configure and build"),
            "Console semantics must report the current operation and status message");
    const auto *copy = find("main/panel/console/copy-all");
    const auto *search = find("main/panel/console/search");
    const auto *severity = find("main/panel/console/severity");
    const auto *row = find("main/panel/console/row/0");
    require(copy != nullptr && copy->enabled && search != nullptr &&
                search->role == SemanticRole::TextField &&
                search->actions == std::vector<std::string>{"set_value"} &&
                severity != nullptr && severity->value == "All severities" &&
                row != nullptr && row->selected == false && row->bounds.has_value() &&
                row->value.find("broken value") != std::string::npos,
            "visible Console controls and rows must expose roles, values, state, text and bounds");

    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/search", "set_value", "warning:"}, status, raw),
            "an accessible Console search field must accept a bounded text replacement");
    nodes = panel.semantic_nodes(status, raw, "main/panel/console");
    const auto searchAfterAction = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &candidate) {
        return candidate.id == "main/panel/console/search";
    });
    require(searchAfterAction != nodes.end() && searchAfterAction->value == "warning:" &&
                !panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                    "main/panel/console/search", "set_value", std::string(256, 'x')}, status, raw),
            "Console text actions update the search value and reject inputs beyond the visible field limit");

    const SemanticAction selectRow{
        HostLayoutId{1}, WindowHostGeneration{1}, 1,
        "main/panel/console/row/0", "select", {}};
    require(panel.dispatch_semantic_action(selectRow, status, raw),
            "a semantic Console row action must select a current projected row");
    nodes = panel.semantic_nodes(status, raw, "main/panel/console");
    const auto selected = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &candidate) {
        return candidate.id == selectRow.nodeId;
    });
    require(selected != nodes.end() && selected->selected,
            "the semantic Console snapshot must report the selected row");

    raw.append("new output arrived before dispatch\n");
    require(!panel.dispatch_semantic_action(selectRow, status, raw),
            "a row action captured before a Console revision change must be rejected");

    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/severity", "activate", {}}, status, raw) &&
                panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/severity", "activate", {}}, status, raw),
            "the semantic severity control must advance through its advertised choices");
    nodes = panel.semantic_nodes(status, raw, "main/panel/console");
    const auto selectedSeverity = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &candidate) {
        return candidate.id == "main/panel/console/severity";
    });
    require(selectedSeverity != nodes.end() && selectedSeverity->value == "Warnings",
            "the severity control must publish the active filter after an accessible action");
    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/clear", "activate", {}}, status, raw) && raw.text().empty(),
            "the semantic Clear action must clear retained output and its projection");
    ImGui::DestroyContext(context);
}

void test_independent_console_severity_badge_filtering()
{
    BoundedConsole raw;
    raw.append("info: engine initialized successfully\n"
               "src/main.cpp:10: warning: unused variable\n"
               "src/main.cpp:25: error: syntax error\n");

    ConsoleProjection view;
    view.rebuild(raw);
    require(view.rows().size() == 3, "projection must produce 3 rows");

    // Direct ConsoleProjection mask test
    view.set_severity_mask(true, true, true);
    require(view.visible_rows(raw.text()).size() == 3, "all 3 rows visible with full mask");

    view.set_severity_mask(true, false, false);
    auto visible = view.visible_rows(raw.text());
    require(visible.size() == 1 && view.rows()[visible[0]].severity == DiagnosticSeverity::Info,
            "only Info row visible when Warning and Error disabled");

    view.set_severity_mask(false, true, false);
    visible = view.visible_rows(raw.text());
    require(visible.size() == 1 && view.rows()[visible[0]].severity == DiagnosticSeverity::Warning,
            "only Warning row visible when Info and Error disabled");

    view.set_severity_mask(false, false, true);
    visible = view.visible_rows(raw.text());
    require(visible.size() == 1 && view.rows()[visible[0]].severity == DiagnosticSeverity::Error,
            "only Error row visible when Info and Warning disabled");

    view.set_severity_mask(false, true, true);
    visible = view.visible_rows(raw.text());
    require(visible.size() == 2, "Warning and Error visible when Info disabled");

    // ConsolePanel draw and semantic toggling test
    PipelineStatus status;
    status.stage = PipelineStage::Idle;
    ConsolePanel panel;

    ImGuiContext *context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    require(io.Fonts->Build(), "font atlas required");

    ImGui::NewFrame();
    ImGui::Begin("Console Badge Test");
    panel.draw(status, raw);
    ImGui::End();
    ImGui::Render();

    const auto &state = panel.filter_state();
    require(state.infoCount == 1, "info count must be 1");
    require(state.warningCount == 1, "warning count must be 1");
    require(state.errorCount == 1, "error count must be 1");
    require(state.showInfo && state.showWarnings && state.showErrors, "all badges initially active");

    // Toggle info badge off via semantic action
    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/console-badge-info", "toggle", {}}, status, raw),
            "toggling info badge must succeed");
    require(!panel.filter_state().showInfo, "info badge must be inactive after toggle");

    // Toggle error badge off
    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/console-badge-error", "toggle", {}}, status, raw),
            "toggling error badge must succeed");
    require(!panel.filter_state().showErrors, "error badge must be inactive after toggle");
    require(panel.filter_state().showWarnings, "warning badge must still be active");

    // Toggle info badge back on
    require(panel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                "main/panel/console/console-badge-info", "toggle", {}}, status, raw),
            "toggling info badge back on must succeed");
    require(panel.filter_state().showInfo, "info badge must be active again");

    ImGui::DestroyContext(context);
}

void test_console_row_severity_icons_and_colors()
{
    require(std::string(severity_icon(DiagnosticSeverity::Error)) == "[x]",
            "error severity icon must be [x]");
    require(std::string(severity_icon(DiagnosticSeverity::Warning)) == "[!]",
            "warning severity icon must be [!]");
    require(std::string(severity_icon(DiagnosticSeverity::Info)) == "[i]",
            "info severity icon must be [i]");

    const auto errColor = severity_color(DiagnosticSeverity::Error);
    require(errColor.x > errColor.y && errColor.x > errColor.z,
            "error color must be predominantly red");

    const auto warnColor = severity_color(DiagnosticSeverity::Warning);
    require(warnColor.x > warnColor.z && warnColor.y > warnColor.z,
            "warning color must be amber/yellow (red+green > blue)");

    const auto infoColor = severity_color(DiagnosticSeverity::Info);
    require(std::abs(infoColor.x - infoColor.y) < 0.1f && std::abs(infoColor.y - infoColor.z) < 0.1f,
            "info color must be a neutral light tone");

    // Verify row rendering in ConsolePanel
    BoundedConsole raw;
    raw.append("info message\nwarning message\nerror message\n");
    PipelineStatus status;
    status.stage = PipelineStage::Idle;
    ConsolePanel panel;

    ImGuiContext *context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    require(io.Fonts->Build(), "font atlas required");

    ImGui::NewFrame();
    ImGui::Begin("Console Row Test");
    panel.draw(status, raw);
    ImGui::End();
    ImGui::Render();

    ImGui::DestroyContext(context);
}
} // namespace

int main()
{
    test_copy_search_severity_selection_and_repeats();
    test_older_retained_rows_and_safe_source_locations();
    test_truncation_clear_and_auto_follow();
    test_console_revision_tracks_visible_mutations();
    test_console_semantics_and_owner_actions();
    test_independent_console_severity_badge_filtering();
    test_console_row_severity_icons_and_colors();
    return 0;
}

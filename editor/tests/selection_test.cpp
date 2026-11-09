#include "document.h"
#include "selection.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace platformator_editor;

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Json object(int id, Json children = Json::array())
{
    return Json{{"id", id}, {"name", "object-" + std::to_string(id)}, {"tag", ""},
                {"active", true}, {"position", {{"x", 0.0}, {"y", 0.0}}},
                {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                {"components", Json::array()}, {"children", std::move(children)}};
}

void ordered_multi_selection_repairs_and_normalizes_group_roots()
{
    DocumentSession document = make_default_document();
    document.root = Json::array({object(10, Json::array({object(11)})), object(20), object(30)});
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    Selection initial;
    initial.objectId = 11;
    initial.focusedPanel = FocusedPanel::Hierarchy;
    initial.focusedHost = WindowHostGeneration{7};
    selection.set(document, initial);
    selection.set_objects(document, {10, 11, 20}, 11, 10);
    require(selection.current().selectedObjectIds == std::vector<int>({10, 11, 20}) &&
                selection.current().objectId == 11 && selection.current().rangeAnchorObjectId == 10,
            "multi-selection must preserve ordered unique IDs, primary object and range anchor");
    require(selection.current().focusedPanel == FocusedPanel::Hierarchy &&
                selection.current().focusedHost == WindowHostGeneration{7},
            "changing selected objects must preserve focused host and panel context");
    require(selected_top_level_roots(document, selection.current()) == std::vector<int>({10, 20}),
            "group operations must omit a selected descendant when its ancestor is selected");

    selection.select_range(document, 30, {10, 11, 20, 30});
    require(selection.current().selectedObjectIds == std::vector<int>({10, 11, 20, 30}) &&
                selection.current().objectId == 30 && selection.current().rangeAnchorObjectId == 10,
            "range selection must include the anchor-to-target interval in displayed order");
    selection.toggle_object(document, 20);
    require(selection.current().selectedObjectIds == std::vector<int>({10, 11, 30}) &&
                selection.current().objectId == 30,
            "additive toggling must remove an existing member without losing selection order");

    document.root[0]["children"] = Json::array();
    ++document.structureRevision;
    rebuild_document_indexes(document);
    selection.repair(document);
    require(selection.current().selectedObjectIds == std::vector<int>({10, 30}) &&
                selection.current().objectId == 30 && selection.current().rangeAnchorObjectId == 10,
            "repair must remove deleted members and preserve surviving primary and anchor identities");
    require(selected_top_level_roots(document, selection.current()) == std::vector<int>({10, 30}),
            "group root normalization must retain every surviving top-level selected object");

    document.root.erase(document.root.begin());
    ++document.structureRevision;
    rebuild_document_indexes(document);
    selection.repair(document);
    require(selection.current().selectedObjectIds == std::vector<int>({30}) &&
                selection.current().objectId == 30 && selection.current().rangeAnchorObjectId == 30,
            "deleting the owning root must promote the first surviving member and repair the anchor");
    selection.toggle_object(document, 30);
    require(selection.current().selectedObjectIds.empty() && !selection.current().objectId,
            "removing the final selected member must clear the object selection");

    Selection stale;
    stale.objectId = 999;
    stale.selectedObjectIds = {999};
    stale.componentId = 1000;
    selection.set(document, stale);
    require(selection.current().selectedObjectIds.empty() && !selection.current().objectId &&
                !selection.current().componentId,
            "stale object, component and multi-selection IDs must not survive repair");
}
} // namespace

int main()
{
    try
    {
        ordered_multi_selection_repairs_and_normalizes_group_roots();
        std::cout << "[PASS] platformator_editor_selection_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_editor_selection_test: " << error.what() << '\n';
        return 1;
    }
}

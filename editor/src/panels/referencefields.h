#pragma once

#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "editcommands.h"
#include "selection.h"

namespace platformator_editor
{
class ResourceIndex;
enum class ReferenceState
{
    Unset,
    Resolved,
    Dangling,
    WrongKind,
    Invalid
};

struct SceneReferenceInfo
{
    ReferenceState state = ReferenceState::Invalid;
    std::optional<int> fileId;
    std::string label;
};

struct AssetReferenceInfo
{
    ReferenceState state = ReferenceState::Invalid;
    std::string path;
    std::string message;
};

using AssetBrowseCallback = std::function<void(PropertyAddress, std::string, bool)>;
using MultiAssetBrowseCallback =
    std::function<void(std::vector<PropertyAddress>, Selection, std::string, bool)>;

struct AssetCompletionResult
{
    bool applied = false;
    std::string message;
};

AssetCompletionResult apply_asset_browse_completion(
    DocumentSession &document, EditCommandService &commands,
    const std::vector<PropertyAddress> &targets,
    const Selection &originSelection,
    const DocumentIdentity &dialogIdentity,
    const std::filesystem::path &activeAssetRoot,
    const std::filesystem::path &selectedPath,
    std::string_view expectedAssetType,
    bool nullable);

std::size_t multi_asset_picker_scan_count_for_test() noexcept;

void draw_clipped_property_label(void *drawListPtr, float x, float y,
                                 float labelWidth, float frameHeight,
                                 const char *label, std::uint32_t color,
                                 bool hovered);

SceneReferenceInfo inspect_scene_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedTarget);
bool validate_scene_reference_assignment(const DocumentSession &document, const Json &value,
                                         std::string_view expectedTarget, bool nullable, std::string &error);
bool draw_template_reference_kind_control(int objectId, int componentId,
                                         std::size_t behaviorOrdinal,
                                         std::string_view controlId,
                                         std::optional<std::string> &declaredKind);
Json make_template_reference_binding_declaration(int ownerId, int componentId,
                                                 std::size_t behaviorOrdinal,
                                                 std::string_view fieldName,
                                                 std::string_view targetKind);
Json make_template_reference_path_declaration(int ownerId, int componentId,
                                              std::size_t behaviorOrdinal,
                                              std::string_view relativePointer,
                                              std::string_view targetKind);
AssetReferenceInfo inspect_asset_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedAssetType,
                                           ResourceIndex *resourceIndex = nullptr);
bool validate_asset_reference_assignment(const DocumentSession &document, const Json &value,
                                         std::string_view expectedAssetType,
                                         bool nullable, std::string &error);

void draw_scene_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, int componentId,
                                std::size_t behaviorOrdinal, const std::vector<FieldPathSegment> &path,
                                std::string_view expectedTarget, bool nullable, const char *label);
void draw_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, std::optional<int> componentId,
                                std::optional<std::size_t> behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedAssetType, bool nullable, const char *label,
                                const AssetBrowseCallback &browse = {}, Json clearValue = nullptr,
                                ResourceIndex *resourceIndex = nullptr);
void draw_multi_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                      const std::vector<PropertyAddress> &targets,
                                      const std::vector<Json> &values,
                                      const Selection &currentSelection,
                                      std::string_view expectedAssetType, bool nullable,
                                      const char *label,
                                      const AssetBrowseCallback &browse = {},
                                      Json clearValue = nullptr,
                                      ResourceIndex *resourceIndex = nullptr,
                                      const MultiAssetBrowseCallback &multiBrowse = {});
} // namespace platformator_editor

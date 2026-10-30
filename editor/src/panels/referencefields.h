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

SceneReferenceInfo inspect_scene_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedTarget);
bool validate_scene_reference_assignment(const DocumentSession &document, const Json &value,
                                         std::string_view expectedTarget, bool nullable, std::string &error);
AssetReferenceInfo inspect_asset_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedAssetType);
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
                                const AssetBrowseCallback &browse = {}, Json clearValue = nullptr);
} // namespace platformator_editor

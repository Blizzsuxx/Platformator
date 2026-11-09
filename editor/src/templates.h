#pragma once

#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "editcommands.h"

namespace platformator_editor
{
// IDs inside the template are local source identities; they never prove a destination match.
class TemplateAssetV1
{
  public:
    explicit TemplateAssetV1(Json envelope);

    const Json &root() const;
    const Json &reference_bindings() const;
    const Json &envelope() const noexcept;
    Json serialize() const;

  private:
    Json envelope_;
};

struct TemplateLoadResult
{
    std::optional<TemplateAssetV1> asset;
    std::string error;

    explicit operator bool() const noexcept { return asset.has_value(); }
};

struct TemplateExternalAssignment
{
    int ownerId = 0;
    std::optional<int> componentId;
    std::string path;
    int destinationTargetId = 0;
};

struct TemplateAuthorSchemaDeclaration
{
    int ownerId = 0;
    int componentId = 0;
    std::size_t behaviorOrdinal = 0;
    std::string behaviorType;
};

struct TemplateBindingCollection
{
    Json bindings = Json::array();
    Json validatedSubtree;
    std::vector<std::string> unresolvedBehaviors;
    std::string error;
    bool assured = false;

    explicit operator bool() const noexcept { return error.empty(); }
};

struct TemplateInsertResult
{
    CommandStatus status = CommandStatus::Rejected;
    std::string error;
    std::unordered_map<int, int> sourceToDestinationIds;
    std::vector<int> insertedObjectIds;

    explicit operator bool() const noexcept { return status == CommandStatus::Changed; }
};

Json make_template_envelope(std::string name, Json root, Json referenceBindings = Json::array());
TemplateLoadResult load_template_envelope(Json document);
TemplateLoadResult parse_template_envelope(std::string_view text);
TemplateBindingCollection template_asset_binding_coverage(const TemplateAssetV1 &asset);
std::string template_scene_identity(const DocumentSession &document);
TemplateBindingCollection collect_template_bindings(
    const DocumentSession &sourceDocument, const Json &subtree,
    std::string_view sourceSceneIdentity, const Json &catalog,
    const std::vector<Json> &authorBindings = {},
    const std::vector<TemplateAuthorSchemaDeclaration> &authorDeclaredBehaviors = {});
TemplateInsertResult insert_template(
    EditCommandService &commands, DocumentSession &destination,
    const TemplateAssetV1 &asset, std::string_view destinationSceneIdentity,
    const TemplateBindingCollection &bindingCoverage,
    std::optional<int> parentObjectId = std::nullopt,
    std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
    const std::vector<TemplateExternalAssignment> &externalAssignments = {},
    SelectionState selection = {});
} // namespace platformator_editor

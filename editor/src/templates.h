#pragma once

#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "assetformats.h"
#include "editcommands.h"

namespace platformator_editor
{
class SelectionModel;
inline constexpr std::string_view EditorTemplateBindingsKey = "_editorTemplateReferenceBindings";
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
void strip_nested_template_metadata(Json &subtree);
const Json *find_document_object_by_id(const DocumentSession &document, int objectId);
std::optional<int> find_template_instance_root(const DocumentSession &document, int objectId);
bool is_template_instance_root(const Json &object) noexcept;
bool is_template_node(const Json &object) noexcept;
bool is_property_overridden(const Json &object, std::string_view fieldPath);
void mark_template_override(DocumentSession &document, int objectId, std::string_view fieldPath);
std::string template_override_path_for_address(const DocumentSession &document,
                                              const PropertyAddress &address);
std::filesystem::path resolve_template_asset_path(const std::filesystem::path &assetRoot,
                                                  std::string_view templateRef);
std::string normalize_template_asset_ref(const std::filesystem::path &assetRoot,
                                         const std::filesystem::path &fullPath);
bool apply_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                             int instanceRootId, const std::filesystem::path &assetRoot,
                             std::string &errorOut, const Json *catalog = nullptr);
bool revert_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, const std::filesystem::path &assetRoot,
                              std::string &errorOut);
bool relink_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, const std::filesystem::path &assetRoot,
                              const std::filesystem::path &candidateTemplate,
                              std::string &errorOut);
bool revert_template_property(DocumentSession &sceneDocument, EditCommandService &commands,
                              int objectId, std::optional<int> componentId,
                              std::optional<std::size_t> behaviorOrdinal,
                              const std::vector<FieldPathSegment> &fieldPath,
                              const std::filesystem::path &assetRoot,
                              std::string &errorOut);
struct TemplatePropertyTarget
{
    int objectId = -1;
    std::optional<int> componentId;
    std::optional<std::size_t> behaviorOrdinal;
    std::vector<FieldPathSegment> fieldPath;
};
bool revert_template_properties(DocumentSession &sceneDocument, EditCommandService &commands,
                                const std::vector<TemplatePropertyTarget> &targets,
                                const std::filesystem::path &assetRoot,
                                std::string &errorOut);
bool unpack_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, std::string &errorOut);
bool replace_template_document_root(DocumentSession &document, EditCommandService &commands,
                                    const Json &newRoot, std::string &errorOut);
std::size_t propagate_template_to_scene(DocumentSession &sceneDocument, EditCommandService &commands,
                                        std::string_view templateRef, const TemplateAssetV1 &asset);
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
    SelectionState selection = {},
    std::string_view linkedTemplatePath = {},
    std::optional<std::pair<float, float>> rootPosition = std::nullopt);

bool instantiate_asset_drop(DocumentSession &document, EditCommandService &commands,
                            SelectionModel &selection, const std::string &assetPath,
                            EditorAssetKind kind, float targetX = 0.0f, float targetY = 0.0f,
                            std::optional<int> parentObjectId = std::nullopt);
} // namespace platformator_editor

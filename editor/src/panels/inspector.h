#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editcommands.h"
#include "../accessibility.h"
#include "../layersettings.h"
#include "behaviorfields.h"
#include "selection.h"
#include "spriteeditor.h"

namespace platformator_editor
{
std::vector<std::pair<int, int>> paired_collider_layer_targets(
    const std::vector<int> &objectIds, const std::vector<int> &componentIds);

class InspectorPanel
{
  public:
    struct PreparedTextureInfo
    {
        SDL_Texture *texture = nullptr;
        int sourceWidth = 0;
        int sourceHeight = 0;
    };
    using TexturePreparer = std::function<PreparedTextureInfo(const std::filesystem::path &)>;
    using SelectTemplateAssetCallback = std::function<void(const std::filesystem::path &)>;
    using SelectedTemplateAssetCallback = std::function<std::optional<std::filesystem::path>()>;

    void draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
              const BehaviorCatalog &catalog, const AssetBrowseCallback &browse = {},
              ResourceIndex *resourceIndex = nullptr);
    void reveal_field(int objectId, std::optional<int> componentId, std::string fieldPath);
    void set_host_generation(std::uint64_t generation, DocumentSession *document = nullptr) noexcept;
    void cancel_active_interactions(DocumentSession *document = nullptr);
    const std::string &feedback() const noexcept;
    const std::string &search_query() const noexcept { return searchQuery_; }
    void set_search_query(std::string query) { searchQuery_ = std::move(query); }
    void set_open_animation_callback(std::function<void(const std::filesystem::path &)> callback)
    {
        openAnimationCallback_ = std::move(callback);
    }
    void set_multi_asset_browse_callback(MultiAssetBrowseCallback callback)
    {
        multiAssetBrowseCallback_ = std::move(callback);
    }
    void set_texture_preparer(TexturePreparer callback)
    {
        texturePreparer_ = std::move(callback);
    }
    void set_layer_settings(const LayerSettings *layerSettings) noexcept
    {
        layerSettings_ = layerSettings;
    }
    void set_open_template_callback(std::function<void(const std::filesystem::path &)> callback)
    {
        openTemplateCallback_ = std::move(callback);
    }
    void set_select_template_asset_callback(SelectTemplateAssetCallback callback)
    {
        selectTemplateAssetCallback_ = std::move(callback);
    }
    void set_selected_template_asset_callback(SelectedTemplateAssetCallback callback)
    {
        selectedTemplateAssetCallback_ = std::move(callback);
    }
    void set_browse_template_asset_callback(std::function<void(int)> callback)
    {
        browseTemplateAssetCallback_ = std::move(callback);
    }
    void set_open_layer_settings_callback(std::function<void()> callback)
    {
        openLayerSettingsCallback_ = std::move(callback);
    }
    void set_template_propagated_callback(std::function<void(const std::filesystem::path &)> callback)
    {
        templatePropagatedCallback_ = std::move(callback);
    }
    const SpriteSourceRegionWidget &sprite_region_widget() const noexcept
    {
        return spriteRegionWidget_;
    }
    std::vector<SemanticNode> semantic_nodes(const DocumentSession &document,
                                             const Selection &selection,
                                             std::string_view scope,
                                             const BehaviorCatalog *catalog = nullptr);
    bool dispatch_semantic_action(DocumentSession &document, SelectionModel &selection,
                                  const SemanticAction &action, std::string_view scope,
                                  const BehaviorCatalog *catalog = nullptr,
                                  EditCommandService *commands = nullptr);

  private:
    bool draw_boolean(DocumentSession &document, EditCommandService &commands, int objectId,
                      std::optional<int> componentId, std::vector<FieldPathSegment> path,
                      const char *label, bool defaultValue = false);
    bool draw_number(DocumentSession &document, EditCommandService &commands, int objectId,
                     std::optional<int> componentId, std::vector<FieldPathSegment> path,
                     const char *label, float defaultValue = 0.0f,
                     std::optional<float> minimum = std::nullopt,
                     std::optional<float> maximum = std::nullopt);
    bool draw_integer(DocumentSession &document, EditCommandService &commands, int objectId,
                      std::optional<int> componentId, std::vector<FieldPathSegment> path,
                      const char *label, int defaultValue = 0,
                      std::optional<int> minimum = std::nullopt,
                      std::optional<int> maximum = std::nullopt);
    bool draw_string(DocumentSession &document, EditCommandService &commands, int objectId,
                     std::optional<int> componentId, std::vector<FieldPathSegment> path,
                     const char *label, std::string defaultValue = {});
    bool draw_enum(DocumentSession &document, EditCommandService &commands, int objectId,
                   std::optional<int> componentId, std::vector<FieldPathSegment> path,
                   const char *label, int defaultValue,
                   const std::vector<std::pair<int, const char *>> &values);
    bool draw_compact_vector2(DocumentSession &document, EditCommandService &commands,
                              int objectId, std::optional<int> componentId,
                              std::vector<FieldPathSegment> basePath, const char *label,
                              float defaultX = 0.0f, float defaultY = 0.0f,
                              std::optional<float> minimum = std::nullopt,
                              std::optional<float> maximum = std::nullopt,
                              float baseStep = 0.1f, float snapIncrement = 0.5f);
    bool draw_compact_rotation(DocumentSession &document, EditCommandService &commands,
                               int objectId, std::optional<int> componentId,
                               std::vector<FieldPathSegment> path, const char *label,
                               float defaultValue = 0.0f,
                               float baseStep = 1.0f, float snapIncrement = 15.0f);
    void draw_common_compact_transform(DocumentSession &document, SelectionModel &selection,
                                       EditCommandService &commands,
                                       const std::vector<int> &objectIds);
    bool set(DocumentSession &document, EditCommandService &commands, int objectId,
             std::optional<int> componentId, std::vector<FieldPathSegment> path, Json value);
    bool field_matches_search(const DocumentSession &document, int objectId,
                              std::optional<int> componentId,
                              const std::vector<FieldPathSegment> &path,
                              std::string_view label) const;
    void draw_search_bar();
    void draw_multi_object_fields(DocumentSession &document, SelectionModel &selection,
                                  EditCommandService &commands, const BehaviorCatalog &catalog,
                                  const std::vector<int> &objectIds,
                                  const AssetBrowseCallback &browse = {});
    void save_selected_template(DocumentSession &document, int objectId,
                                const BehaviorCatalog &catalog);
    bool consume_reveal_target(int objectId, std::optional<int> componentId,
                               const std::vector<FieldPathSegment> &path);

    std::string feedback_;
    std::string searchQuery_;
    std::string templateName_;
    std::string templateNameSelectionKey_;
    std::string addPickerQuery_;
    std::string lastSelectionKey_;
    std::string lastObjectSelectionKey_;
    std::vector<int> currentOrderedSelection_;
    std::optional<Json> componentSettingsClipboard_;
    std::optional<Json> transformSettingsClipboard_;
    std::uint64_t hostGeneration_ = 0;
    const LayerSettings *layerSettings_ = nullptr;
    std::optional<int> revealObjectId_;
    std::optional<int> revealComponentId_;
    std::string revealPath_;
    bool behaviorRevealPending_ = false;
    struct IntegerDraft
    {
        int value = 0;
        bool wasActive = false;
    };
    std::unordered_map<std::uint32_t, IntegerDraft> integerDrafts_;
    struct NumericScrubState
    {
        bool active = false;
        bool moved = false;
        bool seenThisFrame = false;
        std::uint32_t widgetId = 0;
        double accumDelta = 0.0;
        std::uint64_t projectGeneration = 0;
        std::uint64_t tabId = 0;
        std::uint64_t documentGeneration = 0;
        std::uint64_t hostGeneration = 0;
        std::vector<int> orderedSelection;
        std::vector<FieldPathSegment> path;
        int objectId = 0;
        std::optional<int> componentId;
        Json originalSingleJson;
        bool singleHadValue = false;
        std::vector<int> targetObjectIds;
        std::vector<std::optional<int>> targetComponentIds;
        std::vector<Json> originalMultiJson;
        double baseStep = 0.1;
        double snapIncrement = 0.5;
        std::optional<double> minimum;
        std::optional<double> maximum;
    };
    NumericScrubState scrubState_;
    BehaviorFieldsPanel behaviorFields_;
    ResourceIndex *resourceIndex_ = nullptr;
    enum class SemanticOperation
    {
        Reveal,
        SelectComponent,
        SelectBehavior,
        CopySettings,
        PasteSettings,
        ResetSettings,
        RemoveComponent,
        SaveTemplate,
        EditClip
    };
    struct SemanticTarget
    {
        int objectId = 0;
        std::optional<int> componentId;
        std::optional<std::size_t> behaviorOrdinal;
        std::string fieldPath;
        SemanticOperation operation = SemanticOperation::Reveal;
    };
    std::unordered_map<std::string, SemanticTarget> semanticTargets_;
    std::optional<std::string> semanticFocusNode_;
    std::function<void(const std::filesystem::path &)> openAnimationCallback_;
    std::function<void(const std::filesystem::path &)> openTemplateCallback_;
    SelectTemplateAssetCallback selectTemplateAssetCallback_;
    SelectedTemplateAssetCallback selectedTemplateAssetCallback_;
    std::function<void(int)> browseTemplateAssetCallback_;
    std::function<void(const std::filesystem::path &)> templatePropagatedCallback_;
    std::function<void()> openLayerSettingsCallback_;
    bool applyColliderLayerToDescendants_ = false;
    MultiAssetBrowseCallback multiAssetBrowseCallback_;
    TexturePreparer texturePreparer_;
    SpriteSourceRegionWidget spriteRegionWidget_;
    PreparedTextureInfo prepare_sprite_texture(const DocumentSession &document,
                                               std::string_view textureRef);
};
} // namespace platformator_editor

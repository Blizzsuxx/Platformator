#include "behaviorfields.h"
#include "../style.h"

#include "numericscrub.h"
#include "referencefields.h"
#include "../discovery.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

namespace platformator_editor
{
namespace
{
const Json *resolve_object(const Json &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    const Json *object = nullptr;
    const Json *siblings = &root;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (depth > 0)
        {
            if (object == nullptr)
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

const Json *behavior_owner(const DocumentSession &document, int objectId, int componentId,
                           std::size_t ordinal)
{
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    const auto componentLocation = document.indexes.componentsById.find(componentId);
    if (objectLocation == document.indexes.objectsById.end() || componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return nullptr;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return nullptr;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() || componentLocation->second.componentIndex >= components->size())
        return nullptr;
    const auto &component = (*components)[componentLocation->second.componentIndex];
    const auto behaviors = component.find("behaviors");
    return behaviors != component.end() && behaviors->is_array() && ordinal < behaviors->size()
               ? &(*behaviors)[ordinal]
               : nullptr;
}

const Json *read_path(const Json *value, const std::vector<FieldPathSegment> &path)
{
    for (const auto &segment : path)
    {
        if (value == nullptr)
            return nullptr;
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!value->is_object())
                return nullptr;
            const auto found = value->find(*key);
            value = found == value->end() ? nullptr : &*found;
        }
        else
        {
            const auto index = std::get<std::size_t>(segment);
            if (!value->is_array() || index >= value->size())
                return nullptr;
            value = &(*value)[index];
        }
    }
    return value;
}

Json *mutable_behavior_path(DocumentSession &document, int objectId, int componentId,
                            std::size_t ordinal, const std::vector<FieldPathSegment> &path)
{
    return const_cast<Json *>(read_path(behavior_owner(document, objectId, componentId, ordinal), path));
}

std::vector<FieldPathSegment> child_path(const std::vector<FieldPathSegment> &path,
                                         std::string key)
{
    auto result = path;
    result.emplace_back(std::move(key));
    return result;
}

std::vector<FieldPathSegment> child_path(const std::vector<FieldPathSegment> &path,
                                         std::size_t index)
{
    auto result = path;
    result.emplace_back(index);
    return result;
}

std::string stable_field_id(const std::vector<FieldPathSegment> &path)
{
    std::string result;
    for (const auto &segment : path)
    {
        result.push_back('/');
        if (const auto *key = std::get_if<std::string>(&segment))
            result += *key;
        else
            result += std::to_string(std::get<std::size_t>(segment));
    }
    return result;
}

struct BehaviorOverrideContext
{
    DocumentSession *document = nullptr;
    EditCommandService *commands = nullptr;
    std::string *feedback = nullptr;
    std::vector<std::pair<int, int>> targets;
    std::size_t ordinal = 0;
};

thread_local const BehaviorOverrideContext *activeOverrideContext = nullptr;

class BehaviorOverrideContextScope
{
  public:
    BehaviorOverrideContextScope(DocumentSession &document, EditCommandService &commands,
                                 std::string &feedback, int objectId, int componentId,
                                 std::size_t ordinal)
        : context_{&document, &commands, &feedback, {{objectId, componentId}}, ordinal},
          previous_(activeOverrideContext)
    {
        activeOverrideContext = &context_;
    }

    BehaviorOverrideContextScope(DocumentSession &document, EditCommandService &commands,
                                 std::string &feedback,
                                 std::vector<std::pair<int, int>> targets,
                                 std::size_t ordinal)
        : context_{&document, &commands, &feedback, std::move(targets), ordinal},
          previous_(activeOverrideContext)
    {
        activeOverrideContext = &context_;
    }
    ~BehaviorOverrideContextScope() { activeOverrideContext = previous_; }

  private:
    BehaviorOverrideContext context_;
    const BehaviorOverrideContext *previous_ = nullptr;
};

bool active_behavior_field_is_overridden(const std::vector<FieldPathSegment> &path)
{
    if (activeOverrideContext == nullptr || activeOverrideContext->document == nullptr)
        return false;
    const auto &context = *activeOverrideContext;
    for (const auto &[objectId, componentId] : context.targets)
    {
        const Json *object = find_document_object_by_id(*context.document, objectId);
        if (object == nullptr || !is_template_node(*object))
            continue;
        const auto address = address_for_document(*context.document, objectId,
            componentId, context.ordinal, path);
        const std::string key = template_override_path_for_address(*context.document, address);
        const auto overrides = object->find("overrides");
        if (key.empty() || overrides == object->end() || !overrides->is_array())
            continue;
        for (const auto &entry : *overrides)
        {
            if (!entry.is_string())
                continue;
            const std::string value = entry.get<std::string>();
            if (value == key || value.starts_with(key + ".") || value.starts_with(key + "[") ||
                key.starts_with(value + ".") || key.starts_with(value + "["))
                return true;
        }
    }
    return false;
}

ReferenceFieldOverride active_behavior_reference_override(
    const std::vector<FieldPathSegment> &path)
{
    ReferenceFieldOverride result;
    result.overridden = active_behavior_field_is_overridden(path);
    if (!result.overridden || activeOverrideContext == nullptr)
        return result;

    const BehaviorOverrideContext context = *activeOverrideContext;
    result.revert = [context, path]() {
        if (context.document == nullptr || context.commands == nullptr)
            return;
        std::vector<TemplatePropertyTarget> targets;
        targets.reserve(context.targets.size());
        for (const auto &[objectId, componentId] : context.targets)
        {
            const Json *object = find_document_object_by_id(*context.document, objectId);
            if (object == nullptr || !is_template_node(*object))
                continue;
            const auto address = address_for_document(*context.document, objectId,
                componentId, context.ordinal, path);
            const std::string key = template_override_path_for_address(*context.document, address);
            const auto overrides = object->find("overrides");
            if (key.empty() || overrides == object->end() || !overrides->is_array())
                continue;
            const bool overridden = std::any_of(overrides->begin(), overrides->end(),
                [&](const Json &entry) {
                    if (!entry.is_string())
                        return false;
                    const std::string value = entry.get<std::string>();
                    return value == key || value.starts_with(key + ".") ||
                        value.starts_with(key + "[") || key.starts_with(value + ".") ||
                        key.starts_with(value + "[");
                });
            if (overridden)
                targets.push_back({objectId, componentId, context.ordinal, path});
        }
        if (targets.empty())
            return;
        std::string error;
        if (revert_template_properties(*context.document, *context.commands, targets,
                                       context.document->assetRoot, error))
        {
            if (context.feedback != nullptr)
                context.feedback->clear();
        }
        else if (context.feedback != nullptr)
            *context.feedback = std::move(error);
    };
    return result;
}

void draw_active_behavior_revert_context(const std::vector<FieldPathSegment> &path)
{
    if (!active_behavior_field_is_overridden(path) || activeOverrideContext == nullptr ||
        activeOverrideContext->document == nullptr || activeOverrideContext->commands == nullptr)
        return;
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::BeginPopupContextItem("##behavior-template-field-context"))
    {
        if (ImGui::MenuItem("Revert Property"))
        {
            const auto overrideInfo = active_behavior_reference_override(path);
            if (overrideInfo.revert)
                overrideInfo.revert();
        }
        ImGui::EndPopup();
    }
    if (hovered)
        ImGui::SetItemTooltip("This field overrides its linked template value.");
}

struct LabelScrubInteraction
{
    ImGuiID id = 0;
    bool activated = false;
    bool active = false;
    bool deactivated = false;
};

bool draw_clipped_tree_node(const char *id, const char *label)
{
    const float availWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const bool open = ImGui::TreeNodeEx(
        id, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap, "");
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float textStartX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
    const float textWidth = std::max(1.0f, itemMin.x + availWidth - textStartX);
    draw_clipped_property_label(
        ImGui::GetWindowDrawList(), textStartX, itemMin.y,
        textWidth, std::max(1.0f, itemMax.y - itemMin.y), label,
        ImGui::GetColorU32(ImGuiCol_Text), ImGui::IsItemHovered());
    return open;
}

LabelScrubInteraction begin_field(const std::string &label, const std::vector<FieldPathSegment> &path,
                                  bool allowScrub = false)
{
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    ImGui::AlignTextToFramePadding();
    const float available = ImGui::GetContentRegionAvail().x;
    LabelScrubInteraction interaction;
    const bool hasLabel = !label.empty();
    if (hasLabel)
    {
        const float labelWidth = std::min(124.0f * uiScale, available * 0.42f);
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const float frameHeight = ImGui::GetFrameHeight();
        const std::string scrubId = "##scrub_" + stable_field_id(path);
        ImGui::InvisibleButton(scrubId.c_str(), ImVec2(labelWidth, frameHeight));
        interaction = {
            ImGui::GetItemID(),
            ImGui::IsItemActivated(),
            ImGui::IsItemActive(),
            ImGui::IsItemDeactivated(),
        };
        const bool labelHovered = ImGui::IsItemHovered();
        const bool overridden = active_behavior_field_is_overridden(path);
        if (allowScrub && (labelHovered || interaction.active))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        draw_clipped_property_label(
            ImGui::GetWindowDrawList(), cursorPos.x, cursorPos.y, labelWidth, frameHeight,
            label.c_str(),
            ImGui::GetColorU32((allowScrub && (labelHovered || interaction.active))
                                   ? ImGuiCol_Text
                                   : (overridden ? ImGuiCol_NavHighlight : ImGuiCol_TextDisabled)),
            labelHovered);
        ImGui::PushID(stable_field_id(path).c_str());
        draw_active_behavior_revert_context(path);
        ImGui::PopID();
        ImGui::SameLine();
    }

    ImGui::PushID(stable_field_id(path).c_str());
    const bool overridden = active_behavior_field_is_overridden(path);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, overridden
        ? ImVec4(0.28f, 0.25f, 0.16f, 1.0f)
        : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, overridden
        ? ImVec4(0.34f, 0.30f, 0.18f, 1.0f)
        : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, overridden
        ? ImVec4(0.40f, 0.35f, 0.20f, 1.0f)
        : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImGui::SetNextItemWidth(hasLabel ? -FLT_MIN : std::max(1.0f, ImGui::GetContentRegionAvail().x));
    return interaction;
}

struct ScrubContext
{
    BehaviorFieldsPanel::ScrubState &scrub;
    DocumentSession &document;
    const std::vector<int> &orderedSelection;
    std::uint64_t hostGeneration = 0;
};

void restore_scrub_targets(DocumentSession &document, const BehaviorFieldsPanel::ScrubState &scrub)
{
    if (!scrub.active ||
        document.projectGeneration != scrub.projectGeneration ||
        document.tabId.value != scrub.tabId ||
        document.documentGeneration != scrub.documentGeneration)
    {
        return;
    }
    for (const auto &target : scrub.targets)
    {
        if (!target.hadValue)
            continue;
        if (Json *mut = mutable_behavior_path(document, target.objectId, target.componentId,
                                              target.behaviorOrdinal, target.path))
        {
            *mut = target.originalJson;
            ++document.valueRevision;
        }
    }
}

template <typename CaptureTargetsFn, typename ApplyPreviewFn, typename CommitFn>
void apply_label_scrub(const ScrubContext &ctx,
                       const LabelScrubInteraction &interaction,
                       double baseStep,
                       double snapIncrement,
                       std::optional<double> minDelta,
                       std::optional<double> maxDelta,
                       CaptureTargetsFn &&captureTargets,
                       ApplyPreviewFn &&applyPreview,
                       CommitFn &&commit)
{
    auto &scrub = ctx.scrub;
    if (interaction.activated)
    {
        if (scrub.active)
            restore_scrub_targets(ctx.document, scrub);
        scrub = {};
        scrub.active = true;
        scrub.seenThisFrame = true;
        scrub.widgetId = interaction.id;
        scrub.projectGeneration = ctx.document.projectGeneration;
        scrub.tabId = ctx.document.tabId.value;
        scrub.documentGeneration = ctx.document.documentGeneration;
        scrub.hostGeneration = ctx.hostGeneration;
        scrub.orderedSelection = ctx.orderedSelection;
        scrub.targets = captureTargets();
    }
    else if (interaction.active && scrub.active && scrub.widgetId == interaction.id)
    {
        scrub.seenThisFrame = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            restore_scrub_targets(ctx.document, scrub);
            scrub = {};
        }
        else
        {
            const ImGuiIO &io = ImGui::GetIO();
            const float deltaX = io.MouseDelta.x;
            if (deltaX != 0.0f)
            {
                const double mult = io.KeyShift ? 10.0 : (io.KeyAlt ? 0.1 : 1.0);
                double nextDelta = scrub.accumDelta + static_cast<double>(deltaX) * baseStep * mult;
                if (io.KeyCtrl && snapIncrement > 0.0)
                    nextDelta = std::round(nextDelta / snapIncrement) * snapIncrement;
                if (minDelta && nextDelta < *minDelta)
                    nextDelta = *minDelta;
                if (maxDelta && nextDelta > *maxDelta)
                    nextDelta = *maxDelta;
                scrub.accumDelta = nextDelta;
                if (scrub.accumDelta != 0.0)
                    scrub.moved = true;
                if (scrub.moved)
                    applyPreview(scrub.accumDelta);
            }
        }
    }
    else if (interaction.deactivated && scrub.active && scrub.widgetId == interaction.id)
    {
        scrub.seenThisFrame = true;
        restore_scrub_targets(ctx.document, scrub);
        if (scrub.moved && scrub.accumDelta != 0.0)
            commit(scrub.accumDelta);
        scrub = {};
    }
}

void end_field()
{
    ImGui::PopStyleColor(3);
    ImGui::PopID();
}

class BehaviorAddressScope
{
  public:
    BehaviorAddressScope(const DocumentSession &document, int objectId, int componentId,
                         std::size_t behaviorOrdinal)
        : projectGeneration_(std::to_string(document.projectGeneration)),
          tabId_(std::to_string(document.tabId.value)),
          documentGeneration_(std::to_string(document.documentGeneration)),
          objectId_(std::to_string(objectId)), componentId_(std::to_string(componentId)),
          behaviorOrdinal_(std::to_string(behaviorOrdinal))
    {
        ImGui::PushID(projectGeneration_.c_str());
        ImGui::PushID(tabId_.c_str());
        ImGui::PushID(documentGeneration_.c_str());
        ImGui::PushID(objectId_.c_str());
        ImGui::PushID(componentId_.c_str());
        ImGui::PushID(behaviorOrdinal_.c_str());
    }

    ~BehaviorAddressScope()
    {
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }

  private:
    std::string projectGeneration_;
    std::string tabId_;
    std::string documentGeneration_;
    std::string objectId_;
    std::string componentId_;
    std::string behaviorOrdinal_;
};

CommandResult set_value(DocumentSession &document, EditCommandService &commands, int objectId,
                        int componentId, std::size_t ordinal,
                        const std::vector<FieldPathSegment> &path, Json value)
{
    return commands.set_property(document,
                                 address_for_document(document, objectId, componentId, ordinal, path),
                                 std::move(value));
}

std::optional<std::int64_t> signed_bound(const Json &descriptor, const char *key)
{
    const auto found = descriptor.find(key);
    if (found == descriptor.end() || !found->is_string())
        return std::nullopt;
    std::int64_t value = 0;
    const auto &text = found->get_ref<const std::string &>();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? std::optional<std::int64_t>(value)
               : std::nullopt;
}

std::optional<std::uint64_t> unsigned_bound(const Json &descriptor, const char *key)
{
    const auto found = descriptor.find(key);
    if (found == descriptor.end() || !found->is_string())
        return std::nullopt;
    std::uint64_t value = 0;
    const auto &text = found->get_ref<const std::string &>();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? std::optional<std::uint64_t>(value)
               : std::nullopt;
}

std::int64_t json_signed(const Json *value, std::int64_t fallback = 0)
{
    if (value == nullptr || !value->is_number_integer())
        return fallback;
    try
    {
        return value->get<std::int64_t>();
    }
    catch (...)
    {
        return fallback;
    }
}

std::uint64_t json_unsigned(const Json *value, std::uint64_t fallback = 0)
{
    if (value == nullptr || !value->is_number_integer())
        return fallback;
    try
    {
        if (value->is_number_unsigned())
            return value->get<std::uint64_t>();
        const auto parsed = value->get<std::int64_t>();
        return parsed >= 0 ? static_cast<std::uint64_t>(parsed) : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

int json_enum(const Json *value)
{
    const auto parsed = json_signed(value, std::numeric_limits<std::int64_t>::min());
    return parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()
               ? 0
               : static_cast<int>(parsed);
}

Json default_value(const Json &descriptor)
{
    const std::string kind = descriptor.value("kind", "opaque_json");
    if (kind == "boolean")
        return false;
    if (kind == "signed_integer")
        return 0;
    if (kind == "unsigned_integer")
        return 0U;
    if (kind == "number")
        return 0.0;
    if (kind == "string")
        return "";
    if (kind == "enum")
    {
        const auto values = descriptor.find("enum_values");
        const int value = values != descriptor.end() && values->is_array() && !values->empty()
                              ? values->front().value("value", 0)
                              : 0;
        return Json(value);
    }
    if (kind == "vector2")
        return Json{{"x", 0.0}, {"y", 0.0}};
    if (kind == "rectangle")
        return Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}};
    if (kind == "array")
        return Json::array();
    if (kind == "optional" || kind == "asset_reference" || kind == "object_reference")
        return nullptr;
    return Json::object();
}

bool draw_raw_value(DocumentSession &document, EditCommandService &commands, int objectId,
                    int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                    const std::string &label, const ScrubContext &scrubCtx);

bool draw_typed_value(DocumentSession &document, EditCommandService &commands, int objectId,
                      int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                      const Json &descriptor, const std::string &label,
                      const AssetBrowseCallback &browse, ResourceIndex *resourceIndex,
                      const ScrubContext &scrubCtx);

bool draw_vector(DocumentSession &document, EditCommandService &commands, int objectId,
                 int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                 const Json *current, const std::vector<std::string> &names, const std::string &label,
                 const ScrubContext &scrubCtx)
{
    bool changed = false;
    Json draft = current != nullptr && current->is_object() ? *current : Json::object();
    begin_field(label, path);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float columnSpacing = ImGui::GetStyle().ItemSpacing.x;
    const std::size_t columns = names.size() == 4 ? 2 : names.size();
    const float axisWidth = std::max(1.0f,
        (availableWidth - columnSpacing * static_cast<float>(columns - 1)) /
            static_cast<float>(columns));
    const ImVec2 controlsStart = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    for (std::size_t axisIndex = 0; axisIndex < names.size(); ++axisIndex)
    {
        const auto &name = names[axisIndex];
        if (axisIndex > 0)
        {
            if (columns == 2 && axisIndex == 2)
                ImGui::SetCursorScreenPos(ImVec2(controlsStart.x,
                    controlsStart.y + rowHeight + ImGui::GetStyle().ItemSpacing.y));
            else if (axisIndex % columns != 0)
                ImGui::SameLine();
        }
        ImGui::BeginGroup();
        float value = 0.0f;
        const auto old = draft.find(name);
        if (old != draft.end() && old->is_number())
        {
            try
            {
                const float parsed = old->get<float>();
                value = std::isfinite(parsed) ? parsed : 0.0f;
            }
            catch (...)
            {
                value = 0.0f;
            }
        }
        const float originalValue = value;
        ImGui::PushID(name.c_str());
        const auto valuePath = child_path(path, name);
        const char *axisLabel = name == "x" ? "X" : name == "y" ? "Y" : name == "w" ? "Width" : "Height";
        const ImVec4 color = name == "x" ? kAxisXColor
            : name == "y" ? kAxisYColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec2 labelPos = ImGui::GetCursorScreenPos();
        const float axisLabelWidth = ImGui::CalcTextSize(axisLabel).x;
        ImGui::InvisibleButton(("##scrub_" + stable_field_id(valuePath)).c_str(), ImVec2(axisLabelWidth, ImGui::GetFrameHeight()));
        const LabelScrubInteraction scrubIx{ImGui::GetItemID(), ImGui::IsItemActivated(), ImGui::IsItemActive(), ImGui::IsItemDeactivated()};
        ImGui::GetWindowDrawList()->AddText(ImVec2(labelPos.x, labelPos.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(color), axisLabel);
        if (ImGui::IsItemHovered() || scrubIx.active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::PushID(stable_field_id(valuePath).c_str());
        draw_active_behavior_revert_context(valuePath);
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(stable_field_id(valuePath).c_str());
        const float axisInputWidth = std::max(1.0f,
            axisWidth - axisLabelWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(axisInputWidth);
        apply_label_scrub(
            scrubCtx, scrubIx, 0.1, 0.5, std::nullopt, std::nullopt,
            [&]() {
                const Json *curVal = read_path(behavior_owner(document, objectId, componentId, ordinal), valuePath);
                return std::vector<BehaviorFieldsPanel::ScrubTarget>{{
                    objectId, componentId, ordinal, valuePath,
                    curVal != nullptr ? *curVal : Json(static_cast<double>(originalValue)),
                    curVal != nullptr}};
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (Json *mut = mutable_behavior_path(document, objectId, componentId, ordinal, valuePath))
                {
                    *mut = nextJson;
                    ++document.valueRevision;
                }
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (!nextJson.is_number() || !std::isfinite(nextJson.get<double>()))
                    return;
                draft[name] = nextJson;
                if (current == nullptr || !current->is_object())
                    set_value(document, commands, objectId, componentId, ordinal, path, draft);
                else
                    set_value(document, commands, objectId, componentId, ordinal, valuePath, nextJson);
                changed = true;
            });
        if (scrubCtx.scrub.active && scrubCtx.scrub.widgetId == scrubIx.id && scrubCtx.scrub.moved && !scrubCtx.scrub.targets.empty())
            value = static_cast<float>(apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, scrubCtx.scrub.accumDelta).get<double>());
        const ImGuiID valueId = ImGui::GetID("##value");
        const bool valueActive = ImGui::GetActiveID() == valueId;
        const bool useFullPrecision = valueActive || ImGui::GetCurrentContext()->DeactivatedItemData.ID == valueId;
        const char *formatStr = useFullPrecision ? "%.9g" : "%.2f";
        const bool overridden = active_behavior_field_is_overridden(valuePath);
        if (overridden)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.25f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.34f, 0.30f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.40f, 0.35f, 0.20f, 1.0f));
        }
        const bool valueChanged = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, formatStr,
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
        if (overridden)
            ImGui::PopStyleColor(3);
        draw_active_behavior_revert_context(valuePath);
        if (ImGui::IsItemActivated())
        {
            value = originalValue;
            if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetItemID()))
                state->ReloadUserBufAndSelectAll();
        }
        const bool cancel = valueActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !ImGui::IsItemActivated() && !cancel && (valueChanged || ImGui::IsItemDeactivatedAfterEdit()) && value != originalValue;
        if (commit && std::isfinite(value))
        {
            draft[name] = value;
            if (current == nullptr || !current->is_object())
                set_value(document, commands, objectId, componentId, ordinal, path, draft);
            else
                set_value(document, commands, objectId, componentId, ordinal, valuePath, value);
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopID();
        ImGui::EndGroup();
    }
    end_field();
    return changed;
}

bool draw_raw_value(DocumentSession &document, EditCommandService &commands, int objectId,
                    int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                    const std::string &label, const ScrubContext &scrubCtx)
{
    const Json *current = read_path(behavior_owner(document, objectId, componentId, ordinal), path);
    if (current == nullptr)
    {
        ImGui::TextDisabled("%s is missing.", label.c_str());
        return false;
    }
    if (current->is_boolean())
    {
        bool value = current->get<bool>();
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##value", &value);
        draw_active_behavior_revert_context(path);
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_integer())
    {
        begin_field(label, path);
        ImGui::TextDisabled("Field type unavailable");
        end_field();
        return false;
    }
    if (current->is_number_float())
    {
        double value = current->get<double>();
        const double originalValue = value;
        const auto scrubIx = begin_field(label, path, true);
        apply_label_scrub(
            scrubCtx, scrubIx, 0.1, 0.5, std::nullopt, std::nullopt,
            [&]() {
                return std::vector<BehaviorFieldsPanel::ScrubTarget>{{
                    objectId, componentId, ordinal, path, *current, true}};
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (Json *mut = mutable_behavior_path(document, objectId, componentId, ordinal, path))
                {
                    *mut = nextJson;
                    ++document.valueRevision;
                }
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (nextJson.is_number() && std::isfinite(nextJson.get<double>()) &&
                    nextJson != scrubCtx.scrub.targets.front().originalJson)
                    set_value(document, commands, objectId, componentId, ordinal, path, nextJson);
            });
        if (scrubCtx.scrub.active && scrubCtx.scrub.widgetId == scrubIx.id && scrubCtx.scrub.moved && !scrubCtx.scrub.targets.empty())
            value = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, scrubCtx.scrub.accumDelta).get<double>();
        const ImGuiID valueId = ImGui::GetID("##value");
        const bool valueActive = ImGui::GetActiveID() == valueId;
        const bool useFullPrecision = valueActive || ImGui::GetCurrentContext()->DeactivatedItemData.ID == valueId;
        const char *formatStr = useFullPrecision ? "%.17g" : "%.2f";
        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, formatStr,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        draw_active_behavior_revert_context(path);
        if (ImGui::IsItemActivated())
        {
            value = originalValue;
            if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetItemID()))
                state->ReloadUserBufAndSelectAll();
        }
        const bool cancel = valueActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !ImGui::IsItemActivated() && !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()) && value != originalValue;
        end_field();
        if (commit && std::isfinite(value))
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_string())
    {
        const std::string value = current->get<std::string>();
        if (value.size() >= 2048)
        {
            ImGui::TextDisabled("%s is too long for inline editing.", label.c_str());
            return true;
        }
        std::array<char, 2048> buffer{};
        std::memcpy(buffer.data(), value.data(), value.size());
        begin_field(label, path);
        const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        draw_active_behavior_revert_context(path);
        const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, std::string(buffer.data()));
        return true;
    }
    if (current->is_null())
    {
        ImGui::Text("%s: null", label.c_str());
        return true;
    }
    if (current->is_object())
    {
        Json objectCopy = *current;
        const std::string treeId = label + "###object";
        if (draw_clipped_tree_node(treeId.c_str(), label.c_str()))
        {
            for (auto it = objectCopy.begin(); it != objectCopy.end(); ++it)
            {
                ImGui::PushID(it.key().c_str());
                const auto nestedPath = child_path(path, it.key());
                draw_raw_value(document, commands, objectId, componentId, ordinal, nestedPath, it.key(), scrubCtx);
                ImGui::NewLine();
                if (ImGui::SmallButton("Remove"))
                    commands.erase_property(document,
                                            address_for_document(document, objectId, componentId,
                                                                 ordinal, nestedPath));
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        return true;
    }
    if (current->is_array())
    {
        Json arrayCopy = *current;
        ImGui::Text("%s (%zu items)", label.c_str(), arrayCopy.size());
        for (std::size_t index = 0; index < arrayCopy.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            draw_raw_value(document, commands, objectId, componentId, ordinal,
                           child_path(path, index), "Item " + std::to_string(index), scrubCtx);
            ImGui::NewLine();
            if (ImGui::SmallButton("Remove"))
            {
                arrayCopy.erase(arrayCopy.begin() + static_cast<Json::difference_type>(index));
                set_value(document, commands, objectId, componentId, ordinal, path, std::move(arrayCopy));
                ImGui::PopID();
                return true;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton(("Add item##" + label).c_str()))
        {
            arrayCopy.push_back(nullptr);
            set_value(document, commands, objectId, componentId, ordinal, path, std::move(arrayCopy));
        }
        return true;
    }
    return false;
}

bool draw_typed_value(DocumentSession &document, EditCommandService &commands, int objectId,
                      int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                      const Json &descriptor, const std::string &label,
                      const AssetBrowseCallback &browse, ResourceIndex *resourceIndex,
                      const ScrubContext &scrubCtx)
{
    const Json *current = read_path(behavior_owner(document, objectId, componentId, ordinal), path);
    const std::string kind = descriptor.value("kind", "opaque_json");
    if (kind == "boolean")
    {
        bool value = current != nullptr && current->is_boolean() ? current->get<bool>() : false;
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##value", &value);
        draw_active_behavior_revert_context(path);
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (kind == "signed_integer" || kind == "unsigned_integer")
    {
        const bool isUnsigned = kind == "unsigned_integer";
        const auto scrubIx = begin_field(label, path, true);
        if (isUnsigned)
        {
            std::uint64_t value = json_unsigned(current);
            const std::uint64_t originalValue = value;
            auto minimum = unsigned_bound(descriptor, "minimum").value_or(0);
            auto maximum = unsigned_bound(descriptor, "maximum");
            std::uint64_t limit = maximum.value_or(std::numeric_limits<std::uint64_t>::max());
            const auto minOpt = std::optional<double>(static_cast<double>(minimum));
            const auto maxOpt = maximum ? std::optional<double>(static_cast<double>(*maximum)) : std::nullopt;
            apply_label_scrub(
                scrubCtx, scrubIx, 1.0, 1.0, std::nullopt, std::nullopt,
                [&]() {
                    return std::vector<BehaviorFieldsPanel::ScrubTarget>{{
                        objectId, componentId, ordinal, path,
                        current != nullptr ? *current : Json(originalValue), current != nullptr}};
                },
                [&](double delta) {
                    if (scrubCtx.scrub.targets.empty())
                        return;
                    const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta, minOpt, maxOpt);
                    if (Json *mut = mutable_behavior_path(document, objectId, componentId, ordinal, path))
                    {
                        *mut = nextJson;
                        ++document.valueRevision;
                    }
                },
                [&](double delta) {
                    if (scrubCtx.scrub.targets.empty())
                        return;
                    const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta, minOpt, maxOpt);
                    if (nextJson != scrubCtx.scrub.targets.front().originalJson)
                        set_value(document, commands, objectId, componentId, ordinal, path, nextJson);
                });
            if (scrubCtx.scrub.active && scrubCtx.scrub.widgetId == scrubIx.id && scrubCtx.scrub.moved && !scrubCtx.scrub.targets.empty())
                value = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, scrubCtx.scrub.accumDelta, minOpt, maxOpt).get<std::uint64_t>();
            const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value, &minimum, &limit,
                "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
            draw_active_behavior_revert_context(path);
            const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (!cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        else
        {
            std::int64_t value = json_signed(current);
            const std::int64_t originalValue = value;
            auto minimum = signed_bound(descriptor, "minimum").value_or(std::numeric_limits<std::int64_t>::min());
            auto maximum = signed_bound(descriptor, "maximum").value_or(std::numeric_limits<std::int64_t>::max());
            const auto minOpt = signed_bound(descriptor, "minimum") ? std::optional<double>(static_cast<double>(minimum)) : std::nullopt;
            const auto maxOpt = signed_bound(descriptor, "maximum") ? std::optional<double>(static_cast<double>(maximum)) : std::nullopt;
            apply_label_scrub(
                scrubCtx, scrubIx, 1.0, 1.0, std::nullopt, std::nullopt,
                [&]() {
                    return std::vector<BehaviorFieldsPanel::ScrubTarget>{{
                        objectId, componentId, ordinal, path,
                        current != nullptr ? *current : Json(originalValue), current != nullptr}};
                },
                [&](double delta) {
                    if (scrubCtx.scrub.targets.empty())
                        return;
                    const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta, minOpt, maxOpt);
                    if (Json *mut = mutable_behavior_path(document, objectId, componentId, ordinal, path))
                    {
                        *mut = nextJson;
                        ++document.valueRevision;
                    }
                },
                [&](double delta) {
                    if (scrubCtx.scrub.targets.empty())
                        return;
                    const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta, minOpt, maxOpt);
                    if (nextJson != scrubCtx.scrub.targets.front().originalJson)
                        set_value(document, commands, objectId, componentId, ordinal, path, nextJson);
                });
            if (scrubCtx.scrub.active && scrubCtx.scrub.widgetId == scrubIx.id && scrubCtx.scrub.moved && !scrubCtx.scrub.targets.empty())
                value = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, scrubCtx.scrub.accumDelta, minOpt, maxOpt).get<std::int64_t>();
            const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value, &minimum, &maximum,
                "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
            draw_active_behavior_revert_context(path);
            const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (!cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        end_field();
        return true;
    }
    if (kind == "number")
    {
        double value = current != nullptr && current->is_number() ? current->get<double>() : 0.0;
        const double originalValue = value;
        const auto scrubIx = begin_field(label, path, true);
        apply_label_scrub(
            scrubCtx, scrubIx, 0.1, 0.5, std::nullopt, std::nullopt,
            [&]() {
                return std::vector<BehaviorFieldsPanel::ScrubTarget>{{
                    objectId, componentId, ordinal, path,
                    current != nullptr ? *current : Json(originalValue), current != nullptr}};
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (Json *mut = mutable_behavior_path(document, objectId, componentId, ordinal, path))
                {
                    *mut = nextJson;
                    ++document.valueRevision;
                }
            },
            [&](double delta) {
                if (scrubCtx.scrub.targets.empty())
                    return;
                const Json nextJson = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, delta);
                if (nextJson.is_number() && std::isfinite(nextJson.get<double>()) &&
                    nextJson != scrubCtx.scrub.targets.front().originalJson)
                    set_value(document, commands, objectId, componentId, ordinal, path, nextJson);
            });
        if (scrubCtx.scrub.active && scrubCtx.scrub.widgetId == scrubIx.id && scrubCtx.scrub.moved && !scrubCtx.scrub.targets.empty())
            value = apply_numeric_delta(scrubCtx.scrub.targets.front().originalJson, scrubCtx.scrub.accumDelta).get<double>();
        const ImGuiID valueId = ImGui::GetID("##value");
        const bool valueActive = ImGui::GetActiveID() == valueId;
        const bool useFullPrecision = valueActive || ImGui::GetCurrentContext()->DeactivatedItemData.ID == valueId;
        const char *formatStr = useFullPrecision ? "%.17g" : "%.2f";
        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, formatStr,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        draw_active_behavior_revert_context(path);
        if (ImGui::IsItemActivated())
        {
            value = originalValue;
            if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetItemID()))
                state->ReloadUserBufAndSelectAll();
        }
        const bool cancel = valueActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !ImGui::IsItemActivated() && !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()) &&
                            (current == nullptr || value != originalValue);
        end_field();
        if (commit && std::isfinite(value))
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (kind == "string")
    {
        const std::string old = current != nullptr && current->is_string() ? current->get<std::string>() : std::string{};
        if (old.size() >= 2048)
        {
            ImGui::TextDisabled("%s is too long for inline editing.", label.c_str());
            return true;
        }
        std::array<char, 2048> buffer{};
        std::memcpy(buffer.data(), old.data(), old.size());
        begin_field(label, path);
        const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        draw_active_behavior_revert_context(path);
        const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, std::string(buffer.data()));
        return true;
    }
    if (kind == "enum")
    {
        const auto enumValues = descriptor.find("enum_values");
        const int currentValue = json_enum(current);
        std::string preview = "Unknown value";
        if (enumValues != descriptor.end() && enumValues->is_array())
            for (const auto &enumValue : *enumValues)
                if (enumValue.value("value", 0) == currentValue)
                    preview = enumValue.value("name", std::string("Unknown value"));
        begin_field(label, path);
        if (ImGui::BeginCombo("##value", preview.c_str()))
        {
            if (enumValues != descriptor.end() && enumValues->is_array())
            {
                for (const auto &enumValue : *enumValues)
                {
                    const int value = enumValue.value("value", 0);
                    const auto name = enumValue.value("name", std::string("Unknown"));
                    if (ImGui::Selectable(name.c_str(), currentValue == value))
                        set_value(document, commands, objectId, componentId, ordinal, path, value);
                }
            }
            ImGui::EndCombo();
        }
        draw_active_behavior_revert_context(path);
        end_field();
        return true;
    }
    if (kind == "vector2")
        return draw_vector(document, commands, objectId, componentId, ordinal, path, current,
                           {"x", "y"}, label, scrubCtx);
    if (kind == "rectangle")
        return draw_vector(document, commands, objectId, componentId, ordinal, path, current,
                           {"x", "y", "w", "h"}, label, scrubCtx);
    if (kind == "asset_reference")
    {
        draw_asset_reference_field(document, commands, objectId, std::optional<int>(componentId),
                                   std::optional<std::size_t>(ordinal), path,
                                   descriptor.value("target", std::string("Asset")),
                                   descriptor.value("nullable", true), label.c_str(), browse,
                                   nullptr, resourceIndex,
                                   active_behavior_reference_override(path));
        return true;
    }
    if (kind == "object_reference")
    {
        draw_scene_reference_field(document, commands, objectId, componentId, ordinal, path,
                                   descriptor.value("target", std::string("GameObject")),
                                   descriptor.value("nullable", true), label.c_str(),
                                   active_behavior_reference_override(path));
        return true;
    }
    if (kind == "optional")
    {
        const bool hasValue = current != nullptr && !current->is_null();
        bool enabled = hasValue;
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##enabled", &enabled);
        const bool optionalHovered = ImGui::IsItemHovered();
        draw_active_behavior_revert_context(path);
        if (optionalHovered)
            ImGui::SetItemTooltip(enabled ? "Optional value is set."
                : (current != nullptr ? "Optional value is explicitly null."
                                      : "No value saved for this field yet."));
        end_field();
        if (changed)
        {
            if (!enabled)
                set_value(document, commands, objectId, componentId, ordinal, path, nullptr);
            else if (!hasValue)
            {
                const auto element = descriptor.find("element");
                set_value(document, commands, objectId, componentId, ordinal, path,
                          element != descriptor.end() ? default_value(*element) : Json(nullptr));
            }
        }
        if (enabled)
        {
            const auto element = descriptor.find("element");
            if (element != descriptor.end())
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 path, *element, std::string{}, browse, resourceIndex, scrubCtx);
        }
        return true;
    }
    if (kind == "array")
    {
        Json array = current != nullptr && current->is_array() ? *current : Json::array();
        ImGui::Text("%s (%zu items)", label.c_str(), array.size());
        const auto element = descriptor.find("element");
        if (element != descriptor.end())
        {
            for (std::size_t index = 0; index < array.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 child_path(path, index), *element, "Element " + std::to_string(index), browse,
                                 resourceIndex, scrubCtx);
                ImGui::NewLine();
                if (ImGui::SmallButton("Remove"))
                {
                    array.erase(array.begin() + static_cast<Json::difference_type>(index));
                    set_value(document, commands, objectId, componentId, ordinal, path, std::move(array));
                    ImGui::PopID();
                    return true;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton(("Add element##" + label).c_str()))
            {
                array.push_back(default_value(*element));
                set_value(document, commands, objectId, componentId, ordinal, path, std::move(array));
            }
        }
        return true;
    }
    if (kind == "opaque_json")
    {
        if (current == nullptr)
        {
            if (ImGui::SmallButton(("Create JSON object##" + label).c_str()))
                set_value(document, commands, objectId, componentId, ordinal, path, Json::object());
            ImGui::SameLine();
            ImGui::TextDisabled("No value");
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("No value saved for this field yet.");
            return true;
        }
        return draw_raw_value(document, commands, objectId, componentId, ordinal, path, label, scrubCtx);
    }
    return false;
}

const Json *find_behavior_descriptor(const Json &catalog, std::string_view name)
{
    const auto behaviors = catalog.find("behaviors");
    if (behaviors == catalog.end() || !behaviors->is_array())
        return nullptr;
    for (const auto &behavior : *behaviors)
        if (behavior.is_object() && behavior.value("name", "") == name)
            return &behavior;
    return nullptr;
}

std::optional<int> script_component_at_occurrence(const DocumentSession &document, int objectId,
                                                  std::size_t occurrence)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return std::nullopt;
    const Json *object = resolve_object(document.root, location->second);
    if (object == nullptr)
        return std::nullopt;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array())
        return std::nullopt;
    std::size_t current = 0;
    for (const auto &component : *components)
    {
        if (!component.is_object() || component.value("type", -1) != 5)
            continue;
        if (current++ != occurrence)
            continue;
        const int componentId = json_signed(component.contains("id") ? &component["id"] : nullptr, -1);
        return componentId >= 0 ? std::optional<int>(componentId) : std::nullopt;
    }
    return std::nullopt;
}

bool compatible_behavior_value(const Json *value, std::string_view kind)
{
    if (value == nullptr)
        return false;
    if (kind == "boolean")
        return value->is_boolean();
    if (kind == "number")
    {
        if (!value->is_number())
            return false;
        try
        {
            return std::isfinite(value->get<double>());
        }
        catch (...)
        {
            return false;
        }
    }
    if (kind == "signed_integer")
        return value->is_number_integer() && !value->is_number_unsigned();
    if (kind == "unsigned_integer")
    {
        if (value->is_number_unsigned())
            return true;
        return value->is_number_integer() && json_signed(value, -1) >= 0;
    }
    if (kind == "enum")
        return value->is_number_integer();
    return false;
}

std::string object_search_context(const DocumentSession &document, int objectId)
{
    std::string context = " #" + std::to_string(objectId);
    int currentId = objectId;
    while (true)
    {
        const auto location = document.indexes.objectsById.find(currentId);
        if (location == document.indexes.objectsById.end())
            break;
        const Json *object = resolve_object(document.root, location->second);
        if (object == nullptr)
            break;
        for (const char *key : {"name", "tag"})
        {
            const auto value = object->find(key);
            if (value != object->end() && value->is_string())
                context += " " + value->get<std::string>();
        }
        const auto parent = document.indexes.parentById.find(currentId);
        if (parent == document.indexes.parentById.end() || !parent->second)
            break;
        currentId = *parent->second;
    }
    return context;
}

struct MultiBehaviorAddress
{
    int objectId = 0;
    int componentId = 0;
    std::size_t ordinal = 0;
};

} // namespace

void BehaviorFieldsPanel::set_host_generation(std::uint64_t generation, DocumentSession *document) noexcept
{
    if (hostGeneration_ == generation)
        return;
    cancel_active_interactions(document);
    hostGeneration_ = generation;
}

void BehaviorFieldsPanel::cancel_active_interactions(DocumentSession *document)
{
    if (!scrubState_.active)
        return;
    if (document != nullptr)
        restore_scrub_targets(*document, scrubState_);
    scrubState_ = {};
}

void BehaviorFieldsPanel::reveal_field(int objectId, int componentId,
                                      std::size_t behaviorOrdinal, std::string fieldPath)
{
    revealObjectId_ = objectId;
    revealComponentId_ = componentId;
    revealBehaviorOrdinal_ = behaviorOrdinal;
    revealPath_ = std::move(fieldPath);
}

bool BehaviorFieldsPanel::consume_reveal_target(
    int objectId, int componentId, std::size_t behaviorOrdinal,
    const std::vector<FieldPathSegment> &path)
{
    if (!revealObjectId_ || *revealObjectId_ != objectId ||
        revealComponentId_ != componentId || revealBehaviorOrdinal_ != behaviorOrdinal ||
        revealPath_.empty())
        return false;
    const std::string currentPath = stable_field_id(path);
    const bool related = currentPath == revealPath_ ||
                         currentPath.starts_with(revealPath_ + "/") ||
                         revealPath_.starts_with(currentPath + "/");
    if (!related)
        return false;
    revealObjectId_.reset();
    revealComponentId_.reset();
    revealBehaviorOrdinal_.reset();
    revealPath_.clear();
    return true;
}

void BehaviorFieldsPanel::draw(DocumentSession &document, SelectionModel &selection,
                               EditCommandService &commands, const BehaviorCatalog &catalog,
                               const AssetBrowseCallback &browse, ResourceIndex *resourceIndex,
                               std::string_view searchQuery)
{
    selection.repair(document);
    const DocumentIdentity currentIdentity{
        ProjectGeneration{document.projectGeneration}, document.tabId,
        DocumentGeneration{document.documentGeneration}};
    const auto &currentSelection = selection.current();
    if (feedbackDocument_ && (*feedbackDocument_ != currentIdentity ||
        feedbackSelection_ != currentSelection ||
        feedbackObjectIds_ != currentSelection.selectedObjectIds ||
        feedbackRevision_ != document.valueRevision))
        feedback_.clear();
    feedbackDocument_ = currentIdentity;
    feedbackSelection_ = currentSelection;
    feedbackObjectIds_ = currentSelection.selectedObjectIds;
    feedbackRevision_ = document.valueRevision;
    if (scrubState_.active &&
        (document.projectGeneration != scrubState_.projectGeneration ||
         document.tabId.value != scrubState_.tabId ||
         document.documentGeneration != scrubState_.documentGeneration ||
         hostGeneration_ != scrubState_.hostGeneration ||
         selection.current().selectedObjectIds != scrubState_.orderedSelection))
    {
        cancel_active_interactions(&document);
    }
    scrubState_.seenThisFrame = false;
    const ScrubContext scrubCtx{scrubState_, document, selection.current().selectedObjectIds, hostGeneration_};
    if (!selection.current().objectId || !selection.current().componentId)
    {
        cancel_active_interactions(&document);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextDisabled("Select a Script component to view behavior fields.");
        ImGui::PopStyleColor();
        return;
    }
    const int objectId = *selection.current().objectId;
    const int componentId = *selection.current().componentId;
    const auto componentLocation = document.indexes.componentsById.find(componentId);
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (componentLocation == document.indexes.componentsById.end() || objectLocation == document.indexes.objectsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return;
    const Json &component = (*components)[componentLocation->second.componentIndex];
    if (component.value("type", -1) != 5)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextDisabled("Select a Script component to view behavior fields.");
        ImGui::PopStyleColor();
        return;
    }
    const auto behaviorList = component.find("behaviors");
    if (behaviorList == component.end() || !behaviorList->is_array())
        return;
    if (revealObjectId_ &&
        (*revealObjectId_ != objectId || revealComponentId_ != componentId ||
         !revealBehaviorOrdinal_ || *revealBehaviorOrdinal_ >= behaviorList->size()))
    {
        revealObjectId_.reset();
        revealComponentId_.reset();
        revealBehaviorOrdinal_.reset();
        revealPath_.clear();
    }
    if (revealObjectId_ && !behaviorList->empty())
        selection.set(document, Selection{objectId, componentId, *revealBehaviorOrdinal_,
                                          FocusedPanel::Behaviors});
    if (!behaviorList->empty() &&
        (!selection.current().behaviorOrdinal ||
         *selection.current().behaviorOrdinal >= behaviorList->size()))
        selection.set(document, Selection{objectId, componentId, 0, FocusedPanel::Inspector});
    ImGui::TextUnformatted("Behaviors");
    for (std::size_t index = 0; index < behaviorList->size(); ++index)
    {
        const auto &behavior = (*behaviorList)[index];
        const std::string name = behavior.is_object() ? behavior.value("type", std::string("Unknown")) : "Malformed";
        ImGui::PushID(static_cast<int>(index));
        const std::string itemLabel = std::to_string(index) + ": " + name;
        const float itemWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float itemHeight = ImGui::GetFrameHeight();
        const ImVec2 itemPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##behavior_item",
                              selection.current().behaviorOrdinal == index,
                              0,
                              ImVec2(itemWidth, itemHeight)))
            selection.set(document, Selection{objectId, componentId, index, FocusedPanel::Behaviors});
        draw_clipped_property_label(
            ImGui::GetWindowDrawList(),
            itemPos.x + ImGui::GetStyle().FramePadding.x,
            itemPos.y,
            std::max(1.0f, itemWidth - ImGui::GetStyle().FramePadding.x * 2.0f),
            itemHeight,
            itemLabel.c_str(),
            ImGui::GetColorU32(ImGuiCol_Text),
            ImGui::IsItemHovered());
        ImGui::PopID();
    }
    if (!selection.current().behaviorOrdinal || *selection.current().behaviorOrdinal >= behaviorList->size())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextDisabled("Select a behavior to edit its fields.");
        ImGui::PopStyleColor();
        return;
    }
    const auto ordinal = *selection.current().behaviorOrdinal;
    const BehaviorOverrideContextScope overrideContext(document, commands, feedback_,
                                                        objectId, componentId, ordinal);
    const BehaviorAddressScope addressScope(document, objectId, componentId, ordinal);
    const Json *behavior = behavior_owner(document, objectId, componentId, ordinal);
    if (behavior == nullptr || !behavior->is_object())
        return;
    const std::string behaviorName = behavior->value("type", std::string("Unknown"));
    const auto snapshot = catalog.snapshot();
    const Json *descriptor = snapshot ? find_behavior_descriptor(snapshot->document, behaviorName) : nullptr;
    const bool schemaUnavailable = descriptor == nullptr ||
        !descriptor->value("field_schema_available", false);
    if (schemaUnavailable)
    {
        ImGui::TextDisabled("No schema");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Saved fields remain editable as JSON until a source schema is available.");
    }
    else if (descriptor->contains("scanner_warning") && (*descriptor)["scanner_warning"].is_string())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), "%s",
                           (*descriptor)["scanner_warning"].get_ref<const std::string &>().c_str());
    }

    std::set<std::string> knownFields;
    if (descriptor != nullptr && descriptor->value("field_schema_available", false))
    {
        const auto fields = descriptor->find("fields");
        if (fields != descriptor->end() && fields->is_array())
        {
            for (const auto &field : *fields)
            {
                const std::string fieldName = field.value("name", std::string{});
                if (fieldName.empty())
                    continue;
                const std::string searchable = behaviorName + " " + fieldName + " " +
                    field.value("label", fieldName) + " " +
                    field.value("kind", std::string("opaque_json")) + " #" +
                    std::to_string(objectId) + object_search_context(document, objectId);
                knownFields.insert(fieldName);
                if (!discovery_query_matches(searchQuery, searchable))
                    continue;
                const bool reveal = consume_reveal_target(
                    objectId, componentId, ordinal, {std::string(fieldName)});
                if (reveal)
                    ImGui::SetKeyboardFocusHere();
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 {std::string(fieldName)}, field, fieldName, browse, resourceIndex, scrubCtx);
                if (reveal)
                    ImGui::SetScrollHereY(0.5f);
            }
        }
    }

    std::vector<std::string> extras;
    for (auto it = behavior->begin(); it != behavior->end(); ++it)
        if (it.key() != "type" && !knownFields.contains(it.key()) &&
            discovery_query_matches(searchQuery, behaviorName + " " + it.key() +
                object_search_context(document, objectId)))
            extras.push_back(it.key());
    // Undeclared fields are preserved. Only typed metadata can distinguish an
    // integer parameter from a reference; never guess from coincidentally equal IDs.
    for (const auto &key : extras)
    {
        const bool reveal = consume_reveal_target(objectId, componentId, ordinal,
                                                  {std::string(key)});
        if (reveal)
            ImGui::SetKeyboardFocusHere();
        ImGui::PushID(key.c_str());
        const Json *value = read_path(behavior_owner(document, objectId, componentId, ordinal), {key});
        if (value != nullptr && (value->is_number_integer() || value->is_object() || value->is_array()))
        {
            begin_field(key, {key});
            ImGui::TextDisabled("Unrecognized");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(schemaUnavailable
                    ? "This field is not in the current schema. Edit the behavior source to declare it."
                    : "The field type is unavailable.");
            end_field();
        }
        else
            draw_raw_value(document, commands, objectId, componentId, ordinal, {key}, key, scrubCtx);
        ImGui::PopID();
        if (reveal)
            ImGui::SetScrollHereY(0.5f);
    }
    if (!feedback_.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.38f, 1.0f), "%s", feedback_.c_str());
    if (scrubState_.active && !scrubState_.seenThisFrame)
        cancel_active_interactions(&document);
}

void BehaviorFieldsPanel::draw_multi_object_fields(DocumentSession &document,
                                                   const Selection &selection,
                                                   EditCommandService &commands,
                                                   const BehaviorCatalog &catalog,
                                                   const std::vector<int> &objectIds,
                                                   ResourceIndex *resourceIndex,
                                                   std::string_view searchQuery)
{
    const DocumentIdentity currentIdentity{
        ProjectGeneration{document.projectGeneration}, document.tabId,
        DocumentGeneration{document.documentGeneration}};
    if (feedbackDocument_ && (*feedbackDocument_ != currentIdentity ||
        feedbackSelection_ != selection || feedbackObjectIds_ != objectIds ||
        feedbackRevision_ != document.valueRevision))
        feedback_.clear();
    feedbackDocument_ = currentIdentity;
    feedbackSelection_ = selection;
    feedbackObjectIds_ = objectIds;
    feedbackRevision_ = document.valueRevision;
    if (scrubState_.active &&
        (document.projectGeneration != scrubState_.projectGeneration ||
         document.tabId.value != scrubState_.tabId ||
         document.documentGeneration != scrubState_.documentGeneration ||
         hostGeneration_ != scrubState_.hostGeneration ||
         objectIds != scrubState_.orderedSelection))
    {
        cancel_active_interactions(&document);
    }
    scrubState_.seenThisFrame = false;
    const ScrubContext scrubCtx{scrubState_, document, objectIds, hostGeneration_};
    if (objectIds.size() < 2)
    {
        cancel_active_interactions(&document);
        return;
    }
    const auto catalogSnapshot = catalog.snapshot();
    if (!catalogSnapshot)
        return;

    std::string selectionScope("multi-behaviors");
    for (const int objectId : objectIds)
    {
        selectionScope.push_back(':');
        selectionScope += std::to_string(objectId);
    }
    ImGui::PushID(selectionScope.c_str());

    std::size_t primaryScriptCount = 0;
    const auto primaryLocation = document.indexes.objectsById.find(selection.objectId.value_or(-1));
    if (primaryLocation != document.indexes.objectsById.end())
    {
        const Json *primary = resolve_object(document.root, primaryLocation->second);
        const auto components = primary != nullptr ? primary->find("components") : Json::const_iterator{};
        if (primary != nullptr && components != primary->end() && components->is_array())
            for (const auto &component : *components)
                if (component.is_object() && component.value("type", -1) == 5)
                    ++primaryScriptCount;
    }
    if (primaryScriptCount == 0)
    {
        ImGui::PopID();
        return;
    }

    std::vector<std::vector<MultiBehaviorAddress>> scriptComponents(primaryScriptCount);
    bool hasAnyCommonBehavior = false;
    for (std::size_t scriptOccurrence = 0; scriptOccurrence < primaryScriptCount; ++scriptOccurrence)
    {
        auto &participants = scriptComponents[scriptOccurrence];
        participants.reserve(objectIds.size());
        bool complete = true;
        for (const int objectId : objectIds)
        {
            const auto componentId = script_component_at_occurrence(document, objectId, scriptOccurrence);
            if (!componentId)
            {
                complete = false;
                break;
            }
            participants.push_back({objectId, *componentId, 0});
        }
        if (!complete)
            continue;

        std::size_t behaviorCount = std::numeric_limits<std::size_t>::max();
        for (const auto &participant : participants)
        {
            const auto componentLocation = document.indexes.componentsById.find(participant.componentId);
            const auto objectLocation = document.indexes.objectsById.find(participant.objectId);
            if (componentLocation == document.indexes.componentsById.end() ||
                objectLocation == document.indexes.objectsById.end())
            {
                behaviorCount = 0;
                break;
            }
            const Json *object = resolve_object(document.root, objectLocation->second);
            if (object == nullptr)
            {
                behaviorCount = 0;
                break;
            }
            const auto components = object->find("components");
            if (components == object->end() || !components->is_array() ||
                componentLocation->second.componentIndex >= components->size())
            {
                behaviorCount = 0;
                break;
            }
            const auto behaviors = (*components)[componentLocation->second.componentIndex].find("behaviors");
            const std::size_t count = behaviors != (*components)[componentLocation->second.componentIndex].end() &&
                                      behaviors->is_array() ? behaviors->size() : 0;
            behaviorCount = std::min(behaviorCount, count);
        }

        for (std::size_t behaviorOrdinal = 0; behaviorOrdinal < behaviorCount; ++behaviorOrdinal)
        {
            const Json *primaryBehavior = behavior_owner(document, participants.front().objectId,
                                                         participants.front().componentId,
                                                         behaviorOrdinal);
            if (primaryBehavior == nullptr || !primaryBehavior->is_object())
                continue;
            const auto behaviorName = primaryBehavior->value("type", std::string{});
            if (behaviorName.empty())
                continue;
            bool sameBehavior = true;
            for (const auto &participant : participants)
            {
                const Json *behavior = behavior_owner(document, participant.objectId,
                                                      participant.componentId, behaviorOrdinal);
                if (behavior == nullptr || !behavior->is_object() ||
                    behavior->value("type", std::string{}) != behaviorName)
                {
                    sameBehavior = false;
                    break;
                }
            }
            if (!sameBehavior)
                continue;
            const Json *descriptor = find_behavior_descriptor(catalogSnapshot->document, behaviorName);
            if (descriptor == nullptr || !descriptor->value("field_schema_available", false))
                continue;
            const auto fields = descriptor->find("fields");
            if (fields == descriptor->end() || !fields->is_array())
                continue;

            if (!hasAnyCommonBehavior)
            {
                ImGui::SeparatorText("Common Script behavior fields");
                hasAnyCommonBehavior = true;
            }
            ImGui::PushID(static_cast<int>(scriptOccurrence));
            ImGui::PushID(static_cast<int>(behaviorOrdinal));
            ImGui::Text("Script %zu · %s", scriptOccurrence + 1, behaviorName.c_str());
            std::vector<std::pair<int, int>> overrideTargets;
            overrideTargets.reserve(participants.size());
            for (const auto &participant : participants)
                overrideTargets.emplace_back(participant.objectId, participant.componentId);
            const BehaviorOverrideContextScope overrideContext(
                document, commands, feedback_, std::move(overrideTargets), behaviorOrdinal);

            for (const auto &field : *fields)
            {
                const std::string fieldName = field.value("name", std::string{});
                const std::string kind = field.value("kind", std::string("opaque_json"));
                if (fieldName.empty() || (kind != "boolean" && kind != "number" &&
                                          kind != "signed_integer" && kind != "unsigned_integer" &&
                                          kind != "enum"))
                    continue;
                std::string objectContext;
                for (const auto &participant : participants)
                    objectContext += object_search_context(document, participant.objectId);
                const std::string searchable = behaviorName + " " + fieldName + " " +
                    field.value("label", fieldName) + " " + kind;
                if (!discovery_query_matches(searchQuery, searchable + objectContext))
                    continue;
                const std::vector<FieldPathSegment> path{fieldName};
                std::vector<Json> values;
                values.reserve(participants.size());
                bool compatible = true;
                for (const auto &participant : participants)
                {
                    const Json *currentBehavior = behavior_owner(document, participant.objectId,
                                                                 participant.componentId,
                                                                 behaviorOrdinal);
                    const Json *value = read_path(currentBehavior, path);
                    if (!compatible_behavior_value(value, kind))
                    {
                        compatible = false;
                        break;
                    }
                    values.push_back(*value);
                }
                if (!compatible)
                    continue;

                const bool mixed = std::any_of(values.begin() + 1, values.end(),
                                               [&](const Json &value) { return value != values.front(); });
                const auto commit = [&](Json next) {
                    std::vector<BatchPropertyEdit> edits;
                    edits.reserve(participants.size());
                    for (const auto &participant : participants)
                        edits.push_back({address_for_document(document, participant.objectId,
                                                              participant.componentId,
                                                              behaviorOrdinal, path),
                                         PropertyValue::of(next)});
                    commands.apply_batch(document, edits, {selection, selection});
                };

                const bool isNumericKind = kind == "number" || kind == "signed_integer" || kind == "unsigned_integer";
                const auto scrubIx = begin_field(field.value("label", fieldName), path, isNumericKind);
                const auto capture_multi_targets = [&]() {
                    std::vector<BehaviorFieldsPanel::ScrubTarget> captured;
                    captured.reserve(participants.size());
                    for (std::size_t index = 0; index < participants.size(); ++index)
                    {
                        const auto &participant = participants[index];
                        captured.push_back({participant.objectId, participant.componentId,
                                            behaviorOrdinal, path, values[index], true});
                    }
                    return captured;
                };
                const auto commit_multi_delta = [&](double delta) {
                    std::vector<BatchPropertyEdit> edits;
                    edits.reserve(scrubState_.targets.size());
                    for (const auto &target : scrubState_.targets)
                    {
                        const Json nextJson = apply_numeric_delta(target.originalJson, delta);
                        if (nextJson.is_number() &&
                            (!nextJson.is_number_float() || std::isfinite(nextJson.get<double>())))
                        {
                            edits.push_back({address_for_document(document, target.objectId,
                                                                  target.componentId,
                                                                  target.behaviorOrdinal, target.path),
                                             PropertyValue::of(nextJson)});
                        }
                    }
                    if (!edits.empty())
                        commands.apply_batch(document, edits, {selection, selection});
                };
                const auto preview_multi_delta = [&](double delta) {
                    for (const auto &target : scrubState_.targets)
                    {
                        const Json nextJson = apply_numeric_delta(target.originalJson, delta);
                        if (Json *mut = mutable_behavior_path(document, target.objectId,
                                                              target.componentId, target.behaviorOrdinal, target.path))
                        {
                            *mut = nextJson;
                            ++document.valueRevision;
                        }
                    }
                };
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
                if (kind == "boolean")
                {
                    bool value = values.front().get<bool>();
                    const bool changed = ImGui::Checkbox("##value", &value);
                    draw_active_behavior_revert_context(path);
                    ImGui::PopItemFlag();
                    end_field();
                    if (changed)
                        commit(value);
                }
                else if (kind == "number")
                {
                    double value = values.front().get<double>();
                    const double originalValue = value;
                    apply_label_scrub(
                        scrubCtx, scrubIx, 0.1, 0.5, std::nullopt, std::nullopt,
                        capture_multi_targets, preview_multi_delta, commit_multi_delta);
                    if (scrubState_.active && scrubState_.widgetId == scrubIx.id &&
                        scrubState_.moved && !scrubState_.targets.empty())
                    {
                        value = apply_numeric_delta(scrubState_.targets.front().originalJson,
                                                    scrubState_.accumDelta).get<double>();
                    }
                    const ImGuiID valueId = ImGui::GetID("##value");
                    const bool valueActive = ImGui::GetActiveID() == valueId;
                    const bool useFullPrecision = valueActive || ImGui::GetCurrentContext()->DeactivatedItemData.ID == valueId;
                    const char *formatStr = useFullPrecision ? "%.17g" : "%.2f";
                    const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, formatStr,
                                                            ImGuiInputTextFlags_EnterReturnsTrue);
                    draw_active_behavior_revert_context(path);
                    if (ImGui::IsItemActivated())
                    {
                        value = originalValue;
                        if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetItemID()))
                            state->ReloadUserBufAndSelectAll();
                    }
                    const bool cancel = valueActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !ImGui::IsItemActivated() && !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit()) && value != originalValue;
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit && std::isfinite(value))
                        commit(value);
                }
                else if (kind == "signed_integer")
                {
                    std::int64_t value = json_signed(&values.front());
                    const std::int64_t originalValue = value;
                    apply_label_scrub(
                        scrubCtx, scrubIx, 1.0, 1.0, std::nullopt, std::nullopt,
                        capture_multi_targets, preview_multi_delta, commit_multi_delta);
                    if (scrubState_.active && scrubState_.widgetId == scrubIx.id &&
                        scrubState_.moved && !scrubState_.targets.empty())
                    {
                        value = apply_numeric_delta(scrubState_.targets.front().originalJson,
                                                    scrubState_.accumDelta).get<std::int64_t>();
                    }
                    const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value,
                        nullptr, nullptr, "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
                    draw_active_behavior_revert_context(path);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit()) && value != originalValue;
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit)
                        commit(value);
                }
                else if (kind == "unsigned_integer")
                {
                    std::uint64_t value = json_unsigned(&values.front());
                    const std::uint64_t originalValue = value;
                    apply_label_scrub(
                        scrubCtx, scrubIx, 1.0, 1.0, std::nullopt, std::nullopt,
                        capture_multi_targets, preview_multi_delta, commit_multi_delta);
                    if (scrubState_.active && scrubState_.widgetId == scrubIx.id &&
                        scrubState_.moved && !scrubState_.targets.empty())
                    {
                        value = apply_numeric_delta(scrubState_.targets.front().originalJson,
                                                    scrubState_.accumDelta).get<std::uint64_t>();
                    }
                    const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value,
                        nullptr, nullptr, "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
                    draw_active_behavior_revert_context(path);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit()) && value != originalValue;
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit)
                        commit(value);
                }
                else
                {
                    const auto enumValues = field.find("enum_values");
                    const int current = json_enum(&values.front());
                    std::string preview = mixed ? "Multiple values" : "Unknown value";
                    if (!mixed && enumValues != field.end() && enumValues->is_array())
                        for (const auto &entry : *enumValues)
                            if (entry.value("value", 0) == current)
                                preview = entry.value("name", std::string("Unknown value"));
                    if (ImGui::BeginCombo("##value", preview.c_str()))
                    {
                        if (enumValues != field.end() && enumValues->is_array())
                            for (const auto &entry : *enumValues)
                            {
                                const int value = entry.value("value", 0);
                                const auto name = entry.value("name", std::string("Unknown"));
                                if (ImGui::Selectable(name.c_str(), !mixed && current == value))
                                    commit(value);
                            }
                        ImGui::EndCombo();
                    }
                    draw_active_behavior_revert_context(path);
                    ImGui::PopItemFlag();
                    end_field();
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    ImGui::PopID();
    if (!feedback_.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.38f, 1.0f), "%s", feedback_.c_str());
    if (scrubState_.active && !scrubState_.seenThisFrame)
        cancel_active_interactions(&document);
    static_cast<void>(resourceIndex);
}
} // namespace platformator_editor

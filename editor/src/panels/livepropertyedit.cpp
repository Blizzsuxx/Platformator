#include "panels/livepropertyedit.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>

namespace platformator_editor
{
namespace
{
bool addresses_equal(const PropertyAddress &a, const PropertyAddress &b)
{
    return a.documentGeneration == b.documentGeneration &&
           a.objectId == b.objectId &&
           a.componentId == b.componentId &&
           a.behaviorOrdinal == b.behaviorOrdinal &&
           a.fieldPath == b.fieldPath &&
           a.identity == b.identity;
}

template <typename T, typename InputFn>
bool draw_live_real_input(const char *widgetLabel, T &value, bool mixed,
                          const char *fullPrecisionFmt, LiveEditContext &ctx,
                          const std::vector<PropertyAddress> &targets,
                          std::optional<T> minimum, std::optional<T> maximum,
                          const char *rangeErrorMessage,
                          const std::function<void()> &afterWidget,
                          InputFn &&inputFn)
{
    const T originalValue = value;
    const ImGuiID valueId = ImGui::GetID(widgetLabel);
    const std::uint32_t widgetKey = static_cast<std::uint32_t>(valueId);
    const bool valueActive = ImGui::GetActiveID() == valueId;
    const bool wasActive = valueActive || (ctx.state.active() && ctx.state.widget_id() == widgetKey);
    const bool useFullPrecision = valueActive || ImGui::GetCurrentContext()->DeactivatedItemData.ID == valueId;
    const char *formatStr = useFullPrecision ? fullPrecisionFmt : (mixed ? " — " : "%.2f");

    ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
    const bool changed = inputFn(formatStr);
    ImGui::PopItemFlag();

    const bool activated = ImGui::IsItemActivated();
    if (activated)
    {
        value = originalValue;
        if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetItemID()))
            state->ReloadUserBufAndSelectAll();
    }
    if (afterWidget)
        afterWidget();

    const bool itemActive = ImGui::IsItemActive();
    const bool cancel = (wasActive || itemActive) && ImGui::IsKeyPressed(ImGuiKey_Escape);
    bool updated = false;
    if (!activated && !cancel && changed && value != originalValue)
    {
        if (!std::isfinite(value) || (minimum && value < *minimum) || (maximum && value > *maximum))
        {
            if (rangeErrorMessage != nullptr)
                ctx.feedback = rangeErrorMessage;
        }
        else
        {
            updated = ctx.state.update_targets(ctx.document, ctx.commands, targets, ctx.selection,
                                               widgetKey, ctx.hostGeneration, ctx.orderedSelection,
                                               Json(value), ctx.feedback);
        }
    }
    ctx.state.finish_widget(widgetKey, &ctx.document, itemActive, cancel);
    return updated;
}

template <typename T>
bool draw_live_integer_scalar(const char *widgetLabel, ImGuiDataType dataType, const char *formatStr,
                              T &value, const T *step, const T *stepFast,
                              LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                              const std::function<void()> &afterWidget,
                              std::optional<T> minimum = std::nullopt,
                              std::optional<T> maximum = std::nullopt,
                              const char *rangeErrorMessage = nullptr)
{
    const T originalValue = value;
    const ImGuiID valueId = ImGui::GetID(widgetLabel);
    const std::uint32_t widgetKey = static_cast<std::uint32_t>(valueId);
    const bool wasActive = (ImGui::GetActiveID() == valueId) ||
                           (ctx.state.active() && ctx.state.widget_id() == widgetKey);

    ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
    const bool changed = ImGui::InputScalar(widgetLabel, dataType, &value, step, stepFast, formatStr);
    ImGui::PopItemFlag();

    if (afterWidget)
        afterWidget();

    const bool itemActive = ImGui::IsItemActive();
    const bool cancel = (wasActive || itemActive) && ImGui::IsKeyPressed(ImGuiKey_Escape);
    bool updated = false;
    if (!cancel && changed && value != originalValue)
    {
        if ((minimum && value < *minimum) || (maximum && value > *maximum))
        {
            if (rangeErrorMessage != nullptr)
                ctx.feedback = rangeErrorMessage;
        }
        else
        {
            updated = ctx.state.update_targets(ctx.document, ctx.commands, targets, ctx.selection,
                                               widgetKey, ctx.hostGeneration, ctx.orderedSelection,
                                               Json(value), ctx.feedback);
        }
    }
    ctx.state.finish_widget(widgetKey, &ctx.document, itemActive, cancel);
    return updated;
}

void clear_imgui_text_edit_state(std::uint32_t widgetId)
{
    ImGuiContext *imguiContext = ImGui::GetCurrentContext();
    if (imguiContext == nullptr || widgetId == 0)
        return;
    const auto id = static_cast<ImGuiID>(widgetId);
    if (imguiContext->InputTextState.ID == id)
        imguiContext->InputTextState.EditedBefore = false;
    if (imguiContext->InputTextDeactivatedState.ID == id)
        imguiContext->InputTextDeactivatedState.ID = 0;
    if (imguiContext->ActiveId == id)
        ImGui::ClearActiveID();
}
} // namespace

void LivePropertyEditState::begin_frame(const DocumentSession &document,
                                        const std::vector<int> &orderedSelection,
                                        std::uint64_t hostGeneration)
{
    if (active_ &&
        (document.projectGeneration != projectGeneration_ ||
         document.tabId.value != tabId_ ||
         document.documentGeneration != documentGeneration_ ||
         hostGeneration != hostGeneration_ ||
         orderedSelection != orderedSelection_))
    {
        commit();
    }
    seenThisFrame_ = false;
}

void LivePropertyEditState::end_frame()
{
    if (active_ && !seenThisFrame_)
        commit();
}

void LivePropertyEditState::commit()
{
    if (!active_)
        return;
    if (singleGesture_ && singleGesture_->active())
        (void)singleGesture_->commit();
    if (batchGesture_ && batchGesture_->active())
        (void)batchGesture_->commit();
    clear_imgui_text_edit_state(widgetId_);
    reset();
}

void LivePropertyEditState::cancel(DocumentSession *document)
{
    if (!active_)
        return;
    if (document != nullptr && commands_ != nullptr)
    {
        if (singleGesture_ && singleGesture_->active())
            (void)singleGesture_->cancel(*commands_, *document);
        if (batchGesture_ && batchGesture_->active())
            (void)batchGesture_->cancel(*commands_, *document);
    }
    clear_imgui_text_edit_state(widgetId_);
    reset();
}

bool LivePropertyEditState::matches_session(const DocumentSession &document, std::uint32_t widgetId,
                                            std::uint64_t hostGeneration,
                                            const std::vector<int> &orderedSelection) const
{
    return active_ && widgetId_ == widgetId &&
           projectGeneration_ == document.projectGeneration &&
           tabId_ == document.tabId.value &&
           documentGeneration_ == document.documentGeneration &&
           hostGeneration_ == hostGeneration &&
           orderedSelection_ == orderedSelection;
}

bool LivePropertyEditState::matches_single_target(const DocumentSession &document, std::uint32_t widgetId,
                                                  std::uint64_t hostGeneration,
                                                  const std::vector<int> &orderedSelection,
                                                  const PropertyAddress &address) const
{
    return matches_session(document, widgetId, hostGeneration, orderedSelection) &&
           singleGesture_ && singleGesture_->active() &&
           singleAddress_ && addresses_equal(*singleAddress_, address);
}

bool LivePropertyEditState::matches_batch_target(const DocumentSession &document, std::uint32_t widgetId,
                                                 std::uint64_t hostGeneration,
                                                 const std::vector<int> &orderedSelection,
                                                 const std::vector<PropertyAddress> &addresses) const
{
    if (!matches_session(document, widgetId, hostGeneration, orderedSelection) ||
        !batchGesture_ || !batchGesture_->active() ||
        batchAddresses_.size() != addresses.size())
        return false;
    for (std::size_t index = 0; index < addresses.size(); ++index)
        if (!addresses_equal(batchAddresses_[index], addresses[index]))
            return false;
    return true;
}

void LivePropertyEditState::reset() noexcept
{
    active_ = false;
    seenThisFrame_ = false;
    widgetId_ = 0;
    projectGeneration_ = 0;
    tabId_ = 0;
    documentGeneration_ = 0;
    hostGeneration_ = 0;
    orderedSelection_.clear();
    singleAddress_.reset();
    batchAddresses_.clear();
    commands_ = nullptr;
    singleGesture_.reset();
    batchGesture_.reset();
}

bool LivePropertyEditState::update_single(DocumentSession &document, EditCommandService &commands,
                                          const PropertyAddress &address, const Selection &selection,
                                          std::uint32_t widgetId, std::uint64_t hostGeneration,
                                          const std::vector<int> &orderedSelection,
                                          const Json &value, std::string &feedback)
{
    if (!matches_single_target(document, widgetId, hostGeneration, orderedSelection, address) ||
        commands_ != &commands)
    {
        commit();
        auto gesture = commands.begin_gesture(document, address, {selection, selection});
        if (!gesture)
        {
            feedback = commands.last_error();
            return false;
        }
        active_ = true;
        widgetId_ = widgetId;
        projectGeneration_ = document.projectGeneration;
        tabId_ = document.tabId.value;
        documentGeneration_ = document.documentGeneration;
        hostGeneration_ = hostGeneration;
        orderedSelection_ = orderedSelection;
        singleAddress_ = address;
        commands_ = &commands;
        singleGesture_.emplace(std::move(*gesture));
    }

    seenThisFrame_ = true;
    const CommandResult result = singleGesture_->update(commands, document, value);
    if (result.status == CommandStatus::Rejected || result.status == CommandStatus::StaleAddress)
    {
        feedback = result.message;
        return false;
    }
    feedback.clear();
    return true;
}

bool LivePropertyEditState::update_batch(DocumentSession &document, EditCommandService &commands,
                                         std::vector<PropertyAddress> addresses, const Selection &selection,
                                         std::uint32_t widgetId, std::uint64_t hostGeneration,
                                         const std::vector<int> &orderedSelection,
                                         const std::vector<Json> &values, std::string &feedback)
{
    if (!matches_batch_target(document, widgetId, hostGeneration, orderedSelection, addresses) ||
        commands_ != &commands)
    {
        commit();
        auto gesture = commands.begin_batch_gesture(document, addresses, {selection, selection});
        if (!gesture)
        {
            feedback = commands.last_error();
            return false;
        }
        active_ = true;
        widgetId_ = widgetId;
        projectGeneration_ = document.projectGeneration;
        tabId_ = document.tabId.value;
        documentGeneration_ = document.documentGeneration;
        hostGeneration_ = hostGeneration;
        orderedSelection_ = orderedSelection;
        batchAddresses_ = std::move(addresses);
        commands_ = &commands;
        batchGesture_.emplace(std::move(*gesture));
    }

    seenThisFrame_ = true;
    const CommandResult result = batchGesture_->update(commands, document, values);
    if (result.status == CommandStatus::Rejected || result.status == CommandStatus::StaleAddress)
    {
        feedback = result.message;
        return false;
    }
    feedback.clear();
    return true;
}

bool LivePropertyEditState::update_targets(DocumentSession &document, EditCommandService &commands,
                                           const std::vector<PropertyAddress> &targets,
                                           const Selection &selection, std::uint32_t widgetId,
                                           std::uint64_t hostGeneration,
                                           const std::vector<int> &orderedSelection,
                                           const Json &value, std::string &feedback)
{
    if (targets.empty())
        return false;
    if (targets.size() == 1)
        return update_single(document, commands, targets.front(), selection, widgetId,
                             hostGeneration, orderedSelection, value, feedback);
    std::vector<Json> values(targets.size(), value);
    return update_batch(document, commands, targets, selection, widgetId,
                        hostGeneration, orderedSelection, values, feedback);
}

void LivePropertyEditState::finish_widget(std::uint32_t widgetId, DocumentSession *document,
                                          bool itemActive, bool cancelRequested)
{
    if (!active_ || widgetId_ != widgetId)
        return;
    seenThisFrame_ = true;
    if (cancelRequested)
        cancel(document);
    else if (!itemActive)
        commit();
}

bool live_input_float(const char *widgetLabel, float &value, bool mixed,
                      LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                      std::optional<float> minimum, std::optional<float> maximum,
                      const char *rangeErrorMessage,
                      const std::function<void()> &afterWidget)
{
    return draw_live_real_input<float>(
        widgetLabel, value, mixed, "%.9g", ctx, targets, minimum, maximum,
        rangeErrorMessage, afterWidget, [&](const char *formatStr) {
            return ImGui::InputFloat(widgetLabel, &value, 0.0f, 0.0f, formatStr);
        });
}

bool live_input_double(const char *widgetLabel, double &value, bool mixed,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const char *rangeErrorMessage,
                       const std::function<void()> &afterWidget)
{
    return draw_live_real_input<double>(
        widgetLabel, value, mixed, "%.17g", ctx, targets, std::nullopt, std::nullopt,
        rangeErrorMessage, afterWidget, [&](const char *formatStr) {
            return ImGui::InputDouble(widgetLabel, &value, 0.0, 0.0, formatStr);
        });
}

bool live_input_int64(const char *widgetLabel, std::int64_t &value,
                      const std::int64_t *step, const std::int64_t *stepFast,
                      LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                      const std::function<void()> &afterWidget,
                      std::optional<std::int64_t> minimum,
                      std::optional<std::int64_t> maximum,
                      const char *rangeErrorMessage)
{
    return draw_live_integer_scalar<std::int64_t>(
        widgetLabel, ImGuiDataType_S64, "%lld", value, step, stepFast,
        ctx, targets, afterWidget, minimum, maximum, rangeErrorMessage);
}

bool live_input_uint64(const char *widgetLabel, std::uint64_t &value,
                       const std::uint64_t *step, const std::uint64_t *stepFast,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const std::function<void()> &afterWidget)
{
    return draw_live_integer_scalar<std::uint64_t>(
        widgetLabel, ImGuiDataType_U64, "%llu", value, step, stepFast,
        ctx, targets, afterWidget);
}

bool live_input_string(const char *widgetLabel, char *buffer, std::size_t bufferSize,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const std::function<void()> &afterWidget)
{
    const ImGuiID valueId = ImGui::GetID(widgetLabel);
    const std::uint32_t widgetKey = static_cast<std::uint32_t>(valueId);
    const bool wasActive = (ImGui::GetActiveID() == valueId) ||
                           (ctx.state.active() && ctx.state.widget_id() == widgetKey);
    const bool changed = ImGui::InputText(widgetLabel, buffer, bufferSize);
    if (afterWidget)
        afterWidget();
    const bool itemActive = ImGui::IsItemActive();
    const bool cancel = (wasActive || itemActive) && ImGui::IsKeyPressed(ImGuiKey_Escape);
    bool updated = false;
    if (!cancel && changed)
    {
        updated = ctx.state.update_targets(ctx.document, ctx.commands, targets, ctx.selection,
                                           widgetKey, ctx.hostGeneration, ctx.orderedSelection,
                                           Json(std::string(buffer)), ctx.feedback);
    }
    ctx.state.finish_widget(widgetKey, &ctx.document, itemActive, cancel);
    return updated;
}
} // namespace platformator_editor

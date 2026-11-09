#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace platformator_editor
{
enum class NumericDraftOutcomeKind
{
    None,
    Commit,
    Invalid,
    Cancelled
};

struct NumericDraftOutcome
{
    NumericDraftOutcomeKind kind = NumericDraftOutcomeKind::None;
    double value = 0.0;
};

// A single numeric field keeps its text and step edits local until one complete
// interaction ends. The caller supplies a stable document/property address so a
// frame, tab, source, or project transition discards an obsolete draft.
class AddressNumericDraft
{
  public:
    NumericDraftOutcome draw(const char *label, std::string_view address,
                             double committedValue, double step)
    {
        if (active_ && address_ != address)
            cancel();
        if (!active_)
            reset_to(address, committedValue);

        const std::string scopedAddress(address);
        ImGui::PushID(scopedAddress.c_str());
        const bool entered = ImGui::InputText(label, buffer_.data(), buffer_.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        widgetId_ = ImGui::GetItemID();
        const bool inputActive = ImGui::IsItemActive();
        const bool inputActivated = ImGui::IsItemActivated();
        const bool inputDeactivated = ImGui::IsItemDeactivated();
        const bool inputEditedOnDeactivation = ImGui::IsItemDeactivatedAfterEdit();
        if (inputActivated || inputActive)
        {
            active_ = true;
            address_ = scopedAddress;
            originalValue_ = committedValue;
        }

        NumericDraftOutcome outcome;
        const bool escaped = active_ && ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (escaped)
        {
            cancel();
            outcome.kind = NumericDraftOutcomeKind::Cancelled;
        }
        else if (entered || (inputDeactivated && inputEditedOnDeactivation))
            outcome = finish_text();
        if (inputDeactivated && !entered && outcome.kind == NumericDraftOutcomeKind::Invalid)
            reset_to(address, committedValue);
        else if (inputDeactivated && !entered && active_ && stepMode_ == StepMode::None)
            reset_to(address, committedValue);

        ImGui::SameLine();
        ImGui::PushButtonRepeat(true);
        const bool downPressed = ImGui::Button("-", ImVec2(22.0f, 0.0f));
        const bool downActive = ImGui::IsItemActive();
        ImGui::SameLine();
        const bool upPressed = ImGui::Button("+", ImVec2(22.0f, 0.0f));
        const bool upActive = ImGui::IsItemActive();
        ImGui::PopButtonRepeat();

        if (outcome.kind == NumericDraftOutcomeKind::None)
            outcome = finish_step(address, committedValue, step,
                                  downPressed, downActive, upPressed, upActive);
        ImGui::PopID();
        return outcome;
    }

    void cancel() noexcept
    {
        if (widgetId_ != 0)
        {
            if (ImGuiContext *context = ImGui::GetCurrentContext())
            {
                if (ImGui::GetActiveID() == widgetId_)
                    ImGui::ClearActiveID();
                if (context->InputTextState.ID == widgetId_)
                {
                    // A canceled field may reappear under this ID after a fast
                    // frame or tab switch, so discard ImGui's cached edit buffer.
                    context->InputTextState.ID = 0;
                    context->InputTextState.ClearFreeMemory();
                }
                if (context->InputTextDeactivatedState.ID == widgetId_)
                {
                    context->InputTextDeactivatedState.ID = 0;
                    context->InputTextDeactivatedState.TextA.clear();
                }
            }
        }
        active_ = false;
        edited_ = false;
        stepMode_ = StepMode::None;
        address_.clear();
        buffer_.fill('\0');
        widgetId_ = 0;
    }

  private:
    enum class StepMode
    {
        None,
        Down,
        Up
    };

    static bool parse(std::string_view text, double &value) noexcept
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);
        if (text.empty())
            return false;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value,
                                            std::chars_format::general);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
               std::isfinite(value);
    }

    void reset_to(std::string_view address, double value)
    {
        address_.assign(address);
        active_ = false;
        edited_ = false;
        stepMode_ = StepMode::None;
        originalValue_ = value;
        format(value);
    }

    void begin_step(std::string_view address, double committedValue, StepMode mode)
    {
        if (!active_)
        {
            address_.assign(address);
            active_ = true;
            edited_ = false;
            originalValue_ = committedValue;
            format(committedValue);
        }
        stepMode_ = mode;
    }

    void format(double value)
    {
        buffer_.fill('\0');
        if (!std::isfinite(value))
        {
            const std::string_view text = std::isnan(value) ? "nan" : (value < 0.0 ? "-inf" : "inf");
            std::copy(text.begin(), text.end(), buffer_.begin());
            return;
        }
        const auto formatted = std::to_chars(buffer_.data(), buffer_.data() + buffer_.size() - 1,
                                             value, std::chars_format::general);
        if (formatted.ec != std::errc{})
            buffer_[0] = '0';
    }

    NumericDraftOutcome finish_text()
    {
        NumericDraftOutcome outcome;
        double value = 0.0;
        if (!parse(buffer_.data(), value))
        {
            outcome.kind = NumericDraftOutcomeKind::Invalid;
            return outcome;
        }
        else if (value != originalValue_)
        {
            outcome.kind = NumericDraftOutcomeKind::Commit;
            outcome.value = value;
        }
        const std::string address = address_;
        reset_to(address, outcome.kind == NumericDraftOutcomeKind::Commit
                               ? outcome.value : originalValue_);
        return outcome;
    }

    NumericDraftOutcome finish_step(std::string_view address, double committedValue,
                                    double step, bool downPressed, bool downActive,
                                    bool upPressed, bool upActive)
    {
        NumericDraftOutcome outcome;
        if (stepMode_ == StepMode::None)
        {
            if (downActive || downPressed)
                begin_step(address, committedValue, StepMode::Down);
            else if (upActive || upPressed)
                begin_step(address, committedValue, StepMode::Up);
        }

        const bool pressingDown = stepMode_ == StepMode::Down;
        const bool buttonPressed = pressingDown ? downPressed : upPressed;
        const bool buttonActive = pressingDown ? downActive : upActive;
        if (stepMode_ != StepMode::None && buttonPressed)
        {
            double current = 0.0;
            if (!parse(buffer_.data(), current))
                outcome.kind = NumericDraftOutcomeKind::Invalid;
            else
            {
                const double next = current + (pressingDown ? -step : step);
                if (!std::isfinite(next))
                    outcome.kind = NumericDraftOutcomeKind::Invalid;
                else
                {
                    format(next);
                    edited_ = true;
                }
            }
        }
        if (outcome.kind == NumericDraftOutcomeKind::Invalid)
        {
            reset_to(address, committedValue);
            return outcome;
        }
        if (stepMode_ != StepMode::None && !buttonActive)
        {
            double value = 0.0;
            if (edited_ && parse(buffer_.data(), value) && value != originalValue_)
            {
                outcome.kind = NumericDraftOutcomeKind::Commit;
                outcome.value = value;
            }
            reset_to(address, outcome.kind == NumericDraftOutcomeKind::Commit
                                   ? outcome.value : committedValue);
        }
        return outcome;
    }

    std::string address_;
    ImGuiID widgetId_ = 0;
    std::array<char, 64> buffer_{};
    double originalValue_ = 0.0;
    bool active_ = false;
    bool edited_ = false;
    StepMode stepMode_ = StepMode::None;
};
} // namespace platformator_editor

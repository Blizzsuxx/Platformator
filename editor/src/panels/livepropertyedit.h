#pragma once

#include "editcommands.h"
#include "selection.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace platformator_editor
{
class LivePropertyEditState
{
  public:
    void begin_frame(const DocumentSession &document,
                     const std::vector<int> &orderedSelection,
                     std::uint64_t hostGeneration);
    void end_frame();

    void commit();
    void cancel(DocumentSession *document);

    bool active() const noexcept { return active_; }
    std::uint32_t widget_id() const noexcept { return widgetId_; }

    bool update_single(DocumentSession &document, EditCommandService &commands,
                       const PropertyAddress &address, const Selection &selection,
                       std::uint32_t widgetId, std::uint64_t hostGeneration,
                       const std::vector<int> &orderedSelection,
                       const Json &value, std::string &feedback);

    bool update_batch(DocumentSession &document, EditCommandService &commands,
                      std::vector<PropertyAddress> addresses, const Selection &selection,
                      std::uint32_t widgetId, std::uint64_t hostGeneration,
                      const std::vector<int> &orderedSelection,
                      const std::vector<Json> &values, std::string &feedback);

    bool update_targets(DocumentSession &document, EditCommandService &commands,
                        const std::vector<PropertyAddress> &targets,
                        const Selection &selection, std::uint32_t widgetId,
                        std::uint64_t hostGeneration,
                        const std::vector<int> &orderedSelection,
                        const Json &value, std::string &feedback);

    void finish_widget(std::uint32_t widgetId, DocumentSession *document,
                       bool itemActive, bool cancelRequested);

  private:
    bool matches_session(const DocumentSession &document, std::uint32_t widgetId,
                         std::uint64_t hostGeneration,
                         const std::vector<int> &orderedSelection) const;
    bool matches_single_target(const DocumentSession &document, std::uint32_t widgetId,
                               std::uint64_t hostGeneration,
                               const std::vector<int> &orderedSelection,
                               const PropertyAddress &address) const;
    bool matches_batch_target(const DocumentSession &document, std::uint32_t widgetId,
                              std::uint64_t hostGeneration,
                              const std::vector<int> &orderedSelection,
                              const std::vector<PropertyAddress> &addresses) const;
    void reset() noexcept;

    bool active_ = false;
    bool seenThisFrame_ = false;
    std::uint32_t widgetId_ = 0;
    std::uint64_t projectGeneration_ = 0;
    std::uint64_t tabId_ = 0;
    std::uint64_t documentGeneration_ = 0;
    std::uint64_t hostGeneration_ = 0;
    std::vector<int> orderedSelection_;
    std::optional<PropertyAddress> singleAddress_;
    std::vector<PropertyAddress> batchAddresses_;
    EditCommandService *commands_ = nullptr;
    std::optional<PropertyGesture> singleGesture_;
    std::optional<BatchPropertyGesture> batchGesture_;
};

struct LiveEditContext
{
    DocumentSession &document;
    EditCommandService &commands;
    LivePropertyEditState &state;
    const Selection &selection;
    std::uint64_t hostGeneration = 0;
    const std::vector<int> &orderedSelection;
    std::string &feedback;
};

bool live_input_float(const char *widgetLabel, float &value, bool mixed,
                      LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                      std::optional<float> minimum, std::optional<float> maximum,
                      const char *rangeErrorMessage,
                      const std::function<void()> &afterWidget = {});

bool live_input_double(const char *widgetLabel, double &value, bool mixed,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const char *rangeErrorMessage,
                       const std::function<void()> &afterWidget = {});

bool live_input_int64(const char *widgetLabel, std::int64_t &value,
                      const std::int64_t *step, const std::int64_t *stepFast,
                      LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                      const std::function<void()> &afterWidget = {},
                      std::optional<std::int64_t> minimum = std::nullopt,
                      std::optional<std::int64_t> maximum = std::nullopt,
                      const char *rangeErrorMessage = nullptr);

bool live_input_uint64(const char *widgetLabel, std::uint64_t &value,
                       const std::uint64_t *step, const std::uint64_t *stepFast,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const std::function<void()> &afterWidget = {});

bool live_input_string(const char *widgetLabel, char *buffer, std::size_t bufferSize,
                       LiveEditContext &ctx, const std::vector<PropertyAddress> &targets,
                       const std::function<void()> &afterWidget = {});
} // namespace platformator_editor

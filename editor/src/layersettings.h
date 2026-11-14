#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
inline constexpr std::size_t LayerCount = 32;
inline constexpr const char *DefaultLayerName = "Default";

struct LayerSettings
{
    int version = 1;
    std::array<std::string, LayerCount> names{};
    std::array<std::uint32_t, LayerCount> collisionMatrix{};

    bool operator==(const LayerSettings &) const = default;
};

struct LayerSettingsLoadResult
{
    LayerSettings settings;
    bool present = false;
    bool valid = true;
    std::string message;
};

struct LayerSyncRecoveryLoadResult
{
    bool pending = false;
    bool valid = true;
    LayerSettings settings;
    std::string message;
};

struct LayerSettingsSaveResult
{
    bool succeeded = false;
    std::string message;
};

struct LayerSettingsSyncFailure
{
    std::filesystem::path path;
    std::string message;
};

struct LayerSettingsSyncResult
{
    std::size_t updatedFiles = 0;
    std::vector<LayerSettingsSyncFailure> failures;

    bool succeeded() const noexcept { return failures.empty(); }
};

LayerSettings default_layer_settings();
void normalize_layer_settings(LayerSettings &settings);
std::filesystem::path layer_settings_path(const std::filesystem::path &projectRoot);
std::filesystem::path layer_sync_recovery_path(const std::filesystem::path &projectRoot);
LayerSettingsLoadResult load_layer_settings(const std::filesystem::path &projectRoot);
LayerSyncRecoveryLoadResult load_layer_sync_recovery(const std::filesystem::path &projectRoot);
LayerSettingsSaveResult prepare_layer_sync_recovery(const std::filesystem::path &projectRoot,
                                                    const LayerSettings &settings,
                                                    std::string_view reason);
LayerSettingsSaveResult commit_prepared_layer_sync_recovery(const std::filesystem::path &projectRoot);
LayerSettingsSaveResult discard_prepared_layer_sync_recovery(const std::filesystem::path &projectRoot);
LayerSettingsSaveResult save_layer_sync_recovery(const std::filesystem::path &projectRoot,
                                                 const LayerSettings &settings,
                                                 std::string_view reason);
LayerSettingsSaveResult clear_layer_sync_recovery(const std::filesystem::path &projectRoot);
LayerSettingsSaveResult save_layer_settings(const std::filesystem::path &projectRoot,
                                            const LayerSettings &settings);

bool set_layer_name(LayerSettings &settings, std::size_t index, std::string_view rawName, std::string &error);
void set_layer_collision(LayerSettings &settings, std::size_t layerA, std::size_t layerB, bool enabled);
bool can_layers_collide(const LayerSettings &settings, std::size_t layerA, std::size_t layerB) noexcept;
std::string layer_display_label(const LayerSettings &settings, std::size_t index);

std::uint64_t layer_to_collision_group(std::uint8_t layerIndex) noexcept;
std::uint64_t layer_to_collision_mask(const LayerSettings &settings, std::uint8_t layerIndex) noexcept;
std::optional<std::uint8_t> collision_group_to_layer(std::uint64_t group) noexcept;
bool sync_colliders_with_layer_settings(nlohmann::json &sceneRoot, const LayerSettings &settings);
LayerSettingsSyncResult sync_project_files_with_layer_settings(
    const std::filesystem::path &assetRoot, const LayerSettings &settings,
    const std::vector<std::filesystem::path> &excludedPaths = {});
} // namespace platformator_editor

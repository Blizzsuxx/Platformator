#include "layersettings.h"
#include "documentio.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <json.hpp>

namespace platformator_editor
{
namespace
{
std::string trim_copy(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
        ++start;
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
        --end;
    return std::string(text.substr(start, end - start));
}

std::string lowercase_copy(std::string_view text)
{
    std::string result(text);
    for (char &ch : result)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

nlohmann::json layer_settings_json(const LayerSettings &settings)
{
    LayerSettings normalized = settings;
    normalize_layer_settings(normalized);
    return nlohmann::json{{"version", 1}, {"layers", normalized.names},
                          {"collisionMatrix", normalized.collisionMatrix}};
}

LayerSettings parse_layer_settings_json(const nlohmann::json &value)
{
    if (!value.is_object())
        throw std::invalid_argument("recovery record has no settings object");
    const auto version = value.find("version");
    const auto names = value.find("layers");
    const auto matrix = value.find("collisionMatrix");
    if (version == value.end() || !version->is_number_integer() || version->get<int>() != 1 ||
        names == value.end() || !names->is_array() || names->size() != LayerCount ||
        matrix == value.end() || !matrix->is_array() || matrix->size() != LayerCount)
        throw std::invalid_argument("recovery record has invalid layer settings");

    LayerSettings settings = default_layer_settings();
    for (std::size_t index = 0; index < LayerCount; ++index)
    {
        if (!(*names)[index].is_string())
            throw std::invalid_argument("recovery record contains a non-string layer name");
        const auto &row = (*matrix)[index];
        if ((!row.is_number_unsigned() && !row.is_number_integer()) ||
            (row.is_number_integer() && !row.is_number_unsigned() &&
             row.get<std::int64_t>() < 0) ||
            row.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("recovery record contains an invalid collision row");
        settings.names[index] = (*names)[index].get<std::string>();
        settings.collisionMatrix[index] = static_cast<std::uint32_t>(row.get<std::uint64_t>());
    }
    normalize_layer_settings(settings);
    for (std::size_t first = 0; first < LayerCount; ++first)
        for (std::size_t second = first + 1; second < LayerCount; ++second)
            if (!settings.names[first].empty() && !settings.names[second].empty() &&
                lowercase_copy(settings.names[first]) == lowercase_copy(settings.names[second]))
                throw std::invalid_argument("recovery record contains duplicate layer names");
    return settings;
}

struct LayerSyncRecoveryRecord
{
    LayerSettings settings;
    std::string reason;
};

struct LayerSyncRecoveryMarker
{
    bool exists = false;
    bool valid = true;
    std::optional<LayerSyncRecoveryRecord> committed;
    std::optional<LayerSyncRecoveryRecord> prepared;
    std::string error;
};

nlohmann::json recovery_record_json(const LayerSyncRecoveryRecord &record)
{
    return nlohmann::json{{"settings", layer_settings_json(record.settings)},
                          {"reason", record.reason}};
}

LayerSyncRecoveryRecord parse_recovery_record(const nlohmann::json &value)
{
    if (!value.is_object())
        throw std::invalid_argument("recovery record must be an object");
    const auto settings = value.find("settings");
    if (settings == value.end())
        throw std::invalid_argument("recovery record has no settings");
    LayerSyncRecoveryRecord record{parse_layer_settings_json(*settings), {}};
    if (value.contains("reason") && value["reason"].is_string())
        record.reason = value["reason"].get<std::string>();
    return record;
}

LayerSyncRecoveryMarker read_layer_sync_marker(const std::filesystem::path &projectRoot)
{
    LayerSyncRecoveryMarker marker;
    const auto path = layer_sync_recovery_path(projectRoot);
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError))
    {
        if (filesystemError)
        {
            marker.exists = true;
            marker.valid = false;
            marker.error = "Unable to inspect layer synchronization recovery marker: " +
                           filesystemError.message();
        }
        return marker;
    }
    marker.exists = true;
    try
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            throw std::runtime_error("Unable to open layer synchronization recovery marker: " +
                                     path.string());
        nlohmann::json parsed;
        input >> parsed;
        if (!parsed.is_object())
            throw std::invalid_argument("recovery marker root must be an object");
        // Read v1 markers written before prepared drafts were separated from committed syncs.
        if (parsed.value("schema", std::string{}) == "platformator.layer-sync-recovery.v1")
            marker.committed = parse_recovery_record(parsed);
        else if (parsed.value("schema", std::string{}) == "platformator.layer-sync-recovery.v2")
        {
            if (parsed.contains("committed") && !parsed["committed"].is_null())
                marker.committed = parse_recovery_record(parsed["committed"]);
            if (parsed.contains("prepared") && !parsed["prepared"].is_null())
                marker.prepared = parse_recovery_record(parsed["prepared"]);
        }
        else
            throw std::invalid_argument("unsupported layer synchronization recovery schema");
    }
    catch (const std::exception &error)
    {
        marker.valid = false;
        marker.error = "Invalid layer synchronization recovery marker: " +
                       std::string(error.what());
    }
    return marker;
}

LayerSettingsSaveResult write_layer_sync_marker(const std::filesystem::path &projectRoot,
                                                const LayerSyncRecoveryMarker &marker)
{
    const auto path = layer_sync_recovery_path(projectRoot);
    if (!marker.committed && !marker.prepared)
    {
        std::error_code filesystemError;
        if (!std::filesystem::remove(path, filesystemError) && filesystemError)
            return {false, "Failed to clear layer synchronization recovery marker: " +
                           filesystemError.message()};
        return {true, {}};
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError)
        return {false, "Failed to create layer synchronization recovery directory: " +
                       filesystemError.message()};
    nlohmann::json value{{"schema", "platformator.layer-sync-recovery.v2"},
                         {"committed", marker.committed
                              ? recovery_record_json(*marker.committed) : nlohmann::json(nullptr)},
                         {"prepared", marker.prepared
                              ? recovery_record_json(*marker.prepared) : nlohmann::json(nullptr)}};
    const auto written = write_file_atomically(path, value.dump(2) + "\n", true);
    if (!written.succeeded())
        return {false, "Failed to save layer synchronization recovery marker: " + written.message};
    return {true, {}};
}
} // namespace

LayerSettings default_layer_settings()
{
    LayerSettings settings;
    settings.version = 1;
    settings.names[0] = DefaultLayerName;
    settings.collisionMatrix.fill(0xFFFFFFFFu);
    return settings;
}

void normalize_layer_settings(LayerSettings &settings)
{
    settings.version = 1;
    settings.names[0] = DefaultLayerName;
    for (std::size_t i = 1; i < LayerCount; ++i)
        settings.names[i] = trim_copy(settings.names[i]);

    // Enforce symmetric matrix bits across diagonal (canonical OR if mismatched).
    for (std::size_t i = 0; i < LayerCount; ++i)
    {
        for (std::size_t j = i + 1; j < LayerCount; ++j)
        {
            const bool ij = ((settings.collisionMatrix[i] >> j) & 1u) != 0;
            const bool ji = ((settings.collisionMatrix[j] >> i) & 1u) != 0;
            const bool enabled = ij && ji ? true : (ij || ji);
            if (enabled)
            {
                settings.collisionMatrix[i] |= (std::uint32_t{1} << j);
                settings.collisionMatrix[j] |= (std::uint32_t{1} << i);
            }
            else
            {
                settings.collisionMatrix[i] &= ~(std::uint32_t{1} << j);
                settings.collisionMatrix[j] &= ~(std::uint32_t{1} << i);
            }
        }
    }
}

std::filesystem::path layer_settings_path(const std::filesystem::path &projectRoot)
{
    return projectRoot / ".platformator" / "layers.json";
}

std::filesystem::path layer_sync_recovery_path(const std::filesystem::path &projectRoot)
{
    return projectRoot / ".platformator" / "layer-sync-pending.json";
}

LayerSettingsLoadResult load_layer_settings(const std::filesystem::path &projectRoot)
{
    LayerSettingsLoadResult result;
    result.settings = default_layer_settings();

    const auto path = layer_settings_path(projectRoot);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        result.present = false;
        result.valid = true;
        return result;
    }

    result.present = true;
    std::ifstream input(path);
    if (!input.is_open())
    {
        result.valid = false;
        result.message = "Unable to open layer settings file: " + path.string();
        return result;
    }

    try
    {
        nlohmann::json parsed;
        input >> parsed;
        if (!parsed.is_object())
        {
            result.valid = false;
            result.message = "Layer settings root must be a JSON object";
            return result;
        }
        const auto version = parsed.find("version");
        const auto layersIt = parsed.find("layers");
        const auto matrixIt = parsed.find("collisionMatrix");
        const auto invalid = [&](std::string message) {
            result.settings = default_layer_settings();
            result.valid = false;
            result.message = std::move(message);
        };
        if (version == parsed.end() || !version->is_number_integer() || version->get<int>() != 1)
        {
            invalid("Layer settings version must be 1");
            return result;
        }
        if (layersIt == parsed.end() || !layersIt->is_array() || layersIt->size() != LayerCount)
        {
            invalid("Layer settings must contain exactly 32 layer names");
            return result;
        }
        if (matrixIt == parsed.end() || !matrixIt->is_array() || matrixIt->size() != LayerCount)
        {
            invalid("Layer settings must contain exactly 32 collision matrix rows");
            return result;
        }
        for (std::size_t i = 0; i < LayerCount; ++i)
        {
            if (!(*layersIt)[i].is_string())
            {
                invalid("Layer name at slot " + std::to_string(i) + " must be a string");
                return result;
            }
            const auto &row = (*matrixIt)[i];
            if ((!row.is_number_unsigned() && !row.is_number_integer()) ||
                (row.is_number_integer() && !row.is_number_unsigned() && row.get<std::int64_t>() < 0) ||
                row.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
            {
                invalid("Collision matrix row " + std::to_string(i) + " must be a uint32 value");
                return result;
            }
            result.settings.names[i] = (*layersIt)[i].get<std::string>();
            result.settings.collisionMatrix[i] = static_cast<std::uint32_t>(row.get<std::uint64_t>());
        }
        normalize_layer_settings(result.settings);
        for (std::size_t i = 0; i < LayerCount; ++i)
        {
            if (result.settings.names[i].empty())
                continue;
            for (std::size_t j = i + 1; j < LayerCount; ++j)
            {
                if (!result.settings.names[j].empty() &&
                    lowercase_copy(result.settings.names[i]) == lowercase_copy(result.settings.names[j]))
                {
                    invalid("Duplicate layer name '" + result.settings.names[i] + "' in slots " +
                            std::to_string(i) + " and " + std::to_string(j));
                    return result;
                }
            }
        }
    }
    catch (const std::exception &ex)
    {
        result.settings = default_layer_settings();
        result.valid = false;
        result.message = std::string("Invalid layer settings JSON: ") + ex.what();
    }
    return result;
}

LayerSyncRecoveryLoadResult load_layer_sync_recovery(const std::filesystem::path &projectRoot)
{
    LayerSyncRecoveryLoadResult result;
    result.settings = default_layer_settings();
    auto marker = read_layer_sync_marker(projectRoot);
    if (!marker.exists)
        return result;
    if (!marker.valid)
    {
        result.pending = true;
        result.valid = false;
        result.message = marker.error;
        return result;
    }

    if (marker.prepared)
    {
        const auto saved = load_layer_settings(projectRoot);
        if (saved.present && saved.valid && saved.settings == marker.prepared->settings)
        {
            marker.committed = marker.prepared;
            marker.prepared.reset();
            const auto promoted = write_layer_sync_marker(projectRoot, marker);
            if (!promoted.succeeded)
                result.message = "Saved layer settings were committed, but recovery promotion will retry: " +
                                 promoted.message;
        }
    }

    if (marker.committed)
    {
        result.pending = true;
        result.settings = marker.committed->settings;
        if (!marker.committed->reason.empty())
            result.message = marker.committed->reason;
    }
    return result;
}

LayerSettingsSaveResult prepare_layer_sync_recovery(const std::filesystem::path &projectRoot,
                                                    const LayerSettings &settings,
                                                    std::string_view reason)
{
    auto marker = read_layer_sync_marker(projectRoot);
    if (!marker.valid)
        return {false, marker.error};
    LayerSettings normalized = settings;
    normalize_layer_settings(normalized);
    marker.prepared = LayerSyncRecoveryRecord{std::move(normalized), std::string(reason)};
    return write_layer_sync_marker(projectRoot, marker);
}

LayerSettingsSaveResult commit_prepared_layer_sync_recovery(const std::filesystem::path &projectRoot)
{
    auto marker = read_layer_sync_marker(projectRoot);
    if (!marker.valid)
        return {false, marker.error};
    if (!marker.prepared)
        return {true, {}};
    marker.committed = marker.prepared;
    marker.prepared.reset();
    return write_layer_sync_marker(projectRoot, marker);
}

LayerSettingsSaveResult discard_prepared_layer_sync_recovery(const std::filesystem::path &projectRoot)
{
    auto marker = read_layer_sync_marker(projectRoot);
    if (!marker.valid)
        return {false, marker.error};
    marker.prepared.reset();
    return write_layer_sync_marker(projectRoot, marker);
}

LayerSettingsSaveResult save_layer_sync_recovery(const std::filesystem::path &projectRoot,
                                                 const LayerSettings &settings,
                                                 std::string_view reason)
{
    LayerSettings normalized = settings;
    normalize_layer_settings(normalized);
    auto marker = read_layer_sync_marker(projectRoot);
    if (!marker.valid)
    {
        marker = LayerSyncRecoveryMarker{};
        // This API is the explicit repair path for an invalid/missing committed marker.
    }
    marker.committed = LayerSyncRecoveryRecord{std::move(normalized), std::string(reason)};
    marker.prepared.reset();
    return write_layer_sync_marker(projectRoot, marker);
}

LayerSettingsSaveResult clear_layer_sync_recovery(const std::filesystem::path &projectRoot)
{
    LayerSyncRecoveryMarker empty;
    return write_layer_sync_marker(projectRoot, empty);
}

LayerSettingsSaveResult save_layer_settings(const std::filesystem::path &projectRoot,
                                            const LayerSettings &settings)
{
    LayerSettings normalized = settings;
    normalize_layer_settings(normalized);

    const auto path = layer_settings_path(projectRoot);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return {false, "Failed to create .platformator directory: " + ec.message()};

    nlohmann::json doc;
    doc["version"] = 1;
    doc["layers"] = normalized.names;
    doc["collisionMatrix"] = normalized.collisionMatrix;

    const auto written = write_file_atomically(path, doc.dump(2) + "\n", true);
    if (!written.succeeded())
        return {false, "Failed to write layer settings file: " + written.message};

    return {true, {}};
}

bool set_layer_name(LayerSettings &settings, std::size_t index, std::string_view rawName, std::string &error)
{
    if (index == 0 || index >= LayerCount)
    {
        error = index == 0 ? "Layer 0 ('Default') cannot be renamed" : "Layer index out of range (0..31)";
        return false;
    }

    const std::string trimmed = trim_copy(rawName);
    if (!trimmed.empty())
    {
        const std::string lowered = lowercase_copy(trimmed);
        for (std::size_t i = 0; i < LayerCount; ++i)
        {
            if (i == index || settings.names[i].empty())
                continue;
            if (lowercase_copy(settings.names[i]) == lowered)
            {
                error = "Layer name '" + trimmed + "' is already used by Layer " + std::to_string(i);
                return false;
            }
        }
    }

    settings.names[index] = trimmed;
    error.clear();
    return true;
}

void set_layer_collision(LayerSettings &settings, std::size_t layerA, std::size_t layerB, bool enabled)
{
    if (layerA >= LayerCount || layerB >= LayerCount)
        return;

    const std::uint32_t maskB = std::uint32_t{1} << layerB;
    const std::uint32_t maskA = std::uint32_t{1} << layerA;
    if (enabled)
    {
        settings.collisionMatrix[layerA] |= maskB;
        settings.collisionMatrix[layerB] |= maskA;
    }
    else
    {
        settings.collisionMatrix[layerA] &= ~maskB;
        settings.collisionMatrix[layerB] &= ~maskA;
    }
}

bool can_layers_collide(const LayerSettings &settings, std::size_t layerA, std::size_t layerB) noexcept
{
    if (layerA >= LayerCount || layerB >= LayerCount)
        return true;
    return ((settings.collisionMatrix[layerA] >> layerB) & 1u) != 0;
}

std::string layer_display_label(const LayerSettings &settings, std::size_t index)
{
    if (index >= LayerCount)
        return "Layer " + std::to_string(index);
    if (!settings.names[index].empty())
        return std::to_string(index) + ": " + settings.names[index];
    return std::to_string(index) + ": (Unnamed)";
}

std::uint64_t layer_to_collision_group(std::uint8_t layerIndex) noexcept
{
    return std::uint64_t{1} << (layerIndex & 31u);
}

std::uint64_t layer_to_collision_mask(const LayerSettings &settings, std::uint8_t layerIndex) noexcept
{
    return static_cast<std::uint64_t>(settings.collisionMatrix[layerIndex & 31u]);
}

std::optional<std::uint8_t> collision_group_to_layer(std::uint64_t group) noexcept
{
    if (group == 0 || (group & (group - 1)) != 0)
        return std::nullopt;
    for (std::uint8_t bit = 0; bit < static_cast<std::uint8_t>(LayerCount); ++bit)
    {
        if (group == (std::uint64_t{1} << bit))
            return bit;
    }
    return std::nullopt;
}

namespace
{
bool sync_object_colliders(nlohmann::json &object, const LayerSettings &settings)
{
    if (!object.is_object())
        return false;
    bool changed = false;
    if (auto compIt = object.find("components"); compIt != object.end() && compIt->is_array())
    {
        for (auto &comp : *compIt)
        {
            if (!comp.is_object() || comp.value("type", -1) != 3)
                continue;
            std::uint64_t group = 1;
            if (auto gIt = comp.find("collisionGroup"); gIt != comp.end())
            {
                if (gIt->is_number_unsigned())
                    group = gIt->get<std::uint64_t>();
                else if (gIt->is_number_integer())
                {
                    const auto signedVal = gIt->get<std::int64_t>();
                    if (signedVal >= 0)
                        group = static_cast<std::uint64_t>(signedVal);
                }
            }
            if (const auto layer = collision_group_to_layer(group))
            {
                const std::uint64_t desiredMask = layer_to_collision_mask(settings, *layer);
                const std::uint64_t currentMask =
                    comp.contains("collisionMask") &&
                            (comp["collisionMask"].is_number_unsigned() ||
                             (comp["collisionMask"].is_number_integer() &&
                              comp["collisionMask"].get<std::int64_t>() >= 0))
                        ? comp["collisionMask"].get<std::uint64_t>()
                        : 0ULL;
                if (!comp.contains("collisionMask") || currentMask != desiredMask)
                {
                    comp["collisionMask"] = desiredMask;
                    changed = true;
                }
            }
        }
    }
    if (auto childIt = object.find("children"); childIt != object.end() && childIt->is_array())
    {
        for (auto &child : *childIt)
            changed |= sync_object_colliders(child, settings);
    }
    return changed;
}
} // namespace

bool sync_colliders_with_layer_settings(nlohmann::json &sceneRoot, const LayerSettings &settings)
{
    bool changed = false;
    if (sceneRoot.is_array())
    {
        for (auto &obj : sceneRoot)
            changed |= sync_object_colliders(obj, settings);
    }
    else if (sceneRoot.is_object())
    {
        changed |= sync_object_colliders(sceneRoot, settings);
    }
    return changed;
}

LayerSettingsSyncResult sync_project_files_with_layer_settings(
    const std::filesystem::path &assetRoot, const LayerSettings &settings,
    const std::vector<std::filesystem::path> &excludedPaths)
{
    LayerSettingsSyncResult result;
    std::error_code ec;
    if (assetRoot.empty() || !std::filesystem::is_directory(assetRoot, ec) || ec)
    {
        result.failures.push_back({assetRoot, ec ? ec.message() : "asset root is not a directory"});
        return result;
    }

    for (std::filesystem::recursive_directory_iterator it(
             assetRoot, std::filesystem::directory_options::none, ec),
         end;
         it != end;)
    {
        const auto path = it->path();
        std::error_code statusError;
        const bool regularFile = it->is_regular_file(statusError);
        if (statusError)
            result.failures.push_back({path, "could not inspect asset: " + statusError.message()});
        else if (regularFile && (path.extension() == ".scene" || path.extension() == ".pttemplate"))
        {
            const auto candidate = path.lexically_normal();
            const bool excluded = std::any_of(excludedPaths.begin(), excludedPaths.end(),
                [&](const auto &excludedPath) {
                    return candidate == excludedPath.lexically_normal();
                });
            if (!excluded)
            {
                std::ifstream in(path, std::ios::binary);
                if (!in.is_open())
                    result.failures.push_back({path, "could not read asset"});
                else
                {
                    nlohmann::json parsed;
                    bool parsedSuccessfully = true;
                    try
                    {
                        parsed = nlohmann::json::parse(in);
                    }
                    catch (const std::exception &error)
                    {
                        parsedSuccessfully = false;
                        result.failures.push_back({path, "could not parse asset: " + std::string(error.what())});
                    }
                    in.close();
                    bool fileChanged = false;
                    if (parsedSuccessfully)
                    {
                        if (path.extension() == ".scene" && parsed.is_array())
                            fileChanged = sync_colliders_with_layer_settings(parsed, settings);
                        else if (path.extension() == ".pttemplate" && parsed.is_object() && parsed.contains("root"))
                            fileChanged = sync_colliders_with_layer_settings(parsed["root"], settings);
                        else
                            result.failures.push_back({path, "asset has an unsupported scene or template structure"});
                    }

                    if (fileChanged)
                    {
                        const auto written = write_file_atomically(path, parsed.dump(2) + "\n", true);
                        if (written.succeeded())
                            ++result.updatedFiles;
                        else
                            result.failures.push_back({path, "could not persist collider changes: " + written.message});
                    }
                }
            }
        }

        it.increment(ec);
        if (ec)
        {
            result.failures.push_back({path, "could not continue asset scan: " + ec.message()});
            ec.clear();
        }
    }
    if (ec)
        result.failures.push_back({assetRoot, "could not start asset scan: " + ec.message()});
    return result;
}
} // namespace platformator_editor

#include "behaviorcatalog.h"
#include "sourcescanner.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace platformator_editor
{
namespace
{
constexpr std::size_t kMaximumCatalogBytes = 4U * 1024U * 1024U;
constexpr std::chrono::milliseconds kMaximumCatalogTime{10000};
constexpr std::size_t kMaximumDescriptorDepth = 64;

bool has_kind(std::string_view kind)
{
    static constexpr std::array<std::string_view, 13> kinds{
        "boolean", "signed_integer", "unsigned_integer", "number", "string", "enum", "vector2",
        "rectangle", "array", "optional", "asset_reference", "object_reference", "opaque_json"};
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

bool valid_field(const Json &field, std::size_t depth, std::string &error)
{
    if (depth > kMaximumDescriptorDepth)
    {
        error = "catalog descriptor nesting exceeds the supported depth";
        return false;
    }
    if (!field.is_object() || !field.contains("name") || !field["name"].is_string() ||
        field["name"].get_ref<const std::string &>().empty() || !field.contains("kind") ||
        !field["kind"].is_string() || !has_kind(field["kind"].get_ref<const std::string &>()))
    {
        error = "catalog field descriptor has an invalid name or kind";
        return false;
    }
    for (const char *bound : {"minimum", "maximum"})
    {
        const auto value = field.find(bound);
        if (value != field.end() && !value->is_string())
        {
            error = std::string("catalog field '") + field["name"].get<std::string>() +
                    "' has a malformed " + bound + " value";
            return false;
        }
    }
    const auto target = field.find("target");
    if (target != field.end() && !target->is_string())
    {
        error = "catalog reference target must be a string";
        return false;
    }
    const auto nullable = field.find("nullable");
    if (nullable != field.end() && !nullable->is_boolean())
    {
        error = "catalog nullable flag must be boolean";
        return false;
    }
    const auto enumValues = field.find("enum_values");
    if (enumValues != field.end())
    {
        if (!enumValues->is_array())
        {
            error = "catalog enum values must be an array";
            return false;
        }
        std::set<std::string> names;
        for (const auto &value : *enumValues)
        {
            if (!value.is_object() || !value.contains("name") || !value["name"].is_string() ||
                !value.contains("value") || !value["value"].is_number_integer() ||
                !names.insert(value["name"].get<std::string>()).second)
            {
                error = "catalog enum values contain a malformed or duplicate entry";
                return false;
            }
        }
    }
    const auto element = field.find("element");
    if (element != field.end() && !valid_field(*element, depth + 1, error))
        return false;
    return true;
}

bool valid_catalog(const Json &document, std::string &error)
{
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_integer() || document["schema_version"] != 1 ||
        !document.contains("behaviors") || !document["behaviors"].is_array())
    {
        error = "catalog envelope must contain schema_version 1 and a behaviors array";
        return false;
    }
    std::set<std::string> behaviorNames;
    for (const auto &behavior : document["behaviors"])
    {
        if (!behavior.is_object() || !behavior.contains("name") || !behavior["name"].is_string() ||
            behavior["name"].get_ref<const std::string &>().empty() ||
            !behavior.contains("field_schema_available") || !behavior["field_schema_available"].is_boolean() ||
            !behavior.contains("fields") || !behavior["fields"].is_array())
        {
            error = "catalog behavior entry has an invalid name, schema flag, or fields array";
            return false;
        }
        const auto name = behavior["name"].get<std::string>();
        if (!behaviorNames.insert(name).second)
        {
            error = "catalog contains duplicate behavior name '" + name + "'";
            return false;
        }
        std::set<std::string> fieldNames;
        for (const auto &field : behavior["fields"])
        {
            if (!valid_field(field, 0, error))
                return false;
            if (!fieldNames.insert(field["name"].get<std::string>()).second)
            {
                error = "catalog behavior '" + name + "' contains a duplicate field name";
                return false;
            }
        }
        if (!behavior["field_schema_available"].get<bool>() && !behavior["fields"].empty())
        {
            error = "catalog schema-unavailable behavior must not include authoritative fields";
            return false;
        }
    }
    return true;
}
} // namespace

void BehaviorCatalog::configure(CatalogIdentity identity)
{
    if (identity_ == identity)
        return;
    identity_ = std::move(identity);
    ++revision_;
    if (snapshot_)
        state_ = CatalogState::Stale;
    else
    {
        state_ = CatalogState::Unavailable;
    }
}

void BehaviorCatalog::invalidate(std::string message)
{
    state_ = snapshot_ ? CatalogState::Stale : CatalogState::Unavailable;
    error_ = std::move(message);
    ++revision_;
}

CatalogState BehaviorCatalog::state() const noexcept
{
    return state_;
}

const std::string &BehaviorCatalog::last_error() const noexcept
{
    return error_;
}

std::shared_ptr<const BehaviorCatalogSnapshot> BehaviorCatalog::snapshot() const noexcept
{
    return snapshot_;
}

bool BehaviorCatalog::reject(std::string message)
{
    error_ = std::move(message);
    state_ = snapshot_ ? CatalogState::Stale : CatalogState::Unavailable;
    return false;
}

bool BehaviorCatalog::set_source_catalog(Json document, CatalogIdentity identity)
{
    std::string validationError;
    if (!valid_catalog(document, validationError))
        return reject(std::move(validationError));

    if (identity != CatalogIdentity{})
        identity_ = std::move(identity);
    auto next = std::make_shared<BehaviorCatalogSnapshot>();
    next->identity = identity_;
    next->document = std::move(document);
    snapshot_ = std::move(next);
    state_ = CatalogState::Current;
    ++revision_;
    error_.clear();
    return true;
}

bool BehaviorCatalog::load_from_source_scan(const SourceScanResult &scan, CatalogIdentity identity)
{
    if (identity == CatalogIdentity{})
        identity = identity_;

    Json document = scan.to_catalog_json();
    const bool sameProject = snapshot_ &&
        snapshot_->identity.canonicalProjectPath == identity.canonicalProjectPath;
    std::set<std::string> diagnosticTypes;
    std::set<std::string> diagnosticPaths;
    for (const auto &diagnostic : scan.diagnostics)
    {
        if (!diagnostic.typeName.empty())
            diagnosticTypes.insert(diagnostic.typeName);
        if (!diagnostic.filePath.empty())
            diagnosticPaths.insert(diagnostic.filePath.lexically_normal().generic_string());
    }

    auto oldBehaviors = [&](const std::string &name) -> const Json * {
        if (!sameProject)
            return nullptr;
        const auto &items = snapshot_->document["behaviors"];
        for (const auto &item : items)
            if (item.value("name", std::string{}) == name)
                return &item;
        return nullptr;
    };
    auto diagnostic_for = [&](const Json &behavior) {
        if (diagnosticTypes.contains(behavior.value("name", std::string{})))
            return true;
        for (const char *pathKey : {"header_path", "source_path"})
        {
            const std::string path = behavior.value(pathKey, std::string{});
            if (!path.empty() && diagnosticPaths.contains(
                    std::filesystem::path(path).lexically_normal().generic_string()))
                return true;
        }
        return false;
    };

    std::set<std::string> present;
    for (auto &behavior : document["behaviors"])
    {
        const std::string name = behavior.value("name", std::string{});
        present.insert(name);
        if (const Json *previous = oldBehaviors(name); previous != nullptr && diagnostic_for(behavior))
        {
            const std::string warning = "Source declaration is temporarily incomplete; showing the last valid schema.";
            behavior = *previous;
            behavior["scanner_warning"] = warning;
        }
    }
    if (sameProject)
    {
        for (const auto &previous : snapshot_->document["behaviors"])
        {
            const std::string name = previous.value("name", std::string{});
            if (!present.contains(name) && diagnostic_for(previous))
            {
                Json retained = previous;
                retained["scanner_warning"] =
                    "Source declaration is temporarily incomplete; showing the last valid schema.";
                document["behaviors"].push_back(std::move(retained));
                present.insert(name);
            }
        }
    }

    if (!set_source_catalog(std::move(document), std::move(identity)))
        return false;
    if (!scan.diagnostics.empty())
        error_ = std::to_string(scan.diagnostics.size()) +
                 " source schema warning(s); affected behaviors keep their last valid fields.";
    return true;
}
} // namespace platformator_editor

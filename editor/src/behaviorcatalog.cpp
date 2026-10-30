#include "behaviorcatalog.h"

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

void BehaviorCatalog::configure(CatalogProtocol protocol, CatalogIdentity identity)
{
    if (protocol_ == protocol && identity_ == identity)
        return;
    protocol_ = protocol;
    identity_ = std::move(identity);
    if (queryActive_)
    {
        queryActive_ = false;
        stdout_.clear();
        stderr_.clear();
        error_ = "catalog query identity changed before completion";
    }
    state_ = snapshot_ ? CatalogState::Stale : CatalogState::Unavailable;
}

void BehaviorCatalog::invalidate(std::string message)
{
    if (queryActive_)
    {
        queryActive_ = false;
        stdout_.clear();
        stderr_.clear();
    }
    state_ = snapshot_ ? CatalogState::Stale : CatalogState::Unavailable;
    error_ = std::move(message);
}

CatalogProtocol BehaviorCatalog::protocol() const noexcept
{
    return protocol_;
}

CatalogState BehaviorCatalog::state() const noexcept
{
    return state_;
}

bool BehaviorCatalog::can_query() const noexcept
{
    return protocol_ == CatalogProtocol::CommonRunnerV1;
}

bool BehaviorCatalog::can_build_only() const noexcept
{
    return true;
}

bool BehaviorCatalog::can_automatic_run() const noexcept
{
    return protocol_ == CatalogProtocol::CommonRunnerV1 && state_ == CatalogState::Current &&
           snapshot_ && snapshot_->identity == identity_;
}

const std::string &BehaviorCatalog::last_error() const noexcept
{
    return error_;
}

std::shared_ptr<const BehaviorCatalogSnapshot> BehaviorCatalog::snapshot() const noexcept
{
    return snapshot_;
}

bool BehaviorCatalog::begin_query()
{
    if (!can_query())
    {
        error_ = "catalog protocol is unavailable for this target";
        return false;
    }
    if (queryActive_)
    {
        error_ = "a catalog query is already active";
        return false;
    }
    stdout_.clear();
    stderr_.clear();
    error_.clear();
    outputExceeded_ = false;
    queryActive_ = true;
    return true;
}

bool BehaviorCatalog::append(std::string &stream, std::string_view bytes)
{
    if (!queryActive_ || outputExceeded_)
        return false;
    const std::size_t current = stdout_.size() + stderr_.size();
    if (current > kMaximumCatalogBytes || bytes.size() > kMaximumCatalogBytes - current)
    {
        outputExceeded_ = true;
        error_ = "catalog query exceeded the four MiB output limit";
        return false;
    }
    stream.append(bytes.data(), bytes.size());
    return true;
}

bool BehaviorCatalog::append_stdout(std::string_view bytes)
{
    return append(stdout_, bytes);
}

bool BehaviorCatalog::append_stderr(std::string_view bytes)
{
    return append(stderr_, bytes);
}

bool BehaviorCatalog::reject(std::string message)
{
    queryActive_ = false;
    error_ = std::move(message);
    state_ = snapshot_ ? CatalogState::Stale : CatalogState::Unavailable;
    stdout_.clear();
    stderr_.clear();
    return false;
}

bool BehaviorCatalog::finish_query(int exitCode, std::chrono::milliseconds elapsed, bool timedOut)
{
    if (!queryActive_)
        return false;
    if (outputExceeded_)
        return reject(error_);
    queryActive_ = false;
    if (timedOut || elapsed > kMaximumCatalogTime)
        return reject("catalog query exceeded the ten second time limit");
    if (exitCode != 0)
        return reject("catalog executable exited with status " + std::to_string(exitCode));

    Json parsed;
    try
    {
        parsed = Json::parse(stdout_);
    }
    catch (const std::exception &error)
    {
        return reject(std::string("catalog stdout is not one complete JSON document: ") + error.what());
    }
    std::string validationError;
    if (!valid_catalog(parsed, validationError))
        return reject(std::move(validationError));

    auto next = std::make_shared<BehaviorCatalogSnapshot>();
    next->identity = identity_;
    next->document = std::move(parsed);
    snapshot_ = std::move(next);
    state_ = CatalogState::Current;
    error_.clear();
    stdout_.clear();
    stderr_.clear();
    return true;
}

BehaviorCatalogQuery::~BehaviorCatalogQuery()
{
    if (catalog_ != nullptr)
        catalog_->finish_query(-1, std::chrono::milliseconds::zero(), true);
    process_.stop();
}

bool BehaviorCatalogQuery::start(BehaviorCatalog &catalog, ProcessRequest request,
                                 std::uint64_t generation)
{
    if (process_.running() || catalog_ != nullptr || !catalog.begin_query())
        return false;
    catalog_ = &catalog;
    generation_ = generation;
    startedAt_ = std::chrono::steady_clock::now();
    stopRequested_ = false;
    timedOut_ = false;
    request.outputMode = ProcessOutputMode::Separate;
    const bool started = process_.start(
        std::move(request), generation,
        [this](ProcessOutput output) {
            if (catalog_ == nullptr || output.generation != generation_)
                return;
            const bool accepted = output.stream == ProcessStream::Stderr
                                      ? catalog_->append_stderr(output.bytes)
                                      : catalog_->append_stdout(output.bytes);
            if (!accepted)
                stopRequested_ = true;
        },
        [this](ProcessCompletion completion) {
            if (catalog_ == nullptr || completion.generation != generation_)
                return;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt_);
            catalog_->finish_query(completion.launched ? completion.exitCode : -1,
                                   elapsed, timedOut_);
            catalog_ = nullptr;
        });
    if (!started)
    {
        catalog.finish_query(-1, std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - startedAt_));
        catalog_ = nullptr;
        return false;
    }
    return true;
}

void BehaviorCatalogQuery::poll()
{
    if (catalog_ != nullptr &&
        std::chrono::steady_clock::now() - startedAt_ > kMaximumCatalogTime)
    {
        timedOut_ = true;
        stopRequested_ = true;
    }
    if (stopRequested_ && process_.running())
    {
        process_.stop();
        stopRequested_ = false;
    }
    process_.poll();
}

void BehaviorCatalogQuery::stop()
{
    if (catalog_ == nullptr)
        return;
    stopRequested_ = true;
    process_.stop();
}

bool BehaviorCatalogQuery::running() const noexcept
{
    return catalog_ != nullptr && process_.running();
}
} // namespace platformator_editor

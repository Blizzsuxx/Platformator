#include "spatialreplay.h"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace platformator_detail
{
namespace
{
using Json = nlohmann::json;

[[noreturn]] void invalid(const std::string &message)
{
    throw std::runtime_error("Invalid spatial trace: " + message);
}

void requireObject(const Json &value, const char *where)
{
    if (!value.is_object())
        invalid(std::string(where) + " must be an object");
}

void requireFields(const Json &value, std::initializer_list<std::string_view> required,
                   std::initializer_list<std::string_view> optional, const char *where)
{
    requireObject(value, where);
    std::set<std::string_view> allowed(required.begin(), required.end());
    allowed.insert(optional.begin(), optional.end());
    for (const auto &[key, _] : value.items())
        if (!allowed.contains(key))
            invalid(std::string(where) + " contains unknown field '" + key + "'");
    for (const auto key : required)
        if (!value.contains(std::string(key)))
            invalid(std::string(where) + " is missing field '" + std::string(key) + "'");
}

double finiteNumber(const Json &value, const char *where)
{
    if (!value.is_number())
        invalid(std::string(where) + " must be numeric");
    const double number = value.get<double>();
    if (!std::isfinite(number))
        invalid(std::string(where) + " must be finite");
    return number;
}

uint64_t unsignedNumber(const Json &value, const char *where)
{
    if (value.is_number_unsigned())
        return value.get<uint64_t>();
    if (!value.is_number_integer())
        invalid(std::string(where) + " must be an unsigned integer");
    const int64_t signedValue = value.get<int64_t>();
    if (signedValue < 0)
        invalid(std::string(where) + " must be an unsigned integer");
    return static_cast<uint64_t>(signedValue);
}

uint64_t decimalString(const Json &value, const char *where)
{
    if (!value.is_string())
        invalid(std::string(where) + " must be a uint64 decimal string");
    const std::string text = value.get<std::string>();
    uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        std::to_string(parsed) != text)
        invalid(std::string(where) + " must be a canonical uint64 decimal string");
    return parsed;
}

SpatialTraceIdentity identity(const Json &value)
{
    requireFields(value, {"slot", "generation"}, {}, "identity");
    SpatialTraceIdentity result{decimalString(value.at("slot"), "identity.slot"),
                                decimalString(value.at("generation"), "identity.generation")};
    if (result.generation == 0)
        invalid("identity.generation must be greater than zero");
    return result;
}

uint64_t filterBits(const Json &value, const char *where)
{
    return decimalString(value, where);
}

std::array<double, 2> pair(const Json &value, const char *where)
{
    if (!value.is_array() || value.size() != 2)
        invalid(std::string(where) + " must contain two numbers");
    return {finiteNumber(value[0], where), finiteNumber(value[1], where)};
}

std::array<double, 4> bounds(const Json &value, const char *where)
{
    if (!value.is_array() || value.size() != 4)
        invalid(std::string(where) + " must contain four numbers");
    std::array<double, 4> result{};
    for (size_t index = 0; index < result.size(); ++index)
        result[index] = finiteNumber(value[index], where);
    if (result[0] > result[2] || result[1] > result[3])
        invalid(std::string(where) + " must be ordered minX,minY,maxX,maxY");
    for (double coordinate : result)
        if (!std::isfinite(static_cast<float>(coordinate)))
            invalid(std::string(where) + " exceeds finite float bounds");
    return result;
}

bool boolean(const Json &value, const char *where)
{
    if (!value.is_boolean())
        invalid(std::string(where) + " must be boolean");
    return value.get<bool>();
}

bool isSha256(const std::string &value)
{
    if (value.size() != 64)
        return false;
    for (const unsigned char character : value)
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F')))
            return false;
    return true;
}

void requireFiniteTree(const Json &value)
{
    if (value.is_number())
    {
        if (!std::isfinite(value.get<double>()))
            invalid("parameters contain a nonfinite number");
    }
    else if (value.is_array())
        for (const auto &element : value)
            requireFiniteTree(element);
    else if (value.is_object())
        for (const auto &[_, element] : value.items())
            requireFiniteTree(element);
}

struct IdentityLedger
{
    std::unordered_map<uint64_t, SpatialTraceIdentity> live;
    std::unordered_map<uint64_t, uint64_t> lastGeneration;

    void add(const SpatialTraceIdentity &id)
    {
        if (live.contains(id.slot))
            invalid("duplicate live slot " + std::to_string(id.slot));
        auto previous = lastGeneration.find(id.slot);
        if (previous != lastGeneration.end() && id.generation <= previous->second)
            invalid("reused slot " + std::to_string(id.slot) + " must increase its generation");
        lastGeneration[id.slot] = id.generation;
        live[id.slot] = id;
    }

    void remove(const SpatialTraceIdentity &id)
    {
        auto current = live.find(id.slot);
        if (current == live.end() || current->second != id)
            invalid("operation references a removed or stale endpoint generation");
        live.erase(current);
    }

    void requireLive(const SpatialTraceIdentity &id) const
    {
        auto current = live.find(id.slot);
        if (current == live.end() || current->second != id)
            invalid("operation references a removed or stale endpoint generation");
    }
};

CollisionTraceEndpoint collisionEndpoint(const Json &value)
{
    requireFields(value, {"id", "bounds", "group", "mask", "active"}, {}, "collider");
    const auto box = bounds(value.at("bounds"), "collider.bounds");
    return {identity(value.at("id")), static_cast<float>(box[0]), static_cast<float>(box[1]),
            static_cast<float>(box[2]), static_cast<float>(box[3]), filterBits(value.at("group"), "collider.group"),
            filterBits(value.at("mask"), "collider.mask"), boolean(value.at("active"), "collider.active")};
}

VisibilityTraceSprite visibilitySprite(const Json &value)
{
    requireFields(value, {"id", "center", "dimensions", "scale", "angle_degrees", "active"}, {}, "sprite");
    const auto center = pair(value.at("center"), "sprite.center");
    const auto dimensions = pair(value.at("dimensions"), "sprite.dimensions");
    const auto scale = pair(value.at("scale"), "sprite.scale");
    const double angle = finiteNumber(value.at("angle_degrees"), "sprite.angle_degrees");
    if (dimensions[0] <= 0.0 || dimensions[1] <= 0.0 || scale[0] == 0.0 || scale[1] == 0.0)
        invalid("sprite dimensions must be positive and scale components nonzero");
    return {identity(value.at("id")), center[0], center[1], dimensions[0], dimensions[1], scale[0], scale[1], angle,
            boolean(value.at("active"), "sprite.active")};
}

VisibilityTraceView visibilityView(const Json &value)
{
    requireFields(value, {"id", "position", "size", "output"}, {}, "view");
    const auto position = pair(value.at("position"), "view.position");
    const auto size = pair(value.at("size"), "view.size");
    const Json &output = value.at("output");
    if (!output.is_array() || output.size() != 2)
        invalid("view.output must contain two positive integers");
    const uint64_t width = unsignedNumber(output[0], "view.output.width");
    const uint64_t height = unsignedNumber(output[1], "view.output.height");
    if (width == 0 || height == 0 || width > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        invalid("view.output dimensions must be positive and fit in int");
    if (size[0] <= 0.0 || size[1] <= 0.0)
        invalid("view.size dimensions must be positive");
    return {identity(value.at("id")), position[0], position[1], size[0], size[1], static_cast<int>(width),
            static_cast<int>(height)};
}

SpatialTraceHeader parseHeader(const Json &value)
{
    requireFields(value, {"type", "format", "version", "domain", "case_id", "seed", "units", "dt",
                          "warmup_steps", "measure_steps", "generator", "parameters"}, {}, "header");
    if (value.at("type") != "header" || value.at("format") != "platformator-spatial-trace" ||
        unsignedNumber(value.at("version"), "header.version") != 1)
        invalid("unsupported header type, format or version");
    SpatialTraceHeader header;
    const std::string domain = value.at("domain").get<std::string>();
    if (domain == "collision")
        header.domain = SpatialTraceDomain::Collision;
    else if (domain == "visibility")
        header.domain = SpatialTraceDomain::Visibility;
    else
        invalid("domain must be collision or visibility");
    if (!value.at("case_id").is_string() || value.at("case_id").get<std::string>().empty())
        invalid("header.case_id must be a nonempty string");
    header.caseId = value.at("case_id").get<std::string>();
    if (value.at("units") != "world")
        invalid("header.units must be world");
    header.seed = unsignedNumber(value.at("seed"), "header.seed");
    header.dt = finiteNumber(value.at("dt"), "header.dt");
    if (header.dt <= 0.0)
        invalid("header.dt must be positive");
    const uint64_t warmup = unsignedNumber(value.at("warmup_steps"), "header.warmup_steps");
    const uint64_t measure = unsignedNumber(value.at("measure_steps"), "header.measure_steps");
    if (measure == 0 || warmup > std::numeric_limits<size_t>::max() ||
        measure > std::numeric_limits<size_t>::max() - warmup)
        invalid("header step counts are invalid or overflow size_t");
    header.warmupSteps = static_cast<size_t>(warmup);
    header.measureSteps = static_cast<size_t>(measure);

    const Json &generator = value.at("generator");
    requireFields(generator, {"name", "revision", "source_hash"}, {}, "header.generator");
    for (const char *key : {"name", "revision", "source_hash"})
        if (!generator.at(key).is_string() || generator.at(key).get<std::string>().empty())
            invalid(std::string("header.generator.") + key + " must be a nonempty string");
    if (!isSha256(generator.at("source_hash").get<std::string>()))
        invalid("header.generator.source_hash must be a SHA-256 hex digest");
    header.generator = generator;

    const Json &parameters = value.at("parameters");
    requireObject(parameters, "header.parameters");
    if (!parameters.contains("population") ||
        (!parameters.at("population").is_number_integer() && !parameters.at("population").is_number_unsigned()))
        invalid("header.parameters.population must be a nonnegative integer");
    (void)unsignedNumber(parameters.at("population"), "header.parameters.population");
    if (!parameters.contains("bounds_source") || !parameters.at("bounds_source").is_string())
        invalid("header.parameters.bounds_source is required");
    const std::string expectedBounds = header.domain == SpatialTraceDomain::Collision ? "provided-aabb"
                                                                                       : "sprite-transform";
    if (parameters.at("bounds_source") != expectedBounds)
        invalid("header.parameters.bounds_source does not match its trace domain");
    if (header.domain == SpatialTraceDomain::Visibility &&
        (!parameters.contains("renderer") || parameters.at("renderer") != "none-replay"))
        invalid("visibility replay must declare renderer none-replay");
    requireFiniteTree(parameters);
    header.parameters = parameters;
    return header;
}

void validateOperation(const Json &operation, SpatialTraceDomain domain, IdentityLedger &ledger,
                       SpatialTraceIdentity &cameraId)
{
    requireObject(operation, "operation");
    if (!operation.contains("op") || !operation.at("op").is_string())
        invalid("operation.op must be a string");
    const std::string name = operation.at("op").get<std::string>();
    if (domain == SpatialTraceDomain::Collision)
    {
        if (name == "add")
        {
            requireFields(operation, {"op", "collider"}, {}, "collision add operation");
            ledger.add(collisionEndpoint(operation.at("collider")).id);
        }
        else if (name == "remove")
        {
            requireFields(operation, {"op", "id"}, {}, "collision remove operation");
            ledger.remove(identity(operation.at("id")));
        }
        else if (name == "update_bounds")
        {
            requireFields(operation, {"op", "id", "bounds"}, {}, "update_bounds operation");
            ledger.requireLive(identity(operation.at("id")));
            (void)bounds(operation.at("bounds"), "update_bounds.bounds");
        }
        else if (name == "set_filter")
        {
            requireFields(operation, {"op", "id", "group", "mask"}, {}, "set_filter operation");
            ledger.requireLive(identity(operation.at("id")));
            (void)filterBits(operation.at("group"), "set_filter.group");
            (void)filterBits(operation.at("mask"), "set_filter.mask");
        }
        else if (name == "set_active")
        {
            requireFields(operation, {"op", "id", "active"}, {}, "collision set_active operation");
            ledger.requireLive(identity(operation.at("id")));
            (void)boolean(operation.at("active"), "set_active.active");
        }
        else
            invalid("unsupported collision operation '" + name + "'");
        return;
    }

    if (name == "add")
    {
        requireFields(operation, {"op", "sprite"}, {}, "visibility add operation");
        ledger.add(visibilitySprite(operation.at("sprite")).id);
    }
    else if (name == "remove")
    {
        requireFields(operation, {"op", "id"}, {}, "visibility remove operation");
        ledger.remove(identity(operation.at("id")));
    }
    else if (name == "update_sprite")
    {
        requireFields(operation, {"op", "id"}, {"center", "dimensions", "scale", "angle_degrees"},
                      "update_sprite operation");
        ledger.requireLive(identity(operation.at("id")));
        if (operation.size() == 2)
            invalid("update_sprite requires at least one geometry field");
        if (operation.contains("center"))
            (void)pair(operation.at("center"), "update_sprite.center");
        if (operation.contains("dimensions"))
        {
            const auto dimensions = pair(operation.at("dimensions"), "update_sprite.dimensions");
            if (dimensions[0] <= 0.0 || dimensions[1] <= 0.0)
                invalid("update_sprite dimensions must be positive");
        }
        if (operation.contains("scale"))
        {
            const auto scale = pair(operation.at("scale"), "update_sprite.scale");
            if (scale[0] == 0.0 || scale[1] == 0.0)
                invalid("update_sprite scale components must be nonzero");
        }
        if (operation.contains("angle_degrees"))
            (void)finiteNumber(operation.at("angle_degrees"), "update_sprite.angle_degrees");
    }
    else if (name == "set_active")
    {
        requireFields(operation, {"op", "id", "active"}, {}, "visibility set_active operation");
        ledger.requireLive(identity(operation.at("id")));
        (void)boolean(operation.at("active"), "set_active.active");
    }
    else if (name == "set_view")
    {
        requireFields(operation, {"op", "view"}, {}, "set_view operation");
        const auto next = visibilityView(operation.at("view"));
        if (!(next.id == cameraId))
        {
            ledger.remove(cameraId);
            ledger.add(next.id);
            cameraId = next.id;
        }
    }
    else
        invalid("unsupported visibility operation '" + name + "'");
}

void parseInitial(const Json &initial, SpatialTrace &trace, IdentityLedger &ledger)
{
    if (trace.header.domain == SpatialTraceDomain::Collision)
    {
        requireFields(initial, {"type", "colliders"}, {}, "collision initial state");
        if (initial.at("type") != "initial" || !initial.at("colliders").is_array())
            invalid("collision initial state requires a collider array");
        for (const auto &value : initial.at("colliders"))
        {
            auto endpoint = collisionEndpoint(value);
            ledger.add(endpoint.id);
            trace.collisionInitial.push_back(endpoint);
        }
        return;
    }

    requireFields(initial, {"type", "sprites", "view"}, {}, "visibility initial state");
    if (initial.at("type") != "initial" || !initial.at("sprites").is_array())
        invalid("visibility initial state requires a sprite array");
    for (const auto &value : initial.at("sprites"))
    {
        auto sprite = visibilitySprite(value);
        ledger.add(sprite.id);
        trace.visibilityInitial.push_back(sprite);
    }
    trace.visibilityView = visibilityView(initial.at("view"));
    ledger.add(trace.visibilityView.id);
}
} // namespace

SpatialTrace parseSpatialTrace(std::istream &input)
{
    std::vector<Json> records;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.empty())
            invalid("blank line at " + std::to_string(lineNumber));
        try
        {
            records.push_back(Json::parse(line));
        }
        catch (const std::exception &error)
        {
            invalid("JSON parse error on line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (input.bad())
        invalid("failed while reading input");
    if (records.size() < 3)
        invalid("expected a header, initial state and at least one step");

    SpatialTrace trace;
    trace.header = parseHeader(records[0]);
    IdentityLedger ledger;
    parseInitial(records[1], trace, ledger);
    const auto population = unsignedNumber(trace.header.parameters.at("population"), "header.parameters.population");
    const size_t initialPopulation = trace.header.domain == SpatialTraceDomain::Collision
                                         ? trace.collisionInitial.size()
                                         : trace.visibilityInitial.size();
    if (population != initialPopulation)
        invalid("header.parameters.population does not match the initial endpoint count");
    SpatialTraceIdentity cameraId = trace.visibilityView.id;
    const size_t expectedSteps = trace.header.warmupSteps + trace.header.measureSteps;
    if (records.size() != expectedSteps + 2)
        invalid("record count does not equal declared warmup_steps + measure_steps");
    trace.steps.reserve(expectedSteps);
    for (size_t index = 0; index < expectedSteps; ++index)
    {
        const auto &record = records[index + 2];
        requireFields(record, {"type", "index", "ops"}, {}, "step");
        if (record.at("type") != "step" || unsignedNumber(record.at("index"), "step.index") != index ||
            !record.at("ops").is_array())
            invalid("step type/index/ops do not match the declared sequence");
        SpatialTraceStep step;
        step.index = index;
        for (const auto &operation : record.at("ops"))
        {
            validateOperation(operation, trace.header.domain, ledger, cameraId);
            step.operations.push_back(operation);
        }
        trace.steps.push_back(std::move(step));
    }
    return trace;
}

SpatialTrace parseSpatialTraceFile(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unable to open spatial trace: " + path);
    try
    {
        return parseSpatialTrace(input);
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Spatial trace '" + path + "': " + error.what());
    }
}
} // namespace platformator_detail

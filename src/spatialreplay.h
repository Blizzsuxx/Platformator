#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include <json.hpp>

#include "platformator/spatialsettings.h"

namespace platformator_detail
{
enum class SpatialTraceDomain : uint8_t
{
    Collision,
    Visibility
};

struct SpatialTraceIdentity
{
    uint64_t slot = 0;
    uint64_t generation = 0;
    bool operator==(const SpatialTraceIdentity &) const = default;
};

struct SpatialTraceHeader
{
    SpatialTraceDomain domain = SpatialTraceDomain::Collision;
    std::string caseId;
    uint64_t seed = 0;
    double dt = 0.0;
    size_t warmupSteps = 0;
    size_t measureSteps = 0;
    nlohmann::json generator;
    nlohmann::json parameters;
};

struct CollisionTraceEndpoint
{
    SpatialTraceIdentity id;
    float minX = 0;
    float minY = 0;
    float maxX = 0;
    float maxY = 0;
    uint64_t group = 0;
    uint64_t mask = 0;
    bool active = true;
};

struct VisibilityTraceSprite
{
    SpatialTraceIdentity id;
    double centerX = 0;
    double centerY = 0;
    double width = 0;
    double height = 0;
    double scaleX = 1;
    double scaleY = 1;
    double angleDegrees = 0;
    bool active = true;
};

struct VisibilityTraceView
{
    SpatialTraceIdentity id;
    double positionX = 0;
    double positionY = 0;
    double width = 0;
    double height = 0;
    int outputWidth = 0;
    int outputHeight = 0;
};

struct SpatialTraceStep
{
    size_t index = 0;
    std::vector<nlohmann::json> operations;
};

struct SpatialTrace
{
    SpatialTraceHeader header;
    std::vector<CollisionTraceEndpoint> collisionInitial;
    std::vector<VisibilityTraceSprite> visibilityInitial;
    VisibilityTraceView visibilityView;
    std::vector<SpatialTraceStep> steps;
};

struct SpatialTracePair
{
    SpatialTraceIdentity first;
    SpatialTraceIdentity second;
    bool operator==(const SpatialTracePair &) const = default;
    bool operator<(const SpatialTracePair &other) const
    {
        return first.slot != other.first.slot ? first.slot < other.first.slot
             : first.generation != other.first.generation ? first.generation < other.first.generation
             : second.slot != other.second.slot ? second.slot < other.second.slot
                                                : second.generation < other.second.generation;
    }
};

struct SpatialReplayCounters
{
    uint64_t boundsPrepareNanoseconds = 0;
    uint64_t indexUpdateNanoseconds = 0;
    uint64_t indexMaintenanceNanoseconds = 0;
    uint64_t indexQueryNanoseconds = 0;
    uint64_t resultReduceNanoseconds = 0;
    size_t candidatePairCount = 0;
    size_t proxyCount = 0;
    size_t changedBounds = 0;
    size_t filterChanges = 0;
    size_t proxyInserts = 0;
    size_t proxyRemoves = 0;
    size_t proxyMoves = 0;
    size_t envelopeReuse = 0;
    size_t queries = 0;
    size_t rawDiscoveries = 0;
    size_t exactOverlaps = 0;
    size_t pairAdds = 0;
    size_t pairRemoves = 0;
    size_t rebuildCalls = 0;
    size_t rebuildItems = 0;
    size_t treeHeight = 0;
    size_t treeNodes = 0;
    size_t spriteCount = 0;
    size_t visibleCandidates = 0;
    size_t uncullableSprites = 0;
    size_t cullingBypasses = 0;
    size_t visibilityCacheHits = 0;
    size_t visibilityCells = 0;
};

struct SpatialReplaySample
{
    uint64_t durationNanoseconds = 0;
    uint64_t outputHash = 0;
    SpatialReplayCounters counters;
    std::vector<std::string> output;
};

struct SpatialReplayRun
{
    uint64_t initializationNanoseconds = 0;
    SpatialReplayCounters initialCounters;
    uint64_t initialOutputHash = 0;
    std::vector<std::string> initialOutput;
    std::vector<SpatialReplaySample> measuredSamples;
};

// Resource-only observation point. Capacities are sampled after replay initialization and after
// each complete replay step, outside that step's measured interval.
struct SpatialReplayOwnedCapacitySnapshot
{
    uint64_t physicsIndexBytes = 0;
    uint64_t physicsCommonPairsBytes = 0;
    uint64_t visibilityIndexBytes = 0;
    uint64_t scratchBytes = 0;
};

using SpatialReplayResourceObserver = void (*)(const SpatialReplayOwnedCapacitySnapshot &, bool initial,
                                                void *context);

// Loads and validates the complete trace before a caller enters a measured region.
SpatialTrace parseSpatialTrace(std::istream &input);
SpatialTrace parseSpatialTraceFile(const std::string &path);
SpatialReplayRun executeSpatialReplay(const SpatialTrace &trace, const SpatialSettings &settings, bool validate,
                                      SpatialReplayResourceObserver resourceObserver = nullptr,
                                      void *resourceObserverContext = nullptr);
} // namespace platformator_detail

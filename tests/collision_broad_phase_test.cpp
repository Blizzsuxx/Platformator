#include "dynamicaabbtree.h"
#include "spatialindex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spatial_test_support.h"

namespace
{
using platformator_detail::DynamicAabbTree;
using platformator_detail::SegmentedSpatialContext;
using platformator_detail::SpatialBounds;
using platformator_detail::SpatialPairPolicy;
using spatial_test_support::CanonicalPair;
using spatial_test_support::EndpointIdentity;
using spatial_test_support::EndpointState;
using spatial_test_support::SpatialResult;

constexpr uint64_t RuntimeSerial = 913;
constexpr float CellSize = 32.0f;
constexpr float Padding = 8.0f;
uint64_t snapshotFnv64 = UINT64_C(14695981039346656037);
uint64_t inputFnv64 = UINT64_C(14695981039346656037);
size_t comparedSnapshots = 0;
size_t exactPairObservations = 0;
size_t transitionAdds = 0;
size_t transitionRemoves = 0;

void hashSnapshot(const std::string &text)
{
    for (unsigned char byte : text)
    {
        snapshotFnv64 ^= byte;
        snapshotFnv64 *= UINT64_C(1099511628211);
    }
}

void hashInput(const std::string &text)
{
    for (unsigned char byte : text)
    {
        inputFnv64 ^= byte;
        inputFnv64 *= UINT64_C(1099511628211);
    }
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Entry
{
    EndpointState state;
    BoundingRadiusProjectionAxis xAxis;
    BoundingRadiusProjectionAxis yAxis;
    uint64_t token;
    DynamicAabbTree::ProxyId proxy = -1;
    SpatialBounds envelope{};
    bool segmentedRegistered = false;
    bool geometryDirty = true;
    bool filterDirty = true;
    bool retired = false;

    Entry(uint32_t slot, uint64_t generation, uint64_t token, SpatialBounds bounds, uint64_t group,
          uint64_t mask, bool active)
        : state{{42, {slot, generation, RuntimeSerial}},
                {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY}, group, mask, active},
          xAxis(this, ProjectionOwnerKind::Visibility, 0, 0), yAxis(this, ProjectionOwnerKind::Visibility, 0, 0),
          token(token)
    {
    }

    ProjectionOwner owner()
    {
        return {this, ProjectionOwnerKind::Visibility};
    }

    SpatialBounds bounds() const
    {
        return {static_cast<float>(state.bounds.minX), static_cast<float>(state.bounds.minY),
                static_cast<float>(state.bounds.maxX), static_cast<float>(state.bounds.maxY)};
    }
};

DynamicAabbTree::Bounds toTreeBounds(SpatialBounds bounds)
{
    return {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
}

bool contains(const SpatialBounds &outer, const SpatialBounds &inner)
{
    return outer.minX <= inner.minX && outer.minY <= inner.minY && outer.maxX >= inner.maxX &&
           outer.maxY >= inner.maxY;
}

SpatialBounds padded(SpatialBounds bounds)
{
    return bounds.padded(Padding);
}

bool collectTreeHit(DynamicAabbTree::ProxyId, uint64_t userData, void *context)
{
    static_cast<std::vector<uint64_t> *>(context)->push_back(userData);
    return true;
}

class MatchedBroadPhases
{
  public:
    MatchedBroadPhases()
        : segmented(CellSize, SpatialPairPolicy{nullptr, ProjectionOwnerKind::Visibility, nullptr, nullptr})
    {
    }

    Entry &create(uint32_t slot, uint64_t generation, SpatialBounds bounds, uint64_t group = 1,
                  uint64_t mask = UINT64_MAX, bool active = true)
    {
        std::ostringstream operation;
        operation << std::setprecision(9) << "create " << slot << ':' << generation << ' ' << bounds.minX << ' '
                  << bounds.minY << ' ' << bounds.maxX << ' ' << bounds.maxY << ' ' << group << ' ' << mask << ' '
                  << active << '\n';
        hashInput(operation.str());
        auto entry = std::make_unique<Entry>(slot, generation, nextToken++, bounds, group, mask, active);
        Entry &result = *entry;
        byToken.emplace(entry->token, entry.get());
        entries.push_back(std::move(entry));
        dirty.push_back(&result);
        return result;
    }

    void setBounds(Entry &entry, SpatialBounds bounds)
    {
        std::ostringstream operation;
        operation << std::setprecision(9) << "bounds " << entry.state.identity.endpoint.slot << ':'
                  << entry.state.identity.endpoint.generation << ' ' << bounds.minX << ' ' << bounds.minY << ' '
                  << bounds.maxX << ' ' << bounds.maxY << '\n';
        hashInput(operation.str());
        entry.state.bounds = {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
        entry.geometryDirty = true;
        appendDirty(entry);
    }

    void setFilter(Entry &entry, uint64_t group, uint64_t mask)
    {
        hashInput("filter " + std::to_string(entry.state.identity.endpoint.slot) + ':' +
                  std::to_string(entry.state.identity.endpoint.generation) + ' ' + std::to_string(group) + ' ' +
                  std::to_string(mask) + '\n');
        entry.state.group = group;
        entry.state.mask = mask;
        entry.filterDirty = true;
        appendDirty(entry);
    }

    void setActive(Entry &entry, bool active)
    {
        hashInput("active " + std::to_string(entry.state.identity.endpoint.slot) + ':' +
                  std::to_string(entry.state.identity.endpoint.generation) + ' ' + std::to_string(active) + '\n');
        entry.state.active = active;
        appendDirty(entry);
    }

    void retire(Entry &entry)
    {
        hashInput("retire " + std::to_string(entry.state.identity.endpoint.slot) + ':' +
                  std::to_string(entry.state.identity.endpoint.generation) + '\n');
        entry.retired = true;
        entry.state.active = false;
        appendDirty(entry);
    }

    size_t publish()
    {
        std::sort(dirty.begin(), dirty.end(), [](const Entry *a, const Entry *b) { return a->token < b->token; });
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
        const size_t coalescedCount = dirty.size();
        for (Entry *entry : dirty)
        {
            const bool shouldBeRegistered = entry->state.active && !entry->retired;
            if (!shouldBeRegistered)
            {
                if (entry->segmentedRegistered)
                {
                    segmented.remove(entry->owner());
                    entry->segmentedRegistered = false;
                }
                if (entry->proxy >= 0)
                {
                    tree.remove(entry->proxy);
                    entry->proxy = -1;
                }
                entry->geometryDirty = false;
                entry->filterDirty = false;
                continue;
            }

            const SpatialBounds exact = entry->bounds();
            if (entry->proxy < 0)
            {
                entry->envelope = padded(exact);
                entry->proxy = tree.add(toTreeBounds(entry->envelope), entry->state.group, entry->token);
                segmented.insert(entry->owner(), &entry->xAxis, &entry->yAxis, exact);
                entry->segmentedRegistered = true;
            }
            else
            {
                if (entry->geometryDirty)
                {
                    segmented.update(entry->owner(), exact);
                    const SpatialBounds nextEnvelope = padded(exact);
                    if (!contains(entry->envelope, nextEnvelope))
                    {
                        tree.update(entry->proxy, toTreeBounds(nextEnvelope));
                        entry->envelope = nextEnvelope;
                    }
                    else
                    {
                        ++containedMoves;
                    }
                }
                if (entry->filterDirty)
                    tree.setCategoryBits(entry->proxy, entry->state.group);
            }
            entry->geometryDirty = false;
            entry->filterDirty = false;
        }
        dirty.clear();
        ++publishCount;
        return coalescedCount;
    }

    std::vector<EndpointState> activeStates() const
    {
        std::vector<EndpointState> result;
        for (const auto &entry : entries)
            if (entry->state.active && !entry->retired)
                result.push_back(entry->state);
        return result;
    }

    SpatialResult resultForSegmented()
    {
        std::set<CanonicalPair> output;
        std::vector<Entry *> active;
        for (const auto &entry : entries)
            if (entry->state.active && !entry->retired && entry->segmentedRegistered)
                active.push_back(entry.get());
        for (size_t i = 0; i < active.size(); ++i)
            for (size_t j = i + 1; j < active.size(); ++j)
            {
                Entry &a = *active[i];
                Entry &b = *active[j];
                if (!segmented.containsPair(a.owner(), b.owner()))
                    continue;
                if (eligibleExact(a, b))
                    output.insert(CanonicalPair::make(a.state.identity, b.state.identity));
            }
        return withTransitions(output, previousSegmented);
    }

    SpatialResult resultForTree()
    {
        std::set<CanonicalPair> raw;
        for (const auto &entryPtr : entries)
        {
            const Entry &a = *entryPtr;
            if (!a.state.active || a.retired || a.proxy < 0)
                continue;
            std::vector<uint64_t> hits;
            tree.query(toTreeBounds(padded(a.bounds())), a.state.mask, collectTreeHit, &hits);
            for (uint64_t token : hits)
            {
                auto found = byToken.find(token);
                if (found == byToken.end())
                    throw std::logic_error("Tree query returned an unknown or retired stable record");
                const Entry &b = *found->second;
                if (b.state.active && !b.retired && a.token != b.token && (b.state.group & a.state.mask) != 0)
                    raw.insert(CanonicalPair::make(a.state.identity, b.state.identity));
            }
        }

        std::set<CanonicalPair> exact;
        for (const CanonicalPair &pair : raw)
        {
            const Entry *a = find(pair.first);
            const Entry *b = find(pair.second);
            if (a != nullptr && b != nullptr && eligibleExact(*a, *b))
                exact.insert(pair);
        }
        return withTransitions(exact, previousTree);
    }

    const std::vector<std::unique_ptr<Entry>> &allEntries() const { return entries; }
    size_t getContainedMoveCount() const { return containedMoves; }
    size_t getPublishCount() const { return publishCount; }

  private:
    static bool eligibleExact(const Entry &a, const Entry &b)
    {
        return (a.state.group & b.state.mask) != 0 && (b.state.group & a.state.mask) != 0 &&
               a.state.bounds.overlapsInclusive(b.state.bounds);
    }

    static SpatialResult withTransitions(const std::set<CanonicalPair> &current,
                                         std::set<CanonicalPair> &previous)
    {
        SpatialResult result;
        result.candidates.assign(current.begin(), current.end());
        std::set_difference(current.begin(), current.end(), previous.begin(), previous.end(),
                            std::back_inserter(result.added));
        std::set_difference(previous.begin(), previous.end(), current.begin(), current.end(),
                            std::back_inserter(result.removed));
        previous = current;
        return result;
    }

    const Entry *find(const EndpointIdentity &identity) const
    {
        for (const auto &entry : entries)
            if (entry->state.identity == identity)
                return entry.get();
        return nullptr;
    }

    void appendDirty(Entry &entry)
    {
        dirty.push_back(&entry);
    }

    // Declare endpoint storage before the context so context teardown removes projections while
    // their owners and axes are still alive.
    std::vector<std::unique_ptr<Entry>> entries;
    std::unordered_map<uint64_t, Entry *> byToken;
    DynamicAabbTree tree;
    SegmentedSpatialContext segmented;
    std::vector<Entry *> dirty;
    uint64_t nextToken = 1;
    size_t containedMoves = 0;
    size_t publishCount = 0;
    std::set<CanonicalPair> previousSegmented;
    std::set<CanonicalPair> previousTree;
};

SpatialBounds rotatedBox(float x, float y, float halfWidth, float halfHeight, float radians)
{
    const float c = std::abs(std::cos(radians));
    const float s = std::abs(std::sin(radians));
    const float extentX = c * halfWidth + s * halfHeight;
    const float extentY = s * halfWidth + c * halfHeight;
    return {x - extentX, y - extentY, x + extentX, y + extentY};
}

void compareStep(MatchedBroadPhases &fixture, const std::string &seedAndStep)
{
    const auto expected = spatial_test_support::exactOverlapOracle(fixture.activeStates());
    const SpatialResult segmented = fixture.resultForSegmented();
    const SpatialResult tree = fixture.resultForTree();
    auto formatPairs = [](const std::vector<CanonicalPair> &pairs) {
        std::ostringstream out;
        for (const auto &pair : pairs)
            out << " (" << pair.first.endpoint.slot << ':' << pair.first.endpoint.generation << ','
                << pair.second.endpoint.slot << ':' << pair.second.endpoint.generation << ')';
        return out.str();
    };
    auto state = [&] {
        std::ostringstream out;
        for (const auto &entry : fixture.allEntries())
        {
            const auto &e = *entry;
            out << "\n slot=" << e.state.identity.endpoint.slot << " generation="
                << e.state.identity.endpoint.generation << " active=" << e.state.active << " bounds=["
                << e.state.bounds.minX << ',' << e.state.bounds.minY << ',' << e.state.bounds.maxX << ','
                << e.state.bounds.maxY << "] group=" << e.state.group << " mask=" << e.state.mask;
        }
        return out.str();
    };
    const std::string context = seedAndStep + state();
    require(segmented.candidates == expected, "Segmented result differs from exact oracle" + context +
                                                    "\nexpected:" + formatPairs(expected) +
                                                    "\nactual:" + formatPairs(segmented.candidates));
    require(tree.candidates == expected, "Dynamic tree result differs from exact oracle" + context +
                                               "\nexpected:" + formatPairs(expected) +
                                               "\nactual:" + formatPairs(tree.candidates));
    require(segmented.added == tree.added && segmented.removed == tree.removed,
            "Matched backend transitions differ" + context + "\nsegmented added:" +
                formatPairs(segmented.added) + "\ntree added:" + formatPairs(tree.added) +
                "\nsegmented removed:" + formatPairs(segmented.removed) +
                "\ntree removed:" + formatPairs(tree.removed));
    std::ostringstream snapshot;
    snapshot << context << "\nexact:" << formatPairs(expected) << "\nsegmented-add:" << formatPairs(segmented.added)
             << "\nsegmented-remove:" << formatPairs(segmented.removed) << "\ntree-add:" << formatPairs(tree.added)
             << "\ntree-remove:" << formatPairs(tree.removed) << '\n';
    hashSnapshot(snapshot.str());
    ++comparedSnapshots;
    exactPairObservations += expected.size();
    transitionAdds += segmented.added.size();
    transitionRemoves += segmented.removed.size();
}

void controlledCases()
{
    MatchedBroadPhases fixture;
    auto &pointAndHighBit = fixture.create(0, 1, {-3, -3, 3, 3}, UINT64_C(1) << 63,
                                           UINT64_C(1) << 2);
    fixture.create(1, 1, {-12, -3, -3, 3}, UINT64_C(1) << 2, UINT64_C(1) << 63);
    fixture.create(2, 1, {18, 18, 18, 18}, 1, UINT64_MAX, false); // Inactive point.
    fixture.create(3, 1, {-180, -32, 180, 32}, 4, 4); // Large multi-cell bounds.
    auto &rotated = fixture.create(4, 1, rotatedBox(36, -22, 28, 6, 0.71f), 4, 4);
    fixture.create(5, 1, {500, 500, 500, 500}, UINT64_C(1) << 7, UINT64_C(1) << 8);
    fixture.publish();
    compareStep(fixture, "controlled initial scene");

    const auto highPair = CanonicalPair::make(pointAndHighBit.state.identity,
                                               fixture.allEntries()[1]->state.identity);
    const auto initialPairs = fixture.resultForSegmented().candidates;
    require(std::find(initialPairs.begin(), initialPairs.end(), highPair) != initialPairs.end(),
            "The complete 64-bit symmetric filter must accept the matching bit 63/bit 2 pair");

    // Multiple writes before publication model coalesced transform/shape changes.
    fixture.setBounds(pointAndHighBit, {-2.5f, -2.5f, 2.5f, 2.5f});
    fixture.setBounds(pointAndHighBit, {-2.0f, -2.0f, 2.0f, 2.0f});
    fixture.setBounds(pointAndHighBit, {-1.5f, -1.5f, 1.5f, 1.5f});
    fixture.setFilter(pointAndHighBit, UINT64_C(1) << 63, UINT64_C(1) << 2);
    const size_t coalesced = fixture.publish();
    require(coalesced == 1, "Repeated endpoint writes are published once per dirty endpoint");
    require(fixture.getContainedMoveCount() > 0,
            "A contained move under a stable padded envelope avoids tree structural mutation");
    compareStep(fixture, "contained padded move after coalesced rotation/resize updates");

    // Filter directions must both agree. Reversing one endpoint's mask drops the exact pair.
    fixture.setFilter(*fixture.allEntries()[1], UINT64_C(1) << 2, UINT64_C(1) << 4);
    fixture.publish();
    compareStep(fixture, "asymmetric filter rejection");
    fixture.setFilter(*fixture.allEntries()[1], UINT64_C(1) << 2, UINT64_C(1) << 63);
    fixture.publish();
    compareStep(fixture, "symmetric filter restoration");

    fixture.setActive(*fixture.allEntries()[2], true);
    fixture.publish();
    compareStep(fixture, "inactive endpoint activation");
    fixture.setActive(*fixture.allEntries()[2], false);
    fixture.publish();
    compareStep(fixture, "active endpoint deactivation");

    // Retiring and reusing a logical slot creates a distinct generation and cannot inherit the
    // old pair, proxy user data, or interval checkpoints.
    Entry &old = *fixture.allEntries()[1];
    fixture.retire(old);
    fixture.publish();
    auto &replacement = fixture.create(1, 2, {-12, -3, -3, 3}, UINT64_C(1) << 2, UINT64_C(1) << 63);
    fixture.publish();
    require(replacement.state.identity.endpoint.generation == 2,
            "Reused logical slots advance generation identity");
    compareStep(fixture, "slot and tree node reuse");

    fixture.setBounds(rotated, rotatedBox(-48, 61, 47, 4, 1.17f));
    fixture.publish();
    compareStep(fixture, "rotated movement and resize");
}

void randomizedMatchedOperations()
{
    constexpr uint32_t Seed = 0x5a17u;
    std::mt19937 random(Seed);
    MatchedBroadPhases fixture;
    for (uint32_t i = 0; i < 72; ++i)
    {
        const float x = float(int(random() % 1200) - 600);
        const float y = float(int(random() % 1200) - 600);
        const float halfX = float(random() % 65);
        const float halfY = float(random() % 65);
        const uint64_t bit = UINT64_C(1) << (random() % 64);
        const uint64_t mask = (random() % 5 == 0) ? UINT64_MAX : UINT64_C(1) << (random() % 64);
        fixture.create(i, 1, rotatedBox(x, y, halfX, halfY, float(random() % 629) / 100.0f), bit, mask,
                       random() % 9 != 0);
    }
    fixture.publish();
    compareStep(fixture, "seed=0x5a17 step=initial");

    for (int step = 0; step < 160; ++step)
    {
        const auto &all = fixture.allEntries();
        const size_t changes = 1 + random() % 12;
        for (size_t change = 0; change < changes; ++change)
        {
            Entry &entry = *all[random() % all.size()];
            switch (random() % 5)
            {
            case 0:
            case 1:
            {
                const float x = float(int(random() % 1600) - 800);
                const float y = float(int(random() % 1600) - 800);
                const float halfX = float(random() % 96);
                const float halfY = float(random() % 96);
                const float angle = float(random() % 629) / 100.0f;
                fixture.setBounds(entry, rotatedBox(x, y, halfX, halfY, angle));
                break;
            }
            case 2:
            {
                const uint64_t group = (random() % 9 == 0) ? UINT64_MAX : UINT64_C(1) << (random() % 64);
                const uint64_t mask = (random() % 7 == 0) ? UINT64_MAX : UINT64_C(1) << (random() % 64);
                fixture.setFilter(entry, group, mask);
                break;
            }
            case 3:
                if (!entry.retired)
                    fixture.setActive(entry, !entry.state.active);
                break;
            case 4:
                if (!entry.retired && random() % 8 == 0)
                {
                    const uint32_t slot = entry.state.identity.endpoint.slot;
                    const uint64_t generation = entry.state.identity.endpoint.generation + 1;
                    fixture.retire(entry);
                    fixture.publish();
                    const float x = float(int(random() % 400) - 200);
                    const float y = float(int(random() % 400) - 200);
                    fixture.create(slot, generation, {x, y, x + 1, y + 1}, UINT64_C(1) << (random() % 64),
                                   UINT64_MAX, true);
                }
                break;
            }
        }
        fixture.publish();
        compareStep(fixture, "seed=0x5a17 step=" + std::to_string(step));
    }
}
} // namespace

int main()
{
    try
    {
        controlledCases();
        randomizedMatchedOperations();
        std::cout << "Matched collision broad-phase regressions passed\n"
                  << "snapshots=" << comparedSnapshots << " exact_pair_observations=" << exactPairObservations
                  << " transition_adds=" << transitionAdds << " transition_removes=" << transitionRemoves
                  << " input_fnv64=" << std::hex << inputFnv64 << " snapshot_fnv64=" << snapshotFnv64 << std::dec
                  << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

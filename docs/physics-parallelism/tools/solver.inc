    audit::sample[audit::Preparation] = audit::elapsed(begin);
    audit::sample[audit::Constraints] = solverConstraints.size();

    static thread_local std::vector<size_t> parents, sizes, active;
    static thread_local std::vector<std::vector<size_t>> islands;
    static thread_local std::vector<uint64_t> usedColors;
    static thread_local std::vector<size_t> lastLevel, levelPopulation;
    static thread_local std::array<std::vector<size_t>, 65> colors;
    const auto mutableIndex = [](const Rigidbody *body) {
        return body->getBodyType() == DYNAMIC && !body->getIsSleeping() ? body->getPhysicsManagerIndex() : SIZE_MAX;
    };
    const bool parallelIslands = solverConstraints.size() >= 128 && audit::solver == "islands";
    const bool parallelColors = solverConstraints.size() >= 128 && audit::solver == "colors";

    begin = audit::now();
    if (audit::graph || parallelIslands)
    {
        const size_t count = registeredBodies.size();
        parents.resize(count);
        std::iota(parents.begin(), parents.end(), size_t{0});
        sizes.assign(count, 1);
        auto root = [&](size_t n) {
            while (parents[n] != n)
            {
                parents[n] = parents[parents[n]];
                n = parents[n];
            }
            return n;
        };
        for (const auto &constraint : solverConstraints)
        {
            size_t a = mutableIndex(constraint.rbA), b = mutableIndex(constraint.rbB);
            if (a == SIZE_MAX || b == SIZE_MAX) continue;
            a = root(a);
            b = root(b);
            if (a == b) continue;
            if (sizes[a] < sizes[b]) std::swap(a, b);
            parents[b] = a;
            sizes[a] += sizes[b];
        }
        if (islands.size() < count + 1) islands.resize(count + 1);
        for (auto &group : islands) group.clear();
        active.clear();
        for (size_t i = 0; i < solverConstraints.size(); ++i)
        {
            const auto &constraint = solverConstraints[i];
            size_t index = mutableIndex(constraint.rbA);
            if (index == SIZE_MAX) index = mutableIndex(constraint.rbB);
            const size_t group = index == SIZE_MAX ? count : root(index);
            if (index != SIZE_MAX)
                ++audit::sample[audit::WritableConstraints];
            if (islands[group].empty()) active.push_back(group);
            islands[group].push_back(i);
        }
        for (auto i : active)
        {
            if (i == count) continue;
            audit::sample[audit::LargestIsland] = std::max(audit::sample[audit::LargestIsland],
                                                           static_cast<long long>(islands[i].size()));
        }
        if (parallelIslands)
        {
            std::sort(active.begin(), active.end(),
                      [&](size_t a, size_t b) { return islands[a].size() > islands[b].size(); });
        }
    }

    if (audit::graph || parallelColors)
    {
        usedColors.assign(registeredBodies.size(), 0);
        for (auto &color : colors) color.clear();
        if (audit::graph)
        {
            lastLevel.assign(registeredBodies.size(), 0);
            levelPopulation.clear();
        }
        for (size_t i = 0; i < solverConstraints.size(); ++i)
        {
            const auto &constraint = solverConstraints[i];
            const size_t a = mutableIndex(constraint.rbA), b = mutableIndex(constraint.rbB);
            const uint64_t unavailable = (a != SIZE_MAX ? usedColors[a] : 0) | (b != SIZE_MAX ? usedColors[b] : 0);
            const int color = std::countr_zero(~unavailable);
            colors[color].push_back(i);
            if (color < 64)
            {
                const uint64_t bit = uint64_t{1} << color;
                if (a != SIZE_MAX) usedColors[a] |= bit;
                if (b != SIZE_MAX) usedColors[b] |= bit;
            }
            if (audit::graph)
            {
                const auto next = 1 + std::max(a != SIZE_MAX ? lastLevel[a] : 0, b != SIZE_MAX ? lastLevel[b] : 0);
                if (a != SIZE_MAX) lastLevel[a] = next;
                if (b != SIZE_MAX) lastLevel[b] = next;
                if (levelPopulation.size() < next) levelPopulation.resize(next);
                ++levelPopulation[next - 1];
            }
        }
        for (size_t i = 0; i < 64 && !colors[i].empty(); ++i)
        {
            ++audit::sample[audit::GreedyColors];
            audit::sample[audit::LargestColor] = std::max(audit::sample[audit::LargestColor],
                                                        static_cast<long long>(colors[i].size()));
        }
        audit::sample[audit::GreedyOverflow] = colors.back().size();
        if (!levelPopulation.empty())
        {
            audit::sample[audit::OrderedLevels] = levelPopulation.size();
            audit::sample[audit::LargestLevel] = *std::max_element(levelPopulation.begin(), levelPopulation.end());
        }
    }
    audit::sample[audit::Schedule] = audit::elapsed(begin);

    begin = audit::now();
    if (parallelIslands && active.size() > 1)
    {
        tbb::parallel_for(size_t{0}, active.size(), [&](size_t group) {
            for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
                for (auto i : islands[active[group]])
                    resolveCollision(solverConstraints[i]);
        });
    }
    else if (parallelColors)
    {
        for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
        {
            for (size_t i = 0; i < colors.size() && !colors[i].empty(); ++i)
            {
                auto &color = colors[i];
                if (i == 64 || color.size() < 64)
                    for (auto j : color) resolveCollision(solverConstraints[j]);
                else
                    tbb::parallel_for(tbb::blocked_range<size_t>(0, color.size(), 32), [&](const auto &range) {
                        for (size_t j = range.begin(); j < range.end(); ++j)
                            resolveCollision(solverConstraints[color[j]]);
                    });
            }
        }
    }
    else
    {
        for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; iteration++)
            for (const SolverConstraint &constraint : solverConstraints)
                resolveCollision(constraint);
    }
    audit::sample[audit::Iterations] = audit::elapsed(begin);

    begin = audit::now();
    reconcileChangedBodies();
    updateSleepingStates(timeDelta);
    audit::sample[audit::Sleep] = audit::elapsed(begin);


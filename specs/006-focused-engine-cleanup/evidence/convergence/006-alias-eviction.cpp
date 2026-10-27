#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
int main()
{
    using Clock = std::chrono::steady_clock;
    platformator_detail::AssetRequestAliases aliases;
    std::vector<std::string> requests, canonical;
    for (int i = 0; i < 300; ++i)
    {
        requests.push_back("assets/request/" + std::to_string(i));
        canonical.push_back("assets/canonical/" + std::to_string(i));
    }
    std::int64_t total = 0;
    for (int pass = 0; pass < 55; ++pass)
    {
        for (int i = 0; i < 300; ++i) aliases.add(requests[i], canonical[i]);
        const auto start = Clock::now();
        for (const auto &name : canonical) aliases.remove(name);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        if (pass >= 5) total += elapsed;
    }
    std::cout << "count=300 warmup=5 repetitions=50 elapsed_ns=" << total << '\n';
}

#pragma once

#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace platformator_detail
{
struct AssetRequestAliases
{
    std::unordered_map<std::string, std::string> paths;
    std::unordered_map<std::string, std::vector<std::string>> reverse;

    const std::string *find(const std::string &request) const
    {
        auto it = paths.find(request);
        return it == paths.end() ? nullptr : &it->second;
    }

    void add(const std::string &request, const std::string &canonical)
    {
        if (paths.find(request) != paths.end()) return;
        auto [it, inserted] = reverse.try_emplace(canonical);
        try
        {
            it->second.push_back(request);
            try
            {
                paths.emplace(request, canonical);
            }
            catch (...)
            {
                it->second.pop_back();
                throw;
            }
        }
        catch (...)
        {
            if (inserted && it->second.empty())
            {
                reverse.erase(it);
                shrinkIfSparse(reverse);
            }
            throw;
        }
    }

    void remove(const std::string &canonical)
    {
        auto it = reverse.find(canonical);
        if (it == reverse.end()) return;
        for (const auto &request : it->second) paths.erase(request);
        reverse.erase(it);
        shrinkIfSparse(paths);
        shrinkIfSparse(reverse);
    }

    void clear()
    {
        paths.clear();
        reverse.clear();
        shrinkIfSparse(paths);
        shrinkIfSparse(reverse);
    }

  private:
    template <typename Map> static void shrinkIfSparse(Map &map) noexcept
    {
        // Keep small tables for normal churn; shrink geometrically after large peaks.
        if (map.bucket_count() <= 64 || map.size() >= map.bucket_count() / 4) return;
        if (map.empty())
        {
            Map empty;
            map.swap(empty);
            return;
        }
        try
        {
            map.rehash(map.size() * 2);
        }
        catch (...)
        {
            // Reclamation is best-effort and must not make resource destruction throw.
        }
    }
};

template <typename Resource>
Resource *publishAsset(std::unordered_map<std::string, std::unique_ptr<Resource>> &cache,
                       AssetRequestAliases &aliases, const std::string &request,
                       const std::string &canonical, std::unique_ptr<Resource> resource)
{
    auto [it, inserted] = cache.emplace(canonical, std::move(resource));
    try
    {
        aliases.add(request, canonical);
    }
    catch (...)
    {
        if (inserted) cache.erase(it);
        throw;
    }
    return it->second.get();
}
} // namespace platformator_detail

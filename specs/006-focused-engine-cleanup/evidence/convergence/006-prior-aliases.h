#include <string>
#include <unordered_map>
#include <vector>
namespace platformator_detail {
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
        auto &aliases = reverse[canonical];
        aliases.push_back(request);
        try { paths.emplace(request, canonical); }
        catch (...)
        {
            aliases.pop_back();
            if (aliases.empty()) reverse.erase(canonical);
            throw;
        }
    }
    void remove(const std::string &canonical)
    {
        auto it = reverse.find(canonical);
        if (it == reverse.end()) return;
        for (const auto &request : it->second) paths.erase(request);
        reverse.erase(it);
    }
    void clear() { paths.clear(); reverse.clear(); }
};
}

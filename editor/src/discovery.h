#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace platformator_editor
{
inline std::string normalize_discovery_text(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

inline bool discovery_query_matches(std::string_view query, std::string_view candidate)
{
    if (query.empty())
        return true;
    return normalize_discovery_text(candidate).find(normalize_discovery_text(query)) !=
           std::string::npos;
}
} // namespace platformator_editor

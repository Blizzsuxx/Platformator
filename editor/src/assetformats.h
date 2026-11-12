#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace platformator_editor
{
enum class EditorAssetKind : std::uint8_t
{
    Other,
    Animation,
    Template,
    Image,
    Audio,
    Script
};

struct AssetDragPayload
{
    char relativeAssetPath[512] = {0};
    EditorAssetKind kind = EditorAssetKind::Other;
};

struct EditorAssetFormat
{
    EditorAssetKind kind = EditorAssetKind::Other;
    bool textureReference = false;
    bool audioReference = false;
    bool animationReference = false;
    bool behaviorHeader = false;
};

struct AssetFormatMapping
{
    std::string_view extension;
    EditorAssetFormat format;
};

inline constexpr std::array<AssetFormatMapping, 31> EditorAssetFormats{{
    {".animset", {EditorAssetKind::Animation, false, false, true, false}},
    {".pttemplate", {EditorAssetKind::Template}},
    {".png", {EditorAssetKind::Image, true}},
    {".jpg", {EditorAssetKind::Image, true}},
    {".jpeg", {EditorAssetKind::Image, true}},
    {".bmp", {EditorAssetKind::Image, true}},
    {".webp", {EditorAssetKind::Image, true}},
    {".gif", {EditorAssetKind::Image, true}},
    {".tga", {EditorAssetKind::Image, true}},
    {".qoi", {EditorAssetKind::Image, true}},
    {".tif", {EditorAssetKind::Image, true}},
    {".tiff", {EditorAssetKind::Image, true}},
    {".wav", {EditorAssetKind::Audio, false, true}},
    {".ogg", {EditorAssetKind::Audio, false, true}},
    {".mp3", {EditorAssetKind::Audio, false, true}},
    {".flac", {EditorAssetKind::Audio, false, true}},
    {".opus", {EditorAssetKind::Audio, false, true}},
    {".aiff", {EditorAssetKind::Audio}},
    {".aif", {EditorAssetKind::Audio}},
    {".m4a", {EditorAssetKind::Audio}},
    {".h", {EditorAssetKind::Script, false, false, false, true}},
    {".cpp", {EditorAssetKind::Script}},
    {".hpp", {EditorAssetKind::Script}},
    {".c", {EditorAssetKind::Script}},
    {".cc", {EditorAssetKind::Script}},
    {".cxx", {EditorAssetKind::Script}},
    {".lua", {EditorAssetKind::Script}},
    {".py", {EditorAssetKind::Script}},
    {".js", {EditorAssetKind::Script}},
    {".ts", {EditorAssetKind::Script}},
    {".cs", {EditorAssetKind::Script}}
}};

inline std::string lowercase_asset_extension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return extension;
}

inline EditorAssetFormat editor_asset_format(const std::filesystem::path &path)
{
    const auto extension = lowercase_asset_extension(path);
    for (const auto &mapping : EditorAssetFormats)
        if (extension == mapping.extension)
            return mapping.format;
    return {};
}

inline bool editor_asset_is(const std::filesystem::path &path, EditorAssetKind kind)
{
    return editor_asset_format(path).kind == kind;
}

inline bool asset_reference_extension_matches(std::string_view expectedType,
                                              const std::filesystem::path &path)
{
    if (expectedType.empty() || expectedType == "Asset" || expectedType == "unknown")
        return true;
    const auto format = editor_asset_format(path);
    if (expectedType == "Texture")
        return format.textureReference;
    if (expectedType == "Audio")
        return format.audioReference;
    if (expectedType == "AnimationClip")
        return format.animationReference;
    return false;
}
} // namespace platformator_editor

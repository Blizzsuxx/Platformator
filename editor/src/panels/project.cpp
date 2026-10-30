#include "project.h"

#include "../style.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <imgui.h>
#include <SDL3_image/SDL_image.h>

namespace platformator_editor
{
namespace
{
bool path_is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    if (relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == "..")
            return false;
    return true;
}

std::string folder_label(const std::filesystem::path &root,
                         const std::filesystem::path &directory, const std::string &rootLabel)
{
    return directory == root ? rootLabel : directory.filename().string();
}

std::string lowercase_extension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

EditorIcon asset_icon(const std::filesystem::path &path)
{
    const auto extension = lowercase_extension(path);
    if (extension == ".animset")
        return EditorIcon::Animator;
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" ||
        extension == ".flac" || extension == ".aiff" || extension == ".aif" || extension == ".m4a")
        return EditorIcon::Audio;
    if (extension == ".cpp" || extension == ".h" || extension == ".hpp" || extension == ".c" ||
        extension == ".cc" || extension == ".cxx" || extension == ".lua" || extension == ".py" ||
        extension == ".js" || extension == ".ts" || extension == ".cs")
        return EditorIcon::Script;
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".webp" || extension == ".gif" ||
        extension == ".tif" || extension == ".tiff")
        return EditorIcon::Sprite;
    return EditorIcon::NewDocument;
}

float json_number(const Json &value, const char *key, float fallback = 0.0f)
{
    const auto found = value.find(key);
    if (found == value.end() || !found->is_number())
        return fallback;
    try
    {
        const float result = found->get<float>();
        return std::isfinite(result) ? result : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

std::optional<std::string> behavior_type_from_source(const std::filesystem::path &sourcePath)
{
    std::vector<std::filesystem::path> candidates;
    auto header = sourcePath;
    header.replace_extension(".h");
    candidates.push_back(header);
    header.replace_extension(".hpp");
    candidates.push_back(header);
    candidates.push_back(sourcePath);

    for (const auto &candidate : candidates)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(candidate, error);
        if (error || size == 0 || size > 1024U * 1024U)
            continue;
        std::ifstream input(candidate, std::ios::binary);
        if (!input)
            continue;
        std::string source(static_cast<std::size_t>(size), '\0');
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (!input && !input.eof())
            continue;

        std::size_t position = 0;
        while ((position = source.find("class ", position)) != std::string::npos)
        {
            const std::size_t nameStart = position + 6;
            std::size_t nameEnd = nameStart;
            while (nameEnd < source.size() &&
                   (std::isalnum(static_cast<unsigned char>(source[nameEnd])) || source[nameEnd] == '_'))
                ++nameEnd;
            if (nameEnd == nameStart)
            {
                position = nameStart;
                continue;
            }
            const std::string name = source.substr(nameStart, nameEnd - nameStart);
            const auto openingBrace = source.find('{', nameEnd);
            const auto semicolon = source.find(';', nameEnd);
            if (openingBrace != std::string::npos &&
                (semicolon == std::string::npos || openingBrace < semicolon))
            {
                const std::string_view declaration(source.data() + nameEnd, openingBrace - nameEnd);
                const auto publicBase = declaration.find("public");
                const auto baseName = publicBase == std::string_view::npos
                    ? std::string_view::npos : declaration.find("Behavior", publicBase + 6);
                if (baseName != std::string_view::npos)
                {
                    const bool startsToken = baseName == 0 ||
                        !(std::isalnum(static_cast<unsigned char>(declaration[baseName - 1])) ||
                          declaration[baseName - 1] == '_');
                    const auto afterBase = baseName + std::string_view("Behavior").size();
                    const bool endsToken = afterBase == declaration.size() ||
                        !(std::isalnum(static_cast<unsigned char>(declaration[afterBase])) ||
                          declaration[afterBase] == '_');
                    if (startsToken && endsToken)
                        return name;
                }
            }
            position = nameEnd;
        }
    }
    return std::nullopt;
}
} // namespace

ProjectPanel::ProjectPanel(ProjectContext &context, ProjectScanService &scans, SDL_Renderer *renderer)
    : context_(context), scans_(scans), renderer_(renderer)
{
}

ProjectPanel::~ProjectPanel()
{
    release_preview_resources();
}

void ProjectPanel::set_renderer(SDL_Renderer *renderer) noexcept
{
    if (renderer_ == renderer)
        return;
    release_preview_resources();
    renderer_ = renderer;
}

void ProjectPanel::release_preview_resources() noexcept
{
    for (auto &[path, texture] : thumbnails_)
    {
        (void)path;
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
    }
    thumbnails_.clear();
    unavailableThumbnails_.clear();
    animations_.clear();
}

void ProjectPanel::refresh()
{
    for (auto &[path, state] : directories_)
    {
        (void)path;
        state.request.cancel();
    }
    directories_.clear();
    requestPaths_.clear();
    selectedEntry_.reset();
    release_preview_resources();
}

void ProjectPanel::apply_ready_results()
{
    for (auto &result : scans_.take_ready(context_.snapshot().generation))
    {
        const auto found = requestPaths_.find(result.requestId);
        if (found == requestPaths_.end())
            continue;
        auto state = directories_.find(found->second);
        requestPaths_.erase(found);
        if (state == directories_.end())
            continue;
        state->second.request = {};
        state->second.loaded = true;
        state->second.error = std::move(result.error);
        state->second.truncated = result.truncated;
        state->second.files = std::move(result.files);
        state->second.directories = std::move(result.directories);
    }
}

void ProjectPanel::request_scan(const std::filesystem::path &directory)
{
    const auto key = directory.string();
    auto &state = directories_[key];
    if (state.loaded || state.request)
        return;

    state.error.clear();
    try
    {
        state.request = scans_.request_scan(context_.snapshot(), directory,
            ProjectScanOptions{.recursive = false, .maxFiles = 10000,
                               .includeDirectories = true, .maxDirectories = 2000,
                               .maxEntries = 20000});
        if (state.request)
            requestPaths_[state.request.request_id()] = key;
        else
        {
            state.error = "Asset scan service reached its outstanding-scan limit.";
            state.loaded = true;
        }
    }
    catch (const std::exception &error)
    {
        state.error = error.what();
        state.loaded = true;
    }
}

void ProjectPanel::draw_folder_tree(const std::filesystem::path &root,
                                    const std::filesystem::path &directory,
                                    const std::string &rootLabel, std::size_t depth)
{
    const auto key = directory.string();
    auto &state = directories_[key];
    const auto label = folder_label(root, directory, rootLabel);
    const bool leaf = state.loaded && state.directories.empty();
    const bool wasExpanded = expandedDirectories_.contains(key);

    ImGui::PushID(key.c_str());
    if (depth > 0)
        ImGui::Indent(16.0f);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selectedDirectory_ == directory)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf;
    ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
    const bool open = ImGui::TreeNodeEx("##asset-folder", flags, "");
    if (open != wasExpanded)
    {
        if (open)
            expandedDirectories_.insert(key);
        else
            expandedDirectories_.erase(key);
    }

    const ImVec2 rowMinimum = ImGui::GetItemRectMin();
    const ImVec2 rowMaximum = ImGui::GetItemRectMax();
    const float iconSize = 14.0f;
    const float iconX = rowMinimum.x + ImGui::GetTreeNodeToLabelSpacing() + 3.0f;
    const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    draw_editor_icon(EditorIcon::Folder, drawList, {iconX, iconY}, iconSize,
                     ImGui::GetColorU32(ImGuiCol_Text));
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    drawList->AddText({iconX + iconSize + 5.0f,
                       rowMinimum.y + (rowMaximum.y - rowMinimum.y - textSize.y) * 0.5f},
                      ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    if (ImGui::IsItemClicked())
    {
        selectedDirectory_ = directory;
        selectedRoot_ = root;
        selectedRootLabel_ = rootLabel;
        selectedEntry_.reset();
    }

    if (open)
    {
        request_scan(directory);
        if (!state.error.empty())
            ImGui::TextWrapped("%s", state.error.c_str());
        else if (!state.loaded)
            ImGui::TextDisabled("Scanning...");
        else
        {
            for (const auto &child : state.directories)
                draw_folder_tree(root, child, rootLabel, depth + 1);
            if (state.truncated)
                ImGui::TextDisabled("Folder listing reached its scan limit.");
        }
    }
    if (depth > 0)
        ImGui::Unindent(16.0f);
    ImGui::PopID();
}

SDL_Texture *ProjectPanel::thumbnail_for(const std::filesystem::path &path)
{
    if (renderer_ == nullptr)
        return nullptr;
    const std::string key = path.string();
    if (const auto found = thumbnails_.find(key); found != thumbnails_.end())
        return found->second;
    if (unavailableThumbnails_.contains(key))
        return nullptr;
    SDL_Texture *texture = IMG_LoadTexture(renderer_, path.string().c_str());
    if (texture == nullptr)
    {
        unavailableThumbnails_.insert(key);
        return nullptr;
    }
    thumbnails_.emplace(key, texture);
    return texture;
}

const ProjectPanel::AnimationPreview &ProjectPanel::animation_for(
    const std::filesystem::path &path, const std::filesystem::path &assetRoot)
{
    const std::string key = path.string();
    auto &preview = animations_[key];
    if (preview.loaded)
        return preview;
    preview.loaded = true;
    try
    {
        std::ifstream input(path);
        if (!input)
            return preview;
        Json description;
        input >> description;
        if (!description.is_object())
            return preview;
        preview.framesPerSecond = std::max(0.0f, json_number(description, "framesPerSecond", 1.0f));
        preview.loop = description.value("loop", true);
        const auto frames = description.find("frames");
        if (frames == description.end() || !frames->is_array())
            return preview;
        for (const auto &frame : *frames)
        {
            if (!frame.is_object())
                continue;
            const auto texturePathValue = frame.find("textureWrapperFilePath");
            if (texturePathValue == frame.end() || !texturePathValue->is_string())
                continue;
            std::filesystem::path reference = texturePathValue->get<std::string>();
            if (reference.empty())
                continue;
            std::filesystem::path texturePath;
            if (reference.is_absolute())
                texturePath = reference.lexically_normal();
            else
            {
                if (reference.begin() != reference.end() && *reference.begin() == "assets")
                    reference = reference.lexically_relative("assets");
                texturePath = (assetRoot / reference).lexically_normal();
            }
            if (!path_is_within(assetRoot, texturePath))
                continue;
            AnimationFramePreview previewFrame;
            previewFrame.texturePath = std::move(texturePath);
            previewFrame.hasSourceRect = frame.value("hasSourceRect", false);
            const auto sourceRect = frame.find("sourceRect");
            if (previewFrame.hasSourceRect && sourceRect != frame.end() && sourceRect->is_object())
            {
                previewFrame.sourceX = json_number(*sourceRect, "x");
                previewFrame.sourceY = json_number(*sourceRect, "y");
                previewFrame.sourceWidth = json_number(*sourceRect, "w");
                previewFrame.sourceHeight = json_number(*sourceRect, "h");
            }
            preview.frames.push_back(std::move(previewFrame));
        }
    }
    catch (...)
    {
        preview.frames.clear();
    }
    return preview;
}

void ProjectPanel::draw_asset_tile(const std::filesystem::path &path,
                                   const std::filesystem::path &assetRoot,
                                   bool isDirectory, bool behaviorRoot,
                                   float tileWidth, float tileHeight)
{
    const std::string entryKey = path.string();
    const std::string label = path.filename().string();
    const bool selected = selectedEntry_ && *selectedEntry_ == path;
    ImGui::PushID(entryKey.c_str());
    const bool clicked = ImGui::InvisibleButton("##asset-tile", {tileWidth, tileHeight});
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    const bool behaviorSource = behaviorRoot && lowercase_extension(path) == ".h";
    if (!isDirectory && (!behaviorRoot || behaviorSource) && ImGui::BeginDragDropSource())
    {
        if (behaviorSource)
        {
            const auto behaviorType = behavior_type_from_source(path);
            if (behaviorType)
            {
                ImGui::SetDragDropPayload("PLATFORMATOR_BEHAVIOR_TYPE", behaviorType->c_str(),
                                          behaviorType->size() + 1);
                ImGui::Text("Add %s", behaviorType->c_str());
            }
            else
                ImGui::TextUnformatted("Could not find a registered behavior class in this source file.");
        }
        else
        {
            const auto relativeAsset = path.lexically_relative(assetRoot).generic_string();
            const std::string reference = "assets/" + relativeAsset;
            ImGui::SetDragDropPayload("PLATFORMATOR_ASSET_PATH", reference.c_str(), reference.size() + 1);
            ImGui::TextUnformatted(path.filename().string().c_str());
        }
        ImGui::EndDragDropSource();
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 background = selected ? ImGui::GetColorU32(ImGuiCol_Header)
                            : hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered)
                                      : ImGui::GetColorU32(ImGuiCol_FrameBg);
    drawList->AddRectFilled(minimum, maximum, background, 4.0f);
    drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);

    SDL_Texture *texture = nullptr;
    AnimationFramePreview frame;
    bool useFrameRect = false;
    if (!isDirectory && lowercase_extension(path) == ".animset")
    {
        const auto &animation = animation_for(path, assetRoot);
        if (!animation.frames.empty())
        {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - animation.startedAt).count();
            const double framePosition = std::floor(std::max(0.0, elapsed) * animation.framesPerSecond);
            std::size_t frameIndex = 0;
            if (animation.frames.size() > 1 && animation.framesPerSecond > 0.0f)
            {
                if (animation.loop)
                    frameIndex = static_cast<std::size_t>(std::fmod(framePosition,
                        static_cast<double>(animation.frames.size())));
                else
                    frameIndex = std::min(static_cast<std::size_t>(std::max(0.0, framePosition)),
                                          animation.frames.size() - 1);
            }
            frame = animation.frames[frameIndex];
            texture = thumbnail_for(frame.texturePath);
            useFrameRect = frame.hasSourceRect;
        }
    }
    else if (!isDirectory && asset_icon(path) == EditorIcon::Sprite)
        texture = thumbnail_for(path);

    const float previewInset = 8.0f;
    const ImVec2 previewMinimum{minimum.x + previewInset, minimum.y + previewInset};
    const ImVec2 previewMaximum{maximum.x - previewInset, minimum.y + tileHeight - 32.0f};
    if (isDirectory)
        draw_editor_icon(EditorIcon::Folder, drawList,
                         {minimum.x + (tileWidth - 40.0f) * 0.5f, minimum.y + 18.0f}, 40.0f,
                         ImGui::GetColorU32(ImGuiCol_Text));
    else if (texture != nullptr)
    {
        float textureWidth = 0.0f;
        float textureHeight = 0.0f;
        if (SDL_GetTextureSize(texture, &textureWidth, &textureHeight) &&
            textureWidth > 0.0f && textureHeight > 0.0f)
        {
            float sourceX = 0.0f;
            float sourceY = 0.0f;
            float sourceWidth = textureWidth;
            float sourceHeight = textureHeight;
            if (useFrameRect)
            {
                sourceX = std::clamp(frame.sourceX, 0.0f, textureWidth);
                sourceY = std::clamp(frame.sourceY, 0.0f, textureHeight);
                sourceWidth = std::clamp(frame.sourceWidth, 0.0f, textureWidth - sourceX);
                sourceHeight = std::clamp(frame.sourceHeight, 0.0f, textureHeight - sourceY);
                if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
                {
                    sourceX = 0.0f;
                    sourceY = 0.0f;
                    sourceWidth = textureWidth;
                    sourceHeight = textureHeight;
                }
            }
            const float scale = std::min((previewMaximum.x - previewMinimum.x) / sourceWidth,
                                         (previewMaximum.y - previewMinimum.y) / sourceHeight);
            const float displayWidth = sourceWidth * scale;
            const float displayHeight = sourceHeight * scale;
            const ImVec2 displayMinimum{minimum.x + (tileWidth - displayWidth) * 0.5f,
                                        previewMinimum.y + (previewMaximum.y - previewMinimum.y - displayHeight) * 0.5f};
            const ImVec2 displayMaximum{displayMinimum.x + displayWidth, displayMinimum.y + displayHeight};
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
            drawList->AddImage(textureId, displayMinimum, displayMaximum,
                               {sourceX / textureWidth, sourceY / textureHeight},
                               {(sourceX + sourceWidth) / textureWidth,
                                (sourceY + sourceHeight) / textureHeight});
        }
    }
    else
    {
        const EditorIcon icon = isDirectory ? EditorIcon::Folder : asset_icon(path);
        const float iconSize = std::min(40.0f, tileWidth * 0.42f);
        draw_editor_icon(icon, drawList,
                         {minimum.x + (tileWidth - iconSize) * 0.5f, minimum.y + 18.0f}, iconSize,
                         ImGui::GetColorU32(ImGuiCol_Text));
    }

    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const float textX = minimum.x + 5.0f;
    const float textY = maximum.y - textSize.y - 6.0f;
    drawList->PushClipRect({minimum.x + 3.0f, textY}, {maximum.x - 3.0f, maximum.y - 3.0f}, true);
    drawList->AddText({textX, textY}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    drawList->PopClipRect();

    if (hovered)
        ImGui::SetTooltip("%s", path.string().c_str());
    if (clicked)
    {
        selectedEntry_ = path;
        if (isDirectory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            selectedDirectory_ = path;
            selectedEntry_.reset();
            auto parent = path;
            while (path_is_within(assetRoot, parent))
            {
                expandedDirectories_.insert(parent.string());
                if (parent == assetRoot || parent == parent.parent_path())
                    break;
                parent = parent.parent_path();
            }
        }
    }
    ImGui::PopID();
}

void ProjectPanel::draw_contents(const std::filesystem::path &root, const std::string &rootLabel,
                                 bool behaviorRoot)
{
    request_scan(selectedDirectory_);
    auto &state = directories_[selectedDirectory_.string()];
    const auto relative = selectedDirectory_.lexically_relative(root);
    ImGui::TextDisabled("%s", relative.empty() || relative == "."
                                   ? rootLabel.c_str()
                                   : (rootLabel + " / " + relative.generic_string()).c_str());
    ImGui::Separator();

    if (!state.error.empty())
    {
        ImGui::TextWrapped("%s", state.error.c_str());
        return;
    }
    if (!state.loaded)
    {
        ImGui::TextDisabled("Scanning...");
        return;
    }
    if (state.directories.empty() && state.files.empty())
    {
        ImGui::TextDisabled("This folder is empty.");
        return;
    }

    struct Entry
    {
        const std::filesystem::path *path;
        bool isDirectory;
    };
    std::vector<Entry> entries;
    entries.reserve(state.directories.size() + state.files.size());
    for (const auto &directory : state.directories)
        entries.push_back({&directory, true});
    for (const auto &file : state.files)
        if (!behaviorRoot || lowercase_extension(file) == ".h")
            entries.push_back({&file, false});

    constexpr float preferredTileWidth = 116.0f;
    constexpr float tileHeight = 104.0f;
    const float spacingX = ImGui::GetStyle().ItemSpacing.x;
    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const int columns = std::max(1, static_cast<int>((availableWidth + spacingX) /
                                                     (preferredTileWidth + spacingX)));
    const float tileWidth = std::max(72.0f,
        (availableWidth - spacingX * static_cast<float>(columns - 1)) / static_cast<float>(columns));
    const int rowCount = static_cast<int>((entries.size() + static_cast<std::size_t>(columns) - 1) /
                                          static_cast<std::size_t>(columns));
    const float startX = ImGui::GetCursorPosX();
    const float startY = ImGui::GetCursorPosY();
    const float rowStride = tileHeight + spacingY;
    const float scrollY = std::max(0.0f, ImGui::GetScrollY() - startY);
    const float visibleHeight = ImGui::GetWindowSize().y;
    const int firstVisibleRow = std::max(0, static_cast<int>(std::floor(scrollY / rowStride)) - 1);
    const int lastVisibleRow = std::min(rowCount, static_cast<int>(std::ceil(
        (scrollY + visibleHeight) / rowStride)) + 1);
    for (int row = firstVisibleRow; row < lastVisibleRow; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const std::size_t entryIndex = static_cast<std::size_t>(row) *
                                           static_cast<std::size_t>(columns) +
                                           static_cast<std::size_t>(column);
            if (entryIndex >= entries.size())
                break;
            ImGui::SetCursorPos({startX + static_cast<float>(column) * (tileWidth + spacingX),
                                 startY + static_cast<float>(row) * rowStride});
            const auto &entry = entries[entryIndex];
            draw_asset_tile(*entry.path, root, entry.isDirectory, behaviorRoot,
                            tileWidth, tileHeight);
        }
    }
    // Keep the full grid height in the child content even though only visible rows are submitted.
    ImGui::SetCursorPos({startX, startY + static_cast<float>(rowCount) * rowStride});
    ImGui::Dummy({1.0f, 1.0f});
    if (state.truncated)
        ImGui::TextDisabled("Folder listing reached its scan limit.");
}

void ProjectPanel::draw(DocumentSession &document, EditCommandService &commands)
{
    (void)commands;
    apply_ready_results();
    const auto &snapshot = context_.snapshot();
    const auto &assetRoot = snapshot.assetRoot;
    const auto &projectRoot = snapshot.projectRoot;
    if (assetRoot.empty() || document.assetRoot != assetRoot)
    {
        ImGui::TextDisabled("Project asset root is unavailable.");
        return;
    }

    const auto behaviorRoot = assetRoot / "behaviors";
    std::error_code filesystemError;
    const bool hasBehaviorRoot = std::filesystem::is_directory(behaviorRoot, filesystemError) &&
                                 !filesystemError;
    if (assetRoot_ != assetRoot || projectRoot_ != projectRoot)
    {
        assetRoot_ = assetRoot;
        projectRoot_ = projectRoot;
        selectedDirectory_ = assetRoot;
        selectedRoot_ = assetRoot;
        selectedRootLabel_ = "Assets";
        selectedEntry_.reset();
        expandedDirectories_.clear();
        expandedDirectories_.insert(assetRoot.string());
        if (hasBehaviorRoot)
            expandedDirectories_.insert(behaviorRoot.string());
    }
    const bool selectedIsAsset = path_is_within(assetRoot, selectedDirectory_);
    bool selectedIsBehavior = hasBehaviorRoot && path_is_within(behaviorRoot, selectedDirectory_);
    if ((!selectedIsAsset && !selectedIsBehavior) ||
        !std::filesystem::is_directory(selectedDirectory_, filesystemError) || filesystemError)
    {
        selectedDirectory_ = assetRoot;
        selectedRoot_ = assetRoot;
        selectedRootLabel_ = "Assets";
        selectedEntry_.reset();
        expandedDirectories_.insert(assetRoot.string());
        selectedIsBehavior = false;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(240.0f, std::max(90.0f, available.x * 0.28f));
    ImGui::BeginChild("Asset folders", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders);
    draw_folder_tree(assetRoot, assetRoot, "Assets", 0);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Asset contents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    draw_contents(selectedRoot_, selectedRootLabel_, selectedIsBehavior);
    ImGui::EndChild();
}
} // namespace platformator_editor

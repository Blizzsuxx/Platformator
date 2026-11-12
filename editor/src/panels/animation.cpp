#include "animation.h"

#include "../animationeditor.h"
#include "../assetformats.h"
#include "spriteeditor.h"

#include "platformator/animationclip.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <charconv>
#include <system_error>
#include <utility>

namespace platformator_editor
{
namespace
{
bool path_is_within(const std::filesystem::path &root,
                    const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    if (relative.is_absolute())
        return false;
    return std::none_of(relative.begin(), relative.end(), [](const auto &part) {
        return part == "..";
    });
}

std::filesystem::path animation_texture_path(const std::filesystem::path &assetRoot,
                                             std::string_view textureReference)
{
    std::string normalized(textureReference);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::filesystem::path reference(normalized);
    return assetRoot / reference.lexically_relative("assets");
}

const Json *frames_of(const Json &clip)
{
    if (!clip.is_object())
        return nullptr;
    const auto frames = clip.find("frames");
    return frames != clip.end() && frames->is_array() ? &*frames : nullptr;
}

std::string texture_path_of(const Json &frame)
{
    if (!frame.is_object())
        return {};
    const auto path = frame.find("textureWrapperFilePath");
    return path != frame.end() && path->is_string() ? path->get<std::string>() : std::string{};
}

std::string animation_numeric_address(const AssetDocument &document,
                                     const std::filesystem::path &assetRoot,
                                     std::string_view jsonPointer)
{
    const std::string source = document.source_path()
        ? document.source_path()->generic_string() : std::string("untitled");
    const std::string root = assetRoot.generic_string();
    return std::to_string(document.generation()) + ":" + std::to_string(source.size()) + ":" +
        source + ":" + std::to_string(root.size()) + ":" + root + ":" +
        std::string(jsonPointer);
}

double frame_duration(const Json &frame)
{
    if (!frame.is_object())
        return std::numeric_limits<double>::quiet_NaN();
    const auto duration = frame.find("duration");
    if (duration == frame.end() || !duration->is_number())
        return std::numeric_limits<double>::quiet_NaN();
    const double seconds = duration->get<double>();
    return is_valid_animation_frame_duration(seconds) ? seconds
        : std::numeric_limits<double>::quiet_NaN();
}

void bump_revision(std::uint64_t &revision) noexcept
{
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
}

bool parse_finite_double(std::string_view text, double &value)
{
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value,
                                        std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           std::isfinite(value);
}

SemanticNode semantic_node(std::string id, std::string parentId, SemanticRole role,
                           std::string name, std::string value = {})
{
    SemanticNode node;
    node.id = std::move(id);
    node.parentId = std::move(parentId);
    node.role = role;
    node.name = std::move(name);
    node.value = std::move(value);
    return node;
}

} // namespace

AssetEditResult add_animation_frame(AssetDocument &document, Json frame)
{
    if (!frame.is_object())
        return {false, "an animation frame must be a JSON object"};
    const Json *frames = frames_of(document.root());
    if (frames == nullptr)
        return {false, "animation frames must be an array before editing"};

    const Json defaults = make_default_animation_frame();
    for (const char *field : {"duration", "textureWrapperFilePath", "hasSourceRect", "sourceRect"})
        if (!frame.contains(field))
            frame[field] = defaults.at(field);

    Json validationDocument = document.root();
    validationDocument["frames"] = Json::array({frame});
    const auto validation = validate_animation_clip_document(validationDocument);
    if (!validation.valid())
        return {false, validation.issues.front().jsonPointer + ": " +
                       validation.issues.front().message};

    Json next = *frames;
    next.push_back(std::move(frame));
    return document.set("/frames", std::move(next));
}

AssetEditResult remove_animation_frame(AssetDocument &document, std::size_t index)
{
    const Json *frames = frames_of(document.root());
    if (frames == nullptr || index >= frames->size())
        return {false, "animation frame index is out of range"};
    Json next = *frames;
    next.erase(next.begin() + static_cast<Json::difference_type>(index));
    return document.set("/frames", std::move(next));
}

AssetEditResult move_animation_frame(AssetDocument &document, std::size_t from,
                                     std::size_t to)
{
    const Json *frames = frames_of(document.root());
    if (frames == nullptr || from >= frames->size() || to >= frames->size())
        return {false, "animation frame index is out of range"};
    if (from == to)
        return {};
    Json next = *frames;
    Json moving = std::move(next[from]);
    next.erase(next.begin() + static_cast<Json::difference_type>(from));
    next.insert(next.begin() + static_cast<Json::difference_type>(to), std::move(moving));
    return document.set("/frames", std::move(next));
}

bool animation_texture_is_compatible(const std::filesystem::path &assetRoot,
                                     std::string_view textureReference)
{
    std::string normalized(textureReference);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (assetRoot.empty() || normalized.size() <= 7 ||
        !std::string_view(normalized).starts_with("assets/"))
        return false;
    const std::filesystem::path relative(normalized);
    if (relative.is_absolute() || std::any_of(relative.begin(), relative.end(), [](const auto &part) {
            return part == "..";
        }))
        return false;

    if (!asset_reference_extension_matches("Texture", relative))
        return false;

    std::error_code error;
    const auto canonicalAssetRoot = std::filesystem::weakly_canonical(assetRoot, error);
    if (error)
        return false;
    const auto candidate = std::filesystem::weakly_canonical(
        canonicalAssetRoot / relative.lexically_relative("assets"), error);
    if (error || !path_is_within(canonicalAssetRoot, candidate))
        return false;
    return std::filesystem::is_regular_file(candidate, error) && !error;
}

std::size_t animation_frame_at_time(const Json &clip, double seconds) noexcept
{
    const Json *frames = frames_of(clip);
    if (frames == nullptr || frames->empty() || !std::isfinite(seconds) || seconds < 0.0)
        return 0;
    double total = 0.0;
    for (const auto &frame : *frames)
    {
        const double duration = frame_duration(frame);
        if (!std::isfinite(duration) || duration <= 0.0 || !std::isfinite(total + duration))
            return 0;
        total += duration;
    }

    const auto loopValue = clip.find("loop");
    const bool loops = loopValue != clip.end() && loopValue->is_boolean() && loopValue->get<bool>();
    if (loops && total > 0.0)
        seconds = std::fmod(seconds, total);
    else if (seconds >= total)
        return frames->size() - 1;

    double cursor = 0.0;
    for (std::size_t index = 0; index < frames->size(); ++index)
    {
        cursor += frame_duration((*frames)[index]);
        if (seconds < cursor)
            return index;
    }
    return frames->size() - 1;
}

AnimationPanel::AnimationPanel() = default;
AnimationPanel::~AnimationPanel() = default;

void AnimationPanel::sync_texture_draft(const Json &frame)
{
    textureDraftFrame_ = selectedFrame_;
    textureDraft_ = texture_path_of(frame);
    textureDraftBuffer_.fill('\0');
    const auto length = std::min(textureDraft_.size(), textureDraftBuffer_.size() - 1);
    std::memcpy(textureDraftBuffer_.data(), textureDraft_.data(), length);
}

void AnimationPanel::scrub_to(std::size_t frameIndex) noexcept
{
    if (selectedFrame_ != frameIndex)
        bump_revision(viewRevision_);
    selectedFrame_ = frameIndex;
}

void AnimationPanel::scrub_to(const Json &clip, std::size_t frameIndex) noexcept
{
    if (selectedFrame_ != frameIndex)
        bump_revision(viewRevision_);
    selectedFrame_ = frameIndex;
    const Json *frames = frames_of(clip);
    if (frames == nullptr || frameIndex >= frames->size())
        return;
    playbackSeconds_ = 0.0;
    for (std::size_t index = 0; index < frameIndex; ++index)
    {
        const double duration = frame_duration((*frames)[index]);
        if (!std::isfinite(duration) || duration <= 0.0 ||
            !std::isfinite(playbackSeconds_ + duration))
        {
            playbackSeconds_ = 0.0;
            return;
        }
        playbackSeconds_ += duration;
    }
}

void AnimationPanel::set_playing(bool playing) noexcept
{
    if (playing_ != playing)
        bump_revision(viewRevision_);
    playing_ = playing;
}

void AnimationPanel::advance_playback(const Json &clip, double deltaSeconds) noexcept
{
    if (!playing_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
        return;
    playbackSeconds_ += deltaSeconds;
    if (!std::isfinite(playbackSeconds_))
    {
        playbackSeconds_ = 0.0;
        return;
    }
    const std::size_t frame = animation_frame_at_time(clip, playbackSeconds_);
    if (selectedFrame_ != frame)
        bump_revision(viewRevision_);
    selectedFrame_ = frame;
}

std::optional<std::chrono::steady_clock::time_point>
AnimationPanel::next_playback_deadline(
    const Json &clip, std::chrono::steady_clock::time_point now) const noexcept
{
    if (!playing_)
        return std::nullopt;
    const Json *frames = frames_of(clip);
    if (frames == nullptr || frames->empty())
        return std::nullopt;

    double totalDuration = 0.0;
    for (const auto &frame : *frames)
    {
        const double duration = frame_duration(frame);
        if (!std::isfinite(duration) || duration <= 0.0 ||
            !std::isfinite(totalDuration + duration))
            return std::nullopt;
        totalDuration += duration;
    }

    const auto loop = clip.find("loop");
    const bool loops = loop != clip.end() && loop->is_boolean() && loop->get<bool>();
    double position = playbackSeconds_;
    if (loops)
        position = std::fmod(position, totalDuration);
    else if (position >= totalDuration)
        return std::nullopt;
    if (!std::isfinite(position) || position < 0.0)
        return std::nullopt;

    double frameEnd = 0.0;
    for (const auto &frame : *frames)
    {
        const double duration = frame_duration(frame);
        frameEnd += duration;
        if (position < frameEnd)
        {
            const double remainingSeconds = frameEnd - position;
            const auto maximumDelay = std::chrono::duration<double>(
                std::chrono::steady_clock::time_point::max() - now);
            if (remainingSeconds >= maximumDelay.count())
                return std::chrono::steady_clock::time_point::max();
            auto delay = std::chrono::ceil<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(remainingSeconds));
            const auto minimumDelay = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(1));
            delay = std::max(delay, minimumDelay);
            return now + delay;
        }
    }
    return std::nullopt;
}

void AnimationPanel::cancel_numeric_drafts() noexcept
{
    durationDraft_.cancel();
    if (sourceRegionEditor_)
        sourceRegionEditor_->cancel_numeric_drafts();
}

std::vector<SemanticNode> AnimationPanel::semantic_nodes(const AssetDocument &document,
                                                         const std::filesystem::path &assetRoot,
                                                         std::string_view scope,
                                                         const PreparedTexture &preparedTexture)
{
    std::vector<SemanticNode> nodes;
    if (scope.empty())
        return nodes;
    const std::string prefix(scope);
    const std::string rootId = prefix + "/window";
    nodes.push_back(semantic_node(rootId, {}, SemanticRole::Window,
                                  "Animation asset editor"));

    const Json &clip = document.root();
    const Json *frames = frames_of(clip);
    if (frames == nullptr)
    {
        auto invalid = semantic_node(prefix + "/validation", rootId, SemanticRole::Text,
                                     "Animation data error", "Frames must be an array.");
        invalid.error = true;
        nodes.push_back(std::move(invalid));
        return nodes;
    }

    const auto loop = clip.find("loop");
    if (loop != clip.end() && loop->is_boolean())
    {
        auto node = semantic_node(prefix + "/loop", rootId, SemanticRole::CheckBox,
                                  "Loop animation", loop->get<bool>() ? "true" : "false");
        node.selected = loop->get<bool>();
        node.actions = {"set_value"};
        nodes.push_back(std::move(node));
    }

    auto add = semantic_node(prefix + "/add-frame", rootId, SemanticRole::Button,
                             "Add animation frame");
    add.actions = {"activate"};
    nodes.push_back(std::move(add));

    const std::size_t frameCount = frames->size();
    const std::size_t selected = frameCount == 0 ? 0 : std::min(selectedFrame_, frameCount - 1);
    const std::size_t first = selected > 12 ? selected - 12 : 0;
    const std::size_t last = std::min(frameCount, first + 25);
    for (std::size_t index = first; index < last; ++index)
    {
        const Json &frame = (*frames)[index];
        auto node = semantic_node(prefix + "/frame/" + std::to_string(index), rootId,
            SemanticRole::ListItem, "Frame " + std::to_string(index + 1),
            "duration=" + (frame.is_object() && frame.contains("duration")
                ? frame["duration"].dump() : std::string("invalid")) +
            "; texture=" + texture_path_of(frame));
        node.selected = index == selected;
        node.actions = {"select"};
        nodes.push_back(std::move(node));
    }

    if (frameCount != 0)
    {
        const Json &frame = (*frames)[selected];
        auto duration = semantic_node(prefix + "/duration", rootId, SemanticRole::TextField,
                                      "Selected frame duration in seconds",
                                      frame.is_object() && frame.contains("duration")
                                          ? frame["duration"].dump() : std::string("invalid"));
        duration.minimum = 0.0;
        duration.maximum = std::numeric_limits<double>::max();
        duration.actions = {"set_value"};
        nodes.push_back(std::move(duration));

        auto texture = semantic_node(prefix + "/texture-path", rootId, SemanticRole::TextField,
            "Selected frame texture path", texture_path_of(frame));
        texture.actions = {"set_value"};
        nodes.push_back(std::move(texture));

        auto moveUp = semantic_node(prefix + "/move-up", rootId, SemanticRole::Button,
                                     "Move selected frame up");
        moveUp.enabled = selected > 0;
        moveUp.actions = {"activate"};
        nodes.push_back(std::move(moveUp));
        auto moveDown = semantic_node(prefix + "/move-down", rootId, SemanticRole::Button,
                                      "Move selected frame down");
        moveDown.enabled = selected + 1 < frameCount;
        moveDown.actions = {"activate"};
        nodes.push_back(std::move(moveDown));
        auto remove = semantic_node(prefix + "/remove-frame", rootId, SemanticRole::Button,
                                    "Remove selected animation frame");
        remove.enabled = frameCount != 0;
        remove.actions = {"activate"};
        nodes.push_back(std::move(remove));
        auto assign = semantic_node(prefix + "/assign-texture", rootId, SemanticRole::Button,
                                     "Assign selected frame texture");
        const std::string draft = textureDraftFrame_ == selected
            ? textureDraft_ : texture_path_of(frame);
        assign.enabled = animation_texture_is_compatible(assetRoot, draft);
        assign.actions = {"activate"};
        nodes.push_back(std::move(assign));

        const std::string textureReference = texture_path_of(frame);
        semanticSourceFrame_ = selected;
        semanticSourceWidth_ = 0;
        semanticSourceHeight_ = 0;
        semanticSourceReference_.clear();
        auto sourceGroup = semantic_node(prefix + "/source-region", rootId,
                                         SemanticRole::Group, "Selected frame source region");
        if (animation_texture_is_compatible(assetRoot, textureReference) && preparedTexture)
        {
            const auto prepared = preparedTexture(animation_texture_path(assetRoot, textureReference));
            semanticSourceWidth_ = prepared.sourceWidth;
            semanticSourceHeight_ = prepared.sourceHeight;
            semanticSourceReference_ = textureReference;
        }
        if (semanticSourceWidth_ > 0 && semanticSourceHeight_ > 0)
        {
            sourceGroup.value = "Original image: " + std::to_string(semanticSourceWidth_) +
                " by " + std::to_string(semanticSourceHeight_) + " pixels";
            nodes.push_back(std::move(sourceGroup));

            SpriteSourceRegion region{0.0, 0.0, static_cast<double>(semanticSourceWidth_),
                                      static_cast<double>(semanticSourceHeight_)};
            const auto hasRect = frame.find("hasSourceRect");
            const auto rect = frame.find("sourceRect");
            if (hasRect != frame.end() && hasRect->is_boolean() && hasRect->get<bool>() &&
                rect != frame.end() && rect->is_object())
            {
                const auto read = [&](const char *key, double &value) {
                    const auto found = rect->find(key);
                    if (found == rect->end() || !found->is_number())
                        return false;
                    value = found->get<double>();
                    return std::isfinite(value);
                };
                SpriteSourceRegion stored;
                std::string ignored;
                if (read("x", stored.x) && read("y", stored.y) && read("w", stored.width) &&
                    read("h", stored.height) &&
                    validate_sprite_source_region(stored, semanticSourceWidth_,
                                                  semanticSourceHeight_, ignored))
                    region = stored;
            }
            const auto add_region_field = [&](std::string_view suffix, std::string label,
                                              double value, double maximum) {
                auto field = semantic_node(prefix + "/source-region/" + std::string(suffix),
                    prefix + "/source-region", SemanticRole::TextField, std::move(label),
                    std::to_string(value));
                field.minimum = 0.0;
                field.maximum = maximum;
                field.actions = {"set_value"};
                nodes.push_back(std::move(field));
            };
            add_region_field("x", "Source region X", region.x,
                             static_cast<double>(semanticSourceWidth_) - region.width);
            add_region_field("y", "Source region Y", region.y,
                             static_cast<double>(semanticSourceHeight_) - region.height);
            add_region_field("width", "Source region width", region.width,
                             static_cast<double>(semanticSourceWidth_) - region.x);
            add_region_field("height", "Source region height", region.height,
                             static_cast<double>(semanticSourceHeight_) - region.y);
        }
        else
        {
            sourceGroup.value = "Source image dimensions are unavailable.";
            sourceGroup.error = true;
            sourceGroup.help = "Assign an existing image inside the configured asset folder and wait for its preview to load.";
            nodes.push_back(std::move(sourceGroup));
        }
    }

    auto play = semantic_node(prefix + "/play", rootId, SemanticRole::Button,
                              playing_ ? "Pause animation preview" : "Play animation preview");
    play.value = playing_ ? "playing" : "paused";
    play.enabled = frameCount != 0;
    play.actions = {"activate"};
    nodes.push_back(std::move(play));

    const auto validation = validate_animation_clip_document(clip);
    if (!validation.valid())
    {
        auto status = semantic_node(prefix + "/validation", rootId, SemanticRole::Text,
                                     "Animation validation", std::to_string(validation.issues.size()) +
                                     " validation issue(s)");
        status.error = true;
        status.help = validation.issues.front().jsonPointer + ": " +
                      validation.issues.front().message;
        nodes.push_back(std::move(status));
    }
    else
        nodes.push_back(semantic_node(prefix + "/validation", rootId, SemanticRole::Text,
                                      "Animation validation", "Valid"));
    return nodes;
}

bool AnimationPanel::dispatch_semantic_action(AssetDocument &document,
                                               const std::filesystem::path &assetRoot,
                                               const SemanticAction &action,
                                               std::string_view scope,
                                               std::string &error)
{
    error.clear();
    const std::string prefix(scope);
    if (prefix.empty() || !action.nodeId.starts_with(prefix + "/"))
        return false;
    const std::string_view suffix(action.nodeId.data() + prefix.size() + 1,
                                  action.nodeId.size() - prefix.size() - 1);
    const Json *frames = frames_of(document.root());
    const auto fail = [&](std::string message) {
        error = std::move(message);
        return false;
    };
    const auto set = [&](std::string path, Json value) {
        auto result = document.set(std::move(path), std::move(value));
        if (!result.error.empty())
            return fail(std::move(result.error));
        return result.changed;
    };
    if (action.action == "activate" && suffix == "add-frame")
    {
        if (frames == nullptr)
            return fail("animation frames must be an array before editing");
        const std::size_t newIndex = frames->size();
        const auto result = add_animation_frame(document, make_default_animation_frame());
        if (!result.error.empty()) return fail(result.error);
        if (result.changed)
        {
            selectedFrame_ = newIndex;
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            bump_revision(viewRevision_);
        }
        return result.changed;
    }
    if (action.action == "select" && suffix.starts_with("frame/"))
    {
        std::size_t index = 0;
        const auto number = suffix.substr(6);
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
            frames == nullptr || index >= frames->size())
            return fail("animation frame selection is out of range");
        scrub_to(document.root(), index);
        return true;
    }
    if (frames == nullptr)
        return fail("animation frames must be an array before editing");
    const std::size_t selected = frames->empty() ? 0 : std::min(selectedFrame_, frames->size() - 1);
    if (action.action == "activate" && suffix == "move-up")
    {
        if (selected == 0) return fail("the first animation frame cannot move up");
        const auto result = move_animation_frame(document, selected, selected - 1);
        if (!result.error.empty()) return fail(result.error);
        if (result.changed) { --selectedFrame_; bump_revision(viewRevision_); }
        return result.changed;
    }
    if (action.action == "activate" && suffix == "move-down")
    {
        if (selected + 1 >= frames->size()) return fail("the last animation frame cannot move down");
        const auto result = move_animation_frame(document, selected, selected + 1);
        if (!result.error.empty()) return fail(result.error);
        if (result.changed) { ++selectedFrame_; bump_revision(viewRevision_); }
        return result.changed;
    }
    if (action.action == "activate" && suffix == "remove-frame")
    {
        if (frames->empty()) return fail("there is no selected animation frame to remove");
        const std::size_t oldCount = frames->size();
        const auto result = remove_animation_frame(document, selected);
        if (!result.error.empty()) return fail(result.error);
        if (result.changed)
        {
            selectedFrame_ = selected >= oldCount - 1 && oldCount > 1
                ? oldCount - 2 : (oldCount == 1 ? 0 : selected);
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            bump_revision(viewRevision_);
        }
        return result.changed;
    }
    if (action.action == "activate" && suffix == "play")
    {
        set_playing(!playing_);
        return true;
    }
    if (action.action == "activate" && suffix == "assign-texture")
    {
        if (frames->empty()) return fail("there is no selected animation frame");
        const std::string texture = textureDraftFrame_ == selected
            ? textureDraft_ : texture_path_of((*frames)[selected]);
        if (!animation_texture_is_compatible(assetRoot, texture))
            return fail("choose an existing image inside the configured asset folder");
        const auto result = document.set("/frames/" + std::to_string(selected) +
                                         "/textureWrapperFilePath", texture);
        if (!result.error.empty()) return fail(result.error);
        return result.changed;
    }
    if (action.action != "set_value")
        return false;
    if (suffix == "loop")
    {
        if (action.value != "true" && action.value != "false")
            return fail("loop accepts only true or false");
        return set("/loop", action.value == "true");
    }
    if (suffix == "texture-path")
    {
        if (frames->empty()) return fail("there is no selected animation frame");
        textureDraftFrame_ = selected;
        textureDraft_ = action.value;
        textureDraftBuffer_.fill('\0');
        const auto length = std::min(textureDraft_.size(), textureDraftBuffer_.size() - 1);
        std::memcpy(textureDraftBuffer_.data(), textureDraft_.data(), length);
        bump_revision(viewRevision_);
        return true;
    }
    constexpr std::string_view sourceRegionPrefix = "source-region/";
    if (suffix.starts_with(sourceRegionPrefix))
    {
        if (frames->empty() || semanticSourceFrame_ != selected ||
            semanticSourceWidth_ <= 0 || semanticSourceHeight_ <= 0 ||
            semanticSourceReference_.empty() ||
            texture_path_of((*frames)[selected]) != semanticSourceReference_ ||
            !animation_texture_is_compatible(assetRoot, semanticSourceReference_))
            return fail("the selected source image changed; refresh its preview before editing the region");
        double value = 0.0;
        if (!parse_finite_double(action.value, value))
            return fail("source region values must be finite numbers");
        const std::string_view field = suffix.substr(sourceRegionPrefix.size());
        SpriteSourceRegion region{0.0, 0.0, static_cast<double>(semanticSourceWidth_),
                                  static_cast<double>(semanticSourceHeight_)};
        const Json &frame = (*frames)[selected];
        const auto hasRect = frame.find("hasSourceRect");
        const auto rect = frame.find("sourceRect");
        if (hasRect != frame.end() && hasRect->is_boolean() && hasRect->get<bool>() &&
            rect != frame.end() && rect->is_object())
        {
            const auto read = [&](const char *key, double &target) {
                const auto found = rect->find(key);
                if (found == rect->end() || !found->is_number())
                    return false;
                target = found->get<double>();
                return std::isfinite(target);
            };
            std::string ignored;
            SpriteSourceRegion stored;
            if (read("x", stored.x) && read("y", stored.y) && read("w", stored.width) &&
                read("h", stored.height) &&
                validate_sprite_source_region(stored, semanticSourceWidth_,
                                              semanticSourceHeight_, ignored))
                region = stored;
        }
        if (field == "x") region.x = value;
        else if (field == "y") region.y = value;
        else if (field == "width") region.width = value;
        else if (field == "height") region.height = value;
        else return false;
        std::string validationError;
        if (!validate_sprite_source_region(region, semanticSourceWidth_, semanticSourceHeight_,
                                           validationError))
            return fail(std::move(validationError));
        const auto result = commit_animation_frame_source_region(
            AnimationFrameRegionDestination{document, selected}, semanticSourceReference_,
            assetRoot, region, semanticSourceWidth_, semanticSourceHeight_);
        if (!result.error.empty())
            return fail(result.error);
        return result.changed;
    }
    if (suffix == "duration")
    {
        double value = 0.0;
        if (!parse_finite_double(action.value, value))
            return fail("numeric animation values must be finite numbers");
        if (value <= 0.0 || frames->empty())
            return fail("frame duration must be positive and a frame must be selected");
        return set("/frames/" + std::to_string(selected) + "/duration", value);
    }
    return false;
}

void AnimationPanel::draw(AssetDocument &document,
                          const std::filesystem::path &assetRoot,
                          const PreparedTexture &preparedTexture)
{
    const Json &clip = document.root();
    const auto validation = validate_animation_clip_document(clip);
    const Json *frames = frames_of(clip);
    if (frames == nullptr)
    {
        ImGui::TextWrapped("This animation needs a frames array before it can be edited.");
        return;
    }

    if (!validation.valid())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Clip has validation issues:");
        for (const auto &issue : validation.issues)
            ImGui::BulletText("%s: %s", issue.jsonPointer.c_str(), issue.message.c_str());
    }

    const std::size_t frameCount = frames->size();
    if (selectedFrame_ >= frameCount)
        selectedFrame_ = frameCount == 0 ? 0 : frameCount - 1;

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool splitColumns = availableWidth >= 480.0f;
    const float leftColumnWidth = std::clamp(availableWidth * 0.42f, 240.0f, 320.0f);

    if (splitColumns)
    {
        ImGui::Columns(2, "##animation-panel-columns", false);
        ImGui::SetColumnWidth(0, leftColumnWidth);
    }

    const auto loop = clip.find("loop");
    if (loop != clip.end() && loop->is_boolean())
    {
        bool value = loop->get<bool>();
        if (ImGui::Checkbox("Loop", &value))
        {
            const auto result = document.set("/loop", value);
            if (!result.error.empty()) feedback_ = result.error;
            if (result.changed)
            {
                if (splitColumns) ImGui::Columns(1);
                return;
            }
        }
    }

    if (ImGui::Button("Add Frame"))
    {
        const auto result = add_animation_frame(document, make_default_animation_frame());
        if (result.changed)
        {
            selectedFrame_ = frameCount;
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            if (splitColumns) ImGui::Columns(1);
            return;
        }
        if (!result.error.empty()) feedback_ = result.error;
    }
    ImGui::SameLine();
    ImGui::Text("%zu frames", frameCount);

    const float listHeight = splitColumns ? 120.0f : 100.0f;
    if (ImGui::BeginChild("##frame-list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders))
    {
        if (frameCount != 0)
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(std::min<std::size_t>(frameCount,
                static_cast<std::size_t>(std::numeric_limits<int>::max()))));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto index = static_cast<std::size_t>(i);
                    const std::string label = "Frame " + std::to_string(index + 1) + "##animation-frame";
                    if (ImGui::Selectable(label.c_str(), selectedFrame_ == index))
                        scrub_to(clip, index);
                }
            }
        }
        else
        {
            ImGui::TextDisabled("No frames");
        }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(frameCount == 0 || selectedFrame_ == 0);
    if (ImGui::Button("Move Up"))
    {
        const auto result = move_animation_frame(document, selectedFrame_, selectedFrame_ - 1);
        if (result.changed)
        {
            --selectedFrame_;
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            if (splitColumns) ImGui::Columns(1);
            return;
        }
        if (!result.error.empty()) feedback_ = result.error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(frameCount == 0 || selectedFrame_ + 1 >= frameCount);
    if (ImGui::Button("Move Down"))
    {
        const auto result = move_animation_frame(document, selectedFrame_, selectedFrame_ + 1);
        if (result.changed)
        {
            ++selectedFrame_;
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            if (splitColumns) ImGui::Columns(1);
            return;
        }
        if (!result.error.empty()) feedback_ = result.error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(frameCount == 0);
    if (ImGui::Button("Remove Frame"))
    {
        const auto result = remove_animation_frame(document, selectedFrame_);
        if (result.changed)
        {
            selectedFrame_ = selectedFrame_ >= frameCount - 1 && frameCount > 1
                ? frameCount - 2 : (frameCount == 1 ? 0 : selectedFrame_);
            textureDraftFrame_ = static_cast<std::size_t>(-1);
            if (splitColumns) ImGui::Columns(1);
            return;
        }
        if (!result.error.empty()) feedback_ = result.error;
    }
    ImGui::EndDisabled();

    if (frameCount != 0)
    {
        const Json &frame = (*frames)[selectedFrame_];
        const auto duration = frame.is_object() ? frame.find("duration") : Json::const_iterator{};
        if (frame.is_object() && duration != frame.end() && duration->is_number())
        {
            const double value = duration->get<double>();
            ImGui::SetNextItemWidth(120.0f);
            const std::string pointer = "/frames/" + std::to_string(selectedFrame_) + "/duration";
            const auto outcome = durationDraft_.draw("Duration (seconds)",
                animation_numeric_address(document, assetRoot, pointer), value, 0.05);
            if (outcome.kind == NumericDraftOutcomeKind::Invalid)
                feedback_ = "Enter a complete finite frame duration.";
            else if (outcome.kind == NumericDraftOutcomeKind::Commit)
            {
                if (outcome.value <= 0.0)
                    feedback_ = "Frame duration must be positive.";
                else
                {
                    const auto result = document.set(pointer, outcome.value);
                    if (!result.error.empty()) feedback_ = result.error;
                    else feedback_.clear();
                    if (result.changed)
                    {
                        if (splitColumns) ImGui::Columns(1);
                        return;
                    }
                }
            }
        }

        if (textureDraftFrame_ != selectedFrame_)
            sync_texture_draft(frame);

        ImGui::SetNextItemWidth(std::max(80.0f, leftColumnWidth - 140.0f));
        const bool textSubmitted = ImGui::InputText("Texture", textureDraftBuffer_.data(),
                                                    textureDraftBuffer_.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (textSubmitted)
            textureDraft_ = textureDraftBuffer_.data();
        else
            textureDraft_ = textureDraftBuffer_.data();

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_ASSET_PATH"))
            {
                if (payload->Data != nullptr && payload->DataSize > 0)
                {
                    std::string dropped;
                    bool isImage = true;
                    if (payload->DataSize == sizeof(AssetDragPayload))
                    {
                        const auto *drag = static_cast<const AssetDragPayload *>(payload->Data);
                        dropped = drag->relativeAssetPath;
                        isImage = (drag->kind == EditorAssetKind::Image);
                    }
                    else
                    {
                        dropped = static_cast<const char *>(payload->Data);
                    }
                    while (!dropped.empty() && dropped.back() == '\0')
                        dropped.pop_back();
                    std::replace(dropped.begin(), dropped.end(), '\\', '/');

                    if (isImage && animation_texture_is_compatible(assetRoot, dropped))
                    {
                        textureDraft_ = dropped;
                        std::snprintf(textureDraftBuffer_.data(), textureDraftBuffer_.size(), "%s", dropped.c_str());
                        const auto result = document.set("/frames/" + std::to_string(selectedFrame_) +
                                                         "/textureWrapperFilePath", dropped);
                        if (!result.error.empty()) feedback_ = result.error;
                        else feedback_.clear();
                        if (result.changed)
                        {
                            if (splitColumns) ImGui::Columns(1);
                            return;
                        }
                    }
                    else
                    {
                        feedback_ = "Dropped asset is not a compatible image texture.";
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        const bool compatible = animation_texture_is_compatible(assetRoot, textureDraft_);
        ImGui::BeginDisabled(!compatible);
        if (ImGui::Button("Assign Texture") || (textSubmitted && compatible))
        {
            const auto result = document.set("/frames/" + std::to_string(selectedFrame_) +
                                             "/textureWrapperFilePath", textureDraft_);
            if (!result.error.empty()) feedback_ = result.error;
            else feedback_.clear();
            if (result.changed)
            {
                if (splitColumns) ImGui::Columns(1);
                return;
            }
        }
        ImGui::EndDisabled();
        if (!compatible)
            ImGui::TextDisabled("Choose an existing image inside the asset folder.");
    }

    if (splitColumns)
        ImGui::NextColumn();
    else
        ImGui::Separator();

    if (frameCount != 0)
    {
        if (ImGui::Button(playing_ ? "Pause" : "Play"))
            set_playing(!playing_);
        ImGui::SameLine();

        ImGui::BeginDisabled(frameCount <= 1);
        const int sliderMaximum = static_cast<int>(std::min<std::size_t>(
            frameCount - 1, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        int scrub = static_cast<int>(std::min<std::size_t>(selectedFrame_,
            static_cast<std::size_t>(sliderMaximum)));
        ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 60.0f));
        if (ImGui::SliderInt("Scrub", &scrub, 0, sliderMaximum))
            scrub_to(clip, static_cast<std::size_t>(scrub));
        ImGui::EndDisabled();

        const auto &frame = (*frames)[selectedFrame_];
        const auto assignedPath = texture_path_of(frame);
        if (preparedTexture && animation_texture_is_compatible(assetRoot, assignedPath))
        {
            const auto prepared = preparedTexture(animation_texture_path(assetRoot, assignedPath));
            if (!sourceRegionEditor_)
                sourceRegionEditor_ = std::make_unique<AnimationFrameRegionEditor>();
            if (sourceRegionEditor_->draw(document, selectedFrame_, assetRoot, assignedPath,
                                          prepared.texture, prepared.sourceWidth,
                                          prepared.sourceHeight))
            {
                if (splitColumns) ImGui::Columns(1);
                return;
            }
        }
        else
            ImGui::TextDisabled("Assign a compatible prepared texture to preview this frame.");
    }
    else
        ImGui::TextDisabled("Add a frame to begin editing this animation.");

    if (splitColumns)
        ImGui::Columns(1);

    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
    advance_playback(document.root(), ImGui::GetIO().DeltaTime);
}
} // namespace platformator_editor

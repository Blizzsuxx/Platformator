#include "game.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
bool read_unsigned(const Json &value, std::uint64_t &result)
{
    if (!value.is_number_unsigned())
        return false;
    result = value.get<std::uint64_t>();
    return true;
}

std::string runtime_value(const Json &value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_primitive())
        return value.dump();
    return "(structured value)";
}

void add_button(std::vector<SemanticNode> &nodes, const std::string &parentId,
                std::string suffix, std::string name, bool enabled,
                std::string value = {})
{
    SemanticNode node;
    node.id = parentId + "/" + suffix;
    node.parentId = parentId;
    node.role = SemanticRole::Button;
    node.name = std::move(name);
    node.value = std::move(value);
    node.enabled = enabled;
    node.actions = {"activate"};
    nodes.push_back(std::move(node));
}
} // namespace

GamePanel::~GamePanel()
{
    for (const auto &[renderer, texture] : frameTextures_)
    {
        (void)renderer;
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
    }
    frameTextures_.clear();
    renderer_ = nullptr;
    frameTexture_ = nullptr;
}

void GamePanel::set_renderer(SDL_Renderer *renderer) noexcept
{
    renderer_ = renderer;
    frameTexture_ = ensure_frame_texture(renderer_);
}

SDL_Texture *GamePanel::ensure_frame_texture(SDL_Renderer *renderer) noexcept
{
    if (renderer == nullptr)
        return nullptr;
    const auto existing = frameTextures_.find(renderer);
    if (existing != frameTextures_.end())
        return existing->second;
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, FrameWidth, FrameHeight);
    if (texture == nullptr)
        return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    if (hasFramePixels_ && !SDL_UpdateTexture(texture, nullptr, framePixels_.data(), FrameStride))
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    try
    {
        frameTextures_.emplace(renderer, texture);
    }
    catch (...)
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    return texture;
}

void GamePanel::release_renderer(SDL_Renderer *renderer) noexcept
{
    if (renderer == nullptr)
        return;
    const auto found = frameTextures_.find(renderer);
    if (found != frameTextures_.end())
    {
        if (found->second != nullptr)
            SDL_DestroyTexture(found->second);
        frameTextures_.erase(found);
    }
    if (renderer_ == renderer)
    {
        renderer_ = nullptr;
        frameTexture_ = nullptr;
    }
}

std::string GamePanel::asset_change_status_message(bool liveAssetReloadAdvertised)
{
    return liveAssetReloadAdvertised
        ? "Asset files changed. The runner advertises live reload, but this editor has no safe reload transaction path; Build & Restart to use the latest assets."
        : "Asset files changed. This runner does not advertise safe live asset reload; Build & Restart to use the latest assets.";
}

void GamePanel::observe_generation(PlaySession *session)
{
    const PlayGeneration next = session == nullptr ? PlayGeneration{} : session->generation();
    if (next != generation_)
    {
        release_input_capture(session);
        generation_ = next;
    }
    else
    {
        if (session == nullptr || session->state() == PlaySessionState::Idle ||
            session->state() == PlaySessionState::Stopping)
        {
            release_input_capture(session);
        }
        return;
    }
    if (next)
    {
        pipelineStatus_.clear();
        restartAvailable_ = false;
    }
    runtimeObjects_.clear();
    selectedHandle_.reset();
    selectedSnapshot_ = Json::object();
    snapshotRevision_ = 0;
    nextCursor_.reset();
    awaitingRuntimePage_ = false;
    captureVisible_ = false;
    semanticHandles_.clear();
    hasFramePixels_ = false;
    status_ = session != nullptr && session->state() != PlaySessionState::Idle
        ? "Embedded game session is starting."
        : "No embedded game session is active.";
    for (const auto &[renderer, texture] : frameTextures_)
    {
        (void)renderer;
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
    }
    frameTextures_.clear();
    frameTexture_ = nullptr;
}

void GamePanel::set_pipeline_status(std::string message, bool restartAvailable)
{
    pipelineStatus_ = std::move(message);
    restartAvailable_ = restartAvailable;
}

void GamePanel::set_restart_callback(std::function<void()> callback)
{
    restartCallback_ = std::move(callback);
}

bool GamePanel::upload_frame(PlaySession &session, std::uint32_t slot,
                             std::uint64_t sequence)
{
    const bool copied = session.copy_frame_slot(slot, sequence, framePixels_);
    bool uploaded = false;
    if (copied && renderer_ != nullptr)
    {
        hasFramePixels_ = true;
        frameTexture_ = ensure_frame_texture(renderer_);
        uploaded = frameTexture_ != nullptr;
        for (auto &[targetRenderer, texture] : frameTextures_)
        {
            (void)targetRenderer;
            if (texture != nullptr &&
                !SDL_UpdateTexture(texture, nullptr, framePixels_.data(), FrameStride))
                uploaded = false;
        }
    }
    if (copied)
    {
        if (!session.release_frame(slot, sequence))
            status_ = "The copied game frame could not be released; stopping the session will reclaim its slot.";
        else if (!uploaded)
            status_ = renderer_ == nullptr
                ? "Game frame received while the editor renderer is unavailable."
                : std::string("Game frame upload failed: ") + SDL_GetError();
        else
            status_ = "Embedded game running · 640 × 360 · runtime input remains in the game window.";
    }
    return uploaded;
}

void GamePanel::consume_event(PlaySession &session,
                              const platformator::editor_session::Message &message)
{
    observe_generation(&session);
    if (!session.generation() || message.playGeneration != session.generation().value ||
        message.nonce != session.nonce())
        return;
    if (message.type == "frame_ready")
    {
        const auto slot = message.body.find("slot");
        const auto sequence = message.body.find("frame_sequence");
        std::uint64_t slotValue = 0;
        std::uint64_t sequenceValue = 0;
        if (slot == message.body.end() || !read_unsigned(*slot, slotValue) ||
            sequence == message.body.end() || !read_unsigned(*sequence, sequenceValue) ||
            slotValue > 1)
        {
            status_ = "The embedded game sent an invalid frame event.";
            return;
        }
        (void)upload_frame(session, static_cast<std::uint32_t>(slotValue), sequenceValue);
        return;
    }
    if (message.type == "object_page")
    {
        awaitingRuntimePage_ = false;
        const auto result = message.body.value("status", std::string("invalid"));
        if (result != "ok")
        {
            if (result == "stale")
            {
                runtimeObjects_.clear();
                selectedHandle_.reset();
                selectedSnapshot_ = Json::object();
                snapshotRevision_ = 0;
                nextCursor_.reset();
            }
            status_ = "Runtime inspection " + result + ": " +
                message.body.value("message", std::string("refresh the runtime object list"));
            return;
        }
        const auto objects = message.body.find("objects");
        const auto revision = message.body.find("snapshot_revision");
        const auto next = message.body.find("next_cursor");
        if (objects == message.body.end() || !objects->is_array() || objects->size() > 64 ||
            revision == message.body.end() || !read_unsigned(*revision, snapshotRevision_) ||
            next == message.body.end())
        {
            status_ = "Runtime inspection returned an invalid or oversized hierarchy page.";
            runtimeObjects_.clear();
            nextCursor_.reset();
            return;
        }
        const auto requestedCursor = message.body.value("cursor", std::uint64_t{});
        if (requestedCursor == 0)
            runtimeObjects_.clear();
        for (const auto &object : *objects)
            runtimeObjects_.push_back(object);
        nextCursor_.reset();
        if (!next->is_null())
        {
            std::uint64_t cursor = 0;
            if (!read_unsigned(*next, cursor))
            {
                status_ = "Runtime inspection returned an invalid continuation cursor.";
                runtimeObjects_.clear();
                snapshotRevision_ = 0;
                return;
            }
            nextCursor_ = cursor;
        }
        status_ = "Runtime object snapshot is read-only and scoped to this play session.";
        return;
    }
    if (message.type == "object_snapshot")
    {
        const auto result = message.body.value("status", std::string("invalid"));
        if (result == "ok")
        {
            selectedSnapshot_ = message.body;
            selectedHandle_ = message.body.value("handle", Json::object());
            status_ = "Selected live runtime object; authored scene data and history are unchanged.";
        }
        else
        {
            selectedSnapshot_ = Json::object();
            status_ = "Runtime object " + result + ": " +
                message.body.value("message", std::string("select a current runtime object"));
        }
        return;
    }
    if (message.type == "inspection_busy")
    {
        awaitingRuntimePage_ = false;
        status_ = message.body.value("message", std::string("Runtime inspection is busy."));
        return;
    }
    if (message.type == "error")
        status_ = message.body.value("message", std::string("Embedded game session reported an error."));
    else if (message.type == "stopped")
        status_ = "Embedded game is stopping.";
    else if (message.type == "ready")
        status_ = "Embedded game is ready.";
    else if (message.type == "state")
        status_ = message.body.value("state", std::string("Embedded game state changed."));
    else if (message.type == "step_completed")
        status_ = "One fixed simulation step completed.";
}

void GamePanel::set_visible(PlaySession *session, bool visible)
{
    observe_generation(session);
    if (!visible)
        release_input_capture(session);
    if (session == nullptr || session->state() == PlaySessionState::Idle ||
        session->state() == PlaySessionState::Stopping ||
        !session->capabilities().frameStream || captureVisible_ == visible)
        return;
    if (session->request_capture(visible))
        captureVisible_ = visible;
}

void GamePanel::release_input_capture(PlaySession *session)
{
    if (capturingInput_)
    {
        capturingInput_ = false;
        if (session != nullptr)
            session->release_all_inputs();
    }
}

void GamePanel::handle_sdl_event(PlaySession *session, const SDL_Event &event)
{
    if (!capturingInput_ || session == nullptr)
        return;
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            release_input_capture(session);
            return;
        }
        EmbeddedInputEvent inputEvent;
        inputEvent.kind = EmbeddedInputKind::KeyDown;
        inputEvent.scancode = static_cast<int>(event.key.scancode);
        inputEvent.keycode = static_cast<int>(event.key.key);
        inputEvent.modifiers = static_cast<std::uint16_t>(event.key.mod);
        session->send_input(inputEvent);
    }
    else if (event.type == SDL_EVENT_KEY_UP)
    {
        EmbeddedInputEvent inputEvent;
        inputEvent.kind = EmbeddedInputKind::KeyUp;
        inputEvent.scancode = static_cast<int>(event.key.scancode);
        inputEvent.keycode = static_cast<int>(event.key.key);
        inputEvent.modifiers = static_cast<std::uint16_t>(event.key.mod);
        session->send_input(inputEvent);
    }
}

void GamePanel::draw(PlaySession *session, bool visible)
{
    observe_generation(session);
    set_visible(session, visible);
    if (!visible || session == nullptr || session->state() == PlaySessionState::Idle ||
        session->state() == PlaySessionState::Stopping)
    {
        release_input_capture(session);
    }
    if (session == nullptr || session->state() == PlaySessionState::Idle)
    {
        const auto &displayStatus = pipelineStatus_.empty() ? status_ : pipelineStatus_;
        ImGui::TextWrapped("%s", displayStatus.c_str());
        if (pipelineStatus_.find("external game window") != std::string::npos)
            ImGui::TextDisabled("Turn off Embed Game view in Build and Run settings to use the compatible target in its external window.");
        else
            ImGui::TextDisabled("Choose Embedded Game in Build and Run settings, then build and run a compatible target.");
        if (restartAvailable_ && restartCallback_ && ImGui::Button("Build & Restart"))
            restartCallback_();
        return;
    }

    const bool running = session->state() == PlaySessionState::Running;
    const bool paused = session->state() == PlaySessionState::Paused;
    const auto &capabilities = session->capabilities();
    ImGui::BeginDisabled(!running || !capabilities.playControl);
    if (ImGui::Button("Pause", ImVec2(70.0f, 0.0f)))
        session->request_pause();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!paused || !capabilities.playControl);
    if (ImGui::Button("Resume", ImVec2(76.0f, 0.0f)))
        session->request_resume();
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(60.0f, 0.0f)))
        session->request_step();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(session->state() == PlaySessionState::Stopping);
    if (ImGui::Button("Stop", ImVec2(60.0f, 0.0f)))
        session->request_stop();
    ImGui::EndDisabled();
    if (!capabilities.playControl)
        ImGui::TextDisabled("Pause, Resume, and Step are not advertised by this runner.");
    if (!capabilities.inputForwarding)
        ImGui::TextDisabled("Embedded v1 is read-only. Use the external game window for native gameplay input.");
    else if (running || paused)
    {
        if (capturingInput_)
            status_ = "Embedded game running · Capturing gameplay input (Esc to release)";
        else
            status_ = "Embedded game running · Click Game view to capture input";
    }
    else
        capturingInput_ = false;

    ImGui::TextWrapped("%s", status_.c_str());
    if (!pipelineStatus_.empty())
        ImGui::TextWrapped("%s", pipelineStatus_.c_str());
    if (restartAvailable_ && restartCallback_ && ImGui::Button("Build & Restart"))
        restartCallback_();

    const char *aspectNames[] = {"Free Aspect", "16:9", "4:3", "Native 640x360"};
    int currentAspect = static_cast<int>(aspectPreset_);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##game_aspect", &currentAspect, aspectNames, IM_ARRAYSIZE(aspectNames)))
        aspectPreset_ = static_cast<GameAspectPreset>(currentAspect);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("Scale", &zoomScale_, 0.5f, 4.0f, "%.1fx");
    zoomScale_ = std::clamp(zoomScale_, 0.5f, 4.0f);

    if (frameTexture_ != nullptr && hasFramePixels_)
    {
        ImVec2 available = ImGui::GetContentRegionAvail();
        float targetWidth = static_cast<float>(FrameWidth);
        float targetHeight = static_cast<float>(FrameHeight);
        if (aspectPreset_ == GameAspectPreset::Native640x360)
        {
            targetWidth = static_cast<float>(FrameWidth) * zoomScale_;
            targetHeight = static_cast<float>(FrameHeight) * zoomScale_;
        }
        else if (aspectPreset_ == GameAspectPreset::Aspect16x9)
        {
            const float aspect = 16.0f / 9.0f;
            float w = available.x > 0.0f ? available.x : 640.0f;
            float h = w / aspect;
            if (available.y > 0.0f && h > available.y)
            {
                h = available.y;
                w = h * aspect;
            }
            targetWidth = w * zoomScale_;
            targetHeight = h * zoomScale_;
        }
        else if (aspectPreset_ == GameAspectPreset::Aspect4x3)
        {
            const float aspect = 4.0f / 3.0f;
            float w = available.x > 0.0f ? available.x : 640.0f;
            float h = w / aspect;
            if (available.y > 0.0f && h > available.y)
            {
                h = available.y;
                w = h * aspect;
            }
            targetWidth = w * zoomScale_;
            targetHeight = h * zoomScale_;
        }
        else // FreeAspect
        {
            targetWidth = (available.x > 0.0f ? available.x : 640.0f) * zoomScale_;
            targetHeight = (available.y > 0.0f ? available.y : 360.0f) * zoomScale_;
        }
        const ImVec2 size(std::max(1.0f, targetWidth), std::max(1.0f, targetHeight));
        const auto textureId = static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(frameTexture_));
        ImGui::Image(ImTextureRef(textureId), size);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const bool imageHovered = ImGui::IsItemHovered();

        if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            capturingInput_ = true;
            ImGui::SetWindowFocus();
        }

        if (capturingInput_)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !imageHovered) ||
                !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                release_input_capture(session);
            }
            else if (session != nullptr)
            {
                const ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= imageMin.x && mousePos.x <= imageMax.x &&
                    mousePos.y >= imageMin.y && mousePos.y <= imageMax.y)
                {
                    const float normX = (mousePos.x - imageMin.x) / std::max(1.0f, imageMax.x - imageMin.x);
                    const float normY = (mousePos.y - imageMin.y) / std::max(1.0f, imageMax.y - imageMin.y);
                    const float logicalX = std::clamp(normX * static_cast<float>(FrameWidth), 0.0f, static_cast<float>(FrameWidth));
                    const float logicalY = std::clamp(normY * static_cast<float>(FrameHeight), 0.0f, static_cast<float>(FrameHeight));

                    if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)
                    {
                        EmbeddedInputEvent moveEvent;
                        moveEvent.kind = EmbeddedInputKind::MouseMove;
                        moveEvent.logicalX = logicalX;
                        moveEvent.logicalY = logicalY;
                        session->send_input(moveEvent);
                    }
                    for (int btn = 0; btn < 3; ++btn)
                    {
                        const int sdlBtn = btn == 0 ? 1 : (btn == 1 ? 3 : 2);
                        if (ImGui::IsMouseClicked(btn))
                        {
                            EmbeddedInputEvent btnEvent;
                            btnEvent.kind = EmbeddedInputKind::MouseButtonDown;
                            btnEvent.mouseButton = sdlBtn;
                            btnEvent.logicalX = logicalX;
                            btnEvent.logicalY = logicalY;
                            session->send_input(btnEvent);
                        }
                        else if (ImGui::IsMouseReleased(btn))
                        {
                            EmbeddedInputEvent btnEvent;
                            btnEvent.kind = EmbeddedInputKind::MouseButtonUp;
                            btnEvent.mouseButton = sdlBtn;
                            btnEvent.logicalX = logicalX;
                            btnEvent.logicalY = logicalY;
                            session->send_input(btnEvent);
                        }
                    }
                }
            }
        }
    }
    else
        ImGui::TextDisabled("Waiting for the first visible 640 × 360 game frame.");

    if (capabilities.runtimeInspection)
    {
        if (ImGui::Button("Refresh Runtime Objects") &&
            session->request_object_page())
        {
            runtimeObjects_.clear();
            selectedHandle_.reset();
            selectedSnapshot_ = Json::object();
            snapshotRevision_ = 0;
            nextCursor_.reset();
            awaitingRuntimePage_ = true;
        }
        if (nextCursor_ && ImGui::Button("Load More Runtime Objects") &&
            session->request_object_page(nextCursor_, snapshotRevision_))
            awaitingRuntimePage_ = true;
        draw_runtime_inspection(session);
    }
    else
        ImGui::TextDisabled("This runner does not advertise read-only runtime inspection.");
}

void GamePanel::draw_runtime_inspection(PlaySession *session)
{
    if (session == nullptr)
        return;
    if (awaitingRuntimePage_)
        ImGui::TextDisabled("Reading one bounded runtime hierarchy page...");
    if (ImGui::BeginChild("Runtime object list", ImVec2(0.0f, 150.0f), ImGuiChildFlags_Borders))
    {
        for (const auto &object : runtimeObjects_)
        {
            const auto handle = object.find("handle");
            if (handle == object.end() || !handle->is_object())
                continue;
            const auto id = object_semantic_id(*handle);
            const auto label = object.value("name", std::string("GameObject")) +
                "##runtime/" + id;
            const bool selected = selectedHandle_ && *selectedHandle_ == *handle;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                if (session->request_object_snapshot(*handle))
                {
                    selectedHandle_ = *handle;
                    selectedSnapshot_ = Json::object();
                }
            }
        }
    }
    ImGui::EndChild();
    if (selectedSnapshot_.is_object() && selectedSnapshot_.value("status", std::string{}) == "ok")
    {
        ImGui::Text("Runtime object: %s", selectedSnapshot_.value("name", std::string("GameObject")).c_str());
        ImGui::Text("Tag: %s · Active: %s", selectedSnapshot_.value("tag", std::string{}).c_str(),
                    selectedSnapshot_.value("active", false) ? "yes" : "no");
        const auto position = selectedSnapshot_.find("position");
        if (position != selectedSnapshot_.end() && position->is_object())
            ImGui::Text("Position: %s", position->dump().c_str());
        const auto scale = selectedSnapshot_.find("scale");
        if (scale != selectedSnapshot_.end() && scale->is_object())
            ImGui::Text("Scale: %s", scale->dump().c_str());
        const auto components = selectedSnapshot_.find("components");
        if (components != selectedSnapshot_.end() && components->is_array())
            for (std::size_t index = 0; index < std::min<std::size_t>(components->size(), 32); ++index)
            {
                const auto &component = (*components)[index];
                ImGui::TextWrapped("%s", component.dump().c_str());
            }
    }
}

std::string GamePanel::object_semantic_id(const Json &handle)
{
    if (!handle.is_object())
        return "invalid";
    return std::to_string(handle.value("runtime_serial", std::uint64_t{})) + "/" +
           std::to_string(handle.value("slot", std::uint64_t{})) + "/" +
           std::to_string(handle.value("generation", std::uint64_t{}));
}

std::vector<SemanticNode> GamePanel::semantic_nodes(PlaySession *session,
                                                     const std::string &parentId)
{
    std::vector<SemanticNode> nodes;
    const bool active = session != nullptr && session->state() != PlaySessionState::Idle;
    const bool running = session != nullptr && session->state() == PlaySessionState::Running;
    const bool paused = session != nullptr && session->state() == PlaySessionState::Paused;
    const bool controls = session != nullptr && session->capabilities().playControl;
    const bool inspect = session != nullptr && session->capabilities().runtimeInspection;
    add_button(nodes, parentId, "pause", "Pause embedded game", active && running && controls);
    add_button(nodes, parentId, "resume", "Resume embedded game", active && paused && controls);
    add_button(nodes, parentId, "step", "Advance one fixed simulation step", active && paused && controls);
    add_button(nodes, parentId, "stop", "Stop embedded game", active);
    add_button(nodes, parentId, "refresh-objects", "Refresh read-only runtime objects",
               active && inspect && !awaitingRuntimePage_);
    if (restartAvailable_)
        add_button(nodes, parentId, "restart", "Build and restart embedded game",
                   static_cast<bool>(restartCallback_));
    SemanticNode status;
    status.id = parentId + "/status";
    status.parentId = parentId;
    status.role = SemanticRole::Text;
    status.name = "Embedded Game status";
    status.value = session == nullptr || session->state() == PlaySessionState::Idle
        ? (pipelineStatus_.empty() ? status_ : pipelineStatus_)
        : (pipelineStatus_.empty() ? status_ : status_ + " " + pipelineStatus_);
    status.visible = true;
    nodes.push_back(std::move(status));
    SemanticNode frame;
    frame.id = parentId + "/frame";
    frame.parentId = parentId;
    frame.role = SemanticRole::Group;
    frame.name = "Embedded Game frame";
    frame.value = capturingInput_
        ? "capturing input; 640 by 360 RGBA8"
        : (frameTexture_ == nullptr || !hasFramePixels_ ? "waiting" : "640 by 360 RGBA8; aspect preserved");
    frame.help = capturingInput_
        ? "Gameplay input captured. Press Escape or click outside to release."
        : "Click Game view to capture gameplay keyboard and mouse input.";
    frame.visible = true;
    nodes.push_back(std::move(frame));
    semanticHandles_.clear();
    for (const auto &object : runtimeObjects_)
    {
        const auto handle = object.find("handle");
        if (handle == object.end() || !handle->is_object())
            continue;
        const std::string suffix = object_semantic_id(*handle);
        SemanticNode item;
        item.id = parentId + "/object/" + suffix;
        item.parentId = parentId;
        item.role = SemanticRole::ListItem;
        item.name = object.value("name", std::string("GameObject"));
        item.value = object.value("kind", std::string("GameObject"));
        item.selected = selectedHandle_ && *selectedHandle_ == *handle;
        item.actions = {"activate"};
        item.visible = true;
        semanticHandles_.insert_or_assign(item.id, *handle);
        nodes.push_back(std::move(item));
    }
    if (nextCursor_)
        add_button(nodes, parentId, "load-more", "Load more runtime objects",
                   active && inspect && !awaitingRuntimePage_);
    return nodes;
}

bool GamePanel::dispatch_semantic_action(PlaySession &session,
                                         const SemanticAction &action)
{
    if (action.action != "activate")
        return false;
    const std::string &id = action.nodeId;
    if (id.ends_with("/pause"))
        return session.request_pause();
    if (id.ends_with("/resume"))
        return session.request_resume();
    if (id.ends_with("/step"))
        return session.request_step();
    if (id.ends_with("/stop"))
        return session.request_stop();
    if (id.ends_with("/restart") && restartAvailable_ && restartCallback_)
    {
        restartCallback_();
        return true;
    }
    if (id.ends_with("/refresh-objects"))
    {
        if (!session.request_object_page())
            return false;
        runtimeObjects_.clear();
        selectedHandle_.reset();
        selectedSnapshot_ = Json::object();
        snapshotRevision_ = 0;
        nextCursor_.reset();
        awaitingRuntimePage_ = true;
        return true;
    }
    if (id.ends_with("/load-more") && nextCursor_)
    {
        if (!session.request_object_page(nextCursor_, snapshotRevision_))
            return false;
        awaitingRuntimePage_ = true;
        return true;
    }
    const auto target = semanticHandles_.find(id);
    if (target == semanticHandles_.end() || !session.request_object_snapshot(target->second))
        return false;
    selectedHandle_ = target->second;
    selectedSnapshot_ = Json::object();
    return true;
}

const std::string &GamePanel::status() const noexcept
{
    return status_;
}
} // namespace platformator_editor

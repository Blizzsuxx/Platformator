#include "project.h"
#include "document.h"
#include "editcommands.h"
#include "editorwork.h"
#include "panels/project.h"
#include "selection.h"
#include "test_support.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace platformator_editor
{
struct ProjectPanelTestProbe
{
    static bool path_is_within(const std::filesystem::path &root,
                               const std::filesystem::path &candidate)
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

    static void select_directory(ProjectPanel &panel, const std::filesystem::path &assetRoot,
                                 const std::filesystem::path &directory)
    {
        panel.selectedDirectory_ = directory;
        panel.selectedRoot_ = assetRoot;
        panel.selectedRootLabel_ = "Assets";
        panel.selectedEntry_.reset();
        for (auto current = directory; path_is_within(assetRoot, current); current = current.parent_path())
        {
            panel.expandedDirectories_.insert(current.string());
            if (current == assetRoot || current == current.parent_path())
                break;
        }
    }

    static void select_entry(ProjectPanel &panel, const std::filesystem::path &entry)
    {
        panel.selectedEntry_ = entry;
    }

    static bool selected_entry_is(const ProjectPanel &panel, const std::filesystem::path &entry)
    {
        return panel.selectedEntry_ && *panel.selectedEntry_ == entry;
    }

    static bool selected_directory_is(const ProjectPanel &panel,
                                      const std::filesystem::path &directory)
    {
        return panel.selectedDirectory_ == directory;
    }

    static bool template_loaded(const ProjectPanel &panel)
    {
        return panel.selectedTemplate_.has_value();
    }

    static void set_search_query(ProjectPanel &panel, std::string query)
    {
        panel.searchQuery_ = std::move(query);
    }

    static std::vector<std::filesystem::path> filtered_entries(
        const ProjectPanel &panel, const std::filesystem::path &directory)
    {
        const auto found = panel.directories_.find(directory.string());
        if (found == panel.directories_.end())
            return {};
        std::vector<std::filesystem::path> result;
        for (const auto index : found->second.filteredEntryIndexes)
            if (index < found->second.entries.size())
                result.push_back(found->second.entries[index].path);
        return result;
    }

    static bool search_cache_current(const ProjectPanel &panel,
                                    const std::filesystem::path &directory)
    {
        const auto found = panel.directories_.find(directory.string());
        return found != panel.directories_.end() &&
               found->second.filteredQuery == panel.searchQuery_ &&
               found->second.filteredEntriesRevision == found->second.entriesRevision;
    }

    static std::vector<std::string> assignment_labels(
        const ProjectPanel &panel, const DocumentSession &document,
        const SelectionModel &selection, const std::filesystem::path &assetPath)
    {
        std::vector<std::string> labels;
        const auto targets = panel.asset_assignment_targets(document, selection, assetPath);
        for (const auto &target : targets)
            labels.push_back(target.label);
        return labels;
    }

    static bool assign_target(ProjectPanel &panel, DocumentSession &document,
                              EditCommandService &commands, const SelectionModel &selection,
                              const std::filesystem::path &assetPath, std::size_t index,
                              std::string &error,
                              const std::filesystem::path &targetDiscoveryPath = {})
    {
        const auto &discoveryPath = targetDiscoveryPath.empty() ? assetPath : targetDiscoveryPath;
        const auto targets = panel.asset_assignment_targets(document, selection, discoveryPath);
        return index < targets.size() &&
               panel.assign_asset_target(document, commands, targets[index], assetPath, error);
    }

    static bool listing_contains(const ProjectPanel &panel, const std::filesystem::path &directory,
                                 const std::filesystem::path &entry)
    {
        const auto found = panel.directories_.find(directory.string());
        if (found == panel.directories_.end())
            return false;
        const auto &state = found->second;
        return std::find(state.files.begin(), state.files.end(), entry) != state.files.end() ||
               std::find(state.directories.begin(), state.directories.end(), entry) != state.directories.end();
    }

    static std::filesystem::path canonical_root(const ProjectPanel &panel,
                                                const std::filesystem::path &directory)
    {
        const auto found = panel.directories_.find(directory.string());
        return found == panel.directories_.end() ? std::filesystem::path{} : found->second.canonicalRoot;
    }

    static bool listing_loaded(const ProjectPanel &panel, const std::filesystem::path &directory)
    {
        const auto found = panel.directories_.find(directory.string());
        return found != panel.directories_.end() && found->second.loaded;
    }

    static bool presentation_uses_alias(const ProjectPanel &panel,
                                        const std::filesystem::path &logicalPath,
                                        const std::filesystem::path &alias)
    {
        const auto found = panel.presentationDependencies_.find(logicalPath.string());
        return found != panel.presentationDependencies_.end() &&
               std::find(found->second.aliasAncestors.begin(), found->second.aliasAncestors.end(),
                         alias.lexically_normal()) != found->second.aliasAncestors.end();
    }

    static bool directory_uses_alias(const ProjectPanel &panel,
                                     const std::filesystem::path &directory,
                                     const std::filesystem::path &alias)
    {
        const auto found = panel.directories_.find(directory.string());
        return found != panel.directories_.end() &&
               std::find(found->second.aliasAncestors.begin(), found->second.aliasAncestors.end(),
                         alias.lexically_normal()) != found->second.aliasAncestors.end();
    }

    static std::string listing_status(const ProjectPanel &panel,
                                      const std::filesystem::path &directory)
    {
        const auto found = panel.directories_.find(directory.string());
        if (found == panel.directories_.end())
            return "missing";
        return std::string("canonical=") + found->second.canonicalRoot.string() +
            " loaded=" + (found->second.loaded ? "true" : "false") +
            " request=" + (static_cast<bool>(found->second.request) ? "true" : "false") +
            " error=" + found->second.error;
    }

    static SDL_Texture *thumbnail(const ProjectPanel &panel, const std::filesystem::path &path)
    {
        return panel.thumbnails_.find(path);
    }

    static SDL_Texture *request_thumbnail(ProjectPanel &panel, const std::filesystem::path &path)
    {
        SDL_Texture *texture = panel.thumbnail_for(path);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (texture == nullptr && std::chrono::steady_clock::now() < deadline)
        {
            panel.apply_ready_results();
            texture = panel.thumbnail_for(path);
            if (texture == nullptr)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return texture;
    }

    static std::size_t stale_thumbnail_count(const ProjectPanel &panel)
    {
        return panel.thumbnails_.stats().staleEntries;
    }

    static bool thumbnail_stale(const ProjectPanel &panel, const std::filesystem::path &path)
    {
        return panel.thumbnails_.stale(path);
    }

    static bool wait_for_thumbnail_idle(ProjectPanel &panel)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            panel.apply_ready_results();
            const auto stats = panel.thumbnails_.stats();
            if (!stats.running && stats.pendingPaths == 0 && stats.completedBytes == 0)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    static std::filesystem::path request_animation_frame(ProjectPanel &panel,
                                                         const std::filesystem::path &path,
                                                         const std::filesystem::path &assetRoot)
    {
        const auto &preview = panel.animation_for(path, assetRoot);
        return preview.frames.empty() ? std::filesystem::path{} : preview.frames.front().texturePath;
    }

    static bool animation_stale(const ProjectPanel &panel, const std::filesystem::path &path)
    {
        return panel.staleAnimations_.contains(path.string());
    }

    static std::filesystem::path animation_frame(const ProjectPanel &panel,
                                                  const std::filesystem::path &path)
    {
        const auto found = panel.animations_.find(path.string());
        return found == panel.animations_.end() || found->second.frames.empty()
            ? std::filesystem::path{} : found->second.frames.front().texturePath;
    }

    static std::string animation_failure(const ProjectPanel &panel,
                                         const std::filesystem::path &path)
    {
        return panel.failedAnimationRefreshes_.contains(path.string())
            ? panel.coverageMessage_ : "no cached animation failure";
    }
};
} // namespace platformator_editor

namespace
{
class ProjectPanelFrames
{
  public:
    ProjectPanelFrames()
    {
        try
        {
            require(SDL_Init(SDL_INIT_VIDEO), std::string("SDL video init failed: ") + SDL_GetError());
            sdlInitialized_ = true;
            window_ = SDL_CreateWindow("Project panel test", 1100, 820, SDL_WINDOW_HIDDEN);
            require(window_ != nullptr, std::string("SDL window creation failed: ") + SDL_GetError());
            renderer_ = SDL_CreateRenderer(window_, nullptr);
            require(renderer_ != nullptr, std::string("SDL renderer creation failed: ") + SDL_GetError());
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            contextCreated_ = true;
            ImGui::GetIO().IniFilename = nullptr;
            require(ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_),
                    std::string("ImGui SDL backend init failed: ") + SDL_GetError());
            platformInitialized_ = true;
            require(ImGui_ImplSDLRenderer3_Init(renderer_),
                    std::string("ImGui renderer backend init failed: ") + SDL_GetError());
            rendererInitialized_ = true;
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    ~ProjectPanelFrames()
    {
        cleanup();
    }

    std::set<std::uintptr_t> draw(ProjectPanel &panel, DocumentSession &document,
                                  EditCommandService &commands,
                                  SelectionModel *selection = nullptr)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1080.0f, 800.0f}, ImGuiCond_Always);
        ImGui::Begin("Project and Assets###panel.project",
                     nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        panel.draw(document, commands, selection);
        ImGui::End();
        ImGui::Render();
        require(SDL_SetRenderDrawColor(renderer_, 24, 24, 24, 255) && SDL_RenderClear(renderer_),
                std::string("SDL frame clear failed: ") + SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
        std::set<std::uintptr_t> textures;
        const ImDrawData *drawData = ImGui::GetDrawData();
        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
        {
            const ImDrawList *list = drawData->CmdLists[listIndex];
            for (const ImDrawCmd &command : list->CmdBuffer)
            {
                if (command.ElemCount != 0)
                    textures.insert(static_cast<std::uintptr_t>(command.GetTexID()));
            }
        }
        SDL_RenderPresent(renderer_);
        panel.end_frame();
        return textures;
    }

    SDL_Renderer *renderer() const noexcept { return renderer_; }

    std::array<std::uint8_t, 4> texture_pixel(SDL_Texture *texture)
    {
        require(texture != nullptr, "texture pixel checks require a loaded texture");
        SDL_Texture *target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_TARGET, 1, 1);
        require(target != nullptr, std::string("create one-pixel readback target: ") + SDL_GetError());
        require(SDL_SetRenderTarget(renderer_, target), std::string("set one-pixel readback target: ") + SDL_GetError());
        require(SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) && SDL_RenderClear(renderer_),
                std::string("clear one-pixel readback target: ") + SDL_GetError());
        require(SDL_RenderTexture(renderer_, texture, nullptr, nullptr),
                std::string("render cached Project texture for readback: ") + SDL_GetError());
        SDL_Surface *surface = SDL_RenderReadPixels(renderer_, nullptr);
        require(surface != nullptr, std::string("read cached Project texture: ") + SDL_GetError());
        if (surface->format != SDL_PIXELFORMAT_RGBA32)
        {
            SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surface);
            surface = converted;
            require(surface != nullptr, std::string("convert cached Project texture: ") + SDL_GetError());
        }
        const auto *rgba = static_cast<const std::uint8_t *>(surface->pixels);
        const std::array<std::uint8_t, 4> result{rgba[0], rgba[1], rgba[2], rgba[3]};
        SDL_DestroySurface(surface);
        require(SDL_SetRenderTarget(renderer_, nullptr), std::string("restore Project frame target: ") + SDL_GetError());
        SDL_DestroyTexture(target);
        return result;
    }

  private:
    void cleanup() noexcept
    {
        if (rendererInitialized_)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            rendererInitialized_ = false;
        }
        if (platformInitialized_)
        {
            ImGui_ImplSDL3_Shutdown();
            platformInitialized_ = false;
        }
        if (contextCreated_)
        {
            ImGui::DestroyContext();
            contextCreated_ = false;
        }
        if (renderer_ != nullptr)
        {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (sdlInitialized_)
        {
            SDL_Quit();
            sdlInitialized_ = false;
        }
    }

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    bool sdlInitialized_ = false;
    bool contextCreated_ = false;
    bool platformInitialized_ = false;
    bool rendererInitialized_ = false;
};

void write_solid_png(const std::filesystem::path &path, std::uint8_t red,
                     std::uint8_t green, std::uint8_t blue)
{
    std::filesystem::create_directories(path.parent_path());
    SDL_Surface *surface = SDL_CreateSurface(13, 7, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, std::string("test image surface creation failed: ") + SDL_GetError());
    const Uint32 color = SDL_MapSurfaceRGBA(surface, red, green, blue, 255);
    const bool filled = SDL_FillSurfaceRect(surface, nullptr, color);
    const bool saved = filled && IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("test PNG write failed: ") + SDL_GetError());
}

void test_animation_preview_uploads_without_project_draw()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto image = assetRoot / "animations" / "sheet.png";
    const auto unrelatedFolder = assetRoot / "other-folder";
    std::filesystem::create_directories(unrelatedFolder);
    write_solid_png(image, 100, 140, 220);

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    ProjectChanges changes;
    ProjectScanService scans(2);
    ProjectPanel panel(context, changes, scans, frames.renderer());
    ProjectPanelTestProbe::select_directory(panel, assetRoot, unrelatedFolder);
    require(ProjectPanelTestProbe::selected_directory_is(panel, unrelatedFolder),
            "the Project panel is switched away from the animation image folder");

    auto prepared = panel.prepared_animation_texture(image);
    require(prepared.texture == nullptr,
            "the animation image starts cold without drawing the Project panel");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (prepared.texture == nullptr && std::chrono::steady_clock::now() < deadline)
    {
        panel.service_owner_work(frames.renderer());
        prepared = panel.prepared_animation_texture(image);
        panel.end_frame();
        if (prepared.texture == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    panel.end_frame();
    require(prepared.texture != nullptr && prepared.sourceWidth == 13 &&
                prepared.sourceHeight == 7,
            "owner service uploads a cold Animation-panel preview and publishes its dimensions without Project drawing");
}

ProjectChangeBatch project_change(ProjectGeneration generation, ProjectChangeKind kind,
                                  const std::filesystem::path &path, bool directory)
{
    ProjectChangeBatch batch;
    batch.projectGeneration = generation;
    batch.events.push_back(ProjectChangeEvent{.kind = kind, .path = path, .directory = directory,
                                              .projectGeneration = generation});
    return batch;
}

void settle_panel(ProjectPanelFrames &frames, ProjectPanel &panel, DocumentSession &document,
                  EditCommandService &commands, ProjectScanService &scans, int framesToDraw = 4)
{
    for (int index = 0; index < framesToDraw; ++index)
    {
        (void)frames.draw(panel, document, commands);
        scans.wait_for_idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    (void)frames.draw(panel, document, commands);
}

void test_project_panel_alias_presentation_invalidation()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto targetImage = assetRoot / "texture-target" / "canonical-texture.png";
    const auto imageAlias = assetRoot / "linked-texture.png";
    const auto unrelatedImage = assetRoot / "unrelated.png";
    const auto animationPath = assetRoot / "linked.animset";
    const auto canonicalTree = assetRoot / "tree-target";
    const auto aliasTree = assetRoot / "a-tree-alias";
    const auto nested = canonicalTree / "nested";
    std::filesystem::create_directories(nested);
    std::filesystem::create_directories(assetRoot);
    write_solid_png(targetImage, 220, 30, 40);
    write_solid_png(unrelatedImage, 30, 180, 50);
    std::ofstream(nested / "old.scene") << "old";
    std::error_code symlinkError;
    std::filesystem::create_symlink(targetImage, imageAlias, symlinkError);
    require(!symlinkError, "contained file-alias presentation checks require local symlink support");
    std::filesystem::create_directory_symlink(canonicalTree, aliasTree, symlinkError);
    require(!symlinkError, "contained directory-alias presentation checks require local symlink support");
    write_json(animationPath, Json{{"framesPerSecond", 1.0}, {"loop", true},
        {"frames", Json::array({Json{{"duration", 0.0},
                                      {"textureWrapperFilePath", "assets/linked-texture.png"}}})}});

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    changes.start(ProjectGeneration{project.generation}, project.assetRoot);
    ProjectScanService scans(8);
    auto document = make_default_document(project.projectRoot, project.assetRoot, project.generation);
    EditCommandService commands;
    ProjectPanel panel(context, changes, scans, frames.renderer());

    settle_panel(frames, panel, document, commands, scans);
    SDL_Texture *oldAliasTexture = ProjectPanelTestProbe::thumbnail(panel, imageAlias);
    SDL_Texture *unrelatedTexture = ProjectPanelTestProbe::thumbnail(panel, unrelatedImage);
    require(oldAliasTexture != nullptr && unrelatedTexture != nullptr && oldAliasTexture != unrelatedTexture,
            "real Project panel frames must prepare both the contained alias and unrelated image previews");
    const auto oldDrawTextures = frames.draw(panel, document, commands);
    const auto oldAliasId = reinterpret_cast<std::uintptr_t>(oldAliasTexture);
    const auto unrelatedId = reinterpret_cast<std::uintptr_t>(unrelatedTexture);
    std::set<std::uintptr_t> drawTextures = oldDrawTextures;
    const auto drawDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto currentAliasId = oldAliasId;
    auto currentUnrelatedId = unrelatedId;
    while ((!drawTextures.contains(currentAliasId) || !drawTextures.contains(currentUnrelatedId)) &&
           std::chrono::steady_clock::now() < drawDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        drawTextures = frames.draw(panel, document, commands);
        currentAliasId = reinterpret_cast<std::uintptr_t>(ProjectPanelTestProbe::thumbnail(panel, imageAlias));
        currentUnrelatedId = reinterpret_cast<std::uintptr_t>(ProjectPanelTestProbe::thumbnail(panel, unrelatedImage));
    }
    require(currentAliasId != 0 && currentUnrelatedId != 0 &&
                drawTextures.contains(currentAliasId) && drawTextures.contains(currentUnrelatedId),
            "real Project panel draw data must submit both current cached image previews after bounded preparation");

    auto animationFrame = ProjectPanelTestProbe::request_animation_frame(panel, animationPath, assetRoot);
    const auto animationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (animationFrame.empty() && std::chrono::steady_clock::now() < animationDeadline)
    {
        (void)frames.draw(panel, document, commands);
        animationFrame = ProjectPanelTestProbe::request_animation_frame(panel, animationPath, assetRoot);
        if (animationFrame.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (animationFrame != imageAlias || ProjectPanelTestProbe::animation_stale(panel, animationPath))
        throw std::runtime_error("the aliased animation frame did not prepare: path=" +
            animationFrame.string() + ", failure=" +
            ProjectPanelTestProbe::animation_failure(panel, animationPath));

    ProjectPanelTestProbe::select_entry(panel, unrelatedImage);
    const auto documentBeforeChange = document.root;
    const auto documentRevisionBeforeChange = document.valueRevision;
    const auto historyBeforeChange = commands.history_status();
    const auto originalTime = std::filesystem::last_write_time(targetImage);
    const auto originalSize = std::filesystem::file_size(targetImage);
    write_solid_png(targetImage, 20, 60, 230);
    require(std::filesystem::file_size(targetImage) == originalSize,
            "equal-metadata alias fixture must retain its original byte length");
    std::filesystem::last_write_time(targetImage, originalTime);
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified,
                                       std::filesystem::canonical(targetImage), false));
    require(ProjectPanelTestProbe::thumbnail_stale(panel, imageAlias) &&
                ProjectPanelTestProbe::stale_thumbnail_count(panel) > 0,
            "canonical replacement must invalidate the logical alias thumbnail version");
    require(ProjectPanelTestProbe::animation_stale(panel, animationPath),
            "a canonical texture change must invalidate animation-frame consumers reached through a file alias");
    work::reset();
    require(ProjectPanelTestProbe::wait_for_thumbnail_idle(panel),
            "old Project thumbnail jobs must drain before measuring the targeted refresh");
    SDL_Texture *newAliasTexture = nullptr;
    std::set<std::uintptr_t> changedDrawTextures;
    bool newPixelsPresented = false;
    const auto replacementDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < replacementDeadline)
    {
        changedDrawTextures = frames.draw(panel, document, commands);
        newAliasTexture = ProjectPanelTestProbe::thumbnail(panel, imageAlias);
        if (newAliasTexture != nullptr &&
            changedDrawTextures.contains(reinterpret_cast<std::uintptr_t>(newAliasTexture)) &&
            frames.texture_pixel(newAliasTexture) == std::array<std::uint8_t, 4>{20, 60, 230, 255})
        {
            newPixelsPresented = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(newAliasTexture != nullptr && newPixelsPresented,
            "a real Project frame must present the newly prepared alias pixels while retaining last-good until ready");
    SDL_Texture *currentUnrelatedTexture = ProjectPanelTestProbe::thumbnail(panel, unrelatedImage);
    require(currentUnrelatedTexture != nullptr &&
                changedDrawTextures.contains(reinterpret_cast<std::uintptr_t>(currentUnrelatedTexture)) &&
                frames.texture_pixel(currentUnrelatedTexture) == std::array<std::uint8_t, 4>{30, 180, 50, 255},
            "a canonical alias update must preserve unrelated cached preview content");
    require(ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBeforeChange && document.valueRevision == documentRevisionBeforeChange &&
                commands.history_status().currentToken == historyBeforeChange.currentToken &&
                commands.history_status().savedToken == historyBeforeChange.savedToken,
            "canonical preview invalidation must preserve valid selection and document history state");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto changedWork = work::snapshot();
    require(changedWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] >= 1 &&
                changedWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] <= 2 &&
                changedWork[static_cast<std::size_t>(work::Counter::TextureUploads)] == 1,
            "a canonical file-alias change must upload only its affected preview, with at most one stale decode "
            "discarded (decode=" +
                std::to_string(changedWork[static_cast<std::size_t>(work::Counter::ImageDecodes)]) +
                ", upload=" +
                std::to_string(changedWork[static_cast<std::size_t>(work::Counter::TextureUploads)]) + ")");
#endif

    ProjectPanelTestProbe::select_directory(panel, project.assetRoot, aliasTree / "nested");
    settle_panel(frames, panel, document, commands, scans, 5);
    require(ProjectPanelTestProbe::canonical_root(panel, aliasTree) == canonicalTree &&
                ProjectPanelTestProbe::canonical_root(panel, aliasTree / "nested") == nested &&
                ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                        aliasTree / "nested" / "old.scene"),
            "real nested Project frames must cache logical alias-folder listings and canonical roots");
    ProjectPanelTestProbe::select_entry(panel, unrelatedImage);
    const auto movedTree = assetRoot / "moved-tree-target";
    const auto aliasRename = ProjectChangeBatch{
        .projectGeneration = ProjectGeneration{project.generation},
        .events = {ProjectChangeEvent{.kind = ProjectChangeKind::Renamed, .path = movedTree,
            .oldPath = canonicalTree, .affectedPaths = {canonicalTree, movedTree}, .directory = true,
            .projectGeneration = ProjectGeneration{project.generation}}}};
    std::filesystem::rename(canonicalTree, movedTree);
    std::ofstream(movedTree / "nested" / "renamed.scene") << "renamed";
    panel.apply_changes(aliasRename);
    require(!ProjectPanelTestProbe::canonical_root(panel, aliasTree).empty() &&
                !ProjectPanelTestProbe::listing_loaded(panel, aliasTree) &&
                !ProjectPanelTestProbe::listing_loaded(panel, aliasTree / "nested") &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage),
            "canonical ancestor rename must invalidate both cached alias listings and preserve unrelated selection");
    std::filesystem::remove(aliasTree);
    std::filesystem::create_directory_symlink(movedTree, aliasTree);
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified, aliasTree, true));
    settle_panel(frames, panel, document, commands, scans, 5);
    require(ProjectPanelTestProbe::canonical_root(panel, aliasTree) == movedTree,
            std::string("retargeting a directory alias must adopt its new canonical root: ") +
                ProjectPanelTestProbe::listing_status(panel, aliasTree));
    require(ProjectPanelTestProbe::canonical_root(panel, aliasTree / "nested") == movedTree / "nested",
            "retargeting a directory alias must adopt the nested canonical root");
    require(ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                    aliasTree / "nested" / "renamed.scene"),
            "retargeting a directory alias must refresh its nested logical listing");
    require(ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage),
            "retargeting an alias must preserve unrelated Project selection");

    std::filesystem::remove_all(movedTree);
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Removed, movedTree, true));
    (void)frames.draw(panel, document, commands);
    scans.wait_for_idle();
    require(ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                    aliasTree / "nested" / "renamed.scene"),
            "a broken canonical ancestor must retain the last complete alias listing while unavailable");

    const auto repairedNested = movedTree / "nested";
    std::filesystem::create_directories(repairedNested);
    std::ofstream(repairedNested / "repaired.scene") << "repaired";
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Added, movedTree, true));
    settle_panel(frames, panel, document, commands, scans, 5);
    require(ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                    aliasTree / "nested" / "repaired.scene") &&
                !ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                         aliasTree / "nested" / "renamed.scene") &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage),
            "recreating a canonical ancestor must repair the broken alias and replace stale nested contents");

    const auto alternateTree = assetRoot / "alternate-tree-target";
    std::filesystem::create_directories(alternateTree / "nested");
    std::ofstream(alternateTree / "nested" / "retargeted.scene") << "retargeted";
    std::filesystem::remove(aliasTree);
    std::filesystem::create_directory_symlink(alternateTree, aliasTree);
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified, aliasTree, true));
    settle_panel(frames, panel, document, commands, scans, 5);
    require(ProjectPanelTestProbe::canonical_root(panel, aliasTree) == alternateTree,
            "a retargeted alias root must adopt its new canonical presentation root");
    require(ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                    aliasTree / "nested" / "retargeted.scene"),
            "a retargeted alias must refresh its nested logical listing");
    require(ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBeforeChange &&
                document.valueRevision == documentRevisionBeforeChange &&
                commands.history_status().currentToken == historyBeforeChange.currentToken &&
                commands.history_status().savedToken == historyBeforeChange.savedToken,
            "a repaired alias retarget must preserve unrelated selection and authoring history");
}

void test_directory_alias_file_events_refresh_descendant_previews()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto aliasTree = assetRoot / "folder-alias";
    const auto linksRoot = assetRoot / "folder-links";
    const auto middleAlias = linksRoot / "middle";
    const auto targetA = assetRoot / "target-a";
    const auto targetB = assetRoot / "target-b";
    const auto targetC = assetRoot / "target-c";
    const auto unrelatedImage = assetRoot / "unrelated.png";
    const auto make_target = [&](const std::filesystem::path &target, std::string_view marker,
                                 std::string_view frameName, std::array<std::uint8_t, 3> color) {
        const auto nested = target / "nested";
        std::filesystem::create_directories(nested);
        std::ofstream(nested / marker) << marker;
        write_solid_png(nested / frameName, color[0], color[1], color[2]);
        write_json(nested / "preview.animset", Json{
            {"framesPerSecond", 1.0}, {"loop", true},
            {"frames", Json::array({Json{{"duration", 0.0},
                {"textureWrapperFilePath", "assets/folder-alias/nested/" + std::string(frameName)}}})}});
    };
    std::filesystem::create_directories(assetRoot);
    make_target(targetA, "from-a.scene", "frame-a.png", {220, 30, 40});
    make_target(targetB, "from-b.scene", "frame-b.png", {20, 60, 230});
    make_target(targetC, "from-c.scene", "frame-c.png", {30, 210, 80});
    write_solid_png(unrelatedImage, 130, 120, 110);
    std::error_code symlinkError;
    std::filesystem::create_directories(linksRoot);
    std::filesystem::create_directory_symlink("../target-a", middleAlias, symlinkError);
    require(!symlinkError, "intermediate directory alias preview checks require local symlink support");
    std::filesystem::create_directory_symlink("folder-links/middle", aliasTree, symlinkError);
    require(!symlinkError, "outer directory alias preview checks require local symlink support");

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    changes.start(ProjectGeneration{project.generation}, project.assetRoot);
    ProjectScanService scans(2);
    auto document = make_default_document(project.projectRoot, project.assetRoot, project.generation);
    EditCommandService commands;
    ProjectPanel panel(context, changes, scans, frames.renderer());
    settle_panel(frames, panel, document, commands, scans);
    ProjectPanelTestProbe::select_directory(panel, assetRoot, aliasTree / "nested");
    settle_panel(frames, panel, document, commands, scans, 6);

    const auto aliasImageA = aliasTree / "nested" / "frame-a.png";
    const auto aliasAnimation = aliasTree / "nested" / "preview.animset";
    const auto unrelated = ProjectPanelTestProbe::thumbnail(panel, unrelatedImage);
    auto image = ProjectPanelTestProbe::thumbnail(panel, aliasImageA);
    require(image != nullptr && unrelated != nullptr &&
                ProjectPanelTestProbe::animation_frame(panel, aliasAnimation) == aliasImageA,
            "real Project frames must cache an image and animation-frame preview beneath the directory alias");
    ProjectPanelTestProbe::select_entry(panel, unrelatedImage);
    require(frames.texture_pixel(image) == std::array<std::uint8_t, 4>{220, 30, 40, 255},
            "the initial alias preview must show its original target pixels");

    const auto documentBefore = document.root;
    const auto historyBefore = commands.history_status();
    const auto movedLinks = assetRoot / "folder-links-moved";
    std::filesystem::rename(linksRoot, movedLinks, symlinkError);
    require(!symlinkError, "rename the parent directory containing a cached intermediate alias");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Removed, linksRoot, true));
    require(ProjectPanelTestProbe::animation_stale(panel, aliasAnimation),
            "a parent-directory rename must stale cached animation descendants through intermediate links");
    const auto renameRefreshStart = std::chrono::steady_clock::now();
    settle_panel(frames, panel, document, commands, scans, 3);
    require(std::chrono::steady_clock::now() - renameRefreshStart <= std::chrono::seconds(3),
            "the broken parent status and last-good preview must settle within three seconds");
    require(ProjectPanelTestProbe::thumbnail(panel, aliasImageA) == image &&
                frames.texture_pixel(image) == std::array<std::uint8_t, 4>{220, 30, 40, 255},
            "a broken intermediate parent must retain last-good Project previews");
    require(ProjectPanelTestProbe::wait_for_thumbnail_idle(panel),
            "stale requests from the broken alias must settle before measuring its repair");
    std::filesystem::create_directories(linksRoot);
    std::filesystem::create_directory_symlink("../target-b", middleAlias, symlinkError);
    require(!symlinkError, "recreate the parent directory with a new contained intermediate target");
    work::reset();
    const auto recreationRefreshStart = std::chrono::steady_clock::now();
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Added, linksRoot, true));
    settle_panel(frames, panel, document, commands, scans, 6);
    require(std::chrono::steady_clock::now() - recreationRefreshStart <= std::chrono::seconds(3),
            "recreating the intermediate parent must settle within three seconds");
    const auto aliasImageB = aliasTree / "nested" / "frame-b.png";
    image = ProjectPanelTestProbe::thumbnail(panel, aliasImageB);
    require(image != nullptr && ProjectPanelTestProbe::animation_frame(panel, aliasAnimation) == aliasImageB &&
                !ProjectPanelTestProbe::animation_stale(panel, aliasAnimation) &&
                ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                        aliasTree / "nested" / "from-b.scene") &&
                !ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                         aliasTree / "nested" / "from-a.scene"),
            "parent recreation must adopt the new nested listing and animation-frame reference");
    require(frames.texture_pixel(image) == std::array<std::uint8_t, 4>{20, 60, 230, 255},
            "the Project thumbnail must render pixels from the recreated intermediate parent");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto retargetWork = work::snapshot();
    require(retargetWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] >= 1 &&
                retargetWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] <= 3 &&
                retargetWork[static_cast<std::size_t>(work::Counter::TextureUploads)] >= 1 &&
                retargetWork[static_cast<std::size_t>(work::Counter::TextureUploads)] <= 2,
            "recreating an alias parent stays within three targeted decodes and two owner uploads (decode=" +
                std::to_string(retargetWork[static_cast<std::size_t>(work::Counter::ImageDecodes)]) +
                ", upload=" +
                std::to_string(retargetWork[static_cast<std::size_t>(work::Counter::TextureUploads)]) + ")");
#endif
    require(ProjectPanelTestProbe::thumbnail(panel, unrelatedImage) == unrelated &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBefore &&
                commands.history_status().currentToken == historyBefore.currentToken &&
                commands.history_status().savedToken == historyBefore.savedToken,
            "intermediate parent recreation must preserve unrelated preview and authoring state");

    std::filesystem::remove(middleAlias, symlinkError);
    require(!symlinkError, "unlink the cached intermediate directory alias");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Removed, middleAlias, false));
    require(ProjectPanelTestProbe::animation_stale(panel, aliasAnimation),
            "unlinking an alias with directory=false must retain and stale descendant dependencies");
    settle_panel(frames, panel, document, commands, scans, 3);
    require(ProjectPanelTestProbe::thumbnail(panel, aliasImageB) == image &&
                frames.texture_pixel(image) == std::array<std::uint8_t, 4>{20, 60, 230, 255},
            "a broken alias must retain its last-good cached image while the target is unavailable");
    require(ProjectPanelTestProbe::wait_for_thumbnail_idle(panel),
            "stale requests from the broken alias must settle before measuring its next repair");

    std::filesystem::create_directory_symlink(targetC, middleAlias, symlinkError);
    require(!symlinkError, "repair the broken directory alias");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Added, middleAlias, false));
    work::reset();
    settle_panel(frames, panel, document, commands, scans, 6);
    const auto aliasImageC = aliasTree / "nested" / "frame-c.png";
    image = ProjectPanelTestProbe::thumbnail(panel, aliasImageC);
    require(image != nullptr && ProjectPanelTestProbe::animation_frame(panel, aliasAnimation) == aliasImageC &&
                !ProjectPanelTestProbe::animation_stale(panel, aliasAnimation) &&
                ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                        aliasTree / "nested" / "from-c.scene") &&
                !ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                         aliasTree / "nested" / "from-b.scene"),
            "repair must refresh descendant listings and animation references without a child write");
    require(frames.texture_pixel(image) == std::array<std::uint8_t, 4>{30, 210, 80, 255},
            "the repaired alias thumbnail must render pixels from its newly resolved target");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto repairWork = work::snapshot();
    require(repairWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] >= 1 &&
                repairWork[static_cast<std::size_t>(work::Counter::ImageDecodes)] <= 3 &&
                repairWork[static_cast<std::size_t>(work::Counter::TextureUploads)] >= 1 &&
                repairWork[static_cast<std::size_t>(work::Counter::TextureUploads)] <= 2,
            "repairing the alias stays within three targeted decodes and two owner uploads");
#endif
    require(ProjectPanelTestProbe::thumbnail(panel, unrelatedImage) == unrelated &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBefore &&
                commands.history_status().currentToken == historyBefore.currentToken &&
                commands.history_status().savedToken == historyBefore.savedToken,
            "broken-link repair must preserve unrelated preview and authoring state");

    const auto stagedLinks = assetRoot / "folder-links-next";
    const auto displacedLinks = assetRoot / "folder-links-old";
    std::filesystem::create_directories(stagedLinks);
    std::filesystem::create_directory_symlink("../target-a", stagedLinks / "middle", symlinkError);
    require(!symlinkError, "stage a replacement intermediate parent directory");
    std::filesystem::rename(linksRoot, displacedLinks, symlinkError);
    require(!symlinkError, "move the prior intermediate parent before replacement");
    std::filesystem::rename(stagedLinks, linksRoot, symlinkError);
    require(!symlinkError, "replace the intermediate parent directory");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified, linksRoot, true));
    const auto replacementRefreshStart = std::chrono::steady_clock::now();
    settle_panel(frames, panel, document, commands, scans, 6);
    require(std::chrono::steady_clock::now() - replacementRefreshStart <= std::chrono::seconds(3),
            "replacing the intermediate parent must settle within three seconds");
    const auto aliasImageAfterParentReplacement = aliasTree / "nested" / "frame-a.png";
    image = ProjectPanelTestProbe::thumbnail(panel, aliasImageAfterParentReplacement);
    require(image != nullptr &&
                ProjectPanelTestProbe::animation_frame(panel, aliasAnimation) == aliasImageAfterParentReplacement &&
                ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                        aliasTree / "nested" / "from-a.scene") &&
                frames.texture_pixel(image) == std::array<std::uint8_t, 4>{220, 30, 40, 255} &&
                ProjectPanelTestProbe::thumbnail(panel, unrelatedImage) == unrelated &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBefore &&
                commands.history_status().currentToken == historyBefore.currentToken &&
                commands.history_status().savedToken == historyBefore.savedToken,
            "replacing the intermediate parent must refresh actual previews and nested listings");

    const auto alternateMiddleAlias = assetRoot / "folder-middle-alias-next";
    const auto outerReplacement = assetRoot / "folder-alias.next";
    std::filesystem::create_directory_symlink(targetA, alternateMiddleAlias, symlinkError);
    require(!symlinkError, "create the replacement intermediate directory alias");
    std::filesystem::create_directory_symlink(alternateMiddleAlias.filename(), outerReplacement,
                                               symlinkError);
    require(!symlinkError, "create the replacement outer directory alias");
#if defined(__linux__)
    std::filesystem::rename(outerReplacement, aliasTree, symlinkError);
#else
    std::filesystem::remove(aliasTree, symlinkError);
    require(!symlinkError, "remove the old outer directory alias before replacement");
    std::filesystem::rename(outerReplacement, aliasTree, symlinkError);
#endif
    require(!symlinkError, "atomically retarget the outer directory alias");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified, aliasTree, false));
    settle_panel(frames, panel, document, commands, scans, 6);
    const auto aliasImageRetargeted = aliasTree / "nested" / "frame-a.png";
    image = ProjectPanelTestProbe::thumbnail(panel, aliasImageRetargeted);
    require(image != nullptr &&
                ProjectPanelTestProbe::animation_frame(panel, aliasAnimation) == aliasImageRetargeted &&
                ProjectPanelTestProbe::listing_contains(panel, aliasTree / "nested",
                                                        aliasTree / "nested" / "from-a.scene") &&
                frames.texture_pixel(image) == std::array<std::uint8_t, 4>{220, 30, 40, 255},
            "retargeting the outer link must refresh listings and previews through the new intermediate alias");
    require(!ProjectPanelTestProbe::animation_stale(panel, aliasAnimation),
            "the refreshed animation preview must leave its current presentation dependency ready");
    require(ProjectPanelTestProbe::presentation_uses_alias(panel, aliasAnimation, alternateMiddleAlias) &&
                !ProjectPanelTestProbe::presentation_uses_alias(panel, aliasAnimation, middleAlias) &&
                !ProjectPanelTestProbe::presentation_uses_alias(panel, aliasTree / "nested" / "frame-c.png",
                                                                 middleAlias) &&
                ProjectPanelTestProbe::canonical_root(panel, aliasTree) == targetA &&
                ProjectPanelTestProbe::canonical_root(panel, aliasTree / "nested") == targetA / "nested" &&
                ProjectPanelTestProbe::directory_uses_alias(panel, aliasTree, alternateMiddleAlias) &&
                !ProjectPanelTestProbe::directory_uses_alias(panel, aliasTree, middleAlias) &&
                ProjectPanelTestProbe::directory_uses_alias(panel, aliasTree / "nested", alternateMiddleAlias) &&
                !ProjectPanelTestProbe::directory_uses_alias(panel, aliasTree / "nested", middleAlias),
            "successful outer retarget must replace current and missing-child hop dependencies");

    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Modified, middleAlias, false));
    require(!ProjectPanelTestProbe::animation_stale(panel, aliasAnimation),
            "a successful retarget must retire the old intermediate preview dependency");
    require(ProjectPanelTestProbe::canonical_root(panel, aliasTree) == targetA &&
                ProjectPanelTestProbe::canonical_root(panel, aliasTree / "nested") == targetA / "nested",
            "an event on the retired intermediate link must not restore the old canonical listing roots");
}

void test_initially_broken_alias_repairs_project_listings_and_previews()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto links = assetRoot / "links";
    const auto middle = links / "middle";
    const auto alias = assetRoot / "folder-alias";
    const auto target = assetRoot / "target";
    const auto nested = target / "nested";
    const auto unrelatedImage = assetRoot / "unrelated.png";
    std::filesystem::create_directories(links);
    std::filesystem::create_directories(nested);
    write_solid_png(nested / "frame.png", 20, 180, 60);
    write_json(nested / "preview.animset", Json{
        {"framesPerSecond", 1.0}, {"loop", true},
        {"frames", Json::array({Json{{"duration", 0.0},
            {"textureWrapperFilePath", "assets/folder-alias/nested/frame.png"}}})}});
    write_solid_png(unrelatedImage, 120, 30, 150);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink("links/middle", alias, symlinkError);
    require(!symlinkError, "initially broken Project aliases require local symlink support");

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    changes.start(ProjectGeneration{project.generation}, project.assetRoot);
    ProjectScanService scans(2);
    auto document = make_default_document(project.projectRoot, project.assetRoot, project.generation);
    EditCommandService commands;
    ProjectPanel panel(context, changes, scans, frames.renderer());
    settle_panel(frames, panel, document, commands, scans, 2);
    ProjectPanelTestProbe::select_directory(panel, assetRoot, alias / "nested");
    settle_panel(frames, panel, document, commands, scans, 5);

    const auto logicalImage = alias / "nested" / "frame.png";
    const auto logicalAnimation = alias / "nested" / "preview.animset";
    require(ProjectPanelTestProbe::request_thumbnail(panel, logicalImage) == nullptr &&
                ProjectPanelTestProbe::request_animation_frame(panel, logicalAnimation, assetRoot).empty(),
            "first-demand broken Project previews must cache their missing image and animation state");
    SDL_Texture *unrelatedTexture = ProjectPanelTestProbe::request_thumbnail(panel, unrelatedImage);
    require(unrelatedTexture != nullptr,
            "the unrelated Project image must be cached before repairing the broken alias");
    ProjectPanelTestProbe::select_entry(panel, unrelatedImage);
    const auto documentBefore = document.root;
    const auto historyBefore = commands.history_status();

    std::filesystem::create_directory_symlink("../target", middle, symlinkError);
    require(!symlinkError, "repair the initially unresolved intermediate directory link");
    panel.apply_changes(project_change(ProjectGeneration{project.generation},
                                       ProjectChangeKind::Added, middle, false));
    const auto refreshStart = std::chrono::steady_clock::now();
    settle_panel(frames, panel, document, commands, scans, 8);
    require(std::chrono::steady_clock::now() - refreshStart <= std::chrono::seconds(3),
            "an intermediate-link repair must refresh Project state within three seconds");

    const auto image = ProjectPanelTestProbe::thumbnail(panel, logicalImage);
    const auto framePath = ProjectPanelTestProbe::animation_frame(panel, logicalAnimation);
    const bool imageListed = ProjectPanelTestProbe::listing_contains(panel, alias / "nested", logicalImage);
    const bool animationListed = ProjectPanelTestProbe::listing_contains(panel, alias / "nested", logicalAnimation);
    require(image != nullptr && framePath == logicalImage && imageListed && animationListed,
            "repair must clear first-demand negative previews and populate the nested logical listing (image=" +
                std::to_string(image != nullptr) + ", frame=" + framePath.string() + ", imageListed=" +
                std::to_string(imageListed) + ", animationListed=" + std::to_string(animationListed) +
                ", listing=" + ProjectPanelTestProbe::listing_status(panel, alias / "nested") + ")");
    require(frames.texture_pixel(image) == std::array<std::uint8_t, 4>{20, 180, 60, 255},
            "the repaired Project frame must display the current aliased image content");
    require(ProjectPanelTestProbe::thumbnail(panel, unrelatedImage) == unrelatedTexture &&
                ProjectPanelTestProbe::selected_entry_is(panel, unrelatedImage) &&
                document.root == documentBefore &&
                commands.history_status().currentToken == historyBefore.currentToken &&
                commands.history_status().savedToken == historyBefore.savedToken,
            "initial broken-link repair must preserve unrelated selection and authoring history");
}

void test_project_search_tracks_listing_revisions()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto textureDirectory = assetRoot / "Textures";
    std::filesystem::create_directories(textureDirectory);
    write_solid_png(textureDirectory / "Hero.PNG", 120, 140, 180);
    std::ofstream(assetRoot / "walk.animset") << "{}";
    std::ofstream(assetRoot / "hero.webp") << "image fixture";
    std::ofstream(assetRoot / "pixel.qoi") << "image fixture";
    std::ofstream(assetRoot / "theme.aiff") << "audio fixture";

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    changes.start(ProjectGeneration{project.generation}, project.assetRoot);
    ProjectScanService scans(2);
    auto document = make_default_document(project.projectRoot, project.assetRoot, project.generation);
    EditCommandService commands;
    ProjectPanel panel(context, changes, scans, frames.renderer());
    ProjectPanelTestProbe::select_directory(panel, assetRoot, assetRoot);
    settle_panel(frames, panel, document, commands, scans);

    ProjectPanelTestProbe::set_search_query(panel, "folder");
    (void)frames.draw(panel, document, commands);
    require(ProjectPanelTestProbe::filtered_entries(panel, assetRoot) ==
                std::vector<std::filesystem::path>({textureDirectory}) &&
                ProjectPanelTestProbe::search_cache_current(panel, assetRoot),
            "Project search must match directory type labels and cache the query for its listing revision");

    ProjectPanelTestProbe::set_search_query(panel, "animation");
    (void)frames.draw(panel, document, commands);
    require(ProjectPanelTestProbe::filtered_entries(panel, assetRoot) ==
                std::vector<std::filesystem::path>({assetRoot / "walk.animset"}),
            "Project search must match asset type context as well as file names");

    ProjectPanelTestProbe::set_search_query(panel, "image texture");
    (void)frames.draw(panel, document, commands);
    const auto imageEntries = ProjectPanelTestProbe::filtered_entries(panel, assetRoot);
    require(std::find(imageEntries.begin(), imageEntries.end(), assetRoot / "hero.webp") != imageEntries.end() &&
                std::find(imageEntries.begin(), imageEntries.end(), assetRoot / "pixel.qoi") != imageEntries.end(),
            "Project image search and thumbnail icon classification must share the supported image extensions");

    ProjectPanelTestProbe::set_search_query(panel, "audio");
    (void)frames.draw(panel, document, commands);
    require(ProjectPanelTestProbe::filtered_entries(panel, assetRoot) ==
                std::vector<std::filesystem::path>({assetRoot / "theme.aiff"}),
            "Project audio search must use the same extension classification as its asset icon");

    ProjectPanelTestProbe::set_search_query(panel, "png");
    (void)frames.draw(panel, document, commands);
    require(ProjectPanelTestProbe::filtered_entries(panel, assetRoot).empty(),
            "Project search must not recursively surface assets from a different directory");
    ProjectPanelTestProbe::select_directory(panel, assetRoot, textureDirectory);
    settle_panel(frames, panel, document, commands, scans);
    (void)frames.draw(panel, document, commands);
    require(ProjectPanelTestProbe::filtered_entries(panel, textureDirectory) ==
                std::vector<std::filesystem::path>({textureDirectory / "Hero.PNG"}),
            "Project search must match a case-insensitive file extension in the selected directory");

    write_solid_png(textureDirectory / "Second.png", 30, 90, 170);
    panel.refresh();
    settle_panel(frames, panel, document, commands, scans);
    const auto refreshed = ProjectPanelTestProbe::filtered_entries(panel, textureDirectory);
    require(refreshed == std::vector<std::filesystem::path>({textureDirectory / "Hero.PNG",
                                                              textureDirectory / "Second.png"}) &&
                ProjectPanelTestProbe::search_cache_current(panel, textureDirectory),
            "Project search results must rebuild from a new directory-entry revision without reordering files");

    SelectionModel selection;
    (void)frames.draw(panel, document, commands, &selection);
    auto semanticNodes = panel.semantic_nodes(document, selection.current(),
                                              "main/panel/project");
    const auto heroNode = std::find_if(semanticNodes.begin(), semanticNodes.end(),
        [](const SemanticNode &node) { return node.name == "Hero.PNG"; });
    require(heroNode != semanticNodes.end() && heroNode->visible && heroNode->bounds &&
                heroNode->role == SemanticRole::ListItem &&
                std::find(heroNode->actions.begin(), heroNode->actions.end(), "activate") !=
                    heroNode->actions.end(),
            "Project semantics expose the currently visible asset tile with its bounds and activation");
    const SemanticAction selectHero{.nodeId = heroNode->id, .action = "activate"};
    require(panel.dispatch_semantic_action(document, commands, selection, selectHero,
                                          WindowHostGeneration{1}, true) &&
                ProjectPanelTestProbe::selected_entry_is(panel, textureDirectory / "Hero.PNG") &&
                selection.current().focusedPanel == FocusedPanel::Project,
            "activating a current Project asset selects its validated path and focuses Project");

    const auto replacementRoot = projectRoot / "replacement";
    std::filesystem::create_directories(replacementRoot / "assets");
    context.set_roots(replacementRoot, replacementRoot / "assets");
    require(!panel.dispatch_semantic_action(document, commands, selection, selectHero,
                                           WindowHostGeneration{1}, true),
            "Project semantic actions from a retired project generation are rejected");
}

void test_project_assignment_uses_explicit_validated_target()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    std::ofstream(projectRoot / "CMakeLists.txt") << "project(test)";
    const auto texture = assetRoot / "shared.png";
    const auto outsideTexture = temporary.path() / "outside.png";
    std::ofstream(texture) << "texture";
    std::ofstream(outsideTexture) << "outside";

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    ProjectScanService scans(1);
    ProjectPanel panel(context, changes, scans, nullptr);

    const auto object = [](int id, std::string name, int componentId) {
        return Json{{"id", id}, {"name", std::move(name)}, {"tag", ""}, {"active", true},
                    {"position", {{"x", 0.0}, {"y", 0.0}}},
                    {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                    {"children", Json::array()},
                    {"components", Json::array({Json{{"id", componentId}, {"type", 6},
                        {"textureFilePath", "assets/old.png"}, {"flip", 0}, {"width", 32.0},
                        {"height", 32.0}, {"sourceRectEnabled", false}}})}};
    };
    auto document = make_default_document(project.projectRoot, project.assetRoot,
                                          project.generation);
    document.root = Json::array({object(1, "Sprite A", 11), object(2, "Sprite B", 21)});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set_objects(document, {1, 2}, 1, 1);
    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const auto labels = ProjectPanelTestProbe::assignment_labels(panel, document, selection, texture);
    require(labels.size() == 2 && labels[0].find("(#1)") != std::string::npos &&
                labels[1].find("(#2)") != std::string::npos && labels[0] != labels[1],
            "Project Assign must present an explicit identity-qualified target choice for ambiguous selection");
    ProjectPanelTestProbe::select_entry(panel, texture);
    const auto semanticNodes = panel.semantic_nodes(document, selection.current(),
                                                    "main/panel/project");
    const auto semanticTarget = std::find_if(semanticNodes.begin(), semanticNodes.end(),
        [](const SemanticNode &node) {
            return node.name.find("Assign to Sprite B (#2)") != std::string::npos;
        });
    require(semanticTarget != semanticNodes.end() && semanticTarget->visible &&
                semanticTarget->enabled && semanticTarget->actions == std::vector<std::string>{"activate"},
            "Project semantics expose a named action for each compatible explicit assignment target");
    const SemanticAction assignSecond{.nodeId = semanticTarget->id, .action = "activate"};
    std::string error;
    require(panel.dispatch_semantic_action(document, commands, selection, assignSecond,
                                           WindowHostGeneration{1}, true) &&
                document.root[1]["components"][0]["textureFilePath"] == "assets/shared.png" &&
                document.root[0]["components"][0]["textureFilePath"] == "assets/old.png" &&
                commands.history_status().undoCommands == 1,
            "activating an accessible Project assignment changes only that explicit target in one history entry");
    const Json previous = document.root;
    require(!ProjectPanelTestProbe::assign_target(panel, document, commands, selection,
                                                  outsideTexture, 1, error, texture) &&
                document.root == previous && commands.history_status().undoCommands == 1 &&
                error.find("outside") != std::string::npos,
            "an escaping Project assignment must preserve valid scene references and history");
}

void test_project_template_controls_are_semantically_actionable()
{
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto templateDirectory = assetRoot / "templates";
    std::filesystem::create_directories(templateDirectory);
    std::ofstream(projectRoot / "CMakeLists.txt") << "project(template-semantic-test)\n";
    const auto templatePath = templateDirectory / "semantic.pttemplate";
    std::ofstream(templatePath, std::ios::binary)
        << make_template_envelope("Semantic Template", object(50, "Instance")).dump(2);

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();
    ProjectChanges changes;
    ProjectScanService scans(1);
    ProjectPanel panel(context, changes, scans, frames.renderer());
    auto document = make_default_document(project.projectRoot, project.assetRoot, project.generation);
    SelectionModel selection;
    selection.set(document, Selection{0, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    (void)frames.draw(panel, document, commands, &selection);
    ProjectPanelTestProbe::select_entry(panel, templatePath);
    auto nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto loadNode = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/project/template/load";
    });
    require(loadNode != nodes.end() && loadNode->enabled &&
                loadNode->actions == std::vector<std::string>{"activate"},
            "a selected template exposes a named semantic Load Template action");
    require(panel.dispatch_semantic_action(document, commands, selection,
                SemanticAction{.nodeId = loadNode->id, .action = "activate"},
                WindowHostGeneration{4}, true) && ProjectPanelTestProbe::template_loaded(panel),
            "semantic template loading must use the same contained-file validation as the visible control");

    (void)frames.draw(panel, document, commands, &selection);
    nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto instantiateNode = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/project/template/instantiate";
    });
    require(instantiateNode != nodes.end() && instantiateNode->enabled,
            "a validated template with no unresolved external assignments exposes Instantiate Template");
    require(panel.dispatch_semantic_action(document, commands, selection,
                SemanticAction{.nodeId = instantiateNode->id, .action = "activate"},
                WindowHostGeneration{4}, true),
            "a current semantic Instantiate Template action must be accepted");
    (void)frames.draw(panel, document, commands, &selection);
    const auto insertedObjectId = selection.current().objectId;
    require(document.indexes.objectsById.size() == 2 && insertedObjectId &&
                document.indexes.objectsById.contains(*insertedObjectId) &&
                commands.history_status().undoCommands == 1,
            "semantic template instantiation must use the visible action's one-step history path");

    document.root.push_back(object(98, "Destination Target"));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const auto externalTemplatePath = templateDirectory / "external.pttemplate";
    const Json externalRoot = object(70, "External Template", Json::array({component(71, 5,
        Json{{"behaviors", Json::array({Json{{"type", "LinkBehavior"}, {"target", 98}}})}})}));
    const Json externalBindings = Json::array({Json{{"owner_id", 70}, {"component_id", 71},
        {"path", "/behaviors/0/target"}, {"target_kind", "GameObject"}, {"scope", "external"},
        {"source_scene_identity", "/templates/source.scene"},
        {"original_target_kind", "GameObject"}, {"original_target_id", 98}}});
    std::ofstream(externalTemplatePath, std::ios::binary)
        << make_template_envelope("External Template", externalRoot, externalBindings).dump(2);
    ProjectPanelTestProbe::select_entry(panel, externalTemplatePath);
    nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto externalLoad = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/project/template/load";
    });
    require(externalLoad != nodes.end() && panel.dispatch_semantic_action(document, commands, selection,
                SemanticAction{.nodeId = externalLoad->id, .action = "activate"},
                WindowHostGeneration{4}, true),
            "the Project provider must load a template with declared external reference sites");
    (void)frames.draw(panel, document, commands, &selection);
    nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto externalAssignment = std::find_if(nodes.begin(), nodes.end(),
        [](const SemanticNode &node) {
            return node.name.find("Assign external GameObject reference to Destination Target (#98)") !=
                   std::string::npos;
        });
    const auto blockedInstantiate = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/project/template/instantiate";
    });
    require(externalAssignment != nodes.end() && externalAssignment->actions ==
                std::vector<std::string>{"activate"} && blockedInstantiate != nodes.end() &&
                !blockedInstantiate->enabled,
            "cross-scene external targets are selectable semantic actions and block instantiation until assigned");
    require(panel.dispatch_semantic_action(document, commands, selection,
                SemanticAction{.nodeId = externalAssignment->id, .action = "activate"},
                WindowHostGeneration{4}, true),
            "the current semantic external-target action must validate and retain its explicit identity");
    (void)frames.draw(panel, document, commands, &selection);
    nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto readyInstantiate = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/project/template/instantiate";
    });
    require(readyInstantiate != nodes.end() && readyInstantiate->enabled,
            "a validated external assignment enables semantic template instantiation");
    const std::string staleInstantiateId = readyInstantiate->id;
    document.root.push_back(object(99, "Late Destination Target"));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    require(!panel.dispatch_semantic_action(document, commands, selection,
                SemanticAction{.nodeId = staleInstantiateId, .action = "activate"},
                WindowHostGeneration{4}, true),
            "a semantic template action captured before a structural edit must be rejected");
    (void)frames.draw(panel, document, commands, &selection);
    nodes = panel.semantic_nodes(document, selection.current(), "main/panel/project");
    const auto refreshedInstantiate = std::find_if(nodes.begin(), nodes.end(),
        [](const SemanticNode &node) {
            return node.id == "main/panel/project/template/instantiate";
        });
    require(refreshedInstantiate != nodes.end() && refreshedInstantiate->enabled &&
                panel.dispatch_semantic_action(document, commands, selection,
                    SemanticAction{.nodeId = refreshedInstantiate->id, .action = "activate"},
                    WindowHostGeneration{4}, true),
            "a fresh snapshot must restore the explicit template instantiation action");
    (void)frames.draw(panel, document, commands, &selection);
    const int externalInstanceId = selection.current().objectId.value_or(-1);
    const auto externalInstance = std::find_if(document.root.begin(), document.root.end(),
        [externalInstanceId](const Json &record) {
            return record.is_object() && record.value("id", -1) == externalInstanceId;
        });
    require(externalInstance != document.root.end() &&
                (*externalInstance)["components"][0]["behaviors"][0]["target"] == 98 &&
                commands.history_status().undoCommands == 2,
            "semantic external assignment must preserve the chosen destination target in one retained insertion");
}

void test_project_change_owner_commit_budget()
{
    TestEnvironment environment;
    ProjectPanelFrames frames;
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const ProjectGeneration generation{context.snapshot().generation};
    ProjectChanges changes;
    changes.start(generation, assetRoot);
    ProjectScanService scans(2);
    ProjectPanel panel(context, changes, scans, frames.renderer());

    ProjectChangeBatch batch;
    batch.projectGeneration = generation;
    ProjectChangeEvent largeEvent;
    largeEvent.kind = ProjectChangeKind::Modified;
    largeEvent.path = assetRoot;
    largeEvent.directory = false;
    largeEvent.projectGeneration = generation;
    constexpr std::size_t totalPaths = 600;
    largeEvent.affectedPaths.reserve(totalPaths);
    for (std::size_t index = 0; index < totalPaths; ++index)
        largeEvent.affectedPaths.push_back(assetRoot / ("changed-" + std::to_string(index) + ".png"));
    batch.events.push_back(std::move(largeEvent));

    std::vector<std::filesystem::path> committed;
    work::reset();
    const auto firstFrame = panel.apply_changes(batch,
        [&](const ProjectChangeEvent &, const std::filesystem::path &path) { committed.push_back(path); });
    require(firstFrame > 0 && firstFrame <= ProjectPanel::MaxOwnerChangePathsPerFrame &&
                firstFrame == committed.size() && panel.pending_change_paths() == totalPaths - firstFrame,
            "a large coalesced watcher event must stop at the owner-frame path cap and retain its tail");

    ProjectChangeBatch continuation;
    continuation.projectGeneration = generation;
    std::size_t framesUsed = 1;
    while (panel.pending_change_paths() != 0 && framesUsed <= totalPaths)
    {
        const auto processed = panel.apply_changes(continuation,
            [&](const ProjectChangeEvent &, const std::filesystem::path &path) { committed.push_back(path); });
        require(processed > 0 && processed <= ProjectPanel::MaxOwnerChangePathsPerFrame,
                "deferred watcher paths must continue within the same per-frame commit budget");
        ++framesUsed;
    }
    const auto counts = work::snapshot();
    require(panel.pending_change_paths() == 0 && committed.size() == totalPaths &&
                counts[static_cast<std::size_t>(work::Counter::OwnerAssetChangePaths)] == totalPaths,
            "the owner path counter must account for every deferred Project and Scene invalidation commit");
}
} // namespace

void run_project_cases()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto scanRoot = assetRoot / "nested";
    const auto emptyDirectory = scanRoot / "empty";
    std::filesystem::create_directories(emptyDirectory);
    std::ofstream(scanRoot / "z.scene") << "z";
    std::ofstream(scanRoot / "a.scene") << "a";
    std::ofstream(assetRoot / "root.scene") << "root";
    std::ofstream(projectRoot / "CMakeLists.txt") << "project(test)";
    std::ofstream(projectRoot / "CMakePresets.json") << "{}";
    std::ofstream(projectRoot / "main.cpp") << "int main() {}";

    const auto discoveryProject = temporary.path() /
        std::filesystem::path(std::u8string(u8"discovery Ω"));
    const auto discoveryAssets = discoveryProject / "assets";
    const auto buildDirectory = discoveryProject / "build" / "debug";
    const auto replyDirectory = buildDirectory / ".cmake" / "api" / "v1" / "reply";
    const auto queryFile = buildDirectory / ".cmake" / "api" / "v1" / "query" /
        "client-platformator" / "codemodel-v2";
    const auto symlinkBuildDirectory = discoveryProject / "build" / "symlinked";
    const auto externalCmakeDirectory = temporary.path() / "external-cmake";
    std::filesystem::create_directories(discoveryAssets);
    std::filesystem::create_directories(replyDirectory);
    std::filesystem::create_directories(symlinkBuildDirectory);
    std::filesystem::create_directories(externalCmakeDirectory);
    std::error_code querySymlinkError;
    std::filesystem::create_directory_symlink(externalCmakeDirectory,
        symlinkBuildDirectory / ".cmake", querySymlinkError);
    std::ofstream(discoveryProject / "CMakeLists.txt") << "project(discovery)\n";
    std::ofstream(discoveryProject / "CMakePresets.json") << R"({
        "version": 6,
        "configurePresets": [
            {"name": "debug", "binaryDir": "${sourceDir}/build/debug"},
            {"name": "symlinked", "binaryDir": "${sourceDir}/build/symlinked"},
            {"name": "hidden", "hidden": true, "binaryDir": "${sourceDir}/build/hidden"},
            {"name": "external", "binaryDir": "${sourceParentDir}/outside"}
        ],
        "buildPresets": [{"name": "debug-build", "configurePreset": "debug"}]
    })";
    std::ofstream(replyDirectory / "index-test.json") << R"({
        "reply": {"codemodel-v2": {"jsonFile": "codemodel-v2-test.json"}}
    })";
    std::ofstream(replyDirectory / "codemodel-v2-test.json") << R"({
        "configurations": [{"targets": [{"name": "game", "jsonFile": "target-game.json"}] }]
    })";
    std::filesystem::create_directories(buildDirectory / "bin");
    const auto gameExecutable = buildDirectory / "bin" /
        std::filesystem::path(std::u8string(u8"Launch Ω.exe"));
    std::ofstream(gameExecutable) << "fixture executable";
    std::ofstream(replyDirectory / "target-game.json") << R"({
        "type": "EXECUTABLE", "artifacts": [{"path": "bin/Launch Ω.exe"}]
    })";
    const auto discovery = discover_project_run_options(discoveryProject);
    require(discovery.configurePresets == std::vector<std::string>({"debug", "symlinked", "external"}) &&
                discovery.buildPresets == std::vector<std::string>({"debug-build"}) &&
                discovery.executableTargets.size() == 1 &&
                discovery.executableTargets.front().targetName == "game" &&
                discovery.executableTargets.front().executablePath == gameExecutable &&
                discovery.truncated && !std::filesystem::exists(queryFile),
            "bounded project discovery must preserve Unicode project/executable paths while exposing visible presets and rejecting external build roots");
    const auto requestedDiscovery = discover_project_run_options(discoveryProject, true);
    const auto queuedQuerySize = std::filesystem::file_size(queryFile);
    const auto externalQuery = temporary.path() / "outside" / ".cmake" / "api" / "v1" /
        "query" / "client-platformator" / "codemodel-v2";
    const auto escapedSymlinkQuery = externalCmakeDirectory / "api" / "v1" / "query" /
        "client-platformator" / "codemodel-v2";
    require(queuedQuerySize == 0 &&
                requestedDiscovery.status.find("queued for the next configure") != std::string::npos &&
                !std::filesystem::exists(externalQuery) &&
                (querySymlinkError || !std::filesystem::exists(escapedSymlinkQuery)),
            "an explicit discovery refresh must register an empty File API query only in existing in-project build directories and reject a symlink escape");

    std::ofstream(discoveryProject / "CMakePresets.json", std::ios::binary | std::ios::trunc)
        << std::string(1024U * 1024U + 1U, ' ');
    const auto oversizedDiscovery = discover_project_run_options(discoveryProject);
    require(oversizedDiscovery.configurePresets.empty() && oversizedDiscovery.truncated &&
                oversizedDiscovery.status.find("1 MiB") != std::string::npos,
            "oversized preset manifests must stop discovery before an unbounded read");

    const auto validCandidate = validate_project_folder(projectRoot);
    require(validCandidate.projectRoot == std::filesystem::canonical(projectRoot) &&
                validCandidate.assetRoot == std::filesystem::canonical(assetRoot),
            "a selected project folder must resolve its canonical CMake and assets roots");
    bool rejectedFolder = false;
    try
    {
        validate_project_folder(assetRoot);
    }
    catch (const std::invalid_argument &)
    {
        rejectedFolder = true;
    }
    require(rejectedFolder, "a folder without the project CMake/assets conventions must be rejected");

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto first = context.snapshot();
    require(first.projectRoot == std::filesystem::canonical(projectRoot) &&
                first.assetRoot == std::filesystem::canonical(assetRoot),
            "project context roots must be canonicalized");
    bool rejected = false;
    try
    {
        context.set_roots(temporary.path() / "missing", assetRoot);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected && context.snapshot().generation == first.generation &&
                context.snapshot().projectRoot == first.projectRoot,
            "failed project replacement must preserve the current canonical context");

    {
        ProjectChanges changes;
        ProjectScanService panelScans(1);
        ProjectPanel panel(context, changes, panelScans, nullptr);
        ProjectChangeBatch batch;
        batch.projectGeneration = ProjectGeneration{first.generation};
        batch.events.push_back(ProjectChangeEvent{ProjectChangeKind::Modified,
            assetRoot / "root.scene", {}, {}, false, batch.projectGeneration});
        work::reset();
        panel.apply_changes(batch);
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
        require(work::snapshot()[static_cast<std::size_t>(work::Counter::AssetChanges)] == 1,
                "a real project-panel invalidation must increment the asset-change work counter");
#endif
    }

    {
        ProjectScanService scans(2);
        const auto request = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                     .maxFiles = 100});
        scans.wait_for_idle();
        auto results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == request.request_id() &&
                    results.front().files.size() == 3,
                "recursive scans must publish a complete immutable file snapshot");
        require(std::is_sorted(results.front().files.begin(), results.front().files.end()),
                "scan results must have deterministic path order");

        const auto truncated = scans.request_scan(first, assetRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 1, .maxEntries = 100});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == truncated.request_id() &&
                    results.front().files.size() == 1 && results.front().truncated,
                "a bounded scan must identify a partial prefix so callers cannot treat it as complete");

        const auto expanded = scans.request_scan(first, assetRoot,
            ProjectScanOptions{.recursive = false, .includeDirectories = true, .maxDirectories = 8});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == expanded.request_id() &&
                    results.front().files.size() == 1 && results.front().directories.size() == 1 &&
                    results.front().directories.front() == scanRoot,
                "an expanded asset directory scan must return only its immediate files and directories");

        const auto aliasDirectory = assetRoot / "cached-alias";
        const auto aliasFile = assetRoot / "scene-alias.scene";
        const auto brokenAlias = assetRoot / "broken-alias.scene";
        std::error_code aliasError;
        std::filesystem::create_directory_symlink(scanRoot, aliasDirectory, aliasError);
        if (!aliasError)
        {
            std::filesystem::create_symlink(scanRoot / "a.scene", aliasFile);
            std::filesystem::create_symlink(assetRoot / "missing.scene", brokenAlias);
            const auto aliasListing = scans.request_scan(first, assetRoot,
                ProjectScanOptions{.recursive = false, .includeDirectories = true, .maxDirectories = 8});
            scans.wait_for_idle();
            results = scans.take_ready(first.generation);
            require(results.size() == 1 && results.front().requestId == aliasListing.request_id() &&
                        std::find(results.front().directories.begin(), results.front().directories.end(),
                                  aliasDirectory) != results.front().directories.end() &&
                        std::find(results.front().files.begin(), results.front().files.end(), aliasFile) !=
                                  results.front().files.end() &&
                        std::find(results.front().files.begin(), results.front().files.end(), brokenAlias) !=
                                  results.front().files.end(),
                    "contained directory/file aliases and broken aliases must remain visible under logical paths");

            const auto inventoryScan = scans.request_scan(first, assetRoot,
                ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                                   .trackFileMetadata = true});
            scans.wait_for_idle();
            results = scans.take_ready(first.generation);
            require(results.size() == 1 && results.front().requestId == inventoryScan.request_id() &&
                        std::any_of(results.front().inventoryEntries.begin(), results.front().inventoryEntries.end(),
                            [&](const auto &entry) {
                                return entry.first == emptyDirectory && entry.second.starts_with("d:");
                            }) &&
                        std::any_of(results.front().inventoryEntries.begin(), results.front().inventoryEntries.end(),
                            [&](const auto &entry) {
                                return entry.first == aliasDirectory && entry.second.starts_with("d:") &&
                                       entry.second.find(std::filesystem::canonical(scanRoot).generic_string()) !=
                                           std::string::npos;
                            }) &&
                        std::any_of(results.front().inventoryEntries.begin(), results.front().inventoryEntries.end(),
                            [&](const auto &entry) {
                                return entry.first == aliasFile && entry.second.starts_with("f:") &&
                                       entry.second.find(std::filesystem::canonical(scanRoot / "a.scene").generic_string()) !=
                                           std::string::npos;
                            }) &&
                        std::any_of(results.front().inventoryEntries.begin(), results.front().inventoryEntries.end(),
                            [&](const auto &entry) {
                                return entry.first == brokenAlias && entry.second.starts_with("l:");
                            }),
                    "complete watcher inventories must retain empty folders and logical alias identities");

            const auto aliasContents = scans.request_scan(first, aliasDirectory,
                ProjectScanOptions{.recursive = false, .includeDirectories = true, .maxDirectories = 8});
            scans.wait_for_idle();
            results = scans.take_ready(first.generation);
            require(results.size() == 1 && results.front().requestId == aliasContents.request_id() &&
                        results.front().root == aliasDirectory && results.front().canonicalRoot == scanRoot &&
                        std::find(results.front().files.begin(), results.front().files.end(),
                                  aliasDirectory / "a.scene") != results.front().files.end(),
                    "a cached contained directory alias must scan its canonical target but return logical child paths");

            std::filesystem::remove(aliasDirectory);
            const auto retarget = assetRoot / "retarget";
            std::filesystem::create_directories(retarget);
            std::ofstream(retarget / "replacement.scene") << "replacement";
            std::filesystem::create_directory_symlink(retarget, aliasDirectory);
            const auto retargetedListing = scans.request_scan(first, aliasDirectory,
                ProjectScanOptions{.recursive = false});
            scans.wait_for_idle();
            results = scans.take_ready(first.generation);
            require(results.size() == 1 && results.front().requestId == retargetedListing.request_id() &&
                        results.front().canonicalRoot == retarget &&
                        std::find(results.front().files.begin(), results.front().files.end(),
                                  aliasDirectory / "replacement.scene") != results.front().files.end(),
                    "retargeting a cached directory alias must adopt the new contained canonical subtree");

            std::ofstream(assetRoot / "missing.scene") << "repaired";
            const auto repairedListing = scans.request_scan(first, assetRoot,
                ProjectScanOptions{.recursive = false, .includeDirectories = true, .maxDirectories = 8});
            scans.wait_for_idle();
            results = scans.take_ready(first.generation);
            require(results.size() == 1 && results.front().requestId == repairedListing.request_id() &&
                        std::find(results.front().files.begin(), results.front().files.end(), brokenAlias) !=
                                  results.front().files.end(),
                    "a broken alias must stay visible after its target is repaired");

            const auto escape = temporary.path() / "escape";
            std::filesystem::create_directories(escape);
            std::ofstream(escape / "secret.scene") << "secret";
            std::filesystem::create_directory_symlink(escape, assetRoot / "escape-alias", aliasError);
            if (!aliasError)
            {
                bool rejectedEscape = false;
                try
                {
                    (void)scans.request_scan(first, assetRoot / "escape-alias",
                        ProjectScanOptions{.recursive = false, .includeDirectories = true});
                }
                catch (const std::invalid_argument &)
                {
                    rejectedEscape = true;
                }
                require(rejectedEscape,
                        "directory aliases whose canonical target escapes assets must not be traversed");
            }
        }

        const auto identityScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == identityScan.request_id() &&
                    results.front().files.size() == 3 && !results.front().sourceInputIdentity.empty(),
                "source identity scans must include source and CMake inputs, not runtime assets");
        const auto originalIdentity = results.front().sourceInputIdentity;
        std::ofstream(projectRoot / "main.cpp", std::ios::trunc) << "int main() { return 0; }\n";
        const auto changedIdentityScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == changedIdentityScan.request_id() &&
                    results.front().sourceInputIdentity != originalIdentity,
                "source identity scans must detect configured source input changes");

        const auto sameMetadataIdentity = results.front().sourceInputIdentity;
        const auto mainPath = projectRoot / "main.cpp";
        const auto originalTime = std::filesystem::last_write_time(mainPath);
        std::ifstream sourceInput(mainPath, std::ios::binary);
        std::string sameSizeSource((std::istreambuf_iterator<char>(sourceInput)),
                                   std::istreambuf_iterator<char>());
        require(!sameSizeSource.empty(), "same-metadata source fixture must be nonempty");
        sameSizeSource[0] = sameSizeSource[0] == 'x' ? 'y' : 'x';
        std::ofstream(mainPath, std::ios::binary | std::ios::trunc) << sameSizeSource;
        std::filesystem::last_write_time(mainPath, originalTime);
        const auto sameMetadataScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == sameMetadataScan.request_id() &&
                    results.front().sourceInputIdentity != sameMetadataIdentity,
                "source freshness identity must detect same-size writes with restored timestamps");

        const auto byteBoundScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true, .maxSourceBytes = 1});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == byteBoundScan.request_id() &&
                    results.front().truncated,
                "source content hashing must stop at its byte budget without publishing a complete identity");

        const auto stale = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                    .maxFiles = 100});
        scans.wait_for_idle();
        require(scans.take_ready(first.generation + 1).empty(),
                "results from a replaced project generation must not reach the UI");
        stale.cancel();

        for (int i = 0; i < 5; ++i)
            scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true, .maxFiles = 100});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() <= 2,
                "completed scan publications must remain within the configured queue bound");

        auto cancelled = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                 .maxFiles = 100});
        cancelled.cancel();
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().cancelled,
                "cancelled scans must publish a cancellation result without retaining owner state");

        const auto oldRequest = scans.request_scan(first, assetRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100});
        require(static_cast<bool>(oldRequest), "a scan must start before context cancellation");
        scans.cancel_pending_and_wait();
        require(scans.take_ready(first.generation).empty(),
                "project replacement must drain old scan publications before adopting new roots");
        const auto reusableRequest = scans.request_scan(first, assetRoot,
            ProjectScanOptions{.recursive = false, .maxFiles = 100});
        require(static_cast<bool>(reusableRequest),
                "a cancelled scan service must remain reusable for the replacement project");
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == reusableRequest.request_id(),
                "the reusable scan service must publish only replacement-context work");
    }

    context.set_roots(projectRoot, assetRoot);
    require(context.snapshot().generation == first.generation,
            "unchanged canonical roots must not create a new project generation");

    test_project_panel_alias_presentation_invalidation();
    test_directory_alias_file_events_refresh_descendant_previews();
    test_initially_broken_alias_repairs_project_listings_and_previews();
    test_project_search_tracks_listing_revisions();
    test_project_assignment_uses_explicit_validated_target();
    test_project_template_controls_are_semantically_actionable();
    test_project_change_owner_commit_budget();
    test_animation_preview_uploads_without_project_draw();
}

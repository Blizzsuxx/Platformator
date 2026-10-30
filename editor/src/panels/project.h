#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../project.h"
#include "../editcommands.h"

namespace platformator_editor
{
class ProjectPanel
{
  public:
    ProjectPanel(ProjectContext &context, ProjectScanService &scans);
    void draw(DocumentSession &document, EditCommandService &commands);
    void refresh();

  private:
    struct DirectoryState
    {
        ProjectScanToken request;
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;
        std::string error;
        bool loaded = false;
        bool truncated = false;
    };

    void draw_directory(const std::filesystem::path &directory, const char *label);
    void apply_ready_results();

    ProjectContext &context_;
    ProjectScanService &scans_;
    std::map<std::string, DirectoryState> directories_;
    std::map<std::uint64_t, std::string> requestPaths_;
};
} // namespace platformator_editor

#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace platformator_editor
{
using Json = nlohmann::json;

struct ObjectLocation
{
    std::vector<std::size_t> childIndices;
};

struct ComponentLocation
{
    int ownerObjectId = 0;
    std::size_t componentIndex = 0;
};

struct DocumentIndexes
{
    std::uint64_t documentGeneration = 0;
    std::uint64_t structureRevision = 0;
    std::unordered_map<int, ObjectLocation> objectsById;
    std::unordered_map<int, ComponentLocation> componentsById;
    std::unordered_map<int, std::optional<int>> parentById;
    std::vector<int> orderedObjectIds;
    std::vector<int> orderedSpriteIds;

    bool current_for(std::uint64_t generation, std::uint64_t revision) const noexcept
    {
        return documentGeneration == generation && structureRevision == revision;
    }
};

struct FileIdentity
{
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modificationTimeNanoseconds = 0;
    std::int64_t changeTimeNanoseconds = 0;

    bool operator==(const FileIdentity &) const = default;
};

struct DocumentSession
{
    Json root = Json::array();
    std::optional<std::filesystem::path> sourcePath;
    std::optional<FileIdentity> sourceIdentity;
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::uint64_t documentGeneration = 1;
    std::uint64_t projectGeneration = 1;
    std::uint64_t structureRevision = 0;
    std::uint64_t valueRevision = 0;
    DocumentIndexes indexes;
};

struct DocumentChange
{
    std::uint64_t documentGeneration = 0;
    std::uint64_t structureRevision = 0;
    std::uint64_t valueRevision = 0;
    bool structural = false;
};

struct DocumentParseResult
{
    std::optional<DocumentSession> document;
    std::string error;

    explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

DocumentParseResult parse_document(Json root, std::uint64_t documentGeneration = 1,
                                   std::optional<std::filesystem::path> sourcePath = std::nullopt,
                                   std::filesystem::path projectRoot = {},
                                   std::filesystem::path assetRoot = {});
DocumentSession make_default_document(std::filesystem::path projectRoot = {},
                                      std::filesystem::path assetRoot = {},
                                      std::uint64_t documentGeneration = 1);
void rebuild_document_indexes(DocumentSession &document);
} // namespace platformator_editor

#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "document.h"

namespace platformator_editor
{
struct SourceScannerDiagnostic
{
    std::string typeName;
    std::string fieldName;
    std::filesystem::path filePath;
    int line = 0;
    std::string message;

    bool operator==(const SourceScannerDiagnostic &) const = default;
};

struct SourceEnumValue
{
    std::string name;
    std::int64_t value = 0;

    bool operator==(const SourceEnumValue &) const = default;
};

struct SourceBehaviorFieldDescriptor
{
    std::string name;
    std::string kind = "opaque_json";
    std::string cppType;
    std::string targetType;
    bool nullable = false;
    std::vector<SourceEnumValue> enumValues;
    std::optional<Json> defaultValue;
    std::shared_ptr<SourceBehaviorFieldDescriptor> element;
    int lineNumber = 0;
    bool resolved = true;
    std::string diagnostic;
};

struct SourceBehaviorDescriptor
{
    std::string typeName;
    std::string baseClass;
    std::filesystem::path headerPath;
    std::filesystem::path sourcePath;
    int declarationLine = 0;
    std::vector<SourceBehaviorFieldDescriptor> fields;
    std::vector<SourceScannerDiagnostic> diagnostics;
};

struct SourceScanResult
{
    std::map<std::string, SourceBehaviorDescriptor> behaviors;
    std::map<std::string, std::vector<SourceEnumValue>> enums;
    std::vector<SourceScannerDiagnostic> diagnostics;
    std::size_t scannedFileCount = 0;
    std::size_t cachedFileHitCount = 0;
    bool cancelled = false;
    bool truncated = false;

    Json to_catalog_json() const;
};

class SourceBehaviorScanner
{
  public:
    SourceBehaviorScanner();
    ~SourceBehaviorScanner();
    SourceBehaviorScanner(const SourceBehaviorScanner &) = delete;
    SourceBehaviorScanner &operator=(const SourceBehaviorScanner &) = delete;

    void clear_cache() noexcept;
    std::size_t cache_size() const noexcept;

    SourceScanResult scan_project(const std::filesystem::path &projectRoot,
                                  const std::filesystem::path &assetRoot = {},
                                  const std::atomic_bool *cancelled = nullptr);

    // Exposed for unit testing single/in-memory header+source pairs directly.
    static SourceScanResult scan_sources(
        const std::vector<std::pair<std::filesystem::path, std::string>> &files);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor

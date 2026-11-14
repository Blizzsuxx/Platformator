#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "document.h"
#include "process.h"

namespace platformator_editor
{
struct SourceScanResult;

enum class CatalogState
{
    Unavailable,
    Stale,
    Current
};

struct CatalogIdentity
{
    std::string canonicalProjectPath;
    std::string profile;
    std::string target;
    std::string executablePath;
    std::string binaryIdentity;
    std::string sourceInputIdentity;

    bool operator==(const CatalogIdentity &) const = default;
};

struct BehaviorCatalogSnapshot
{
    CatalogIdentity identity;
    Json document;
};

class BehaviorCatalog
{
  public:
    void configure(CatalogIdentity identity);
    void invalidate(std::string message);
    bool set_source_catalog(Json document, CatalogIdentity identity = {});
    bool load_from_source_scan(const SourceScanResult &scan, CatalogIdentity identity = {});
    CatalogState state() const noexcept;
    const std::string &last_error() const noexcept;
    std::shared_ptr<const BehaviorCatalogSnapshot> snapshot() const noexcept;
    std::uint64_t revision() const noexcept { return revision_; }

  private:
    bool reject(std::string message);

    CatalogIdentity identity_;
    CatalogState state_ = CatalogState::Unavailable;
    std::shared_ptr<const BehaviorCatalogSnapshot> snapshot_;
    std::string error_;
    std::uint64_t revision_ = 0;
};
} // namespace platformator_editor

#pragma once

#include <cstdint>
#include <cstddef>
#include <compare>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace platformator_editor
{
template <class Tag>
struct Identity
{
    std::uint64_t value = 0;

    constexpr Identity() noexcept = default;
    constexpr explicit Identity(std::uint64_t raw) noexcept : value(raw) {}
    constexpr auto operator<=>(const Identity &) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return value != 0; }
};

struct ProjectGenerationTag;
struct TabIdTag;
struct DocumentGenerationTag;
struct StructureRevisionTag;
struct ValueRevisionTag;
struct DirectoryRevisionTag;
struct PathVersionTag;
struct HostLayoutIdTag;
struct WindowHostGenerationTag;
struct RendererGenerationTag;
struct PlayGenerationTag;
struct RuntimeSerialTag;

using ProjectGeneration = Identity<ProjectGenerationTag>;
using TabId = Identity<TabIdTag>;
using DocumentGeneration = Identity<DocumentGenerationTag>;
using StructureRevision = Identity<StructureRevisionTag>;
using ValueRevision = Identity<ValueRevisionTag>;
using DirectoryRevision = Identity<DirectoryRevisionTag>;
using PathVersion = Identity<PathVersionTag>;
using HostLayoutId = Identity<HostLayoutIdTag>;
using WindowHostGeneration = Identity<WindowHostGenerationTag>;
using RendererGeneration = Identity<RendererGenerationTag>;
using PlayGeneration = Identity<PlayGenerationTag>;
using RuntimeSerial = Identity<RuntimeSerialTag>;

inline constexpr HostLayoutId main_host_layout_id{1};

// Tab IDs, host incarnations, and play generations are allocated monotonically
// by their owner. Exhaustion is an explicit failure; values never wrap or get
// reused. Document generations use a tab-owned source, so equal values in
// different tabs are expected and must be qualified by TabId.
template <class Id>
class MonotonicIdentitySource
{
    static_assert(std::is_trivially_copyable_v<Id>);

  public:
    explicit constexpr MonotonicIdentitySource(std::uint64_t first = 1) noexcept
        : next_(first)
    {
    }

    Id next()
    {
        if (exhausted_ || next_ == 0)
            throw std::overflow_error("identity space exhausted");
        const auto result = Id{next_};
        if (next_ == std::numeric_limits<std::uint64_t>::max())
            exhausted_ = true;
        else
            ++next_;
        return result;
    }

  private:
    std::uint64_t next_ = 1;
    bool exhausted_ = false;
};

struct DocumentIdentity
{
    ProjectGeneration projectGeneration;
    TabId tabId;
    DocumentGeneration documentGeneration;
    constexpr auto operator<=>(const DocumentIdentity &) const noexcept = default;
};

struct ResourceIdentity
{
    ProjectGeneration projectGeneration;
    PathVersion pathVersion;
    constexpr auto operator<=>(const ResourceIdentity &) const noexcept = default;
};

constexpr bool matches(const DocumentIdentity &identity, ProjectGeneration project,
                       TabId tab, DocumentGeneration document) noexcept
{
    return identity.projectGeneration == project && identity.tabId == tab &&
           identity.documentGeneration == document;
}
} // namespace platformator_editor

namespace std
{
template <class Tag>
struct hash<platformator_editor::Identity<Tag>>
{
    size_t operator()(platformator_editor::Identity<Tag> identity) const noexcept
    {
        return hash<uint64_t>{}(identity.value);
    }
};
} // namespace std

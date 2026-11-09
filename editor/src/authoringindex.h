#pragma once

#include "document.h"

#include <platformator/gameobject.h>

#include <array>
#include <cstdint>
#include <optional>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace platformator_editor
{
struct AuthoringBounds
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct AuthoringObjectDescriptor
{
    int documentId = 0;
    std::optional<int> parentId;
    std::array<int, COMPONENT_TYPE_COUNT> componentIds{};
    float worldX = 0.0f;
    float worldY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotationRadians = 0.0f;
    bool active = true;
    bool hasSprite = false;
    bool hasCollider = false;
    bool hasCamera = false;
    bool emptyMarker = false;
    bool circleCollider = false;
    bool colliderPlaceholder = false;
    bool cameraWidthConflict = false;
    bool cameraHeightConflict = false;
    bool cameraUsesLegacyWidth = false;
    bool cameraUsesLegacyHeight = false;
    float spriteWidth = 0.0f;
    float spriteHeight = 0.0f;
    float colliderWidth = 0.0f;
    float colliderHeight = 0.0f;
    float colliderRadius = 0.0f;
    float colliderOffsetX = 0.0f;
    float colliderOffsetY = 0.0f;
    float cameraWidth = 0.0f;
    float cameraHeight = 0.0f;
    AuthoringBounds spriteBounds;
    AuthoringBounds nonSpriteBounds;
    AuthoringBounds bounds;
};

class AuthoringIndex
{
  public:
    void rebuild(const DocumentSession &document);
    void update_subtree(const DocumentSession &document, int rootId,
                        std::vector<int> &updatedObjectIds, bool includeDescendants = true);
    void clear() noexcept;
    const AuthoringObjectDescriptor *find(int documentId) const noexcept;
    const std::vector<int> &ordered_ids() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t document_generation() const noexcept;
    std::uint64_t structure_revision() const noexcept;

  private:
    struct PendingUpdate
    {
        const Json *record = nullptr;
        float parentX = 0.0f;
        float parentY = 0.0f;
    };

    std::unordered_map<int, AuthoringObjectDescriptor> objects_;
    std::vector<int> orderedIds_;
    std::vector<PendingUpdate> updateStack_;
    std::uint64_t documentGeneration_ = 0;
    std::uint64_t structureRevision_ = 0;
};
} // namespace platformator_editor

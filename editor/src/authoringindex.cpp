#include "authoringindex.h"
#include "editorwork.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

#include <platformator/gameobject.h>

#include "camerainterpretation.h"

namespace platformator_editor
{
namespace
{
const Json *resolve_object(const DocumentSession &document, const ObjectLocation &location)
{
    if (location.childIndices.empty())
        return nullptr;
    const Json *siblings = &document.root;
    const Json *object = nullptr;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (!siblings->is_array() || location.childIndices[depth] >= siblings->size())
            return nullptr;
        object = &(*siblings)[location.childIndices[depth]];
        if (depth + 1 < location.childIndices.size())
        {
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
    }
    return object;
}

float number(const Json &record, const char *key, float fallback)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number())
        return fallback;
    try
    {
        const float value = found->get<float>();
        return std::isfinite(value) ? value : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

std::optional<int> integer(const Json &record, const char *key)
{
    const auto found = record.find(key);
    if (found == record.end() || (!found->is_number_integer() && !found->is_number_unsigned()))
        return std::nullopt;
    try
    {
        if (found->is_number_unsigned())
        {
            const auto value = found->get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                return std::nullopt;
            return static_cast<int>(value);
        }
        const auto value = found->get<std::int64_t>();
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<float> positive_number(const Json &record, const char *key)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number())
        return std::nullopt;
    try
    {
        const float value = found->get<float>();
        return std::isfinite(value) && value > 0.0f ? std::optional<float>(value) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

float scaled_positive(float value, float scale, float fallback)
{
    const double scaled = static_cast<double>(value) * std::abs(static_cast<double>(scale));
    if (!std::isfinite(scaled) || scaled <= 0.0 || scaled > std::numeric_limits<float>::max())
        return fallback;
    return static_cast<float>(scaled);
}

float placeholder_dimension(float scale)
{
    return std::max(12.0f, scaled_positive(12.0f, scale, 12.0f));
}

float nested_number(const Json &record, const char *outer, const char *inner, float fallback)
{
    const auto found = record.find(outer);
    return found != record.end() && found->is_object() ? number(*found, inner, fallback) : fallback;
}

int component_id(const Json &component)
{
    const auto found = component.find("id");
    if (found == component.end() || !found->is_number_integer())
        return 0;
    try
    {
        return found->get<int>();
    }
    catch (...)
    {
        return 0;
    }
}

const Json *component_of_type(const Json &object, int type)
{
    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return nullptr;
    for (const auto &component : *components)
        if (component.is_object() && integer(component, "type") == type)
            return &component;
    return nullptr;
}

struct Extents
{
    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();

    void include(float x, float y, float halfWidth, float halfHeight, float rotationRadians)
    {
        const float cosine = std::cos(rotationRadians);
        const float sine = std::sin(rotationRadians);
        const float extentX = std::abs(cosine) * halfWidth + std::abs(sine) * halfHeight;
        const float extentY = std::abs(sine) * halfWidth + std::abs(cosine) * halfHeight;
        minX = std::min(minX, x - extentX);
        minY = std::min(minY, y - extentY);
        maxX = std::max(maxX, x + extentX);
        maxY = std::max(maxY, y + extentY);
    }

    AuthoringBounds result() const
    {
        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY))
            return {};
        return {minX, minY, maxX - minX, maxY - minY};
    }

    void include(const AuthoringBounds &bounds)
    {
        if (bounds.width <= 0.0f || bounds.height <= 0.0f)
            return;
        minX = std::min(minX, bounds.x);
        minY = std::min(minY, bounds.y);
        maxX = std::max(maxX, bounds.x + bounds.width);
        maxY = std::max(maxY, bounds.y + bounds.height);
    }
};

void add_component_ids(const Json &object, AuthoringObjectDescriptor &descriptor)
{
    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return;
    for (const auto &component : *components)
    {
        if (!component.is_object())
            continue;
        const auto type = integer(component, "type");
        if (!type || *type < 0 || *type >= COMPONENT_TYPE_COUNT)
            continue;
        descriptor.componentIds[static_cast<std::size_t>(*type)] = component_id(component);
    }
}

AuthoringObjectDescriptor describe(const DocumentSession &document, int id, const Json &object)
{
    AuthoringObjectDescriptor descriptor;
    descriptor.documentId = id;
    const auto parent = document.indexes.parentById.find(id);
    if (parent != document.indexes.parentById.end())
        descriptor.parentId = parent->second;
    descriptor.worldX = nested_number(object, "position", "x", 0.0f);
    descriptor.worldY = nested_number(object, "position", "y", 0.0f);
    descriptor.scaleX = nested_number(object, "scale", "x", 1.0f);
    descriptor.scaleY = nested_number(object, "scale", "y", 1.0f);
    descriptor.rotationRadians = number(object, "rotation", 0.0f);
    const auto active = object.find("active");
    descriptor.active = active == object.end() || !active->is_boolean() || active->get<bool>();
    add_component_ids(object, descriptor);

    const Json *sprite = component_of_type(object, SPRITE);
    const Json *collider = component_of_type(object, COLLIDER);
    const Json *camera = component_of_type(object, CAMERA);
    descriptor.hasSprite = sprite != nullptr;
    descriptor.hasCollider = collider != nullptr;
    descriptor.hasCamera = camera != nullptr;
    if (sprite != nullptr)
    {
        descriptor.spriteWidth = number(*sprite, "width", 0.0f);
        descriptor.spriteHeight = number(*sprite, "height", 0.0f);
    }
    if (collider != nullptr)
    {
        const auto kind = integer(*collider, "colliderType");
        const bool supportedKind = kind && (*kind == 0 || *kind == 1);
        descriptor.circleCollider = supportedKind && *kind == 0;
        const auto width = positive_number(*collider, "width");
        const auto height = positive_number(*collider, "height");
        const auto radius = positive_number(*collider, "radius");
        const bool validDimensions = descriptor.circleCollider ? radius.has_value()
                                                                 : width.has_value() && height.has_value();
        descriptor.colliderPlaceholder = !supportedKind || !validDimensions;
        if (descriptor.colliderPlaceholder)
        {
            descriptor.colliderWidth = placeholder_dimension(descriptor.scaleX);
            descriptor.colliderHeight = placeholder_dimension(descriptor.scaleY);
            const float scaledRadius = scaled_positive(6.0f,
                std::max(std::abs(descriptor.scaleX), std::abs(descriptor.scaleY)), 6.0f);
            descriptor.colliderRadius = std::max(6.0f, scaledRadius);
        }
        else if (descriptor.circleCollider)
        {
            const float scale = std::max(std::abs(descriptor.scaleX), std::abs(descriptor.scaleY));
            const double expectedRadius = static_cast<double>(*radius) * scale;
            if (!std::isfinite(expectedRadius) || expectedRadius <= 0.0 ||
                expectedRadius > std::numeric_limits<float>::max())
            {
                descriptor.colliderPlaceholder = true;
                descriptor.colliderWidth = placeholder_dimension(descriptor.scaleX);
                descriptor.colliderHeight = placeholder_dimension(descriptor.scaleY);
                descriptor.colliderRadius = 6.0f;
            }
            else
                descriptor.colliderRadius = static_cast<float>(expectedRadius);
        }
        else
        {
            descriptor.colliderWidth = scaled_positive(*width, descriptor.scaleX, 12.0f);
            descriptor.colliderHeight = scaled_positive(*height, descriptor.scaleY, 12.0f);
            const double expectedWidth = static_cast<double>(*width) * std::abs(descriptor.scaleX);
            const double expectedHeight = static_cast<double>(*height) * std::abs(descriptor.scaleY);
            if (!std::isfinite(expectedWidth) || expectedWidth <= 0.0 ||
                expectedWidth > std::numeric_limits<float>::max() ||
                !std::isfinite(expectedHeight) || expectedHeight <= 0.0 ||
                expectedHeight > std::numeric_limits<float>::max())
            {
                descriptor.colliderPlaceholder = true;
                descriptor.colliderWidth = placeholder_dimension(descriptor.scaleX);
                descriptor.colliderHeight = placeholder_dimension(descriptor.scaleY);
            }
        }
        descriptor.colliderOffsetX = nested_number(*collider, "offset", "x", 0.0f);
        descriptor.colliderOffsetY = nested_number(*collider, "offset", "y", 0.0f);
    }
    if (camera != nullptr)
    {
        const auto dimensions = interpret_camera_dimensions(*camera);
        descriptor.cameraWidth = static_cast<float>(dimensions.width.value_or(1920.0));
        descriptor.cameraHeight = static_cast<float>(dimensions.height.value_or(1080.0));
        descriptor.cameraWidthConflict = dimensions.widthConflict;
        descriptor.cameraHeightConflict = dimensions.heightConflict;
        descriptor.cameraUsesLegacyWidth = dimensions.widthUsesLegacy;
        descriptor.cameraUsesLegacyHeight = dimensions.heightUsesLegacy;
    }

    Extents spriteBounds;
    Extents nonSpriteBounds;
    const float rotation = descriptor.rotationRadians;
    if (descriptor.hasSprite)
        spriteBounds.include(descriptor.worldX, descriptor.worldY,
                             std::abs(descriptor.spriteWidth * descriptor.scaleX) * 0.5f,
                             std::abs(descriptor.spriteHeight * descriptor.scaleY) * 0.5f, rotation);
    if (descriptor.hasCollider)
    {
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        const float offsetX = descriptor.colliderOffsetX * cosine - descriptor.colliderOffsetY * sine;
        const float offsetY = descriptor.colliderOffsetX * sine + descriptor.colliderOffsetY * cosine;
        if (descriptor.circleCollider)
            nonSpriteBounds.include(descriptor.worldX + offsetX, descriptor.worldY + offsetY,
                                    descriptor.colliderRadius, descriptor.colliderRadius, 0.0f);
        else
            nonSpriteBounds.include(descriptor.worldX + offsetX, descriptor.worldY + offsetY,
                                    descriptor.colliderWidth * 0.5f, descriptor.colliderHeight * 0.5f, rotation);
    }
    if (descriptor.hasCamera)
        nonSpriteBounds.include(descriptor.worldX + descriptor.cameraWidth * 0.5f,
                                descriptor.worldY + descriptor.cameraHeight * 0.5f,
                                descriptor.cameraWidth * 0.5f, descriptor.cameraHeight * 0.5f, 0.0f);
    descriptor.emptyMarker = !descriptor.hasSprite && !descriptor.hasCollider && !descriptor.hasCamera;
    if (descriptor.emptyMarker)
        nonSpriteBounds.include(descriptor.worldX, descriptor.worldY, 6.0f, 6.0f, 0.0f);
    descriptor.spriteBounds = spriteBounds.result();
    descriptor.nonSpriteBounds = nonSpriteBounds.result();
    Extents allBounds;
    allBounds.include(descriptor.spriteBounds);
    allBounds.include(descriptor.nonSpriteBounds);
    descriptor.bounds = allBounds.result();
    return descriptor;
}
} // namespace

void AuthoringIndex::rebuild(const DocumentSession &document)
{
    std::unordered_map<int, AuthoringObjectDescriptor> next;
    std::vector<int> ordered;
    next.reserve(document.indexes.orderedObjectIds.size());
    ordered.reserve(document.indexes.orderedObjectIds.size());
    for (const int id : document.indexes.orderedObjectIds)
    {
        const auto location = document.indexes.objectsById.find(id);
        if (location == document.indexes.objectsById.end())
            continue;
        const Json *object = resolve_object(document, location->second);
        if (object == nullptr)
            continue;
        auto descriptor = describe(document, id, *object);
        if (descriptor.parentId)
        {
            const auto parent = next.find(*descriptor.parentId);
            if (parent != next.end())
            {
                descriptor.worldX += parent->second.worldX;
                descriptor.worldY += parent->second.worldY;
                descriptor.bounds.x += parent->second.worldX;
                descriptor.bounds.y += parent->second.worldY;
                if (descriptor.spriteBounds.width > 0.0f && descriptor.spriteBounds.height > 0.0f)
                {
                    descriptor.spriteBounds.x += parent->second.worldX;
                    descriptor.spriteBounds.y += parent->second.worldY;
                }
                if (descriptor.nonSpriteBounds.width > 0.0f && descriptor.nonSpriteBounds.height > 0.0f)
                {
                    descriptor.nonSpriteBounds.x += parent->second.worldX;
                    descriptor.nonSpriteBounds.y += parent->second.worldY;
                }
            }
        }
        ordered.push_back(id);
        next.emplace(id, std::move(descriptor));
    }
    objects_.swap(next);
    orderedIds_.swap(ordered);
    documentGeneration_ = document.documentGeneration;
    structureRevision_ = document.structureRevision;
}

void AuthoringIndex::update_subtree(const DocumentSession &document, int rootId,
                                    std::vector<int> &updatedObjectIds, bool includeDescendants)
{
    const auto rootLocation = document.indexes.objectsById.find(rootId);
    if (rootLocation == document.indexes.objectsById.end())
        return;
    const Json *root = resolve_object(document, rootLocation->second);
    if (root == nullptr)
        return;

    float inheritedX = 0.0f;
    float inheritedY = 0.0f;
    if (const auto parent = document.indexes.parentById.find(rootId);
        parent != document.indexes.parentById.end() && parent->second)
    {
        const auto parentDescriptor = objects_.find(*parent->second);
        if (parentDescriptor != objects_.end())
        {
            inheritedX = parentDescriptor->second.worldX;
            inheritedY = parentDescriptor->second.worldY;
        }
    }

    if (updateStack_.capacity() < 64)
    {
        updateStack_.reserve(64);
        work::record(work::Counter::AuthoringSubtreeScratchGrowths);
    }
    updateStack_.clear();
    updateStack_.push_back({root, inheritedX, inheritedY});
    while (!updateStack_.empty())
    {
        const PendingUpdate current = updateStack_.back();
        updateStack_.pop_back();
        if (current.record == nullptr || !current.record->is_object())
            continue;
        const auto id = integer(*current.record, "id");
        if (!id)
            continue;

        auto descriptor = describe(document, *id, *current.record);
        descriptor.worldX += current.parentX;
        descriptor.worldY += current.parentY;
        descriptor.bounds.x += current.parentX;
        descriptor.bounds.y += current.parentY;
        if (descriptor.spriteBounds.width > 0.0f && descriptor.spriteBounds.height > 0.0f)
        {
            descriptor.spriteBounds.x += current.parentX;
            descriptor.spriteBounds.y += current.parentY;
        }
        if (descriptor.nonSpriteBounds.width > 0.0f && descriptor.nonSpriteBounds.height > 0.0f)
        {
            descriptor.nonSpriteBounds.x += current.parentX;
            descriptor.nonSpriteBounds.y += current.parentY;
        }
        const float worldX = descriptor.worldX;
        const float worldY = descriptor.worldY;
        const auto [entry, inserted] = objects_.insert_or_assign(*id, std::move(descriptor));
        (void)entry;
        if (inserted)
            work::record(work::Counter::AuthoringDescriptorInsertions);
        if (updatedObjectIds.size() == updatedObjectIds.capacity())
            work::record(work::Counter::AuthoringSubtreeScratchGrowths);
        updatedObjectIds.push_back(*id);

        const auto children = current.record->find("children");
        if (!includeDescendants || children == current.record->end() || !children->is_array())
            continue;
        for (std::size_t index = children->size(); index > 0; --index)
        {
            if (updateStack_.size() == updateStack_.capacity())
                work::record(work::Counter::AuthoringSubtreeScratchGrowths);
            updateStack_.push_back({&(*children)[index - 1], worldX, worldY});
        }
    }
}

void AuthoringIndex::clear() noexcept
{
    objects_.clear();
    orderedIds_.clear();
    updateStack_.clear();
    documentGeneration_ = 0;
    structureRevision_ = 0;
}

const AuthoringObjectDescriptor *AuthoringIndex::find(int documentId) const noexcept
{
    const auto found = objects_.find(documentId);
    return found == objects_.end() ? nullptr : &found->second;
}

const std::vector<int> &AuthoringIndex::ordered_ids() const noexcept { return orderedIds_; }
std::size_t AuthoringIndex::size() const noexcept { return objects_.size(); }
std::uint64_t AuthoringIndex::document_generation() const noexcept { return documentGeneration_; }
std::uint64_t AuthoringIndex::structure_revision() const noexcept { return structureRevision_; }
} // namespace platformator_editor

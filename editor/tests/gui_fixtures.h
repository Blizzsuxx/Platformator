#pragma once

#include <cstddef>

#include <json.hpp>

namespace platformator_editor_test::gui_fixtures
{
using Json = nlohmann::json;

// A compact representative scene containing every currently supported
// component kind, an empty object, a parent/child pair, and an inactive object.
Json all_component_kinds();

// Lossless edge data for unknown members, omitted fields, explicit null,
// zero, empty strings, and an unknown component discriminator.
Json unknown_missing_and_null_data();

// Two overlapping sprite objects plus coincident camera/collider geometry.
Json overlap_scene();

// Mixed visible authoring geometry backed by textured sprite references.
Json textured_mixed_scene();

// Self, internal, external, and component reference values in declared data.
Json typed_reference_scene();

// Flat scene with exactly count unique objects. Zero and duplicate IDs are
// intentionally not introduced by the generator.
Json many_objects(std::size_t count);

// A single root chain with exactly depth objects (depth zero yields an empty
// scene). Construction is iterative so the helper itself does not recurse.
Json deep_hierarchy(std::size_t depth);

// Deep hierarchy where every level owns a sprite component.
Json deep_sprite_chain(std::size_t depth);
} // namespace platformator_editor_test::gui_fixtures

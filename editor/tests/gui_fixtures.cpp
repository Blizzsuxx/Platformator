#include "gui_fixtures.h"

#include "test_support.h"

namespace platformator_editor_test::gui_fixtures
{
namespace
{
Json basic_object(int id, std::string name, Json components = Json::array(),
                  Json children = Json::array())
{
    return platformator_editor_test::object(id, std::move(name),
                                             std::move(components), std::move(children));
}

Json sprite(int id, std::string path = "assets/textures/checker.png")
{
    return Json{{"id", id}, {"type", 6}, {"textureFilePath", std::move(path)},
                {"flip", 0}, {"width", 32.0}, {"height", 24.0},
                {"sourceRectEnabled", false}};
}
} // namespace

Json all_component_kinds()
{
    Json all = Json::array();
    all.push_back(Json{{"id", 10}, {"type", 0}, {"currentFrameIndex", 0},
                       {"playbackSpeed", 1.0}, {"playing", false},
                       {"animationClipFilePath", ""}});
    all.push_back(Json{{"id", 11}, {"type", 1}, {"filePath", ""}, {"gain", 1.0},
                       {"loopCount", 0}, {"autoPlay", false}});
    all.push_back(Json{{"id", 12}, {"type", 2}, {"width", 640.0}, {"height", 480.0}});
    all.push_back(Json{{"id", 13}, {"type", 3}, {"colliderType", 1}, {"width", 16.0},
                       {"height", 16.0}, {"offset", {{"x", 0.0}, {"y", 0.0}}},
                       {"trigger", false}, {"collisionGroup", 1}, {"collisionMask", 1}});
    all.push_back(Json{{"id", 14}, {"type", 4}, {"velocity", {{"x", 0.0}, {"y", 0.0}}},
                       {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 1.0},
                       {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", 0.5},
                       {"restitution", 0.0}, {"bodyType", 0}, {"gravity", true}});
    all.push_back(Json{{"id", 15}, {"type", 5}, {"behaviors", Json::array({
                       Json{{"type", "FixtureBehavior"}, {"fields", Json::object()}}})}});
    all.push_back(sprite(16));

    Json child = basic_object(2, "Nested Empty Object");
    Json inactive = basic_object(3, "Inactive Object");
    inactive["active"] = false;
    return Json::array({basic_object(1, "Every Component", std::move(all),
                                     Json::array({std::move(child)})),
                        basic_object(4, "Empty Object"), std::move(inactive)});
}

Json unknown_missing_and_null_data()
{
    Json partial = Json::object({{"id", 1}, {"name", "Partial"},
                                 {"unknownObjectMember", {{"keep", true}}},
                                 {"components", Json::array({
                                     Json{{"id", 2}, {"type", 97}, {"opaque", "preserve"}},
                                     Json{{"id", 3}, {"type", 6}, {"textureFilePath", nullptr},
                                          {"width", 0}, {"optionalText", ""}},
                                     Json{{"type", 5}, {"behaviors", nullptr}},
                                     nullptr})},
                                 {"children", Json::array()}});
    return Json::array({std::move(partial)});
}

Json overlap_scene()
{
    Json first = basic_object(1, "Overlap A", Json::array({sprite(11)}));
    Json second = basic_object(2, "Overlap B", Json::array({sprite(12)}));
    Json geometry = basic_object(3, "Coincident Geometry", Json::array({
        Json{{"id", 13}, {"type", 2}, {"width", 320.0}, {"height", 200.0}},
        Json{{"id", 14}, {"type", 3}, {"colliderType", 0}, {"width", 32.0},
             {"height", 24.0}, {"offset", {{"x", 0.0}, {"y", 0.0}}}}}));
    return Json::array({std::move(first), std::move(second), std::move(geometry)});
}

Json textured_mixed_scene()
{
    Json spriteObject = basic_object(1, "Textured Sprite", Json::array({sprite(11)}));
    Json mixed = basic_object(2, "Mixed Geometry", Json::array({
        sprite(12),
        Json{{"id", 13}, {"type", 2}, {"width", 240.0}, {"height", 160.0}},
        Json{{"id", 14}, {"type", 3}, {"colliderType", 0}, {"width", 48.0},
             {"height", 32.0}, {"offset", {{"x", 4.0}, {"y", -3.0}}}}}));
    return Json::array({std::move(spriteObject), std::move(mixed),
                        basic_object(3, "Empty Object")});
}

Json typed_reference_scene()
{
    const Json behavior{{"type", "ReferenceFixture"},
                        {"references", {{"self", 1}, {"internal", 2},
                                         {"external", 9000}, {"component", 12}}}};
    Json source = basic_object(1, "Reference Source", Json::array({
        Json{{"id", 10}, {"type", 5}, {"behaviors", Json::array({behavior})}}}));
    Json target = basic_object(2, "Internal Target", Json::array({
        Json{{"id", 12}, {"type", 2}, {"width", 320.0}, {"height", 200.0}}}));
    return Json::array({std::move(source), std::move(target)});
}

Json many_objects(std::size_t count)
{
    Json roots = Json::array();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto id = static_cast<int>(i);
        roots.push_back(basic_object(id, "Object " + std::to_string(i)));
    }
    return roots;
}

Json deep_hierarchy(std::size_t depth)
{
    if (depth == 0)
        return Json::array();

    Json chain = basic_object(static_cast<int>(depth - 1), "Depth " + std::to_string(depth - 1));
    for (std::size_t i = depth - 1; i > 0; --i)
        chain = basic_object(static_cast<int>(i - 1), "Depth " + std::to_string(i - 1),
                             Json::array(), Json::array({std::move(chain)}));
    return Json::array({std::move(chain)});
}

Json deep_sprite_chain(std::size_t depth)
{
    if (depth == 0)
        return Json::array();
    Json chain = basic_object(static_cast<int>(depth - 1),
                              "Sprite Depth " + std::to_string(depth - 1),
                              Json::array({sprite(static_cast<int>(depth * 2 - 1))}));
    for (std::size_t index = depth - 1; index > 0; --index)
    {
        const auto objectId = static_cast<int>(index - 1);
        const auto componentId = static_cast<int>(depth + index - 1);
        chain = basic_object(objectId, "Sprite Depth " + std::to_string(objectId),
                             Json::array({sprite(componentId)}), Json::array({std::move(chain)}));
    }
    return Json::array({std::move(chain)});
}
} // namespace platformator_editor_test::gui_fixtures

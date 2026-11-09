#include "authoringindex.h"
#include "editcommands.h"
#include "validation.h"

#include "test_support.h"

#include <cmath>
#include <algorithm>
#include <numbers>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
Json positioned_object(int id, double x, double y, Json components = Json::array(),
                       Json children = Json::array(), double scaleX = 1.0,
                       double scaleY = 1.0, double rotation = 0.0, bool active = true)
{
    Json result = object(id, "object " + std::to_string(id), std::move(components), std::move(children));
    result["position"] = {{"x", x}, {"y", y}};
    result["scale"] = {{"x", scaleX}, {"y", scaleY}};
    result["rotation"] = rotation;
    result["active"] = active;
    return result;
}

Json typed_component(int id, int type, Json fields)
{
    fields["type"] = type;
    fields["id"] = id;
    return fields;
}

void all_object_descriptors_and_bounds()
{
    constexpr double quarterTurn = std::numbers::pi / 2.0;
    Json cameraLegacy = typed_component(203, CAMERA, Json{{"camera", {{"w", 640.0}, {"h", 360.0}}}});
    Json cameraConflict = typed_component(204, CAMERA,
        Json{{"width", 800.0}, {"height", 450.0},
             {"camera", {{"w", 700.0}, {"h", 450.0}}}});
    Json collider = typed_component(202, COLLIDER,
        Json{{"colliderType", 1}, {"width", 10.0}, {"height", 8.0},
             {"offset", {{"x", 4.0}, {"y", -2.0}}}});
    Json sprite = typed_component(205, SPRITE,
        Json{{"textureFilePath", "missing.png"}, {"width", 20.0}, {"height", 12.0}});
    Json nestedEmpty = positioned_object(11, 10.0, 20.0, Json::array(), Json::array(),
                                         7.0, 5.0, quarterTurn);
    Json parent = positioned_object(10, 100.0, 50.0, Json::array(), Json::array({nestedEmpty}),
                                    3.0, 4.0, quarterTurn);
    Json colliderOnly = positioned_object(20, -10.0, 5.0, Json::array({collider}),
                                          Json::array(), 2.0, 3.0, quarterTurn);
    Json legacyCamera = positioned_object(30, 7.0, -9.0, Json::array({cameraLegacy}));
    Json conflictingCamera = positioned_object(40, 0.0, 0.0, Json::array({cameraConflict}));
    Json inactiveMissingSprite = positioned_object(50, 4.0, 6.0, Json::array({sprite}),
                                                   Json::array(), 1.0, 1.0, 0.0, false);

    DocumentSession document;
    document.root = Json::array({parent, colliderOnly, legacyCamera, conflictingCamera,
                                 inactiveMissingSprite});
    document.documentGeneration = 19;
    document.structureRevision = 4;
    rebuild_document_indexes(document);

    AuthoringIndex index;
    index.rebuild(document);
    require(index.size() == 6, "the authoring index must include empty, collider-only, camera and sprite objects");
    require(index.ordered_ids() == std::vector<int>({10, 11, 20, 30, 40, 50}),
            "authoring order must preserve document preorder and include non-sprite objects");
    require(index.document_generation() == 19 && index.structure_revision() == 4,
            "the index identity must match its source document and structure revision");

    const auto *empty = index.find(11);
    require(empty != nullptr && empty->emptyMarker && empty->active,
            "an empty nested object must retain its identity and explicit marker state");
    require(std::abs(empty->worldX - 110.0f) < 0.001f && std::abs(empty->worldY - 70.0f) < 0.001f,
            "nested authoring positions must inherit parent translation without parent scale or rotation");
    require(empty->bounds.x == 104.0f && empty->bounds.y == 64.0f &&
                empty->bounds.width == 12.0f && empty->bounds.height == 12.0f,
            "empty marker bounds must be independently centered on the accumulated world position");

    const auto *box = index.find(20);
    require(box != nullptr && box->hasCollider && !box->hasSprite && box->componentIds[COLLIDER] == 202,
            "collider-only objects must retain their typed component identity");
    require(std::abs(box->worldX + 10.0f) < 0.001f && std::abs(box->worldY - 5.0f) < 0.001f,
            "root objects must retain their authored world translation");
    // The box is 20 by 24 after its own scale. Its local offset (4,-2) rotates to (2,4).
    // A quarter-turn makes the rotated AABB 24 by 20, centered at (-8,9).
    require(std::abs(box->bounds.x - (-20.0f)) < 0.001f &&
                std::abs(box->bounds.y - (-1.0f)) < 0.001f &&
                std::abs(box->bounds.width - 24.0f) < 0.001f &&
                std::abs(box->bounds.height - 20.0f) < 0.001f,
            "collider bounds must include scaled dimensions, rotated offset and object rotation");

    const auto *legacy = index.find(30);
    require(legacy != nullptr && legacy->hasCamera && legacy->componentIds[CAMERA] == 203 &&
                legacy->cameraUsesLegacyWidth && legacy->cameraUsesLegacyHeight &&
                legacy->cameraWidth == 640.0f && legacy->cameraHeight == 360.0f,
            "legacy-only camera dimensions must be available to authoring geometry with component identity");
    require(legacy->bounds.x == 7.0f && legacy->bounds.y == -9.0f &&
                legacy->bounds.width == 640.0f && legacy->bounds.height == 360.0f,
            "camera bounds must use the saved dimensions and translation");
    DocumentSession legacyValidationDocument = make_default_document();
    legacyValidationDocument.root[0]["components"][0].erase("width");
    legacyValidationDocument.root[0]["components"][0].erase("height");
    legacyValidationDocument.root[0]["components"][0]["camera"] = {{"w", 640.0}, {"h", 360.0}};
    const auto legacyValidation = validate_document(legacyValidationDocument);
    require(std::none_of(legacyValidation.issues.begin(), legacyValidation.issues.end(),
                         [](const ValidationIssue &issue) {
                             return (issue.fieldPath == "width" || issue.fieldPath == "height") && issue.blocksRun;
                         }),
            "the shared legacy camera dimensions must pass validation without canonical duplicates");

    const auto *conflict = index.find(40);
    require(conflict != nullptr && conflict->cameraWidth == 800.0f && conflict->cameraHeight == 450.0f &&
                conflict->cameraWidthConflict && !conflict->cameraHeightConflict,
            "canonical camera values must win while conflicting legacy dimensions remain detectable");
    require(index.find(50) != nullptr && !index.find(50)->active && index.find(50)->hasSprite &&
                index.find(50)->componentIds[SPRITE] == 205,
            "inactive objects and sprites with unavailable assets must remain indexed for authoring");
}

void malformed_colliders_keep_finite_editable_placeholders()
{
    struct MalformedCase
    {
        const char *label;
        Json fields;
        double scaleX = 1.0;
        double scaleY = 1.0;
        const char *diagnosticPath;
        bool circle = false;
    };
    const std::vector<MalformedCase> cases{
        {"null subtype", Json{{"colliderType", nullptr}, {"width", 9.0}, {"height", 4.0},
                              {"futureColliderPayload", Json{{"keep", true}}}}, 1.0, 1.0, "colliderType", false},
        {"string subtype", Json{{"colliderType", "box"}, {"width", 9.0}, {"height", 4.0}},
         1.0, 1.0, "colliderType", false},
        {"malformed box width", Json{{"colliderType", 1}, {"width", "wide"}, {"height", 4.0}},
         1.0, 1.0, "width", false},
        {"null circle radius", Json{{"colliderType", 0}, {"radius", nullptr}},
         1.0, 1.0, "radius", true},
        {"unrepresentable dimension", Json{{"colliderType", 1}, {"width", 1.0e100}, {"height", 2.0}},
         10.0, 1.0, "width", false},
    };

    for (std::size_t caseIndex = 0; caseIndex < cases.size(); ++caseIndex)
    {
        const auto &testCase = cases[caseIndex];
        Json collider = typed_component(301, COLLIDER, testCase.fields);
        Json authored = positioned_object(300, 100.0, -50.0, Json::array({collider}), Json::array(),
                                          testCase.scaleX, testCase.scaleY);
        auto parsed = parse_document(Json::array({authored}), 23);
        require(static_cast<bool>(parsed), std::string(testCase.label) +
                                             " collider must remain structurally loadable");
        DocumentSession document = std::move(*parsed.document);
        const Json original = document.root;
        const auto originalValueRevision = document.valueRevision;
        const auto originalStructureRevision = document.structureRevision;

        AuthoringIndex authoringIndex;
        authoringIndex.rebuild(document);
        const auto *descriptor = authoringIndex.find(300);
        require(descriptor != nullptr && descriptor->hasCollider && descriptor->colliderPlaceholder &&
                    descriptor->circleCollider == testCase.circle,
                std::string(testCase.label) + " must produce an editable placeholder descriptor");
        const auto finite = [](float value) { return std::isfinite(value); };
        require(finite(descriptor->bounds.x) && finite(descriptor->bounds.y) &&
                    finite(descriptor->bounds.width) && finite(descriptor->bounds.height) &&
                    descriptor->bounds.width > 0.0f && descriptor->bounds.height > 0.0f,
                std::string(testCase.label) + " must produce finite nonempty bounds");
        require(document.root == original && document.valueRevision == originalValueRevision &&
                    document.structureRevision == originalStructureRevision,
                std::string(testCase.label) + " indexing must preserve saved authored content and revisions");

        const auto validation = validate_document(document);
        require(testCase.diagnosticPath == nullptr ||
                    std::any_of(validation.issues.begin(), validation.issues.end(), [&](const ValidationIssue &issue) {
                    return issue.objectId == 300 && issue.componentId == 301 &&
                           issue.fieldPath == testCase.diagnosticPath;
                }),
                std::string(testCase.label) + " must retain a field-specific validation diagnostic");

        if (caseIndex == 0)
        {
            EditCommandService commands;
            commands.reset_history(document.documentGeneration);
            commands.mark_saved();
            const auto repaired = commands.set_property(document,
                address_for_document(document, 300, 301, std::nullopt, {std::string("colliderType")}), 1);
            require(repaired.status == CommandStatus::Changed &&
                        document.root[0]["components"][0]["futureColliderPayload"] ==
                            Json{{"keep", true}},
                    "a malformed collider subtype must remain editable without dropping unknown payload");
        }
    }
}
} // namespace

int main()
{
    all_object_descriptors_and_bounds();
    malformed_colliders_keep_finite_editable_placeholders();
    return 0;
}

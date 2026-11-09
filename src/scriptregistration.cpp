#include "platformator/scriptregistration.h"

#include "behaviorfactoryregistry.h"

#include <utility>

namespace platformator
{
    void registerScriptFactory(std::string_view typeName, Behavior *(*factory)())
    {
        BehaviorFactoryRegistry::getInstance().registerFactory(std::string(typeName), factory);
    }

    void registerScriptDescriptorProvider(std::string_view typeName, ScriptDescriptorProvider provider)
    {
        BehaviorFactoryRegistry::getInstance().registerDescriptorProvider(std::string(typeName), provider);
    }

    void registerScriptInspectionProvider(std::string_view typeName, ScriptInspectionProvider provider)
    {
        BehaviorFactoryRegistry::getInstance().registerInspectionProvider(std::string(typeName), provider);
    }

    ScriptInspectionResult inspectRegisteredScriptFields(std::string_view typeName, const Behavior &instance,
                                                         ScriptInspectionLimits limits)
    {
        return BehaviorFactoryRegistry::getInstance().inspectFields(std::string(typeName), instance, limits);
    }

    Behavior *createRegisteredBehavior(std::string_view typeName)
    {
        return BehaviorFactoryRegistry::getInstance().createBehavior(std::string(typeName));
    }

    nlohmann::json exportRegisteredBehaviorCatalog()
    {
        nlohmann::json behaviors = nlohmann::json::array();
        for (const auto &descriptor : BehaviorFactoryRegistry::getInstance().registeredDescriptors())
            behaviors.push_back(script_descriptor_json(descriptor));
        nlohmann::json catalog = {{"schema_version", 1}, {"behaviors", std::move(behaviors)}};
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        // These controls are available only in the opt-in runner, where the
        // owner-thread loop services Pause, Resume, and one fixed Step.
        catalog["editor_capabilities"] = {
            {"session_protocol", "platformator-editor-session-v1"},
            {"play_control", true},
            {"runtime_inspection", true},
            {"rgba_frame_stream", { {"version", 1}, {"width", 640},
                                      {"height", 360}, {"max_hz", 15} }},
            {"live_asset_reload", false},
            {"input_forwarding", false}};
#endif
        return catalog;
    }
} // namespace platformator

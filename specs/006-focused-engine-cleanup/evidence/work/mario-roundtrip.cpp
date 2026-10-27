#include <json.hpp>
#include "platformator/runtime.h"
#include <fstream>
#include <iostream>
#include <filesystem>
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    RuntimeOptions options; options.assetRoot = argv[2];
    platformator::Runtime runtime(options);
    runtime.loadScene(argv[1]);
    runtime.simulateFrame(1.0 / 120.0);
    const auto savedPath = std::filesystem::temp_directory_path() / "focused-mario-roundtrip.scene";
    runtime.saveScene(savedPath.string());
    nlohmann::json scene;
    std::ifstream input(savedPath); input >> scene;
    std::filesystem::remove(savedPath);
    for (const auto &object : scene)
        for (const auto &component : object.at("components"))
            if (component.contains("behaviors"))
                for (const auto &behavior : component.at("behaviors"))
                    if (behavior.at("type") == "MarioCameraRig")
                    {
                        if (behavior.at("cameraLead") != 90) return 3;
                        std::cout << "MarioCameraRig cameraLead=90 round-tripped\n";
                        return 0;
                    }
    return 4;
}

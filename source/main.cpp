#include <iostream>
#include <fstream>
#include <filesystem>
#include <flags/flags.h>
#include <json/json.hpp>

#include <application.hpp>

#include "states/menu-state.hpp"
#include "states/play-state.hpp"
#include "states/shader-test-state.hpp"
#include "states/mesh-test-state.hpp"
#include "states/transform-test-state.hpp"
#include "states/pipeline-test-state.hpp"
#include "states/texture-test-state.hpp"
#include "states/sampler-test-state.hpp"
#include "states/material-test-state.hpp"
#include "states/entity-test-state.hpp"
#include "states/renderer-test-state.hpp"

namespace {
    std::filesystem::path resolve_config_path(const std::string& path) {
        namespace fs = std::filesystem;
        fs::path requested(path);

        if(requested.is_absolute()) return requested;

        const fs::path search_prefixes[] = {
            ".",
            "..",
            "../..",
            "../../.."
        };

        for(const auto& prefix : search_prefixes) {
            fs::path candidate = (prefix / requested).lexically_normal();
            if(fs::exists(candidate)) {
                std::error_code ec;
                fs::path absolute_candidate = fs::absolute(candidate, ec);
                return ec ? candidate : absolute_candidate;
            }
        }

        return requested;
    }

    std::filesystem::path find_project_root_from_config(const std::filesystem::path& config_path) {
        namespace fs = std::filesystem;
        fs::path current = config_path.parent_path();

        while(!current.empty()) {
            if(current.filename() == "config") {
                fs::path root = current.parent_path();
                if(!root.empty() && fs::exists(root / "assets")) {
                    return root;
                }
                break;
            }

            fs::path parent = current.parent_path();
            if(parent == current) break;
            current = parent;
        }

        return {};
    }
}

int main(int argc, char** argv) {
    
    flags::args args(argc, argv); // Parse the command line arguments
    // config_path is the path to the json file containing the application configuration
    // Default: "config/app.json"
    std::string config_path = args.get<std::string>("c", "config/app.jsonc");
    auto resolved_config_path = resolve_config_path(config_path);
    // run_for_frames is how many frames to run the application before automatically closing
    // This is useful for testing multiple configurations in a batch
    // Default: 0 where the application runs indefinitely until manually closed
    int run_for_frames = args.get<int>("f", 0);

    // Open the config file and exit if failed
    std::ifstream file_in(resolved_config_path);
    if(!file_in){
        std::cerr << "Couldn't open file: " << config_path << std::endl;
        return -1;
    }
    // Read the file into a json object then close the file
    nlohmann::json app_config = nlohmann::json::parse(file_in, nullptr, true, true);
    file_in.close();

    // Keep runtime-relative paths (assets/, screenshots/, ...) stable when launched from bin/.
    auto project_root = find_project_root_from_config(resolved_config_path);
    if(!project_root.empty()) {
        std::error_code ec;
        std::filesystem::current_path(project_root, ec);
        if(ec) {
            std::cerr << "Warning: couldn't set working directory to " << project_root << ": " << ec.message() << std::endl;
        }
    }

    // Create the application
    our::Application app(app_config);
    
    // Register all the states of the project in the application
    app.registerState<Menustate>("menu");
    app.registerState<Playstate>("play");
    app.registerState<ShaderTestState>("shader-test");
    app.registerState<MeshTestState>("mesh-test");
    app.registerState<TransformTestState>("transform-test");
    app.registerState<PipelineTestState>("pipeline-test");
    app.registerState<TextureTestState>("texture-test");
    app.registerState<SamplerTestState>("sampler-test");
    app.registerState<MaterialTestState>("material-test");
    app.registerState<EntityTestState>("entity-test");
    app.registerState<RendererTestState>("renderer-test");
    // Then choose the state to run based on the option "start-scene" in the config
    if(app_config.contains(std::string{"start-scene"})){
        std::string startState = app_config["start-scene"].get<std::string>();
        std::cout << "[MAIN] start-scene from config: " << startState << std::endl;
        app.changeState(startState);
    }

    // Finally run the application
    // Here, the application loop will run till the terminatio condition is statisfied
    return app.run(run_for_frames);
}
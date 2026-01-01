#include "src/Core/GameBuilder.h"
#include <iostream>

int main() {
    BuildSettings settings;
    settings.gameName = "TestGame";
    settings.outputPath = "build/export";
    settings.mainScene = "scene.json";
    settings.windowWidth = 800;
    settings.windowHeight = 600;
    
    GameBuilder& builder = GameBuilder::Get();
    if (builder.Build(settings)) {
        std::cout << "Build successful!" << std::endl;
        return 0;
    } else {
        std::cerr << "Build failed: " << builder.GetLastError() << std::endl;
        return 1;
    }
}

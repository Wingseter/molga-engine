#pragma once

#include <string>
#include <vector>
#include <array>
#include <nlohmann/json.hpp>
#include "Common/Types.h"

class ProjectSettings {
public:
    static ProjectSettings& Get();

    ProjectSettings();

    // Data fields
    std::vector<std::string> tags;
    std::array<std::string, 32> layerNames;
    std::array<std::array<bool, 32>, 32> collisionMatrix;
    std::vector<std::string> sortingLayers;

    // 2D physics uses pixels at the engine boundary and metres internally in
    // Box2D. These defaults preserve the engine's historical 981 px/s^2
    // gravity while using Box2D's conventional 9.81 m/s^2.
    Vector2 gravity = Vector2(0.0f, 981.0f);
    float pixelsPerMeter = 100.0f;
    int substeps = 4;

    // Reset settings to default values
    void SetDefaults();

    // Load settings from a JSON file. If the file doesn't exist, it uses defaults.
    bool LoadFromFile(const std::string& filepath);

    // Save settings to a JSON file.
    bool SaveToFile(const std::string& filepath) const;

    // Load settings from a JSON object.
    void Deserialize(const nlohmann::json& j);

    // Save settings to a JSON object.
    nlohmann::json Serialize() const;

    // Helper functions
    bool IsCollisionEnabled(int layerA, int layerB) const;
    void SetCollisionEnabled(int layerA, int layerB, bool enabled);
    
    // Manage Tags & Layers
    int GetLayerIndex(const std::string& name) const;
    std::string GetLayerName(int index) const;
};

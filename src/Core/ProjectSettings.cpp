#include "ProjectSettings.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>

ProjectSettings& ProjectSettings::Get() {
    static ProjectSettings instance;
    return instance;
}

ProjectSettings::ProjectSettings() {
    SetDefaults();
}

void ProjectSettings::SetDefaults() {
    tags = { "Untagged", "MainCamera", "Player" };

    // Initialize all layers to empty
    for (int i = 0; i < 32; ++i) {
        layerNames[i] = "";
    }
    // Set standard default layers
    layerNames[0] = "Default";
    layerNames[1] = "TransparentFX";
    layerNames[2] = "Ignore Raycast";
    layerNames[3] = "Water";
    layerNames[4] = "UI";
    layerNames[5] = "Ground";

    // Collision matrix: default all layer pairs enabled (Unity convention).
    // 이름 없는 레이어만 false로 두면, 나중에 새 레이어(6~31)에 오브젝트를 배치했을 때
    // 매트릭스가 false라 아무 충돌도 일어나지 않는 '조용한 실패'가 발생한다.
    // 기본 전부 true로 두고, 비활성화는 사용자가 명시적으로 끄도록 한다.
    for (int i = 0; i < 32; ++i) {
        for (int j = 0; j < 32; ++j) {
            collisionMatrix[i][j] = true;
        }
    }

    sortingLayers = { "Default", "Background", "Foreground" };
    gravity = Vector2(0.0f, 981.0f);
    pixelsPerMeter = 100.0f;
    substeps = 4;
}

bool ProjectSettings::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        SetDefaults();
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;
        Deserialize(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ProjectSettings] Failed to parse " << filepath << ": " << e.what() << std::endl;
        SetDefaults();
        return false;
    }
}

bool ProjectSettings::SaveToFile(const std::string& filepath) const {
    try {
        std::filesystem::path path(filepath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "[ProjectSettings] Failed to open " << filepath << " for writing" << std::endl;
            return false;
        }

        nlohmann::json j = Serialize();
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ProjectSettings] Failed to save settings to " << filepath << ": " << e.what() << std::endl;
        return false;
    }
}

void ProjectSettings::Deserialize(const nlohmann::json& j) {
    SetDefaults(); // Load defaults first

    if (j.contains("tags") && j["tags"].is_array()) {
        tags.clear();
        for (const auto& tag : j["tags"]) {
            if (tag.is_string()) {
                tags.push_back(tag.get<std::string>());
            }
        }
    }

    if (j.contains("layerNames") && j["layerNames"].is_array()) {
        int idx = 0;
        for (const auto& name : j["layerNames"]) {
            if (idx >= 32) break;
            if (name.is_string()) {
                layerNames[idx] = name.get<std::string>();
            }
            idx++;
        }
    }

    if (j.contains("collisionMatrix") && j["collisionMatrix"].is_array()) {
        int r = 0;
        for (const auto& row : j["collisionMatrix"]) {
            if (r >= 32) break;
            if (row.is_array()) {
                int c = 0;
                for (const auto& val : row) {
                    if (c >= 32) break;
                    if (val.is_boolean()) {
                        collisionMatrix[r][c] = val.get<bool>();
                    }
                    c++;
                }
            }
            r++;
        }
    }

    if (j.contains("sortingLayers") && j["sortingLayers"].is_array()) {
        sortingLayers.clear();
        for (const auto& sLayer : j["sortingLayers"]) {
            if (sLayer.is_string()) {
                sortingLayers.push_back(sLayer.get<std::string>());
            }
        }
    }

    if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() >= 2) {
        const float x = j["gravity"][0].get<float>();
        const float y = j["gravity"][1].get<float>();
        if (std::isfinite(x) && std::isfinite(y)) gravity = Vector2(x, y);
    }
    if (j.contains("pixelsPerMeter")) {
        const float value = j["pixelsPerMeter"].get<float>();
        if (std::isfinite(value) && value > 0.0f) pixelsPerMeter = value;
    }
    if (j.contains("substeps")) {
        substeps = std::max(1, j["substeps"].get<int>());
    } else if (j.contains("physicsSubsteps")) {
        // Read the early implementation key for migration compatibility.
        substeps = std::max(1, j["physicsSubsteps"].get<int>());
    }
}

nlohmann::json ProjectSettings::Serialize() const {
    nlohmann::json j;
    j["tags"] = tags;
    
    nlohmann::json layersArr = nlohmann::json::array();
    for (int i = 0; i < 32; ++i) {
        layersArr.push_back(layerNames[i]);
    }
    j["layerNames"] = layersArr;

    nlohmann::json matrixArr = nlohmann::json::array();
    for (int i = 0; i < 32; ++i) {
        nlohmann::json row = nlohmann::json::array();
        for (int j = 0; j < 32; ++j) {
            row.push_back(collisionMatrix[i][j]);
        }
        matrixArr.push_back(row);
    }
    j["collisionMatrix"] = matrixArr;

    j["sortingLayers"] = sortingLayers;
    j["gravity"] = { gravity.x, gravity.y };
    j["pixelsPerMeter"] = pixelsPerMeter;
    j["substeps"] = substeps;
    return j;
}

bool ProjectSettings::IsCollisionEnabled(int layerA, int layerB) const {
    if (layerA < 0 || layerA >= 32 || layerB < 0 || layerB >= 32) {
        return false;
    }
    return collisionMatrix[layerA][layerB];
}

void ProjectSettings::SetCollisionEnabled(int layerA, int layerB, bool enabled) {
    if (layerA >= 0 && layerA < 32 && layerB >= 0 && layerB < 32) {
        collisionMatrix[layerA][layerB] = enabled;
        collisionMatrix[layerB][layerA] = enabled; // Keep it symmetric
    }
}

int ProjectSettings::GetLayerIndex(const std::string& name) const {
    if (name.empty()) return -1;
    for (int i = 0; i < 32; ++i) {
        if (layerNames[i] == name) {
            return i;
        }
    }
    return -1;
}

std::string ProjectSettings::GetLayerName(int index) const {
    if (index >= 0 && index < 32) {
        return layerNames[index];
    }
    return "";
}

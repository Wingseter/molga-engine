#include "ProjectSettingsWindow.h"
#include "../EditorConstants.h"
#include "../../Core/ProjectSettings.h"
#include "../../Systems/Audio.h"
#include "../../Systems/Input.h"
#include "../Project.h"
#include <imgui.h>
#include <filesystem>
#include <vector>
#include <algorithm>

ProjectSettingsWindow::ProjectSettingsWindow()
    : EditorWindow("Project Settings") {
    isOpen = false; // Closed by default
}

void ProjectSettingsWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin(title.c_str(), &isOpen);

    if (!Project::Get().IsOpen()) {
        ImGui::Text("No project is open. Settings cannot be edited.");
        ImGui::End();
        return;
    }

    auto& settings = ProjectSettings::Get();
    bool modified = false;

    if (ImGui::BeginTabBar("ProjectSettingsTabs")) {
        // --- TAGS TAB ---
        if (ImGui::BeginTabItem("Tags")) {
            ImGui::Text("GameObject Tags");
            ImGui::Separator();
            ImGui::Spacing();

            // Display existing tags
            static int selectedTagIdx = -1;
            if (ImGui::BeginChild("TagList", ImVec2(0, 250), true)) {
                for (size_t i = 0; i < settings.tags.size(); ++i) {
                    const bool isSelected = (selectedTagIdx == (int)i);
                    if (ImGui::Selectable(settings.tags[i].c_str(), isSelected)) {
                        selectedTagIdx = (int)i;
                    }
                }
                ImGui::EndChild();
            }

            // Add Tag
            static char newTagBuffer[128] = "";
            ImGui::InputText("New Tag Name", newTagBuffer, sizeof(newTagBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Add Tag")) {
                std::string newTag(newTagBuffer);
                if (!newTag.empty() && 
                    std::find(settings.tags.begin(), settings.tags.end(), newTag) == settings.tags.end()) {
                    settings.tags.push_back(newTag);
                    newTagBuffer[0] = '\0';
                    modified = true;
                }
            }

            // Delete Tag
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected")) {
                if (selectedTagIdx >= 0 && selectedTagIdx < (int)settings.tags.size()) {
                    // Prevent deleting built-in "Untagged"
                    if (settings.tags[selectedTagIdx] != "Untagged") {
                        settings.tags.erase(settings.tags.begin() + selectedTagIdx);
                        selectedTagIdx = -1;
                        modified = true;
                    }
                }
            }

            ImGui::EndTabItem();
        }

        // --- LAYERS TAB ---
        if (ImGui::BeginTabItem("Layers")) {
            ImGui::Text("Physics/Culling Layers (32 Slots)");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginChild("LayerList", ImVec2(0, 350), true)) {
                for (int i = 0; i < 32; ++i) {
                    char nameBuf[128];
                    strncpy(nameBuf, settings.layerNames[i].c_str(), sizeof(nameBuf) - 1);
                    nameBuf[sizeof(nameBuf) - 1] = '\0';

                    std::string label = "Layer " + std::to_string(i);
                    
                    // Don't allow editing Slot 0 ("Default") since it's the engine fallback
                    ImGuiInputTextFlags flags = (i == 0) ? ImGuiInputTextFlags_ReadOnly : 0;

                    ImGui::SetNextItemWidth(-100.0f);
                    if (ImGui::InputText(label.c_str(), nameBuf, sizeof(nameBuf), flags)) {
                        settings.layerNames[i] = nameBuf;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        modified = true;
                        
                        // Update default collision settings for recently named/cleared layers
                        for (int j = 0; j < 32; ++j) {
                            if (!settings.layerNames[i].empty() && !settings.layerNames[j].empty()) {
                                // Default enable collision if both are named and it was just created
                                if (settings.layerNames[i] != "" && !settings.collisionMatrix[i][j]) {
                                    settings.SetCollisionEnabled(i, j, true);
                                }
                            } else {
                                // Disable collision if one is empty
                                settings.SetCollisionEnabled(i, j, false);
                            }
                        }
                    }
                }
                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        // --- COLLISION MATRIX TAB ---
        if (ImGui::BeginTabItem("Collision Matrix")) {
            ImGui::Text("Define which layers collide with each other.");
            ImGui::Separator();
            ImGui::Spacing();

            // Find all named (non-empty) layers to display in the matrix
            std::vector<int> activeIndices;
            for (int i = 0; i < 32; ++i) {
                if (!settings.layerNames[i].empty()) {
                    activeIndices.push_back(i);
                }
            }

            if (activeIndices.empty()) {
                ImGui::TextDisabled("No active layers found. Name some layers first.");
            } else {
                int numLayers = (int)activeIndices.size();
                if (ImGui::BeginTable("CollisionMatrixTable", numLayers + 1, 
                                       ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, 
                                       ImVec2(0, 320))) {
                    
                    // Column Setup
                    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    for (int i : activeIndices) {
                        ImGui::TableSetupColumn(settings.layerNames[i].c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    }
                    ImGui::TableHeadersRow();

                    for (int r = 0; r < numLayers; ++r) {
                        int i = activeIndices[r];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", settings.layerNames[i].c_str());

                        for (int c = 0; c < numLayers; ++c) {
                            int j = activeIndices[c];
                            ImGui::TableSetColumnIndex(c + 1);

                            // Only allow editing upper-right triangle + diagonal (symmetric editing helper)
                            // We display everything, but checking/unchecking keeps it symmetric.
                            bool val = settings.IsCollisionEnabled(i, j);
                            std::string id = "##cb_" + std::to_string(i) + "_" + std::to_string(j);
                            
                            // Let's render the checkbox
                            if (ImGui::Checkbox(id.c_str(), &val)) {
                                settings.SetCollisionEnabled(i, j, val);
                                modified = true;
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Physics 2D")) {
            float gravity[2] = {settings.gravity.x, settings.gravity.y};
            if (ImGui::DragFloat2("Gravity (px/s^2)", gravity, 1.0f)) {
                settings.gravity = Vector2(gravity[0], gravity[1]);
                modified = true;
            }
            float ppm = settings.pixelsPerMeter;
            if (ImGui::DragFloat("Pixels Per Meter", &ppm, 1.0f, 1.0f, 10000.0f)) {
                settings.pixelsPerMeter = std::max(ppm, 1.0f);
                modified = true;
            }
            int substeps = settings.substeps;
            if (ImGui::DragInt("Substeps", &substeps, 1.0f, 1, 32)) {
                settings.substeps = std::max(substeps, 1);
                modified = true;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Audio")) {
            ImGui::Text("Fixed Mixer Buses");
            ImGui::TextDisabled("Master is the final gain; child buses route through it.");
            ImGui::Separator();
            for (std::size_t i = 0; i < ProjectSettings::AudioBusCount; ++i) {
                const AudioBus bus = static_cast<AudioBus>(i);
                auto& busSettings = settings.audioBusSettings[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s", AudioBusName(bus));
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(260.0f);
                float volume = busSettings.volume;
                if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f")) {
                    busSettings.volume = volume;
                    Audio::SetBusVolume(bus, volume);
                    modified = true;
                }
                ImGui::SameLine();
                bool muted = busSettings.muted;
                if (ImGui::Checkbox("Mute", &muted)) {
                    busSettings.muted = muted;
                    Audio::SetBusMuted(bus, muted);
                    modified = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // --- SORTING LAYERS TAB ---
        if (ImGui::BeginTabItem("Sorting Layers")) {
            ImGui::Text("Sprite Sorting Layers (in render order: top = background/rendered first)");
            ImGui::Separator();
            ImGui::Spacing();

            // Display existing sorting layers
            static int selectedSortingLayerIdx = -1;
            if (ImGui::BeginChild("SortingLayerList", ImVec2(0, 250), true)) {
                for (size_t i = 0; i < settings.sortingLayers.size(); ++i) {
                    const bool isSelected = (selectedSortingLayerIdx == (int)i);
                    if (ImGui::Selectable(settings.sortingLayers[i].c_str(), isSelected)) {
                        selectedSortingLayerIdx = (int)i;
                    }
                }
                ImGui::EndChild();
            }

            // Add Sorting Layer
            static char newSLayerBuffer[128] = "";
            ImGui::InputText("New Layer Name", newSLayerBuffer, sizeof(newSLayerBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Add Layer")) {
                std::string newLayer(newSLayerBuffer);
                if (!newLayer.empty() && 
                    std::find(settings.sortingLayers.begin(), settings.sortingLayers.end(), newLayer) == settings.sortingLayers.end()) {
                    settings.sortingLayers.push_back(newLayer);
                    newSLayerBuffer[0] = '\0';
                    modified = true;
                }
            }

            // Reorder buttons (Move Up / Down)
            if (ImGui::Button("Move Up")) {
                if (selectedSortingLayerIdx > 0 && selectedSortingLayerIdx < (int)settings.sortingLayers.size()) {
                    std::swap(settings.sortingLayers[selectedSortingLayerIdx], settings.sortingLayers[selectedSortingLayerIdx - 1]);
                    selectedSortingLayerIdx--;
                    modified = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Move Down")) {
                if (selectedSortingLayerIdx >= 0 && selectedSortingLayerIdx < (int)settings.sortingLayers.size() - 1) {
                    std::swap(settings.sortingLayers[selectedSortingLayerIdx], settings.sortingLayers[selectedSortingLayerIdx + 1]);
                    selectedSortingLayerIdx++;
                    modified = true;
                }
            }

            // Delete Sorting Layer
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected##SL")) {
                if (selectedSortingLayerIdx >= 0 && selectedSortingLayerIdx < (int)settings.sortingLayers.size()) {
                    // Prevent deleting Default sorting layer
                    if (settings.sortingLayers[selectedSortingLayerIdx] != "Default") {
                        settings.sortingLayers.erase(settings.sortingLayers.begin() + selectedSortingLayerIdx);
                        selectedSortingLayerIdx = -1;
                        modified = true;
                    }
                }
            }

            ImGui::EndTabItem();
        }

        // --- INPUT TAB ---
        if (ImGui::BeginTabItem("Input")) {
            ImGui::Text("Input Actions Config");
            ImGui::Separator();
            ImGui::Spacing();

            auto& actions = Input::GetActions();
            bool inputModified = false;

            // Add new Action
            static char newActionName[128] = "";
            static bool newActionIsAxis = false;
            ImGui::InputText("New Action Name", newActionName, sizeof(newActionName));
            ImGui::Checkbox("Is Axis?", &newActionIsAxis);
            ImGui::SameLine();
            if (ImGui::Button("Add Action")) {
                if (strlen(newActionName) > 0) {
                    Input::Action newAct;
                    newAct.name = newActionName;
                    newAct.isAxis = newActionIsAxis;
                    actions.push_back(newAct);
                    newActionName[0] = '\0';
                    newActionIsAxis = false;
                    inputModified = true;
                }
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Display Actions
            if (ImGui::BeginChild("ActionList", ImVec2(0, 300), true)) {
                for (size_t i = 0; i < actions.size(); ++i) {
                    auto& action = actions[i];
                    std::string actionHeader = action.name + (action.isAxis ? " (Axis)" : " (Button)");
                    std::string imguiId = "##action_" + std::to_string(i);

                    if (ImGui::TreeNode((actionHeader + imguiId).c_str())) {
                        // Action Name editing
                        char nameBuf[128];
                        strncpy(nameBuf, action.name.c_str(), sizeof(nameBuf));
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        if (ImGui::InputText(("Name" + imguiId).c_str(), nameBuf, sizeof(nameBuf))) {
                            action.name = nameBuf;
                            inputModified = true;
                        }

                        // Action Type editing
                        if (ImGui::Checkbox(("Is Axis##" + imguiId).c_str(), &action.isAxis)) {
                            inputModified = true;
                        }

                        ImGui::SameLine();
                        if (ImGui::Button(("Delete Action##" + imguiId).c_str())) {
                            actions.erase(actions.begin() + i);
                            --i;
                            inputModified = true;
                            ImGui::TreePop();
                            continue;
                        }

                        ImGui::Spacing();
                        ImGui::Text("Bindings:");
                        ImGui::Indent();

                        for (size_t j = 0; j < action.bindings.size(); ++j) {
                            auto& binding = action.bindings[j];
                            std::string bindingId = imguiId + "_bind_" + std::to_string(j);

                            // Device Type Combo
                            const char* deviceTypes[] = { "Keyboard", "Mouse", "GamepadButton", "GamepadAxis" };
                            int currentDevice = static_cast<int>(binding.device);
                            ImGui::SetNextItemWidth(120.0f);
                            if (ImGui::Combo(("Device" + bindingId).c_str(), &currentDevice, deviceTypes, 4)) {
                                binding.device = static_cast<Input::DeviceType>(currentDevice);
                                inputModified = true;
                            }

                            // Code
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80.0f);
                            if (ImGui::InputInt(("Code" + bindingId).c_str(), &binding.code)) {
                                inputModified = true;
                            }

                            // Multiplier
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80.0f);
                            if (ImGui::InputFloat(("Mult" + bindingId).c_str(), &binding.multiplier, 0.1f, 1.0f, "%.2f")) {
                                inputModified = true;
                            }

                            // Remove Binding button
                            ImGui::SameLine();
                            if (ImGui::Button(("Delete##" + bindingId).c_str())) {
                                action.bindings.erase(action.bindings.begin() + j);
                                --j;
                                inputModified = true;
                            }
                        }

                        if (ImGui::Button(("Add Binding##" + imguiId).c_str())) {
                            Input::Binding newBind;
                            action.bindings.push_back(newBind);
                            inputModified = true;
                        }

                        ImGui::Unindent();
                        ImGui::TreePop();
                    }
                }
                ImGui::EndChild();
            }

            if (inputModified) {
                std::string inputPath = (std::filesystem::path(Project::Get().GetPath()) / "ProjectSettings" / "input_actions.json").string();
                Input::SaveActions(inputPath);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (modified) {
        std::string settingsPath = (std::filesystem::path(Project::Get().GetPath()) / "ProjectSettings" / "project_settings.json").string();
        settings.SaveToFile(settingsPath);
    }

    ImGui::End();
}

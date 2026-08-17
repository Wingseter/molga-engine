#include "InspectorWindow.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Component.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/Animator2D.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../../ECS/Components/ParticleSystem.h"
#include "../../ECS/Components/BoxCollider2D.h"
#include "../../ECS/Components/CircleCollider2D.h"
#include "../../ECS/Components/Rigidbody2D.h"
#include "../../ECS/Components/MarrowRenderer.h"
#include "../../ECS/Components/AudioSource.h"
#include "../../ECS/Components/AudioListener.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/PointLight2D.h"
#include "../../ECS/Components/ShadowOccluder2D.h"
#include "../../ECS/Components/TextRenderer2D.h"
#include "../../ECS/Components/RectTransform.h"
#include "../../Scripting/ScriptManager.h"
#include "../../Core/AssetDatabase.h"
#include "../../Core/AssetMeta.h"
#include "../../Core/TextureImportSettings.h"
#include "../FontManager.h"
#include "../UIRegistry.h"
#include "../../Core/ProjectSettings.h"
#include "../Editor.h"
#include "../EditorConstants.h"
#include "../Project.h"
#include "../../ECS/Components/PrefabInstance.h"
#include "../Commands/PrefabCommands.h"
#include "../Commands/SceneSnapshots.h"
#include "../Commands/PropertyCommands.h"
#include "../Commands/ComponentCommands.h"
#include "../Properties/EditorPropertyDescriptor.h"
#include "../Commands/ProjectFileCommands.h"
#include "Rendering/PostProcessProfileResolver.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace {

Component* FindComponentInstance(GameObject* object, const std::string& typeName,
                                 std::uint64_t instanceId) {
    if (!object || typeName.empty() || instanceId == 0) return nullptr;
    for (Component* component : object->GetComponents()) {
        if (component && component->GetInstanceID() == instanceId &&
            component->GetTypeName() == typeName) {
            return component;
        }
    }
    return nullptr;
}

Component* FindComponentType(GameObject* object, const std::string& typeName) {
    if (!object) return nullptr;
    for (Component* component : object->GetComponents()) {
        if (component && component->GetTypeName() == typeName) return component;
    }
    return nullptr;
}

bool IsLightingProperty(const std::string& componentType,
                        const std::string& key) {
    if (componentType == "Camera") {
        return key == "lightingEnabled" || key == "ambientIntensity" ||
               key.rfind("ambientColor.", 0) == 0;
    }
    if (componentType == "SpriteRenderer") {
        return key == "lightingMode" || key == "normalMapGuid" ||
               key == "normalStrength";
    }
    return componentType == "TilemapRenderer" && key == "lightingMode";
}

std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

bool CreateTileSetAndConvert(TilemapRenderer& tilemap, std::string& errorOut) {
    namespace fs = std::filesystem;
    errorOut.clear();
    if (tilemap.spriteSheetPath.empty() || tilemap.tileSize <= 0) {
        errorOut = "A legacy sprite sheet and positive tile size are required.";
        return false;
    }

    auto& database = molga::AssetDatabase::Get();
    std::string textureGuid = database.GuidForSource(tilemap.spriteSheetPath);
    if (textureGuid.empty()) {
        textureGuid = database.GuidForAbsolutePath(tilemap.spriteSheetPath);
    }
    if (textureGuid.empty() && Project::Get().IsOpen()) {
        textureGuid = database.GuidForAbsolutePath(
            Project::Get().GetAbsolutePath(tilemap.spriteSheetPath));
    }
    const molga::AssetRecord* textureRecord = database.Find(textureGuid);
    if (!textureRecord || textureRecord->importer != "TextureImporter" ||
        textureRecord->importFailed || textureRecord->textureWidth <= 0 ||
        textureRecord->textureHeight <= 0) {
        errorOut = "The legacy sprite sheet is not a valid imported texture.";
        return false;
    }
    const fs::path texturePath = database.AbsoluteSourcePath(textureGuid);
    if (texturePath.empty()) {
        errorOut = "The sprite sheet source path could not be resolved.";
        return false;
    }

    molga::AssetMeta meta = database.MetaForGuid(textureGuid);
    molga::TextureImportSettings settings =
        molga::DeserializeTextureImportSettings(meta.settings, true);
    settings.spriteMode = molga::SpriteImportMode::Multiple;
    settings.slices = molga::BuildGridSlices(
        textureRecord->textureWidth, textureRecord->textureHeight,
        tilemap.tileSize, tilemap.tileSize, settings.slices,
        settings.defaultPivot);
    if (settings.slices.empty()) {
        errorOut = "The texture dimensions do not contain a complete tile cell.";
        return false;
    }
    // Retain settings owned by newer importers/plugins while replacing the
    // texture keys understood by this editor version.
    if (!meta.settings.is_object()) meta.settings = nlohmann::json::object();
    const nlohmann::json serializedSettings =
        molga::SerializeTextureImportSettings(settings);
    for (auto it = serializedSettings.begin(); it != serializedSettings.end(); ++it) {
        meta.settings[it.key()] = it.value();
    }
    nlohmann::json metaDocument = meta.preserved.is_object()
        ? meta.preserved : nlohmann::json::object();
    metaDocument["guid"] = meta.guid;
    metaDocument["importer"] = meta.importer;
    metaDocument["importerVersion"] = meta.importerVersion;
    metaDocument["settings"] = meta.settings;
    const fs::path metaPath = molga::AssetMeta::MetaPathFor(texturePath);
    const std::string beforeMeta = ReadFileText(metaPath);
    const std::string afterMeta = metaDocument.dump(2) + '\n';
    if (beforeMeta != afterMeta) {
        Editor::Get().GetAssetCommandHistory().Execute(
            std::make_unique<molga::AssetContentCommand>(
                metaPath, beforeMeta, afterMeta, textureGuid));
    }

    molga::TileSetAsset tileSet;
    tileSet.cellWidth = tilemap.tileSize;
    tileSet.cellHeight = tilemap.tileSize;
    tileSet.tiles.reserve(settings.slices.size());
    for (std::size_t index = 0; index < settings.slices.size(); ++index) {
        const bool solid = index < tilemap.solidTiles.size() &&
                           tilemap.solidTiles[index];
        tileSet.tiles.push_back({
            static_cast<int>(index), settings.slices[index].name,
            {textureGuid, settings.slices[index].id}, solid, -1});
    }

    fs::path tileSetPath = texturePath.parent_path() /
        (texturePath.stem().string() + ".tileset");
    int suffix = 1;
    while (fs::exists(tileSetPath)) {
        tileSetPath = texturePath.parent_path() /
            (texturePath.stem().string() + "_" + std::to_string(suffix++) + ".tileset");
    }
    Editor::Get().GetAssetCommandHistory().Execute(
        std::make_unique<molga::ProjectFileCreateCommand>(
            tileSetPath, tileSet.Serialize().dump(2) + '\n', false));
    const std::string tileSetGuid = database.GuidForAbsolutePath(tileSetPath);
    if (!molga::Guid::IsValid(tileSetGuid)) {
        errorOut = "The generated TileSet was not indexed.";
        return false;
    }
    if (!tilemap.ConvertToLayered(tileSetGuid)) {
        errorOut = "The legacy cells could not be converted.";
        return false;
    }
    tilemap.ResolveAssets();
    return true;
}

bool DrawEditorPropertyValue(const molga::EditorPropertyDescriptor& descriptor,
                             const molga::EditorPropertyValue& current,
                             molga::EditorPropertyValue& edited);
bool DrawCameraViewportPresets(CameraViewport& selected);
void DrawCameraOutputWarnings(const std::vector<Camera*>& inspected);

} // namespace

InspectorWindow::InspectorWindow()
    : EditorWindow("Inspector") {
}

void InspectorWindow::SetTarget(GameObject* object) {
    if (!postFxEditGuid_.empty())
        molga::PostProcessProfileResolver::Get().ClearTransientOverride(postFxEditGuid_);
    postFxEditLoaded_ = false;
    postFxEditDirty_ = false;
    postFxEditGuid_.clear();
    postFxEditError_.clear();
    CommitMultiEdit();
    target = object;
    targetId_ = object ? object->GetID() : 0u;
    targetIds_.clear();
    if (object) targetIds_.push_back(object->GetID());
    ClearMultiEdit();
}

void InspectorWindow::SetTargets(const std::vector<GameObject*>& objects) {
    if (!postFxEditGuid_.empty())
        molga::PostProcessProfileResolver::Get().ClearTransientOverride(postFxEditGuid_);
    postFxEditLoaded_ = false;
    postFxEditDirty_ = false;
    postFxEditGuid_.clear();
    postFxEditError_.clear();
    CommitMultiEdit();
    targetIds_.clear();
    for (GameObject* object : objects) {
        if (!object || std::find(targetIds_.begin(), targetIds_.end(), object->GetID()) !=
                           targetIds_.end()) continue;
        targetIds_.push_back(object->GetID());
    }
    target = objects.empty() ? nullptr : objects.back();
    targetId_ = target ? target->GetID() : 0u;
    ClearActiveEdit();
    ClearMultiEdit();
}

void InspectorWindow::SetAssetTarget(const std::string& path) {
    const std::string previousPostFxGuid = postFxEditGuid_;
    CommitMultiEdit();
    target = nullptr;
    targetId_ = 0;
    targetIds_.clear();
    ClearActiveEdit();
    assetPath_ = path;
    assetGuid_ = molga::AssetDatabase::Get().GuidForAbsolutePath(path);
    if (assetGuid_.empty()) {
        assetGuid_ = molga::AssetDatabase::Get().GuidForSource(path);
    }
    if (tileSetEditGuid_ != assetGuid_) {
        tileSetEditLoaded_ = false;
        tileSetEditDirty_ = false;
        tileSetEditError_.clear();
        tileSetEditGuid_ = assetGuid_;
    }
    if (previousPostFxGuid != assetGuid_) {
        if (!previousPostFxGuid.empty()) {
            molga::PostProcessProfileResolver::Get().ClearTransientOverride(
                previousPostFxGuid);
        }
        postFxEditLoaded_ = false;
        postFxEditDirty_ = false;
        postFxEditError_.clear();
        postFxEditGuid_ = assetGuid_;
    }
}

void InspectorWindow::ClearAssetTarget() {
    if (!postFxEditGuid_.empty()) {
        molga::PostProcessProfileResolver::Get().ClearTransientOverride(
            postFxEditGuid_);
    }
    assetPath_.clear();
    assetGuid_.clear();
    postFxEditLoaded_ = false;
    postFxEditDirty_ = false;
    postFxEditGuid_.clear();
    postFxEditError_.clear();
}

void InspectorWindow::ClearActiveEdit() {
    activeEditComponentType_.clear();
    activeEditComponentInstanceId_ = 0;
    activeEditTargetId_ = 0;
    beforeEditSnap_ = nlohmann::json{};
}

void InspectorWindow::ClearMultiEdit() {
    activeMultiEditKey_.clear();
    activeMultiComponentType_.clear();
    activeMultiBaselines_.clear();
}

void InspectorWindow::CommitMultiEdit() {
    if (activeMultiEditKey_.empty() || activeMultiComponentType_.empty()) return;
    std::vector<molga::ComponentSnapshotChange> changes =
        molga::CaptureAppliedComponentChanges(activeMultiBaselines_);
    ClearMultiEdit();
    if (!changes.empty()) {
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::BatchComponentSnapshotCommand>(
                std::move(changes), true));
    }
}

bool InspectorWindow::IsActiveEdit(unsigned int targetId, const Component* component) const {
    return component && activeEditTargetId_ == targetId &&
           activeEditComponentInstanceId_ == component->GetInstanceID() &&
           activeEditComponentType_ == component->GetTypeName();
}

void InspectorWindow::DrawMultiInspector() {
    std::vector<unsigned int> objectIds;
    objectIds.reserve(targetIds_.size());
    for (unsigned int id : targetIds_) {
        if (molga::FindGameObjectById(id)) objectIds.push_back(id);
    }
    if (objectIds.size() < 2) return;

    const molga::EditorComponentResolver resolve =
        [](const molga::EditorComponentIdentity& identity) -> Component* {
            if (!identity.IsValid()) return nullptr;
            GameObject* object = molga::FindGameObjectById(identity.objectId);
            if (!object) return nullptr;
            for (Component* component : object->GetComponents()) {
                if (component &&
                    component->GetRuntimeTypeID() == identity.runtimeTypeId &&
                    component->GetInstanceID() == identity.instanceId &&
                    component->GetTypeName() == identity.componentType) {
                    return component;
                }
            }
            return nullptr;
        };

    const auto captureBaselines =
        [&resolve](const std::vector<molga::EditorComponentIdentity>& identities) {
            std::vector<molga::ComponentSnapshotBaseline> baselines;
            baselines.reserve(identities.size());
            for (const molga::EditorComponentIdentity& identity : identities) {
                Component* component = resolve(identity);
                if (!component) continue;
                nlohmann::json before = molga::CaptureComponentSnapshot(component);
                // Serialize() may replace this component or another selected
                // target. Only retain an exact instance that survived it.
                if (!resolve(identity)) continue;
                baselines.push_back({identity.objectId, identity.runtimeTypeId,
                                     identity.instanceId, identity.componentType,
                                     std::move(before)});
            }
            return baselines;
        };

    const auto commitApplied =
        [](const std::vector<molga::ComponentSnapshotBaseline>& baselines) {
            auto changes = molga::CaptureAppliedComponentChanges(baselines);
            if (changes.empty()) return;
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::BatchComponentSnapshotCommand>(
                    std::move(changes), true));
        };

    bool locked = Editor::Get().GetSelection().IsInspectorLocked();
    if (ImGui::Checkbox("Lock", &locked)) {
        if (locked) Editor::Get().GetSelection().LockInspector();
        else Editor::Get().GetSelection().UnlockInspector();
    }
    ImGui::SameLine();
    ImGui::Text("%zu objects selected", objectIds.size());
    ImGui::TextDisabled("Only common components and value properties are editable.");
    ImGui::Separator();

    std::vector<std::string> commonTypes;
    GameObject* firstObject = molga::FindGameObjectById(objectIds.front());
    if (!firstObject) return;
    for (Component* component : firstObject->GetComponents()) {
        if (!component) continue;
        const std::string type = component->GetTypeName();
        if (std::all_of(objectIds.begin() + 1, objectIds.end(),
                        [&type](unsigned int objectId) {
                            return FindComponentType(
                                molga::FindGameObjectById(objectId), type) != nullptr;
                        })) {
            commonTypes.push_back(type);
        }
    }

    for (const std::string& type : commonTypes) {
        ImGui::PushID(type.c_str());
        const bool open = ImGui::TreeNodeEx(
            "CommonComponent", ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s %s", UIRegistry::GetComponentInfo(type).icon, type.c_str());
        if (open) {
            std::vector<molga::EditorComponentIdentity> identities;
            identities.reserve(objectIds.size());
            for (unsigned int objectId : objectIds) {
                Component* component = FindComponentType(
                    molga::FindGameObjectById(objectId), type);
                if (!component) break;
                identities.push_back(
                    molga::CaptureEditorComponentIdentity(*component));
            }
            if (identities.size() != objectIds.size()) {
                ImGui::TreePop();
                ImGui::PopID();
                continue;
            }

            const auto descriptors =
                molga::CommonEditorProperties(identities, resolve);
            bool lightingHeaderShown = false;
            for (const auto& descriptor : descriptors) {
                if (!descriptor.getter || !descriptor.setter) continue;
                if (!lightingHeaderShown &&
                    IsLightingProperty(type, descriptor.key)) {
                    ImGui::SeparatorText("Lighting");
                    lightingHeaderShown = true;
                }
                ImGui::PushID(descriptor.key.c_str());
                const bool mixed =
                    molga::HasMixedEditorPropertyValue(
                        descriptor, identities, resolve);
                Component* firstComponent = resolve(identities.front());
                if (!firstComponent) {
                    ImGui::PopID();
                    continue;
                }
                const molga::EditorPropertyValue first =
                    descriptor.getter(*firstComponent);
                if (!resolve(identities.front())) {
                    ImGui::PopID();
                    continue;
                }

                const std::vector<molga::ComponentSnapshotBaseline> rowBaselines =
                    captureBaselines(identities);
                if (rowBaselines.size() != identities.size()) {
                    ImGui::PopID();
                    continue;
                }

                molga::EditorPropertyValue edited = first;
                if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                const bool changed = DrawEditorPropertyValue(descriptor, first, edited);
                if (mixed) ImGui::PopItemFlag();

                const bool activated = ImGui::IsItemActivated();
                const bool itemActive = ImGui::IsItemActive();
                const bool finished = ImGui::IsItemDeactivatedAfterEdit();
                if (activated &&
                    descriptor.type != molga::EditorPropertyType::Bool &&
                    descriptor.type != molga::EditorPropertyType::LayerMask) {
                    activeMultiEditKey_ = descriptor.key;
                    activeMultiComponentType_ = type;
                    activeMultiBaselines_ = rowBaselines;
                }
                const bool ownsActiveGesture =
                    activeMultiEditKey_ == descriptor.key &&
                    activeMultiComponentType_ == type;
                if (changed) {
                    molga::ApplyEditorPropertyValue(
                        descriptor, identities, resolve, edited);
                    if (molga::ShouldCommitEditorPropertyImmediately(
                            descriptor.type, true, activated, itemActive,
                            ownsActiveGesture)) {
                        commitApplied(rowBaselines);
                    }
                }
                if (finished && ownsActiveGesture) {
                    CommitMultiEdit();
                }
                ImGui::PopID();
            }
            if (type == "Camera") {
                CameraViewport preset;
                if (DrawCameraViewportPresets(preset)) {
                    const auto presetBaselines = captureBaselines(identities);
                    if (presetBaselines.size() == identities.size()) {
                        for (const auto& identity : identities) {
                            if (auto* camera =
                                    dynamic_cast<Camera*>(resolve(identity))) {
                                camera->SetViewport(preset);
                            }
                        }
                        commitApplied(presetBaselines);
                    }
                }
                std::vector<Camera*> cameras;
                cameras.reserve(identities.size());
                for (const auto& identity : identities) {
                    if (auto* camera = dynamic_cast<Camera*>(resolve(identity)))
                        cameras.push_back(camera);
                }
                DrawCameraOutputWarnings(cameras);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void InspectorWindow::OnGUI() {
    if (!isOpen) {
        CommitMultiEdit();
        return;
    }

    if (!activeMultiEditKey_.empty() && !ImGui::IsAnyItemActive()) {
        CommitMultiEdit();
    }

    // Check if active drag edit has finished
    if (activeEditComponentInstanceId_ != 0 && !ImGui::IsAnyItemActive()) {
        GameObject* editObj = molga::FindGameObjectById(activeEditTargetId_);
        if (editObj) {
            Component* comp = FindComponentInstance(
                editObj, activeEditComponentType_, activeEditComponentInstanceId_);
            if (comp) {
                nlohmann::json afterSnap = molga::CaptureComponentSnapshot(comp);
                if (beforeEditSnap_ != afterSnap) {
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::BatchComponentSnapshotCommand>(
                            std::vector<molga::ComponentSnapshotChange>{
                                {activeEditTargetId_, activeEditComponentType_,
                                 beforeEditSnap_, afterSnap}}, true)
                    );
                }
            }
        }
        ClearActiveEdit();
    }

    if (targetIds_.size() > 1) {
        targetIds_.erase(std::remove_if(targetIds_.begin(), targetIds_.end(),
            [](unsigned int id) { return molga::FindGameObjectById(id) == nullptr; }),
            targetIds_.end());
        if (targetIds_.size() > 1) {
            ImGui::Begin(title.c_str(), &isOpen);
            DrawMultiInspector();
            ImGui::End();
            return;
        }
        if (targetIds_.size() == 1) {
            target = molga::FindGameObjectById(targetIds_.front());
            targetId_ = target ? target->GetID() : 0u;
        }
    }

    // Selection changes normally keep this synchronized, but commands and
    // custom inspectors can delete an object between frames. Validate by the
    // stored value identity before the first member dereference.
    if (target) {
        GameObject* liveTarget = molga::FindGameObjectById(targetId_);
        if (!liveTarget || liveTarget != target) {
            target = nullptr;
            targetId_ = 0;
            ClearActiveEdit();
        }
    }

    ImGui::Begin(title.c_str(), &isOpen);

    if (!target && !assetPath_.empty()) {
        ClearActiveEdit();
        DrawAssetInspector();
        ImGui::End();
        return;
    }

    if (!target) {
        ClearActiveEdit();
        ImGui::Spacing();
        ImGui::TextDisabled("%s No object selected", Icons::Cube);
        ImGui::TextDisabled("Select an object from the Hierarchy");
        ImGui::End();
        return;
    }

    // Lock checkbox
    bool locked = Editor::Get().GetSelection().IsInspectorLocked();
    if (ImGui::Checkbox("Lock", &locked)) {
        if (locked) {
            Editor::Get().GetSelection().LockInspector(target->GetID());
        } else {
            Editor::Get().GetSelection().UnlockInspector();
        }
    }
    ImGui::SameLine();

    // GameObject header with icon
    ImGui::Text("%s", Icons::Cube);
    ImGui::SameLine();

    static char nameBuffer[256];
    strncpy(nameBuffer, target->GetName().c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    static std::string beforeName;
    static bool isEditingName = false;

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
        if (!isEditingName) {
            beforeName = target->GetName();
            isEditingName = true;
        }
        target->SetName(nameBuffer);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        isEditingName = false;
        std::string afterName = target->GetName();
        auto beforeProp = molga::CaptureGameObjectProperties(target);
        beforeProp["name"] = beforeName;
        auto afterProp = molga::CaptureGameObjectProperties(target);
        afterProp["name"] = afterName;

        target->SetName(beforeName); // temporarily revert
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
        );
    } else if (ImGui::IsItemDeactivated()) {
        isEditingName = false;
    }

    ImGui::Spacing();

    // Tag and Layer dropdowns
    auto& settings = ProjectSettings::Get();
    
    // Tag Combo
    std::string currentTag = target->GetTag();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 8.0f);
    if (ImGui::BeginCombo("##TagCombo", ("Tag: " + currentTag).c_str())) {
        for (const auto& t : settings.tags) {
            bool isSelected = (currentTag == t);
            if (ImGui::Selectable(t.c_str(), isSelected)) {
                if (currentTag != t) {
                    auto beforeProp = molga::CaptureGameObjectProperties(target);
                    auto afterProp = beforeProp;
                    afterProp["tag"] = t;
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
                    );
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Add Tag...")) {
            Editor::Get().GetWindowManager().SetVisible(EditorConstants::WIN_PROJECT_SETTINGS, true);
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    
    // Layer Combo
    int currentLayer = target->GetLayer();
    std::string currentLayerName = settings.GetLayerName(currentLayer);
    if (currentLayerName.empty()) {
        currentLayerName = "Layer " + std::to_string(currentLayer);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.0f);
    if (ImGui::BeginCombo("##LayerCombo", ("Layer: " + currentLayerName).c_str())) {
        for (int i = 0; i < 32; ++i) {
            std::string name = settings.GetLayerName(i);
            std::string displayName = name.empty() ? ("Layer " + std::to_string(i) + " (Empty)") : name;
            bool isSelected = (currentLayer == i);
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                if (currentLayer != i) {
                    auto beforeProp = molga::CaptureGameObjectProperties(target);
                    auto afterProp = beforeProp;
                    afterProp["layer"] = i;
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
                    );
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Edit Layers...")) {
            Editor::Get().GetWindowManager().SetVisible(EditorConstants::WIN_PROJECT_SETTINGS, true);
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    bool active = target->IsActive();
    if (ImGui::Checkbox((std::string(Icons::Eye) + " Active").c_str(), &active)) {
        const unsigned int activeTargetId = target->GetID();
        GameObject* const activeTargetIdentity = target;
        auto beforeProp = molga::CaptureGameObjectProperties(target);
        auto afterProp = beforeProp;
        afterProp["active"] = active;
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::GameObjectPropertyCommand>(activeTargetId, beforeProp, afterProp)
        );
        GameObject* liveTarget = molga::FindGameObjectById(activeTargetId);
        if (!liveTarget || liveTarget != activeTargetIdentity) {
            target = liveTarget;
            targetId_ = liveTarget ? activeTargetId : 0u;
            ClearActiveEdit();
            ImGui::End();
            return;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Prefab Header Controls
    if (auto* pi = target->GetComponent<PrefabInstance>()) {
        const unsigned int prefabTargetId = target->GetID();
        GameObject* const prefabTargetIdentity = target;
        bool prefabCommandExecuted = false;
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.28f, 0.48f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.35f, 0.60f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.35f, 0.60f, 1.0f));
        
        bool openPrefabHeader = ImGui::CollapsingHeader((std::string(Icons::Sitemap) + " Prefab Instance").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        
        ImGui::PopStyleColor(3);
        
        if (openPrefabHeader) {
            ImGui::Indent();
            ImGui::TextDisabled("GUID: %s", pi->GetPrefabGuid().c_str());
            
            ImGui::Spacing();
            if (ImGui::Button((std::string(Icons::Save) + " Apply").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ApplyPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Undo) + " Revert").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::RevertPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Times) + " Unpack").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::UnpackPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }

            // Revert can rebuild the instance and Unpack destroys `pi`.
            // Never inspect the old component after one of those commands.
            if (!prefabCommandExecuted) {
                ImGui::Spacing();
                const auto& mods = pi->GetModifications();
                if (!mods.empty()) {
                    ImGui::Text("Overridden Properties (%d):", (int)mods.size());
                    for (const auto& mod : mods) {
                        if (mod.contains("component") && mod.contains("key")) {
                            ImGui::BulletText("%s: %s",
                                mod["component"].get<std::string>().c_str(),
                                mod["key"].get<std::string>().c_str());
                        }
                    }
                } else {
                    ImGui::TextDisabled("No overrides (follows template)");
                }
            }
            
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (prefabCommandExecuted) {
            GameObject* liveTarget = molga::FindGameObjectById(prefabTargetId);
            if (!liveTarget) {
                target = nullptr;
                targetId_ = 0;
                ClearActiveEdit();
                ImGui::End();
                return;
            }
            if (liveTarget != prefabTargetIdentity) ClearActiveEdit();
            target = liveTarget;
            targetId_ = prefabTargetId;
        }
    }

    // Snapshot value identities rather than pointers. Removing one component
    // can synchronously invoke user OnDisable/OnDetach code that removes a
    // different component; re-resolution prevents visiting a dangling entry.
    struct ComponentDrawIdentity {
        std::string typeName;
        std::uint64_t instanceId = 0;
    };
    const unsigned int drawTargetId = target->GetID();
    GameObject* const drawTargetIdentity = target;
    std::vector<ComponentDrawIdentity> drawComponents;
    for (Component* component : target->GetComponents()) {
        if (component) {
            drawComponents.push_back({component->GetTypeName(), component->GetInstanceID()});
        }
    }
    for (const ComponentDrawIdentity& identity : drawComponents) {
        GameObject* liveTarget = molga::FindGameObjectById(drawTargetId);
        if (!liveTarget || liveTarget != drawTargetIdentity) break;
        Component* component = FindComponentInstance(
            liveTarget, identity.typeName, identity.instanceId);
        if (!component) continue;
        target = liveTarget;
        DrawComponent(component);
        ImGui::Spacing();
    }

    // A custom inspector may destroy and flush the whole target object. Do not
    // let the controls below continue through the stale member pointer.
    GameObject* postDrawTarget = molga::FindGameObjectById(drawTargetId);
    if (!postDrawTarget || postDrawTarget != drawTargetIdentity) {
        target = nullptr;
        targetId_ = 0;
        ClearActiveEdit();
        ImGui::End();
        return;
    }
    target = postDrawTarget;

    ImGui::Separator();
    ImGui::Spacing();

    // Centered Add component button
    float buttonWidth = 200.0f;
    float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

    if (ImGui::Button((std::string(Icons::Plus) + " Add Component").c_str(), ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::Text("%s Components", Icons::Cubes);
        ImGui::Separator();

        if (ImGui::MenuItem((std::string(Icons::ArrowsAlt) + " Transform").c_str())) {
            if (!target->HasComponent<Transform>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Transform")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite Renderer").c_str())) {
            if (!target->HasComponent<SpriteRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "SpriteRenderer")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Animator 2D").c_str())) {
            if (!target->HasComponent<Animator2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Animator2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap Renderer").c_str())) {
            if (!target->HasComponent<TilemapRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "TilemapRenderer")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Lightbulb) + " Particle System").c_str())) {
            if (!target->HasComponent<ParticleSystem>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "ParticleSystem")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Square) + " Box Collider 2D").c_str())) {
            if (!target->HasComponent<BoxCollider2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "BoxCollider2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Circle) + " Circle Collider 2D").c_str())) {
            if (!target->HasComponent<CircleCollider2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "CircleCollider2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Cogs) + " Rigidbody 2D").c_str())) {
            if (!target->HasComponent<Rigidbody2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Rigidbody2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Music) + " Audio Source").c_str())) {
            if (!target->HasComponent<AudioSource>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "AudioSource")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::VolumeUp) + " Audio Listener").c_str())) {
            if (!target->HasComponent<AudioListener>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "AudioListener")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Camera) + " Camera").c_str())) {
            if (!target->HasComponent<Camera>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Camera")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Lightbulb) + " Point Light 2D").c_str())) {
            if (!target->HasComponent<PointLight2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(
                        target->GetID(), "PointLight2D"));
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Square) + " Shadow Occluder 2D").c_str())) {
            if (!target->HasComponent<ShadowOccluder2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(
                        target->GetID(), "ShadowOccluder2D"));
            }
        }
        if (ImGui::MenuItem((std::string(Icons::ListUl) + " Text Renderer 2D").c_str())) {
            if (!target->HasComponent<TextRenderer2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "TextRenderer2D")
                );
            }
        }
        if (ImGui::BeginMenu("UI")) {
            auto addUI = [&](const char* label, const char* type) {
                if (ImGui::MenuItem(label)) {
                    bool exists = false;
                    for (auto* c : target->GetComponents()) {
                        if (c && c->GetTypeName() == type) { exists = true; break; }
                    }
                    if (!exists) {
                        Editor::Get().GetCommandHistory().Execute(
                            std::make_unique<molga::ComponentAddCommand>(target->GetID(), type));
                    }
                }
            };
            addUI("Canvas", "UICanvas");
            addUI("Rect Transform", "RectTransform");
            addUI("Image", "UIImage");
            addUI("Label", "UILabel");
            addUI("Button", "UIButton");
            ImGui::EndMenu();
        }
#ifdef MOLGA_MARROW_SUPPORT
        if (ImGui::MenuItem((std::string(Icons::Image) + " Marrow Renderer").c_str())) {
            if (!target->HasComponent<MarrowRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "MarrowRenderer")
                );
            }
        }
#endif
        ImGui::Separator();
        if (ImGui::BeginMenu((std::string(Icons::Code) + " Scripts").c_str())) {
            auto scripts = ScriptManager::Get().GetRegisteredScripts();
            for (const auto& scriptName : scripts) {
                bool hasScript = false;
                for (auto* c : target->GetComponents()) {
                    if (c && c->GetTypeName() == scriptName) {
                        hasScript = true;
                        break;
                    }
                }
                if (!hasScript) {
                    if (ImGui::MenuItem((std::string(Icons::FileCode) + " " + scriptName).c_str())) {
                        Editor::Get().GetCommandHistory().Execute(
                            std::make_unique<molga::ComponentAddCommand>(target->GetID(), scriptName)
                        );
                    }
                }
            }
            if (scripts.empty()) {
                ImGui::TextDisabled("No scripts registered");
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void InspectorWindow::DrawAssetInspector() {
    namespace fs = std::filesystem;
    ImGui::Text("Asset");
    ImGui::Separator();
    ImGui::TextWrapped("%s", assetPath_.c_str());
    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(assetGuid_);
    if (!record) {
        ImGui::TextDisabled("Directory or unindexed asset");
        return;
    }
    ImGui::TextDisabled("GUID %s", record->guid.c_str());
    ImGui::Text("Importer: %s v%d", record->importer.c_str(), record->importerVersion);
    if (record->importFailed) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Import failed: %s",
                           record->importError.c_str());
    }

    auto readText = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream stream;
        stream << input.rdbuf();
        return stream.str();
    };
    auto metaJson = [](const molga::AssetMeta& meta) {
        nlohmann::json json = meta.preserved.is_object()
            ? meta.preserved : nlohmann::json::object();
        json["guid"] = meta.guid;
        json["importer"] = meta.importer;
        json["importerVersion"] = meta.importerVersion;
        json["settings"] = meta.settings;
        return json.dump(2);
    };
    auto commitMeta = [&](const molga::AssetMeta& changed) {
        const fs::path path = molga::AssetMeta::MetaPathFor(assetPath_);
        const std::string before = readText(path);
        const std::string after = metaJson(changed);
        if (before == after) return;
        Editor::Get().GetAssetCommandHistory().Execute(
            std::make_unique<molga::AssetContentCommand>(
                path, before, after, assetGuid_));
    };

    if (ImGui::Button("Reimport")) {
        std::string error;
        molga::AssetDatabase::Get().TryReimport(assetGuid_, &error);
    }
    ImGui::SameLine();
    auto& history = Editor::Get().GetAssetCommandHistory();
    const bool canUndo = history.CanUndo();
    if (!canUndo) ImGui::BeginDisabled();
    if (ImGui::Button("Undo Asset")) {
        history.Undo();
        tileSetEditLoaded_ = false;
        tileSetEditDirty_ = false;
        molga::PostProcessProfileResolver::Get().ClearTransientOverride(assetGuid_);
        postFxEditLoaded_ = false;
        postFxEditDirty_ = false;
    }
    if (!canUndo) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canRedo = history.CanRedo();
    if (!canRedo) ImGui::BeginDisabled();
    if (ImGui::Button("Redo Asset")) {
        history.Redo();
        tileSetEditLoaded_ = false;
        tileSetEditDirty_ = false;
        molga::PostProcessProfileResolver::Get().ClearTransientOverride(assetGuid_);
        postFxEditLoaded_ = false;
        postFxEditDirty_ = false;
    }
    if (!canRedo) ImGui::EndDisabled();

    // Reimport and asset history commands may replace the map value. Never
    // carry the pre-command pointer into the importer-specific inspector.
    record = molga::AssetDatabase::Get().Find(assetGuid_);
    if (!record) {
        ImGui::TextDisabled("Asset is no longer indexed");
        return;
    }

    if (record->importer == "PostProcessProfileImporter") {
        auto& resolver = molga::PostProcessProfileResolver::Get();
        if (!postFxEditLoaded_ || postFxEditGuid_ != assetGuid_) {
            if (!postFxEditGuid_.empty() && postFxEditGuid_ != assetGuid_)
                resolver.ClearTransientOverride(postFxEditGuid_);
            postFxEditGuid_ = assetGuid_;
            postFxEditLoaded_ = molga::PostProcessProfile2D::LoadFromFile(
                assetPath_, postFxEdit_, &postFxEditError_);
            postFxEditDirty_ = false;
        }
        if (!postFxEditLoaded_) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "%s",
                               postFxEditError_.empty()
                                   ? "Could not load post-process profile"
                                   : postFxEditError_.c_str());
            return;
        }

        bool changed = false;
        for (std::size_t index = 0; index < postFxEdit_.effects.size();) {
            auto& effect = postFxEdit_.effects[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SeparatorText(molga::PostProcessEffectTypeName(effect.type));
            std::visit([&](auto& settings) {
                changed |= ImGui::Checkbox("Enabled", &settings.enabled);
                using Settings = std::decay_t<decltype(settings)>;
                if constexpr (std::is_same_v<Settings, molga::BloomSettings2D>) {
                    changed |= ImGui::SliderFloat("Threshold", &settings.threshold,
                                                  0.0f, 16.0f);
                    changed |= ImGui::SliderFloat("Soft Knee", &settings.softKnee,
                                                  0.0f, 1.0f);
                    changed |= ImGui::SliderFloat("Intensity", &settings.intensity,
                                                  0.0f, 10.0f);
                    changed |= ImGui::SliderFloat("Scatter", &settings.scatter,
                                                  0.0f, 1.0f);
                } else if constexpr (std::is_same_v<Settings,
                                                    molga::ColorAdjustSettings2D>) {
                    changed |= ImGui::SliderFloat("Exposure EV", &settings.exposureEV,
                                                  -8.0f, 8.0f);
                    changed |= ImGui::SliderFloat("Contrast", &settings.contrast,
                                                  -1.0f, 1.0f);
                    changed |= ImGui::SliderFloat("Saturation", &settings.saturation,
                                                  0.0f, 2.0f);
                    changed |= ImGui::DragFloat3("Tint", settings.tint, 0.01f,
                                                0.0f, 4.0f);
                } else {
                    changed |= ImGui::SliderFloat("Intensity", &settings.intensity,
                                                  0.0f, 1.0f);
                    changed |= ImGui::SliderFloat("Smoothness", &settings.smoothness,
                                                  0.01f, 1.0f);
                    changed |= ImGui::ColorEdit3("Color", settings.color);
                }
            }, effect.settings);

            bool remove = ImGui::Button("Remove");
            ImGui::SameLine();
            const bool first = index == 0;
            if (first) ImGui::BeginDisabled();
            const bool moveUp = ImGui::Button("Up");
            if (first) ImGui::EndDisabled();
            ImGui::SameLine();
            const bool last = index + 1 == postFxEdit_.effects.size();
            if (last) ImGui::BeginDisabled();
            const bool moveDown = ImGui::Button("Down");
            if (last) ImGui::EndDisabled();
            ImGui::PopID();

            if (remove) {
                postFxEdit_.effects.erase(postFxEdit_.effects.begin() +
                    static_cast<std::ptrdiff_t>(index));
                changed = true;
                continue;
            }
            if (moveUp && index > 0) {
                std::swap(postFxEdit_.effects[index - 1],
                          postFxEdit_.effects[index]);
                changed = true;
            } else if (moveDown && index + 1 < postFxEdit_.effects.size()) {
                std::swap(postFxEdit_.effects[index],
                          postFxEdit_.effects[index + 1]);
                changed = true;
            }
            ++index;
        }

        const auto hasType = [&](molga::PostProcessEffectType2D type) {
            return std::any_of(postFxEdit_.effects.begin(), postFxEdit_.effects.end(),
                [type](const auto& effect) { return effect.type == type; });
        };
        ImGui::SeparatorText("Add Effect");
        const auto addEffect = [&](const char* label,
                                   molga::PostProcessEffectType2D type) {
            const bool exists = hasType(type);
            if (exists) ImGui::BeginDisabled();
            const bool add = ImGui::Button(label);
            if (exists) ImGui::EndDisabled();
            if (!add) return false;
            molga::PostProcessEffect2D effect;
            effect.type = type;
            switch (type) {
                case molga::PostProcessEffectType2D::Bloom:
                    effect.settings = molga::BloomSettings2D{}; break;
                case molga::PostProcessEffectType2D::ColorAdjust:
                    effect.settings = molga::ColorAdjustSettings2D{}; break;
                case molga::PostProcessEffectType2D::Vignette:
                    effect.settings = molga::VignetteSettings2D{}; break;
            }
            postFxEdit_.effects.push_back(std::move(effect));
            return true;
        };
        changed |= addEffect("Bloom", molga::PostProcessEffectType2D::Bloom);
        ImGui::SameLine();
        changed |= addEffect("Color Adjust",
                             molga::PostProcessEffectType2D::ColorAdjust);
        ImGui::SameLine();
        changed |= addEffect("Vignette", molga::PostProcessEffectType2D::Vignette);

        if (changed) {
            postFxEditDirty_ = true;
            resolver.SetTransientOverride(assetGuid_, postFxEdit_, &postFxEditError_);
        }
        if (postFxEditDirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "Unsaved post-process changes (previewing)");
        }
        if (!postFxEditError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "%s",
                               postFxEditError_.c_str());
        }
        const bool canSaveProfile = postFxEditDirty_;
        if (!canSaveProfile) ImGui::BeginDisabled();
        if (ImGui::Button("Save Profile")) {
            molga::PostProcessProfile2D validated;
            const nlohmann::json document = postFxEdit_.Serialize();
            if (molga::PostProcessProfile2D::Deserialize(
                    document, validated, &postFxEditError_)) {
                const std::string before = readText(assetPath_);
                const std::string after = document.dump(2) + '\n';
                if (before != after) {
                    Editor::Get().GetAssetCommandHistory().Execute(
                        std::make_unique<molga::AssetContentCommand>(
                            assetPath_, before, after, assetGuid_));
                }
                resolver.ClearTransientOverride(assetGuid_);
                postFxEditDirty_ = false;
                postFxEditLoaded_ = false;
            }
        }
        if (!canSaveProfile) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Revert Profile")) {
            resolver.ClearTransientOverride(assetGuid_);
            postFxEditLoaded_ = molga::PostProcessProfile2D::LoadFromFile(
                assetPath_, postFxEdit_, &postFxEditError_);
            postFxEditDirty_ = false;
        }
        return;
    }

    if (record->importer == "TileSetImporter") {
        if (!tileSetEditLoaded_ || tileSetEditGuid_ != assetGuid_) {
            tileSetEditGuid_ = assetGuid_;
            tileSetEditLoaded_ = true;
            tileSetEditDirty_ = false;
            tileSetEditError_.clear();
            tileSetEdit_ = molga::TileSetAsset{};
            tileSetEdit_.LoadFromFile(assetPath_, &tileSetEditError_);
        }
        auto& tileSet = tileSetEdit_;
        int cellSize[2] = {tileSet.cellWidth, tileSet.cellHeight};
        if (ImGui::DragInt2("Cell Size", cellSize, 1.0f, 1, 8192)) {
            tileSet.cellWidth = cellSize[0];
            tileSet.cellHeight = cellSize[1];
            tileSetEditDirty_ = true;
        }

        ImGui::SeparatorText("Tiles");
        for (std::size_t index = 0; index < tileSet.tiles.size();) {
            auto& tile = tileSet.tiles[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("Tile %d", tile.id);
            char name[128];
            std::strncpy(name, tile.name.c_str(), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (ImGui::InputText("Name", name, sizeof(name))) {
                tile.name = name;
                tileSetEditDirty_ = true;
            }
            if (ImGui::InputInt("ID", &tile.id)) tileSetEditDirty_ = true;
            char texture[128];
            std::strncpy(texture, tile.sprite.textureGuid.c_str(), sizeof(texture) - 1);
            texture[sizeof(texture) - 1] = '\0';
            if (ImGui::InputText("Texture GUID", texture, sizeof(texture))) {
                tile.sprite.textureGuid = texture;
                tileSetEditDirty_ = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    const char* data = static_cast<const char*>(payload->Data);
                    const std::string guid = data ? data : "";
                    const auto* textureRecord = molga::AssetDatabase::Get().Find(guid);
                    if (textureRecord && textureRecord->importer == "TextureImporter") {
                        tile.sprite.textureGuid = guid;
                        tileSetEditDirty_ = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            const auto* textureRecord =
                molga::AssetDatabase::Get().Find(tile.sprite.textureGuid);
            std::vector<molga::SpriteSlice> slices;
            if (textureRecord && textureRecord->importer == "TextureImporter") {
                slices = molga::DeserializeTextureImportSettings(
                    textureRecord->settings, true).slices;
            }
            const auto selectedSlice = std::find_if(slices.begin(), slices.end(),
                [&](const auto& slice) { return slice.id == tile.sprite.sliceId; });
            const std::string slicePreview = selectedSlice == slices.end()
                ? (tile.sprite.sliceId.empty() ? "(full texture)" : tile.sprite.sliceId)
                : selectedSlice->name;
            if (!slices.empty() && ImGui::BeginCombo("Slice", slicePreview.c_str())) {
                for (const auto& slice : slices) {
                    if (ImGui::Selectable((slice.name + "##" + slice.id).c_str(),
                                          slice.id == tile.sprite.sliceId)) {
                        tile.sprite.sliceId = slice.id;
                        tileSetEditDirty_ = true;
                    }
                }
                ImGui::EndCombo();
            } else if (slices.empty()) {
                char slice[128];
                std::strncpy(slice, tile.sprite.sliceId.c_str(), sizeof(slice) - 1);
                slice[sizeof(slice) - 1] = '\0';
                if (ImGui::InputText("Slice ID", slice, sizeof(slice))) {
                    tile.sprite.sliceId = slice;
                    tileSetEditDirty_ = true;
                }
            }
            if (ImGui::Checkbox("Solid", &tile.solid)) tileSetEditDirty_ = true;
            if (ImGui::InputInt("Terrain ID", &tile.terrainId)) tileSetEditDirty_ = true;
            const bool remove = ImGui::Button("Remove Tile");
            ImGui::Separator();
            ImGui::PopID();
            if (remove) {
                const int removedId = tile.id;
                tileSet.tiles.erase(tileSet.tiles.begin() +
                                    static_cast<std::ptrdiff_t>(index));
                tileSet.terrainRules.erase(
                    std::remove_if(tileSet.terrainRules.begin(), tileSet.terrainRules.end(),
                        [&](const auto& rule) { return rule.tileId == removedId; }),
                    tileSet.terrainRules.end());
                tileSetEditDirty_ = true;
            } else {
                ++index;
            }
        }
        if (ImGui::Button("Add Tile")) {
            int nextId = 0;
            for (const auto& tile : tileSet.tiles) nextId = std::max(nextId, tile.id + 1);
            tileSet.tiles.push_back({nextId, "Tile " + std::to_string(nextId), {}, false, -1});
            tileSetEditDirty_ = true;
        }

        ImGui::SeparatorText("NESW Terrain Rules");
        for (std::size_t index = 0; index < tileSet.terrainRules.size();) {
            auto& rule = tileSet.terrainRules[index];
            ImGui::PushID(100000 + static_cast<int>(index));
            if (ImGui::InputInt("Terrain", &rule.terrainId)) tileSetEditDirty_ = true;
            if (ImGui::SliderInt("Mask (NESW)", &rule.mask, 0, 15)) tileSetEditDirty_ = true;
            if (ImGui::InputInt("Tile ID", &rule.tileId)) tileSetEditDirty_ = true;
            const bool remove = ImGui::Button("Remove Rule");
            ImGui::Separator();
            ImGui::PopID();
            if (remove) {
                tileSet.terrainRules.erase(tileSet.terrainRules.begin() +
                    static_cast<std::ptrdiff_t>(index));
                tileSetEditDirty_ = true;
            } else {
                ++index;
            }
        }
        if (!tileSet.tiles.empty() && ImGui::Button("Add Terrain Rule")) {
            tileSet.terrainRules.push_back({0, 0, tileSet.tiles.front().id});
            tileSetEditDirty_ = true;
        }

        if (tileSetEditDirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Unsaved TileSet changes");
        }
        if (!tileSetEditError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "%s",
                               tileSetEditError_.c_str());
        }
        if (ImGui::Button("Save TileSet")) {
            molga::TileSetAsset validated;
            const nlohmann::json document = tileSet.Serialize();
            if (validated.Deserialize(document, &tileSetEditError_)) {
                const std::string before = readText(assetPath_);
                const std::string after = document.dump(2) + '\n';
                if (before != after) {
                    Editor::Get().GetAssetCommandHistory().Execute(
                        std::make_unique<molga::AssetContentCommand>(
                            assetPath_, before, after, assetGuid_));
                }
                tileSetEditDirty_ = false;
                tileSetEditError_.clear();
            }
        }
        return;
    }

    if (record->importer != "TextureImporter") {
        if (!record->metadata.empty()) {
            ImGui::TextWrapped("Metadata: %s", record->metadata.dump().c_str());
        }
        if (!record->dependencies.empty()) {
            ImGui::Text("Dependencies");
            for (const auto& dependency : record->dependencies) {
                ImGui::BulletText("%s", dependency.c_str());
            }
        }
        return;
    }

    molga::AssetMeta meta = molga::AssetDatabase::Get().MetaForGuid(assetGuid_);
    molga::TextureImportSettings settings =
        molga::DeserializeTextureImportSettings(meta.settings, true);
    bool changed = false;
    int usage = settings.usage == molga::TextureUsage::Color ? 0 : 1;
    const char* usages[] = {"Color", "Normal Map"};
    if (ImGui::Combo("Usage", &usage, usages, 2)) {
        settings.usage = usage == 0 ? molga::TextureUsage::Color
                                    : molga::TextureUsage::NormalMap;
        if (settings.usage == molga::TextureUsage::NormalMap)
            settings.colorSpace = molga::TextureColorSpace::LegacyLinear;
        changed = true;
    }
    int filter = settings.filter == molga::TextureFilterMode::Nearest ? 0 : 1;
    const char* filters[] = {"Nearest", "Linear"};
    if (ImGui::Combo("Filter", &filter, filters, 2)) {
        settings.filter = filter == 0 ? molga::TextureFilterMode::Nearest
                                      : molga::TextureFilterMode::Linear;
        changed = true;
    }
    auto drawWrap = [&](const char* label, molga::TextureWrapMode& value) {
        int selected = value == molga::TextureWrapMode::Clamp ? 0
            : value == molga::TextureWrapMode::Repeat ? 1 : 2;
        const char* options[] = {"Clamp", "Repeat", "Mirrored Repeat"};
        if (ImGui::Combo(label, &selected, options, 3)) {
            value = selected == 0 ? molga::TextureWrapMode::Clamp
                : selected == 1 ? molga::TextureWrapMode::Repeat
                                : molga::TextureWrapMode::MirroredRepeat;
            changed = true;
        }
    };
    drawWrap("Wrap U", settings.wrapU);
    drawWrap("Wrap V", settings.wrapV);
    changed |= ImGui::Checkbox("Mipmaps", &settings.mipmaps);
    int colorSpace = settings.colorSpace == molga::TextureColorSpace::LegacyLinear ? 0 : 1;
    const char* spaces[] = {"Legacy Linear", "sRGB"};
    const bool normalMap = settings.usage == molga::TextureUsage::NormalMap;
    if (normalMap) ImGui::BeginDisabled();
    if (ImGui::Combo("Color Space", &colorSpace, spaces, 2)) {
        settings.colorSpace = colorSpace == 0 ? molga::TextureColorSpace::LegacyLinear
                                               : molga::TextureColorSpace::SRGB;
        changed = true;
    }
    if (normalMap) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Normal maps are always imported as linear data.");
    }
    changed |= ImGui::DragFloat("Pixels Per Unit", &settings.pixelsPerUnit,
                                0.1f, 0.001f, 100000.0f);
    int spriteMode = settings.spriteMode == molga::SpriteImportMode::Single ? 0 : 1;
    const char* modes[] = {"Single", "Multiple"};
    if (ImGui::Combo("Sprite Mode", &spriteMode, modes, 2)) {
        settings.spriteMode = spriteMode == 0 ? molga::SpriteImportMode::Single
                                               : molga::SpriteImportMode::Multiple;
        changed = true;
    }
    float pivot[2] = {settings.defaultPivot.x, settings.defaultPivot.y};
    if (ImGui::DragFloat2("Default Pivot", pivot, 0.01f, 0.0f, 1.0f)) {
        settings.defaultPivot = {pivot[0], pivot[1]};
        changed = true;
    }

    if (settings.spriteMode == molga::SpriteImportMode::Multiple) {
        static std::string slicingAsset;
        static int grid[2] = {32, 32};
        if (slicingAsset != assetGuid_) {
            slicingAsset = assetGuid_;
            grid[0] = grid[1] = 32;
        }
        ImGui::DragInt2("Grid Cell", grid, 1.0f, 1, 8192);
        if (ImGui::Button("Slice Grid (preserve IDs)")) {
            settings.slices = molga::BuildGridSlices(
                record->textureWidth, record->textureHeight, grid[0], grid[1],
                settings.slices, settings.defaultPivot);
            changed = true;
        }
        ImGui::Text("Slices: %zu", settings.slices.size());
        for (std::size_t index = 0; index < settings.slices.size(); ++index) {
            auto& slice = settings.slices[index];
            ImGui::PushID(static_cast<int>(index));
            char name[128];
            std::strncpy(name, slice.name.c_str(), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (ImGui::InputText("Name", name, sizeof(name))) {
                slice.name = name;
                changed = true;
            }
            int rect[4] = {slice.pixelRect.x, slice.pixelRect.y,
                           slice.pixelRect.width, slice.pixelRect.height};
            if (ImGui::DragInt4("Pixel Rect", rect, 1.0f, 0, 16384)) {
                slice.pixelRect = {rect[0], rect[1], rect[2], rect[3]};
                changed = true;
            }
            float slicePivot[2] = {slice.pivot.x, slice.pivot.y};
            if (ImGui::DragFloat2("Pivot", slicePivot, 0.01f, 0.0f, 1.0f)) {
                slice.pivot = {slicePivot[0], slicePivot[1]};
                changed = true;
            }
            ImGui::TextDisabled("ID %s", slice.id.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
    }
    if (changed) {
        settings.Sanitize();
        // Preserve importer/plugin keys this editor version does not know.
        nlohmann::json serialized = molga::SerializeTextureImportSettings(settings);
        if (!meta.settings.is_object()) meta.settings = nlohmann::json::object();
        for (auto it = serialized.begin(); it != serialized.end(); ++it) {
            meta.settings[it.key()] = it.value();
        }
        commitMeta(meta);
    }
}

namespace {

bool AcceptAssetGuidDrop(const char* expectedImporter, std::string& outGuid);

bool DrawEditorPropertyValue(const molga::EditorPropertyDescriptor& descriptor,
                             const molga::EditorPropertyValue& current,
                             molga::EditorPropertyValue& edited) {
    switch (descriptor.type) {
        case molga::EditorPropertyType::Bool: {
            bool value = std::get<bool>(current);
            const bool changed = ImGui::Checkbox(descriptor.label.c_str(), &value);
            edited = value;
            return changed;
        }
        case molga::EditorPropertyType::Integer: {
            std::int64_t value = std::get<std::int64_t>(current);
            const bool changed = ImGui::DragScalar(
                descriptor.label.c_str(), ImGuiDataType_S64, &value, 1.0f);
            edited = value;
            return changed;
        }
        case molga::EditorPropertyType::Float: {
            float value = static_cast<float>(std::get<double>(current));
            const bool changed = ImGui::DragFloat(
                descriptor.label.c_str(), &value, 0.1f);
            edited = static_cast<double>(value);
            return changed;
        }
        case molga::EditorPropertyType::Enum: {
            if (descriptor.enumLabels.empty() ||
                descriptor.enumLabels.size() != descriptor.enumValues.size()) return false;
            int selected = 0;
            for (std::size_t index = 0; index < descriptor.enumValues.size(); ++index) {
                if (molga::EditorPropertyValuesEqual(
                        descriptor, current, descriptor.enumValues[index])) {
                    selected = static_cast<int>(index);
                    break;
                }
            }
            if (!ImGui::Combo(descriptor.label.c_str(), &selected,
                              [](void* data, int index) -> const char* {
                                  const auto* labels =
                                      static_cast<const std::vector<std::string>*>(data);
                                  if (index < 0 || static_cast<std::size_t>(index) >= labels->size())
                                      return nullptr;
                                  return (*labels)[static_cast<std::size_t>(index)].c_str();
                              }, const_cast<std::vector<std::string>*>(&descriptor.enumLabels),
                              static_cast<int>(descriptor.enumLabels.size()))) {
                return false;
            }
            edited = descriptor.enumValues[static_cast<std::size_t>(selected)];
            return true;
        }
        case molga::EditorPropertyType::LayerMask: {
            const std::int64_t authored = std::get<std::int64_t>(current);
            std::uint32_t mask = static_cast<std::uint32_t>(authored);
            std::string preview;
            if (mask == 0u) {
                preview = "Nothing";
            } else if (mask == 0xFFFFFFFFu) {
                preview = "All";
            } else {
                int selectedLayers = 0;
                std::string onlyLayer;
                for (int layer = 0; layer < 32; ++layer) {
                    if ((mask & (std::uint32_t{1} << layer)) == 0u) continue;
                    ++selectedLayers;
                    const std::string name = ProjectSettings::Get().GetLayerName(layer);
                    onlyLayer = name.empty() ? "Layer " + std::to_string(layer) : name;
                }
                preview = selectedLayers == 1
                    ? onlyLayer : std::to_string(selectedLayers) + " Layers";
            }

            bool changed = false;
            if (ImGui::BeginCombo(descriptor.label.c_str(), preview.c_str())) {
                if (ImGui::Selectable("All", mask == 0xFFFFFFFFu)) {
                    mask = 0xFFFFFFFFu;
                    changed = true;
                }
                if (ImGui::Selectable("Nothing", mask == 0u)) {
                    mask = 0u;
                    changed = true;
                }
                ImGui::Separator();
                for (int layer = 0; layer < 32; ++layer) {
                    ImGui::PushID(layer);
                    const std::uint32_t bit = std::uint32_t{1} << layer;
                    bool included = (mask & bit) != 0u;
                    const std::string configured =
                        ProjectSettings::Get().GetLayerName(layer);
                    const std::string label = std::to_string(layer) + ": " +
                        (configured.empty() ? "(Unnamed)" : configured);
                    if (ImGui::Checkbox(label.c_str(), &included)) {
                        if (included) mask |= bit;
                        else mask &= ~bit;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            edited = static_cast<std::int64_t>(mask);
            return changed;
        }
        case molga::EditorPropertyType::String:
        case molga::EditorPropertyType::AssetGuid: {
            char buffer[512]{};
            const std::string& value = std::get<std::string>(current);
            std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
            if (descriptor.type == molga::EditorPropertyType::AssetGuid &&
                !descriptor.assetType.empty()) {
                const molga::AssetRecord* record = value.empty()
                    ? nullptr : molga::AssetDatabase::Get().Find(value);
                if (!value.empty() && (!record || record->importFailed ||
                                       record->importer != descriptor.assetType ||
                                       (!descriptor.assetUsage.empty() &&
                                        record->settings.value(
                                            "usage", std::string{"Color"}) !=
                                            descriptor.assetUsage))) {
                    const char* reason = !record ? "missing GUID" :
                        record->importFailed ? "asset import failed" :
                        record->importer != descriptor.assetType
                            ? "asset type mismatch" : "texture usage mismatch";
                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                                       "Warning: %s (reference preserved)", reason);
                }
            }
            bool changed = ImGui::InputText(descriptor.label.c_str(), buffer,
                                            sizeof(buffer));
            edited = std::string(buffer);
            if (descriptor.type == molga::EditorPropertyType::AssetGuid &&
                !descriptor.assetType.empty()) {
                std::string dropped;
                if (AcceptAssetGuidDrop(descriptor.assetType.c_str(), dropped)) {
                    molga::EditorPropertyValue candidate = dropped;
                    if (molga::IsEditorPropertyValueValid(descriptor, candidate)) {
                        edited = std::move(dropped);
                        changed = true;
                    }
                }
            }
            return changed && molga::IsEditorPropertyValueValid(descriptor, edited);
        }
    }
    return false;
}

bool DrawCameraViewportPresets(CameraViewport& selected) {
    struct Preset {
        const char* label;
        CameraViewport viewport;
    };
    static constexpr Preset presets[] = {
        {"Full", {0.0f, 0.0f, 1.0f, 1.0f}},
        {"Left", {0.0f, 0.0f, 0.5f, 1.0f}},
        {"Right", {0.5f, 0.0f, 0.5f, 1.0f}},
        {"Top", {0.0f, 0.0f, 1.0f, 0.5f}},
        {"Bottom", {0.0f, 0.5f, 1.0f, 0.5f}},
        {"PIP", {0.70f, 0.05f, 0.25f, 0.25f}},
    };

    ImGui::SeparatorText("Viewport Presets");
    for (std::size_t index = 0; index < std::size(presets); ++index) {
        if (index != 0) ImGui::SameLine();
        if (ImGui::Button(presets[index].label)) {
            selected = presets[index].viewport;
            return true;
        }
    }
    return false;
}

molga::PixelSize CameraInspectorLogicalSize() {
    if (Project::Get().IsOpen()) {
        const auto& window = Project::Get().GetBuildProfile().window;
        if (window.width > 0 && window.height > 0)
            return {window.width, window.height};
    }
    return {800, 600};
}

bool CameraViewportRoundsAway(const CameraViewport& viewport,
                              molga::PixelSize logicalSize) {
    if (!logicalSize.IsValid()) return false;
    const int left = static_cast<int>(std::floor(
        static_cast<double>(viewport.x) * logicalSize.width));
    const int right = static_cast<int>(std::floor(
        static_cast<double>(viewport.x + viewport.width) * logicalSize.width));
    const int top = static_cast<int>(std::floor(
        static_cast<double>(viewport.y) * logicalSize.height));
    const int bottom = static_cast<int>(std::floor(
        static_cast<double>(viewport.y + viewport.height) * logicalSize.height));
    return right <= left || bottom <= top;
}

void DrawCameraOutputWarnings(const std::vector<Camera*>& inspected) {
    std::size_t primaryCount = 0;
    std::size_t secondaryCount = 0;
    if (const auto* objects = Editor::Get().GetGameObjects()) {
        for (const auto& object : *objects) {
            if (!object || !object->IsActive()) continue;
            Camera* camera = object->GetComponent<Camera>();
            if (!camera || !camera->IsEnabled() ||
                camera->GetOutputRole() == CameraOutputRole::Disabled) {
                continue;
            }
            if (camera->GetOutputRole() == CameraOutputRole::Primary) {
                ++primaryCount;
            } else {
                ++secondaryCount;
            }
        }
    }
    const std::size_t participantCount = secondaryCount +
        (primaryCount > 0u ? 1u : 0u);

    const ImVec4 warningColor(1.0f, 0.65f, 0.2f, 1.0f);
    if (primaryCount > 1u) {
        ImGui::TextColored(warningColor,
            "Warning: %zu Primary cameras; only the highest-depth Primary outputs.",
            primaryCount);
    }
    if (participantCount > 8u) {
        ImGui::TextColored(warningColor,
            "Warning: %zu output cameras; lower-priority cameras above the 8-camera limit are excluded.",
            participantCount);
    }

    const molga::PixelSize logicalSize = CameraInspectorLogicalSize();
    std::size_t subpixelCount = 0;
    std::size_t invalidProfileCount = 0;
    for (const Camera* camera : inspected) {
        if (!camera) continue;
        if (camera->GetOutputRole() != CameraOutputRole::Disabled &&
            CameraViewportRoundsAway(camera->GetViewport(), logicalSize)) {
            ++subpixelCount;
        }
        if (!camera->IsPostProcessEnabled()) continue;
        const std::string& guid = camera->GetPostProcessProfileGuid();
        if (guid.empty()) {
            ++invalidProfileCount;
            continue;
        }
        const molga::PostProcessProfileResolveResult resolved =
            molga::PostProcessProfileResolver::Get().Resolve(guid);
        if (resolved.status != molga::PostProcessProfileResolveStatus::Resolved &&
            resolved.status !=
                molga::PostProcessProfileResolveStatus::TransientPreview) {
            ++invalidProfileCount;
        }
    }
    if (subpixelCount > 0u) {
        ImGui::TextColored(warningColor,
            "Warning: %zu viewport(s) collapse below one logical pixel at %dx%d and will be skipped.",
            subpixelCount, logicalSize.width, logicalSize.height);
    }
    if (invalidProfileCount > 0u) {
        ImGui::TextColored(warningColor,
            "Warning: %zu enabled PostFX profile(s) are missing or invalid.",
            invalidProfileCount);
    }
}

bool AcceptAssetGuidDrop(const char* expectedImporter, std::string& outGuid) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool accepted = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
        const auto* data = static_cast<const char*>(payload->Data);
        const std::size_t size = payload->DataSize > 0
            ? static_cast<std::size_t>(payload->DataSize - 1) : 0u;
        const std::string guid(data ? data : "", size);
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
        if (record && record->importer == expectedImporter) {
            outGuid = guid;
            accepted = true;
        }
    }
    ImGui::EndDragDropTarget();
    return accepted;
}

// Single-selection-only editors for structural collections and runtime preview
// controls. Scalar values are intentionally absent: those are rendered by the
// shared EditorPropertyDescriptor path above for both single and multi edit.
void DrawComponentStructureEditors(Component* component) {
    if (auto* camera = dynamic_cast<Camera*>(component)) {
        CameraViewport viewport;
        if (DrawCameraViewportPresets(viewport)) camera->SetViewport(viewport);
        DrawCameraOutputWarnings({camera});
        return;
    }

    if (auto* occluder = dynamic_cast<ShadowOccluder2D*>(component)) {
        ImGui::SeparatorText("Occluder Shape");
        if (!occluder->IsShapeValid()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
                "The serialized polygon is invalid and is excluded at runtime.");
            if (ImGui::Button("Recover 100 x 100 Box"))
                occluder->ResetToDefaultBox();
            return;
        }

        if (occluder->GetShape() == ShadowOccluderShape2D::Polygon) {
            std::vector<Vector2> edited = occluder->GetVertices();
            bool changed = false;
            for (std::size_t index = 0; index < edited.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                float vertex[2] = {edited[index].x, edited[index].y};
                if (ImGui::DragFloat2("Vertex", vertex, 0.5f)) {
                    edited[index] = {vertex[0], vertex[1]};
                    changed = true;
                }
                ImGui::PopID();
            }
            if (changed && !occluder->SetPolygon(edited)) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.15f, 1.0f),
                    "Polygon must remain finite, strict convex, CCW-normalizable, and non-zero.");
            }
            if (ImGui::Button("Triangle Preset")) {
                occluder->SetPolygon({
                    {0.0f, -50.0f}, {50.0f, 50.0f}, {-50.0f, 50.0f}});
            }
            ImGui::SameLine();
            if (ImGui::Button("Rectangle Preset")) {
                occluder->SetPolygon({
                    {-50.0f, -50.0f}, {50.0f, -50.0f},
                    {50.0f, 50.0f}, {-50.0f, 50.0f}});
            }
        }
        return;
    }

    if (auto* sprite = dynamic_cast<SpriteRenderer*>(component)) {
        const molga::SpriteRef& reference = sprite->GetAuthoredSpriteRef();
        const Vector2 size = sprite->GetSize();
        const Vector2 pivot = sprite->GetPivot();
        ImGui::TextDisabled("Sprite: %s / %s", reference.textureGuid.empty()
            ? "(none)" : reference.textureGuid.c_str(), reference.sliceId.empty()
            ? "(whole texture)" : reference.sliceId.c_str());
        ImGui::TextDisabled("Resolved %.2f x %.2f, pivot %.2f %.2f",
                            size.x, size.y, pivot.x, pivot.y);
        const bool usesUnsupportedCustomMaterial =
            (sprite->material.shaderName != "default" &&
             sprite->material.shaderName != "batch") ||
            !sprite->material.properties.empty();
        if (sprite->GetLightingMode() == SpriteLightingMode2D::Lit &&
            usesUnsupportedCustomMaterial) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.15f, 1.0f),
                "Lit mode does not support custom material shaders; "
                "this Sprite renders Unlit.");
        }
        return;
    }

    if (auto* animator = dynamic_cast<Animator2D*>(component)) {
        ImGui::SeparatorText("Runtime Preview");
        if (ImGui::Button("Play")) animator->Play();
        ImGui::SameLine();
        if (ImGui::Button("Pause")) animator->Pause();
        ImGui::SameLine();
        if (ImGui::Button("Resume")) animator->Resume();
        ImGui::SameLine();
        if (ImGui::Button("Stop")) animator->Stop();
        ImGui::TextDisabled("State: %s  time: %.3f  frame: %zu",
            animator->GetCurrentStateName().empty()
                ? "(none)" : animator->GetCurrentStateName().c_str(),
            animator->GetNormalizedTime(), animator->GetCurrentFrameIndex());
        if (const auto* controller = animator->GetController()) {
            if (ImGui::TreeNode("Runtime Parameters")) {
                for (const auto& parameter : controller->GetParameters()) {
                    ImGui::PushID(parameter.name.c_str());
                    using Type = molga::AnimatorParameterType2D;
                    if (parameter.type == Type::Bool) {
                        bool value = animator->GetBool(parameter.name);
                        if (ImGui::Checkbox(parameter.name.c_str(), &value))
                            animator->SetBool(parameter.name, value);
                    } else if (parameter.type == Type::Int) {
                        int value = animator->GetInt(parameter.name);
                        if (ImGui::InputInt(parameter.name.c_str(), &value))
                            animator->SetInt(parameter.name, value);
                    } else if (parameter.type == Type::Float) {
                        float value = animator->GetFloat(parameter.name);
                        if (ImGui::DragFloat(parameter.name.c_str(), &value, 0.05f))
                            animator->SetFloat(parameter.name, value);
                    } else {
                        const bool set = animator->IsTriggerSet(parameter.name);
                        const std::string label = (set ? "Reset " : "Set ") + parameter.name;
                        if (ImGui::Button(label.c_str())) {
                            if (set) animator->ResetTrigger(parameter.name);
                            else animator->SetTrigger(parameter.name);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        return;
    }

    if (auto* tilemap = dynamic_cast<TilemapRenderer*>(component)) {
        ImGui::SeparatorText("Tilemap Structure");
        if (!tilemap->IsLayered()) {
            ImGui::TextDisabled("Legacy single-layer tilemap");
            static std::uint64_t conversionOwner = 0;
            static std::string conversionError;
            if (conversionOwner != tilemap->GetInstanceID()) {
                conversionOwner = tilemap->GetInstanceID();
                conversionError.clear();
            }
            if (ImGui::Button("Create TileSet & Convert"))
                CreateTileSetAndConvert(*tilemap, conversionError);
            const bool canConvert = molga::Guid::IsValid(tilemap->GetTileSetGuid());
            ImGui::SameLine();
            if (!canConvert) ImGui::BeginDisabled();
            if (ImGui::Button("Use Selected TileSet & Convert")) {
                if (!tilemap->ConvertToLayered(tilemap->GetTileSetGuid()))
                    conversionError = "The selected TileSet could not be applied.";
                else {
                    conversionError.clear();
                    tilemap->ResolveAssets();
                }
            }
            if (!canConvert) ImGui::EndDisabled();
            if (!conversionError.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "%s",
                                   conversionError.c_str());
            }
            return;
        }

        const TilemapLayer* active = tilemap->GetLayer(tilemap->GetActiveLayerId());
        if (ImGui::BeginCombo("Active Layer", active ? active->name.c_str() : "(none)")) {
            for (const auto& layer : tilemap->GetLayers()) {
                if (ImGui::Selectable(layer.name.c_str(), active && layer.id == active->id))
                    tilemap->SetActiveLayer(layer.id);
            }
            ImGui::EndCombo();
        }
        active = tilemap->GetLayer(tilemap->GetActiveLayerId());
        if (ImGui::Button("Add Layer")) {
            const std::string added = tilemap->AddLayer("Layer");
            if (!added.empty()) tilemap->SetActiveLayer(added);
        }
        ImGui::SameLine();
        active = tilemap->GetLayer(tilemap->GetActiveLayerId());
        const bool canRemove = active && tilemap->GetLayers().size() > 1;
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Layer") && active) tilemap->RemoveLayer(active->id);
        if (!canRemove) ImGui::EndDisabled();

        active = tilemap->GetLayer(tilemap->GetActiveLayerId());
        if (active) {
            const std::string activeId = active->id;
            const auto& layers = tilemap->GetLayers();
            const auto found = std::find_if(layers.begin(), layers.end(),
                [&](const auto& layer) { return layer.id == activeId; });
            const int index = found == layers.end() ? -1
                : static_cast<int>(std::distance(layers.begin(), found));
            if (index <= 0) ImGui::BeginDisabled();
            if (ImGui::Button("Layer Up")) tilemap->MoveLayer(activeId, index - 1);
            if (index <= 0) ImGui::EndDisabled();
            ImGui::SameLine();
            const bool canMoveDown = index >= 0 &&
                index + 1 < static_cast<int>(layers.size());
            if (!canMoveDown) ImGui::BeginDisabled();
            if (ImGui::Button("Layer Down")) tilemap->MoveLayer(activeId, index + 1);
            if (!canMoveDown) ImGui::EndDisabled();

            active = tilemap->GetLayer(activeId);
            if (active) {
                char name[128]{};
                std::strncpy(name, active->name.c_str(), sizeof(name) - 1);
                if (ImGui::InputText("Layer Name", name, sizeof(name)))
                    tilemap->SetLayerName(activeId, name);
                bool visible = active->visible;
                bool locked = active->locked;
                bool collision = active->collisionEnabled;
                float opacity = active->opacity;
                int offset = active->sortingOffset;
                if (ImGui::Checkbox("Visible", &visible))
                    tilemap->SetLayerVisible(activeId, visible);
                ImGui::SameLine();
                if (ImGui::Checkbox("Locked", &locked))
                    tilemap->SetLayerLocked(activeId, locked);
                ImGui::SameLine();
                if (ImGui::Checkbox("Collision", &collision))
                    tilemap->SetLayerCollisionEnabled(activeId, collision);
                if (ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f))
                    tilemap->SetLayerOpacity(activeId, opacity);
                if (ImGui::InputInt("Layer Sorting Offset", &offset))
                    tilemap->SetLayerSortingOffset(activeId, offset);
            }
        }
        if (ImGui::Button("Open Tile Palette")) {
            Editor::Get().GetWindowManager().SetVisible(
                EditorConstants::WIN_TILE_PALETTE, true);
        }
        std::string warning;
        if (!tilemap->CanAuthor(&warning)) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.15f, 1.0f), "%s",
                               warning.c_str());
        }
        return;
    }

    if (auto* particles = dynamic_cast<ParticleSystem*>(component)) {
        bool changed = false;
        ImGui::SeparatorText("Particle Structures");
        if (ImGui::TreeNodeEx("Size Curve", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& keys = particles->config.sizeOverLife.keys;
            for (std::size_t index = 0; index < keys.size();) {
                ImGui::PushID(static_cast<int>(index));
                float key[2] = {keys[index].time, keys[index].value};
                if (ImGui::DragFloat2("Key", key, 0.01f)) {
                    keys[index] = {key[0], key[1]};
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-")) {
                    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    continue;
                }
                ImGui::PopID();
                ++index;
            }
            if (ImGui::Button("Add Size Key")) {
                keys.push_back({keys.empty() ? 0.0f : 1.0f,
                    keys.empty() ? particles->config.startSize
                                 : particles->config.endSize});
                changed = true;
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Color Gradient", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& keys = particles->config.colorOverLife.keys;
            for (std::size_t index = 0; index < keys.size();) {
                ImGui::PushID(static_cast<int>(index));
                changed |= ImGui::SliderFloat("Time", &keys[index].time, 0.0f, 1.0f);
                float color[4] = {keys[index].color.r, keys[index].color.g,
                                  keys[index].color.b, keys[index].color.a};
                if (ImGui::ColorEdit4("Color", color)) {
                    keys[index].color = {color[0], color[1], color[2], color[3]};
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-")) {
                    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    continue;
                }
                ImGui::PopID();
                ++index;
            }
            if (ImGui::Button("Add Color Key")) {
                keys.push_back({keys.empty() ? 0.0f : 1.0f, Color::White()});
                changed = true;
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Particle Sprites", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sprites = particles->config.sprites;
            for (std::size_t index = 0; index < sprites.size();) {
                ImGui::PushID(static_cast<int>(index));
                char texture[128]{};
                char slice[128]{};
                std::strncpy(texture, sprites[index].textureGuid.c_str(), sizeof(texture) - 1);
                std::strncpy(slice, sprites[index].sliceId.c_str(), sizeof(slice) - 1);
                if (ImGui::InputText("Texture", texture, sizeof(texture))) {
                    sprites[index].textureGuid = texture;
                    changed = true;
                }
                std::string dropped;
                if (AcceptAssetGuidDrop("TextureImporter", dropped)) {
                    sprites[index].textureGuid = std::move(dropped);
                    changed = true;
                }
                if (ImGui::InputText("Slice", slice, sizeof(slice))) {
                    sprites[index].sliceId = slice;
                    changed = true;
                }
                if (ImGui::Button("Remove Sprite")) {
                    sprites.erase(sprites.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    continue;
                }
                ImGui::Separator();
                ImGui::PopID();
                ++index;
            }
            if (ImGui::Button("Add Sprite")) {
                sprites.push_back({});
                changed = true;
            }
            ImGui::TreePop();
        }
        if (changed) {
            particles->config.Normalize();
            particles->ResetEditorPreview();
        }

        ImGui::SeparatorText("Editor Preview");
        ParticleEmitter& preview = particles->GetEditorPreviewEmitter();
        if (ImGui::Button("Preview Play")) preview.Start();
        ImGui::SameLine();
        if (ImGui::Button("Preview Pause")) preview.Pause();
        ImGui::SameLine();
        if (ImGui::Button("Preview Stop")) preview.Stop();
        ImGui::SameLine();
        if (ImGui::Button("Burst 10")) preview.Burst(10);
        ImGui::SameLine();
        if (ImGui::Button("Clear Preview")) preview.Clear();
        ImGui::TextDisabled("%d live preview particles", preview.GetActiveCount());
        return;
    }

    if (auto* audio = dynamic_cast<AudioSource*>(component)) {
        ImGui::SeparatorText("Runtime Preview");
        if (ImGui::Button("Play Clip")) audio->Play();
        ImGui::SameLine();
        if (ImGui::Button("Pause Clip")) audio->Pause();
        ImGui::SameLine();
        if (ImGui::Button("Resume Clip")) audio->Resume();
        ImGui::SameLine();
        if (ImGui::Button("Stop Clip")) audio->Stop();
        ImGui::TextDisabled("Voice: %s", audio->IsPlaying() ? "playing" : "stopped");
        return;
    }

    if (auto* rect = dynamic_cast<RectTransform*>(component)) {
        ImGui::SeparatorText("Anchor Presets");
        if (ImGui::Button("Center"))
            rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
        ImGui::SameLine();
        if (ImGui::Button("Stretch")) {
            rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
            rect->SetSizeDelta({0.0f, 0.0f});
        }
    }
}

} // namespace

void InspectorWindow::DrawComponent(Component* component) {
    if (!component || !target) return;

    const unsigned int componentTargetId = target->GetID();
    GameObject* const componentTargetIdentity = target;
    // User serialization/inspector/lifecycle callbacks may remove the object
    // from the editor World. Keep its storage alive for this stack frame, while
    // re-resolving editor membership before every subsequent use.
    const std::shared_ptr<GameObject> componentTargetHold =
        Editor::Get().ShareObjectById(componentTargetId);
    std::string typeName = component->GetTypeName();
    const std::uint64_t componentInstanceId = component->GetInstanceID();
    auto refreshComponent = [&]() -> bool {
        GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
        if (!liveTarget || liveTarget != componentTargetIdentity) return false;
        Component* liveComponent = FindComponentInstance(
            liveTarget, typeName, componentInstanceId);
        if (!liveComponent) return false;
        target = liveTarget;
        component = liveComponent;
        return true;
    };

    // Check if this component has overrides in PrefabInstance
    bool isOverridden = false;
    if (target) {
        if (auto* pi = target->GetComponent<PrefabInstance>()) {
            for (const auto& mod : pi->GetModifications()) {
                if (mod.contains("component") && mod["component"].get<std::string>() == typeName) {
                    isOverridden = true;
                    break;
                }
            }
        }
    }

    // Capture snapshot before rendering
    nlohmann::json snapBefore = molga::CaptureComponentSnapshot(component);
    if (!refreshComponent()) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    if (IsActiveEdit(componentTargetId, component)) {
        // keep beforeEditSnap_
    } else if (activeEditComponentInstanceId_ == 0) {
        beforeEditSnap_ = snapBefore;
    }

    // Determine icon based on component type
    const char* icon = UIRegistry::GetComponentInfo(typeName).icon;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;

    if (isOverridden) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.7f, 1.0f, 1.0f)); // Style color for override highlight
    }

    std::string label = std::string(icon) + " " + typeName + (isOverridden ? " *" : "");
    bool open = ImGui::TreeNodeEx((void*)component, flags, "%s", label.c_str());

    if (isOverridden) {
        ImGui::PopStyleColor();
    }

    // Context menu for component header (except Transform)
    bool removed = false;
    if (ImGui::BeginPopupContextItem()) {
        if (typeName != "Transform") {
            if (ImGui::MenuItem((std::string(Icons::Trash) + " Remove Component").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentRemoveCommand>(target->GetID(), typeName)
                );
                removed = true;
            }
        }
        ImGui::EndPopup();
    }

    // ComponentRemoveCommand destroys the component synchronously. Balance the
    // ImGui tree and leave immediately; even reading IsEnabled()/serializing a
    // snapshot below this point would dereference freed memory.
    if (removed) {
        if (activeEditTargetId_ == componentTargetId &&
            activeEditComponentInstanceId_ == componentInstanceId &&
            activeEditComponentType_ == typeName) ClearActiveEdit();
        if (open) ImGui::TreePop();
        return;
    }

    // Enable/disable checkbox on the same line
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    bool enabled = component->IsEnabled();
    if (ImGui::Checkbox(("##" + typeName + "Enabled").c_str(), &enabled)) {
        nlohmann::json beforeSnap = molga::CaptureComponentSnapshot(component);
        if (!refreshComponent()) {
            if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
            if (open) ImGui::TreePop();
            return;
        }
        const auto descriptors = molga::DescribeEditorProperties(*component);
        const auto enabledDescriptor = std::find_if(
            descriptors.begin(), descriptors.end(),
            [](const auto& descriptor) { return descriptor.key == "enabled"; });
        if (enabledDescriptor == descriptors.end() ||
            molga::ApplyEditorPropertyValue(*enabledDescriptor, {component}, enabled) == 0u) {
            if (open) ImGui::TreePop();
            return;
        }

        // SetEnabled invokes user lifecycle callbacks. They are allowed to
        // remove this component (or the target object), so refresh before the
        // open-body code below uses either pointer again.
        GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
        Component* liveComponent = liveTarget && liveTarget == componentTargetIdentity
            ? FindComponentInstance(liveTarget, typeName, componentInstanceId)
            : nullptr;
        if (!liveComponent) {
            if (activeEditTargetId_ == componentTargetId &&
                activeEditComponentInstanceId_ == componentInstanceId &&
                activeEditComponentType_ == typeName) ClearActiveEdit();
            if (open) ImGui::TreePop();
            return;
        }
        target = liveTarget;
        component = liveComponent;
        const nlohmann::json afterSnap =
            molga::CaptureComponentSnapshot(liveComponent);
        if (afterSnap != beforeSnap) {
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::BatchComponentSnapshotCommand>(
                    std::vector<molga::ComponentSnapshotChange>{
                        {componentTargetId, typeName, beforeSnap, afterSnap}}, true)
            );
        }
    }

    if (open) {
        ImGui::Spacing();
        // Single and multi selection intentionally share the exact same
        // descriptor/getter/setter path. Component::OnInspectorGUI and the old
        // P0 renderer mutated fields independently and could create duplicate
        // widgets or separate undo gestures for one value.
        const auto descriptors = molga::DescribeEditorProperties(*component);
        bool componentAlive = true;
        bool lightingHeaderShown = false;
        for (const auto& descriptor : descriptors) {
            if (descriptor.key == "enabled" || !descriptor.getter || !descriptor.setter)
                continue;
            if (!lightingHeaderShown &&
                IsLightingProperty(typeName, descriptor.key)) {
                ImGui::SeparatorText("Lighting");
                lightingHeaderShown = true;
            }
            ImGui::PushID(descriptor.key.c_str());
            const molga::EditorPropertyValue current = descriptor.getter(*component);
            molga::EditorPropertyValue edited = current;
            const bool changed = DrawEditorPropertyValue(descriptor, current, edited);
            ImGui::PopID();
            if (!changed) continue;
            molga::ApplyEditorPropertyValue(descriptor, {component}, edited);

            // Deserialize/ResolveAssets are extensibility hooks. Re-resolve
            // after each mutation before the next descriptor dereferences it.
            GameObject* liveOwner = molga::FindGameObjectById(componentTargetId);
            Component* liveComponent = liveOwner && liveOwner == componentTargetIdentity
                ? FindComponentInstance(liveOwner, typeName, componentInstanceId)
                : nullptr;
            if (!liveComponent) {
                componentAlive = false;
                break;
            }
            target = liveOwner;
            component = liveComponent;
        }
        if (componentAlive) DrawComponentStructureEditors(component);
        ImGui::TreePop();
    }

    // Custom inspector code is user code and may remove this component, a
    // sibling, or the target object itself. Re-resolve before touching it again.
    GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
    if (!liveTarget || liveTarget != componentTargetIdentity) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    component = FindComponentInstance(liveTarget, typeName, componentInstanceId);
    if (!component) {
        if (activeEditTargetId_ == componentTargetId &&
            activeEditComponentInstanceId_ == componentInstanceId &&
            activeEditComponentType_ == typeName) ClearActiveEdit();
        return;
    }
    target = liveTarget;

    // Capture snapshot after rendering and compare
    nlohmann::json snapAfter = molga::CaptureComponentSnapshot(component);
    if (!refreshComponent()) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    if (snapBefore != snapAfter) {
        if (ImGui::IsAnyItemActive()) {
            activeEditComponentType_ = typeName;
            activeEditComponentInstanceId_ = componentInstanceId;
            activeEditTargetId_ = componentTargetId;
        } else {
            // Immediate change
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::BatchComponentSnapshotCommand>(
                    std::vector<molga::ComponentSnapshotChange>{
                        {componentTargetId, typeName, beforeEditSnap_, snapAfter}}, true)
            );
            ClearActiveEdit();
        }
    }
}

#pragma once

#include "EditorWindow.h"
#include "Core/TileSetAsset.h"
#include "Rendering/PostProcessProfile2D.h"
#include "Editor/Commands/ComponentCommands.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class GameObject;
class Component;

class InspectorWindow : public EditorWindow {
public:
    InspectorWindow();

    void OnGUI() override;

    // Object to inspect
    void SetTarget(GameObject* obj);
    void SetTargets(const std::vector<GameObject*>& objects);
    GameObject* GetTarget() const { return target; }
    void SetAssetTarget(const std::string& path);
    void ClearAssetTarget();
    const std::string& GetAssetTarget() const { return assetPath_; }

private:
    void DrawComponent(Component* component);
    void ClearActiveEdit();
    bool IsActiveEdit(unsigned int targetId, const Component* component) const;
    void DrawAssetInspector();
    void DrawMultiInspector();
    void ClearMultiEdit();
    void CommitMultiEdit();

    GameObject* target = nullptr;
    unsigned int targetId_ = 0;
    std::vector<unsigned int> targetIds_;
    std::string assetPath_;
    std::string assetGuid_;
    molga::TileSetAsset tileSetEdit_;
    std::string tileSetEditGuid_;
    std::string tileSetEditError_;
    bool tileSetEditLoaded_ = false;
    bool tileSetEditDirty_ = false;
    molga::PostProcessProfile2D postFxEdit_;
    std::string postFxEditGuid_;
    std::string postFxEditError_;
    bool postFxEditLoaded_ = false;
    bool postFxEditDirty_ = false;

    // Keep only value identities across frames. Components may be removed or
    // replaced synchronously by undo/redo, script reload, or inspector code.
    // A raw pointer here would become dangling before the edit is committed.
    std::string activeEditComponentType_;
    std::uint64_t activeEditComponentInstanceId_ = 0;
    nlohmann::json beforeEditSnap_;
    unsigned int activeEditTargetId_ = 0;

    std::string activeMultiEditKey_;
    std::string activeMultiComponentType_;
    std::vector<molga::ComponentSnapshotBaseline> activeMultiBaselines_;
};

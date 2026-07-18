#pragma once

#include "EditorWindow.h"
#include "Editor/ViewportMath.h"
#include "ECS/Components/TilemapRenderer.h"

#include <imgui.h>
#include <map>
#include <string>
#include <utility>

class GameObject;

class TilePaletteWindow final : public EditorWindow {
public:
    enum class Tool { Pencil, Erase, Rectangle, FloodFill, Eyedropper };

    TilePaletteWindow();
    void OnGUI() override;

    // Called by Scene View after its image has established the viewport.
    bool HandleSceneInput(const Vector2& world, bool hovered,
                          bool pressed, bool held, bool released);
    void DrawSceneOverlay(ImDrawList* drawList, ImVec2 panelPos,
                          ImVec2 panelSize,
                          const molga::ViewportCamera& camera) const;

    bool HasActiveStroke() const { return strokeActive_; }
    bool IsPaintingTool() const { return tool_ != Tool::Eyedropper; }

private:
    struct CapturedDelta {
        TilemapCell before;
        TilemapCell after;
    };

    TilemapRenderer* SelectedTilemap(GameObject** objectOut = nullptr) const;
    bool BeginStroke(TilemapRenderer& tilemap, GameObject& object, int x, int y);
    void ApplyAt(TilemapRenderer& tilemap, int x, int y, bool erase);
    void ApplyRectangle(TilemapRenderer& tilemap, int x0, int y0, int x1, int y1,
                        bool erase);
    void ApplyFloodFill(TilemapRenderer& tilemap, int x, int y);
    void CaptureBefore(TilemapRenderer& tilemap, int x, int y);
    void CaptureAfter(TilemapRenderer& tilemap, int x, int y);
    void CommitStroke();
    void CancelStroke();

    Tool tool_ = Tool::Pencil;
    int selectedTileId_ = -1;
    int selectedTerrainId_ = -1;
    bool terrainMode_ = false;

    bool strokeActive_ = false;
    unsigned int strokeObjectId_ = 0;
    std::string strokeLayerId_;
    int strokeWidth_ = 0;
    int startX_ = -1;
    int startY_ = -1;
    int hoverX_ = -1;
    int hoverY_ = -1;
    int lastPaintX_ = -1;
    int lastPaintY_ = -1;
    std::map<std::pair<int, int>, CapturedDelta> captured_;
};

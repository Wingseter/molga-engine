#include "TilePaletteWindow.h"

#include "Common/Log.h"
#include "Editor/Commands/TilePaintCommand.h"
#include "Editor/Editor.h"
#include "Editor/EditorConstants.h"
#include "ECS/GameObject.h"

#include <algorithm>
#include <array>
#include <queue>
#include <set>
#include <vector>

namespace {

constexpr std::array<std::pair<int, int>, 5> kTerrainNeighborhood{{
    {0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}
}};

const char* ToolName(TilePaletteWindow::Tool tool) {
    switch (tool) {
        case TilePaletteWindow::Tool::Pencil: return "Pencil";
        case TilePaletteWindow::Tool::Erase: return "Erase";
        case TilePaletteWindow::Tool::Rectangle: return "Rectangle";
        case TilePaletteWindow::Tool::FloodFill: return "Flood Fill";
        case TilePaletteWindow::Tool::Eyedropper: return "Eyedropper";
    }
    return "Tool";
}

} // namespace

TilePaletteWindow::TilePaletteWindow()
    : EditorWindow(EditorConstants::WIN_TILE_PALETTE) {}

TilemapRenderer* TilePaletteWindow::SelectedTilemap(GameObject** objectOut) const {
    GameObject* object = Editor::Get().GetSelectedObject();
    if (objectOut) *objectOut = object;
    return object ? object->GetComponent<TilemapRenderer>() : nullptr;
}

void TilePaletteWindow::OnGUI() {
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    GameObject* object = nullptr;
    TilemapRenderer* tilemap = SelectedTilemap(&object);
    if (!tilemap) {
        ImGui::TextDisabled("Select an object with TilemapRenderer.");
        ImGui::End();
        return;
    }
    if (!tilemap->IsLayered()) {
        ImGui::TextWrapped("This is a legacy tilemap. Use 'Create TileSet & Convert' in the Inspector before painting layers.");
        ImGui::End();
        return;
    }

    std::string warning;
    if (!tilemap->CanAuthor(&warning)) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", warning.c_str());
    }

    const Tool tools[] = {Tool::Pencil, Tool::Erase, Tool::Rectangle,
                          Tool::FloodFill, Tool::Eyedropper};
    for (const Tool tool : tools) {
        if (tool_ == tool) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.45f, 0.75f, 1.0f));
        if (ImGui::SmallButton(ToolName(tool))) tool_ = tool;
        if (tool_ == tool) ImGui::PopStyleColor();
        if (tool != Tool::Eyedropper) ImGui::SameLine();
    }

    ImGui::SeparatorText("Layers");
    const auto layers = tilemap->GetLayers();
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const TilemapLayer& snapshot = layers[index];
        TilemapLayer* layer = tilemap->GetLayer(snapshot.id);
        if (!layer) continue;
        ImGui::PushID(layer->id.c_str());
        bool active = tilemap->GetActiveLayerId() == layer->id;
        if (ImGui::Selectable(layer->name.c_str(), active)) tilemap->SetActiveLayer(layer->id);
        ImGui::SameLine();
        bool visible = layer->visible;
        if (ImGui::Checkbox("V", &visible)) {
            tilemap->SetLayerVisible(layer->id, visible);
            Editor::Get().MarkSceneModified();
        }
        ImGui::SameLine();
        bool locked = layer->locked;
        if (ImGui::Checkbox("L", &locked)) {
            tilemap->SetLayerLocked(layer->id, locked);
            Editor::Get().MarkSceneModified();
        }
        ImGui::SameLine();
        bool collision = layer->collisionEnabled;
        if (ImGui::Checkbox("C", &collision)) {
            tilemap->SetLayerCollisionEnabled(layer->id, collision);
            Editor::Get().MarkSceneModified();
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Layer")) {
        const std::string id = tilemap->AddLayer("Layer " + std::to_string(layers.size() + 1));
        tilemap->SetActiveLayer(id);
        Editor::Get().MarkSceneModified();
    }
    ImGui::SameLine();
    const bool canRemove = layers.size() > 1;
    if (!canRemove) ImGui::BeginDisabled();
    if (ImGui::SmallButton("- Layer")) {
        tilemap->RemoveLayer(tilemap->GetActiveLayerId());
        Editor::Get().MarkSceneModified();
    }
    if (!canRemove) ImGui::EndDisabled();

    if (!tilemap->GetTileSet() && !tilemap->GetTileSetGuid().empty()) {
        tilemap->ResolveAssets();
    }
    const molga::TileSetAsset* tileSet = tilemap->GetTileSet();
    if (!tileSet) {
        ImGui::Separator();
        ImGui::TextDisabled("TileSet is missing or failed to import.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Palette");
    ImGui::Checkbox("Terrain paint", &terrainMode_);
    if (!terrainMode_) {
        for (const auto& tile : tileSet->tiles) {
            ImGui::PushID(tile.id);
            const bool selected = selectedTileId_ == tile.id;
            if (ImGui::Selectable(tile.name.empty() ? std::to_string(tile.id).c_str() : tile.name.c_str(),
                                  selected, 0, ImVec2(120.0f, 0.0f))) {
                selectedTileId_ = tile.id;
            }
            ImGui::PopID();
        }
    } else {
        std::set<int> terrainIds;
        for (const auto& tile : tileSet->tiles) if (tile.terrainId >= 0) terrainIds.insert(tile.terrainId);
        for (const auto& rule : tileSet->terrainRules) if (rule.terrainId >= 0) terrainIds.insert(rule.terrainId);
        for (int terrainId : terrainIds) {
            ImGui::PushID(terrainId);
            const std::string label = "Terrain " + std::to_string(terrainId);
            if (ImGui::Selectable(label.c_str(), selectedTerrainId_ == terrainId)) {
                selectedTerrainId_ = terrainId;
            }
            ImGui::PopID();
        }
    }

    const TilemapLayer* activeLayer = tilemap->GetLayer(tilemap->GetActiveLayerId());
    if (activeLayer && activeLayer->locked) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Active layer is locked.");
    }
    ImGui::End();
}

void TilePaletteWindow::CaptureBefore(TilemapRenderer& tilemap, int x, int y) {
    if (x < 0 || y < 0 || x >= tilemap.width || y >= tilemap.height) return;
    const auto key = std::make_pair(x, y);
    if (captured_.find(key) == captured_.end()) {
        const TilemapCell cell = tilemap.GetCell(strokeLayerId_, x, y);
        captured_.emplace(key, CapturedDelta{cell, cell});
    }
}

void TilePaletteWindow::CaptureAfter(TilemapRenderer& tilemap, int x, int y) {
    if (x < 0 || y < 0 || x >= tilemap.width || y >= tilemap.height) return;
    CaptureBefore(tilemap, x, y);
    captured_[{x, y}].after = tilemap.GetCell(strokeLayerId_, x, y);
}

void TilePaletteWindow::ApplyAt(TilemapRenderer& tilemap, int x, int y, bool erase) {
    if (x < 0 || y < 0 || x >= tilemap.width || y >= tilemap.height) return;
    if (terrainMode_ || erase) {
        for (const auto& [dx, dy] : kTerrainNeighborhood) CaptureBefore(tilemap, x + dx, y + dy);
        if (erase) {
            const TilemapCell current = tilemap.GetCell(strokeLayerId_, x, y);
            if (current.terrainId >= 0) tilemap.SetTerrain(strokeLayerId_, x, y, -1);
            else tilemap.SetCell(strokeLayerId_, x, y, {-1, -1});
        } else if (selectedTerrainId_ >= 0) {
            tilemap.SetTerrain(strokeLayerId_, x, y, selectedTerrainId_);
        }
        for (const auto& [dx, dy] : kTerrainNeighborhood) CaptureAfter(tilemap, x + dx, y + dy);
        return;
    }
    if (selectedTileId_ < 0) return;
    CaptureBefore(tilemap, x, y);
    tilemap.SetCell(strokeLayerId_, x, y, {selectedTileId_, -1});
    CaptureAfter(tilemap, x, y);
}

void TilePaletteWindow::ApplyRectangle(TilemapRenderer& tilemap, int x0, int y0,
                                       int x1, int y1, bool erase) {
    const int left = std::max(0, std::min(x0, x1));
    const int right = std::min(tilemap.width - 1, std::max(x0, x1));
    const int top = std::max(0, std::min(y0, y1));
    const int bottom = std::min(tilemap.height - 1, std::max(y0, y1));
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) ApplyAt(tilemap, x, y, erase);
}

void TilePaletteWindow::ApplyFloodFill(TilemapRenderer& tilemap, int x, int y) {
    const TilemapCell original = tilemap.GetCell(strokeLayerId_, x, y);
    const TilemapCell replacement = terrainMode_
        ? TilemapCell{original.tileId, selectedTerrainId_}
        : TilemapCell{selectedTileId_, -1};
    if (selectedTileId_ < 0 && !terrainMode_) return;
    if (selectedTerrainId_ < 0 && terrainMode_) return;
    if ((!terrainMode_ && original == replacement) ||
        (terrainMode_ && original.terrainId == selectedTerrainId_)) return;

    std::queue<std::pair<int, int>> pending;
    std::vector<unsigned char> visited(static_cast<std::size_t>(tilemap.width * tilemap.height), 0);
    pending.push({x, y});
    while (!pending.empty()) {
        const auto [cx, cy] = pending.front();
        pending.pop();
        if (cx < 0 || cy < 0 || cx >= tilemap.width || cy >= tilemap.height) continue;
        const std::size_t index = static_cast<std::size_t>(cy * tilemap.width + cx);
        if (visited[index]) continue;
        visited[index] = 1;
        const TilemapCell candidate = tilemap.GetCell(strokeLayerId_, cx, cy);
        if ((!terrainMode_ && candidate != original) ||
            (terrainMode_ && candidate.terrainId != original.terrainId)) continue;
        ApplyAt(tilemap, cx, cy, false);
        pending.push({cx + 1, cy});
        pending.push({cx - 1, cy});
        pending.push({cx, cy + 1});
        pending.push({cx, cy - 1});
    }
}

bool TilePaletteWindow::BeginStroke(TilemapRenderer& tilemap, GameObject& object,
                                    int x, int y) {
    std::string warning;
    const TilemapLayer* layer = tilemap.GetLayer(tilemap.GetActiveLayerId());
    if (!tilemap.IsLayered() || !tilemap.CanAuthor(&warning) || !layer || layer->locked) {
        if (!warning.empty()) Log::Warn("TilePalette", warning);
        return false;
    }
    strokeActive_ = true;
    strokeObjectId_ = object.GetID();
    strokeLayerId_ = layer->id;
    strokeWidth_ = tilemap.width;
    startX_ = lastPaintX_ = x;
    startY_ = lastPaintY_ = y;
    captured_.clear();
    return true;
}

void TilePaletteWindow::CommitStroke() {
    if (!strokeActive_) return;
    std::vector<molga::TilePaintDelta> changes;
    changes.reserve(captured_.size());
    for (const auto& [coordinate, delta] : captured_) {
        if (delta.before == delta.after) continue;
        changes.push_back({coordinate.first, coordinate.second, delta.before, delta.after});
    }
    if (!changes.empty()) {
        Editor::Get().GetCommandHistory().Execute(std::make_unique<molga::TilePaintCommand>(
            strokeObjectId_, strokeLayerId_, std::move(changes)));
    }
    CancelStroke();
}

void TilePaletteWindow::CancelStroke() {
    strokeActive_ = false;
    strokeObjectId_ = 0;
    strokeLayerId_.clear();
    strokeWidth_ = 0;
    startX_ = startY_ = lastPaintX_ = lastPaintY_ = -1;
    captured_.clear();
}

bool TilePaletteWindow::HandleSceneInput(const Vector2& world, bool hovered,
                                         bool pressed, bool held, bool released) {
    GameObject* object = nullptr;
    TilemapRenderer* tilemap = SelectedTilemap(&object);
    int x = -1;
    int y = -1;
    const bool inCell = tilemap && tilemap->WorldToCell(world, x, y);
    hoverX_ = inCell ? x : -1;
    hoverY_ = inCell ? y : -1;

    if (released && strokeActive_) {
        GameObject* strokeObject = Editor::Get().FindObjectById(strokeObjectId_);
        TilemapRenderer* strokeTilemap = strokeObject
            ? strokeObject->GetComponent<TilemapRenderer>() : nullptr;
        if (strokeTilemap && tool_ == Tool::Rectangle) {
            const int endX = inCell ? x : lastPaintX_;
            const int endY = inCell ? y : lastPaintY_;
            ApplyRectangle(*strokeTilemap, startX_, startY_, endX, endY, false);
        }
        CommitStroke();
        return true;
    }
    if (!hovered || !inCell || !tilemap || !object) return strokeActive_;

    if (pressed && tool_ == Tool::Eyedropper) {
        const TilemapCell cell = tilemap->GetCell(tilemap->GetActiveLayerId(), x, y);
        selectedTileId_ = cell.tileId;
        selectedTerrainId_ = cell.terrainId;
        terrainMode_ = cell.terrainId >= 0;
        tool_ = Tool::Pencil;
        return true;
    }
    if (pressed) {
        if (!BeginStroke(*tilemap, *object, x, y)) return false;
        if (tool_ == Tool::Pencil) ApplyAt(*tilemap, x, y, false);
        else if (tool_ == Tool::Erase) ApplyAt(*tilemap, x, y, true);
        else if (tool_ == Tool::FloodFill) {
            ApplyFloodFill(*tilemap, x, y);
            CommitStroke();
        }
        return true;
    }
    if (held && strokeActive_) {
        lastPaintX_ = x;
        lastPaintY_ = y;
        if ((tool_ == Tool::Pencil || tool_ == Tool::Erase) &&
            (x != lastPaintX_ || y != lastPaintY_)) {
            ApplyAt(*tilemap, x, y, tool_ == Tool::Erase);
        } else if (tool_ == Tool::Pencil || tool_ == Tool::Erase) {
            ApplyAt(*tilemap, x, y, tool_ == Tool::Erase);
        }
        return true;
    }
    return false;
}

void TilePaletteWindow::DrawSceneOverlay(ImDrawList* drawList, ImVec2 panelPos,
                                         ImVec2 panelSize,
                                         const molga::ViewportCamera& camera) const {
    if (!drawList || !isOpen) return;
    TilemapRenderer* tilemap = SelectedTilemap();
    if (!tilemap || !tilemap->IsLayered()) return;

    const Vector2 origin = tilemap->CellToWorld(0, 0);
    const Vector2 extent = tilemap->CellToWorld(tilemap->width, tilemap->height);
    auto screen = [&](const Vector2& world) {
        float sx = 0.0f;
        float sy = 0.0f;
        molga::WorldToScreen(camera, panelSize.x, panelSize.y, world.x, world.y, sx, sy);
        return ImVec2(panelPos.x + sx, panelPos.y + sy);
    };
    const ImVec2 topLeft = screen(origin);
    const ImVec2 bottomRight = screen(extent);
    drawList->PushClipRect(panelPos, ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), true);
    const ImU32 gridColor = IM_COL32(110, 180, 230, 75);
    for (int x = 0; x <= tilemap->width; ++x) {
        const ImVec2 p = screen(tilemap->CellToWorld(x, 0));
        drawList->AddLine(ImVec2(p.x, topLeft.y), ImVec2(p.x, bottomRight.y), gridColor);
    }
    for (int y = 0; y <= tilemap->height; ++y) {
        const ImVec2 p = screen(tilemap->CellToWorld(0, y));
        drawList->AddLine(ImVec2(topLeft.x, p.y), ImVec2(bottomRight.x, p.y), gridColor);
    }
    if (hoverX_ >= 0 && hoverY_ >= 0) {
        int left = hoverX_, right = hoverX_, top = hoverY_, bottom = hoverY_;
        if (strokeActive_ && tool_ == Tool::Rectangle) {
            left = std::min(startX_, hoverX_);
            right = std::max(startX_, hoverX_);
            top = std::min(startY_, hoverY_);
            bottom = std::max(startY_, hoverY_);
        }
        const ImVec2 a = screen(tilemap->CellToWorld(left, top));
        const ImVec2 b = screen(tilemap->CellToWorld(right + 1, bottom + 1));
        drawList->AddRectFilled(a, b, IM_COL32(80, 190, 255, 65));
        drawList->AddRect(a, b, IM_COL32(100, 210, 255, 220), 0.0f, 0, 2.0f);
    }
    std::string warning;
    if (!tilemap->CanAuthor(&warning)) {
        drawList->AddText(ImVec2(panelPos.x + 8.0f, panelPos.y + 64.0f),
                          IM_COL32(255, 90, 65, 255), warning.c_str());
    }
    drawList->PopClipRect();
}

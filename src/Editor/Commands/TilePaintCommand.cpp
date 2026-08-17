#include "Editor/Commands/TilePaintCommand.h"

#include "Editor/Commands/SceneSnapshots.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"

namespace molga {

TilePaintCommand::TilePaintCommand(unsigned int targetId, std::string layerId,
                                   std::vector<TilePaintDelta> changes)
    : targetId_(targetId), layerId_(std::move(layerId)),
      changes_(std::move(changes)) {}

void TilePaintCommand::Apply(bool after) {
    GameObject* object = FindGameObjectById(targetId_);
    TilemapRenderer* tilemap = object ? object->GetComponent<TilemapRenderer>() : nullptr;
    if (!tilemap) return;
    std::vector<TilemapCellEdit> edits;
    edits.reserve(changes_.size());
    for (const auto& change : changes_) {
        edits.push_back({change.x, change.y, after ? change.after : change.before});
    }
    tilemap->ApplyCellEdits(layerId_, edits, true);
    Editor::Get().MarkSceneModified();
}

void TilePaintCommand::Execute() { Apply(true); }
void TilePaintCommand::Undo() { Apply(false); }

} // namespace molga

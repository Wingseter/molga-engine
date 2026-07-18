#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "ECS/Components/TilemapRenderer.h"

#include <string>
#include <vector>

namespace molga {

struct TilePaintDelta {
    int x = 0;
    int y = 0;
    TilemapCell before;
    TilemapCell after;
};

class TilePaintCommand final : public ICommand {
public:
    TilePaintCommand(unsigned int targetId, std::string layerId,
                     std::vector<TilePaintDelta> changes);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Paint Tiles"; }
    std::size_t ChangeCount() const { return changes_.size(); }

private:
    void Apply(bool after);
    unsigned int targetId_ = 0;
    std::string layerId_;
    std::vector<TilePaintDelta> changes_;
};

} // namespace molga

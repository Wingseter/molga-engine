#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <nlohmann/json.hpp>
#include <string>

namespace molga {

// GameObject header properties command (name, tag, layer, active state)
class GameObjectPropertyCommand : public ICommand {
public:
    GameObjectPropertyCommand(unsigned int targetId,
                              const nlohmann::json& beforeProp,
                              const nlohmann::json& afterProp);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Modify GameObject Properties"; }

private:
    unsigned int targetId_;
    nlohmann::json beforeProp_;
    nlohmann::json afterProp_;
};

} // namespace molga

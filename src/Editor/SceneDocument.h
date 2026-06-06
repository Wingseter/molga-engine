#pragma once

#include "Core/World.h"
#include <memory>
#include <string>

// 에디터가 편집 중인 씬 1개를 소유한다.
// editWorld는 권위 있는 사본, playWorld는 Play 중에만 존재하는 휘발 사본.
class SceneDocument {
public:
    World& EditWorld() { return editWorld_; }
    const World& EditWorld() const { return editWorld_; }

    World& ActiveWorld() { return playWorld_ ? *playWorld_ : editWorld_; }
    bool IsPlaying() const { return playWorld_ != nullptr; }

    void EnterPlay() {
        playWorld_ = editWorld_.Clone();
        playWorld_->StartPending();
    }
    void ExitPlay() { playWorld_.reset(); }

    const std::string& Path() const { return path_; }
    void SetPath(std::string p) { path_ = std::move(p); }

private:
    World editWorld_;
    std::unique_ptr<World> playWorld_;
    std::string path_;
};

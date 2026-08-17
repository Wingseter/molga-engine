#pragma once

#include "Core/World.h"
#include "Core/SceneRuntime.h"
#include <filesystem>
#include <memory>
#include <string>

// 에디터가 편집 중인 씬 1개를 소유한다.
// editWorld는 권위 있는 사본, playWorld는 Play 중에만 존재하는 휘발 사본.
class SceneDocument {
public:
    World& EditWorld() { return editWorld_; }
    const World& EditWorld() const { return editWorld_; }

    World& ActiveWorld() { return playRuntime_ ? playRuntime_->ActiveWorld() : editWorld_; }
    const World& ActiveWorld() const { return playRuntime_ ? playRuntime_->ActiveWorld() : editWorld_; }
    bool IsPlaying() const { return playRuntime_ != nullptr; }

    bool EnterPlay(SceneRuntime::SceneCatalog catalog = {},
                   std::string currentScenePath = {}) {
        try {
            auto runtime = std::make_unique<SceneRuntime>(std::move(catalog));
            auto playWorld = editWorld_.Clone();
            if (currentScenePath.empty()) currentScenePath = path_;
            if (!runtime->SetInitialWorld(std::move(playWorld),
                                         std::move(currentScenePath), true)) {
                return false;
            }
            playRuntime_ = std::move(runtime);
        } catch (...) {
            return false;
        }
        return true;
    }
    void ExitPlay() {
        // A script/event can request Stop while the play World is still on the
        // callback stack. Keep ownership intact and let the editor retry at the
        // next safe frame boundary instead of resetting a rejected shutdown.
        if (playRuntime_ && !playRuntime_->Shutdown()) return;
        playRuntime_.reset();
    }

    SceneRuntime* PlayRuntime() { return playRuntime_.get(); }
    const SceneRuntime* PlayRuntime() const { return playRuntime_.get(); }

    bool Open(const std::string& path) {
        World loaded;
        if (!loaded.LoadFromFile(path)) return false;

        const std::string sceneName = std::filesystem::path(path).stem().string();
        loaded.SetName(sceneName.empty() ? "Untitled" : sceneName);
        editWorld_ = std::move(loaded);
        ExitPlay();
        path_ = path;
        return true;
    }

    const std::string& Path() const { return path_; }
    void SetPath(std::string p) { path_ = std::move(p); }

private:
    World editWorld_;
    std::unique_ptr<SceneRuntime> playRuntime_;
    std::string path_;
};

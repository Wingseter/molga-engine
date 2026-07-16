#pragma once

#include <memory>
#include <string>
#include <unordered_map>

class World;

// Owns the active gameplay World and performs registered, transactional scene
// changes. RequestLoad only records a request; CommitPendingLoad is intended to
// run once at the frame boundary after updates and queued events have finished.
class SceneRuntime {
public:
    using SceneCatalog = std::unordered_map<std::string, std::string>;

    SceneRuntime() = default;
    explicit SceneRuntime(SceneCatalog catalog);
    ~SceneRuntime();

    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;
    SceneRuntime(SceneRuntime&&) = delete;
    SceneRuntime& operator=(SceneRuntime&&) = delete;

    void SetCatalog(SceneCatalog catalog);
    const SceneCatalog& Catalog() const { return catalog_; }

    // Used by editor Play mode to adopt the unsaved edit-world clone. This does
    // not publish a SceneLoadEvent because it is the initial world, not a change.
    bool SetInitialWorld(std::unique_ptr<World> world,
                         std::string currentScenePath,
                         bool resolveAndStart = true);

    // The first valid request wins until it is committed. An invalid request
    // does not consume the frame's request slot.
    bool RequestLoad(const std::string& registeredPath);
    bool CommitPendingLoad();

    World& ActiveWorld();
    const World& ActiveWorld() const;
    bool HasActiveWorld() const { return activeWorld_ != nullptr; }

    const std::string& CurrentScenePath() const { return currentScenePath_; }
    const std::string& LastError() const { return lastError_; }
    bool IsSceneLoadPending() const { return !pendingScenePath_.empty(); }

    // Explicit shutdown keeps component destruction ahead of renderer/audio
    // teardown in the application entrypoints.
    // Returns false when teardown is requested from inside an active scene
    // callback/commit and must be retried at the frame boundary.
    bool Shutdown();

private:
    bool FailLoad(const std::string& registeredPath, const std::string& error);

    SceneCatalog catalog_;
    std::unique_ptr<World> activeWorld_;
    std::string currentScenePath_;
    std::string pendingScenePath_;
    std::string lastError_;
    bool committing_ = false;
    bool commitCallActive_ = false;
};

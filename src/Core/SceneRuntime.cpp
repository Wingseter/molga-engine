#include "Core/SceneRuntime.h"

#include "Common/Log.h"
#include "Core/EventBus.h"
#include "Core/Events/SceneEvents.h"
#include "Core/World.h"

#include <cassert>
#include <exception>
#include <filesystem>
#include <utility>

namespace {

class CommitFlagGuard {
public:
    explicit CommitFlagGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~CommitFlagGuard() { flag_ = false; }

    CommitFlagGuard(const CommitFlagGuard&) = delete;
    CommitFlagGuard& operator=(const CommitFlagGuard&) = delete;

    void Unlock() { flag_ = false; }

private:
    bool& flag_;
};

} // namespace

SceneRuntime::SceneRuntime(SceneCatalog catalog)
    : catalog_(std::move(catalog)) {
}

SceneRuntime::~SceneRuntime() {
    Shutdown();
}

void SceneRuntime::SetCatalog(SceneCatalog catalog) {
    catalog_ = std::move(catalog);
    if (!pendingScenePath_.empty() && catalog_.count(pendingScenePath_) == 0) {
        const std::string removedPath = std::move(pendingScenePath_);
        pendingScenePath_.clear();
        FailLoad(removedPath,
                 "Pending scene was removed from the scene catalog: " + removedPath);
    }
}

bool SceneRuntime::SetInitialWorld(std::unique_ptr<World> world,
                                   std::string currentScenePath,
                                   bool resolveAndStart) {
    if (!world) {
        lastError_ = "Cannot adopt a null initial World.";
        return false;
    }
    if (activeWorld_) {
        lastError_ = "SceneRuntime already has an active World.";
        return false;
    }

    // Install the initial scene context before lifecycle callbacks run. Scripts
    // in Awake/Start must observe their own scene path, and a LoadScene request
    // made from Start is the pending transition for the next frame rather than
    // initialization state that is cleared after the callback returns.
    pendingScenePath_.clear();
    lastError_.clear();
    activeWorld_ = std::move(world);
    currentScenePath_ = std::move(currentScenePath);
    activeWorld_->SetSceneRuntime(this);

    // Initial-world lifecycle has the same reentrancy boundary as an incoming
    // scene's lifecycle. Start may queue the next scene, but it must not commit
    // that request while StartPending still has the initial World on its stack.
    CommitFlagGuard callGuard(commitCallActive_);
    try {
        if (resolveAndStart) {
            activeWorld_->ResolveAssets();
            activeWorld_->StartPending();
        }
    } catch (const std::exception& e) {
        const std::string failedPath = currentScenePath_;
        activeWorld_->SetSceneRuntime(nullptr);
        activeWorld_.reset();
        currentScenePath_.clear();
        pendingScenePath_.clear();
        lastError_ = "Failed to initialize scene '" + failedPath + "': " + e.what();
        Log::Error("SceneRuntime", lastError_);
        return false;
    } catch (...) {
        const std::string failedPath = currentScenePath_;
        activeWorld_->SetSceneRuntime(nullptr);
        activeWorld_.reset();
        currentScenePath_.clear();
        pendingScenePath_.clear();
        lastError_ = "Failed to initialize scene '" + failedPath + "'.";
        Log::Error("SceneRuntime", lastError_);
        return false;
    }

    return true;
}

bool SceneRuntime::RequestLoad(const std::string& registeredPath) {
    if (committing_) return false;
    if (registeredPath.empty()) {
        return FailLoad(registeredPath, "Scene path must not be empty.");
    }
    if (catalog_.find(registeredPath) == catalog_.end()) {
        return FailLoad(registeredPath, "Scene is not registered in the scene catalog: " + registeredPath);
    }
    if (!pendingScenePath_.empty()) {
        return false;
    }

    pendingScenePath_ = registeredPath;
    lastError_.clear();
    return true;
}

bool SceneRuntime::CommitPendingLoad() {
    // RequestLoad is allowed from incoming Start callbacks, but a nested commit
    // would destroy the World whose lifecycle is currently on the stack.
    if (commitCallActive_) return false;
    // Commit is a frame-boundary operation. A component, scheduler, physics, or
    // lifecycle callback may request the next scene, but replacing the World
    // while one of its dispatch frames is still active would invalidate that
    // frame's `this` pointer and object snapshots.
    if (activeWorld_ && activeWorld_->IsDispatchingCallbacks()) return false;
    if (pendingScenePath_.empty()) return false;

    CommitFlagGuard callGuard(commitCallActive_);

    const std::string requestedPath = std::move(pendingScenePath_);
    pendingScenePath_.clear();
    CommitFlagGuard commitGuard(committing_);

    const auto catalogIt = catalog_.find(requestedPath);
    if (catalogIt == catalog_.end()) {
        return FailLoad(requestedPath,
                        "Scene was removed from the catalog before it could be loaded: " + requestedPath);
    }

    auto candidate = std::make_unique<World>();
    try {
        if (!candidate->LoadFromFile(catalogIt->second)) {
            return FailLoad(requestedPath,
                            "Failed to load scene '" + requestedPath + "' from '" + catalogIt->second + "'.");
        }

        const std::string sceneName = std::filesystem::path(catalogIt->second).stem().string();
        candidate->SetName(sceneName.empty() ? "Untitled" : sceneName);
        candidate->SetSceneRuntime(this);
        candidate->ResolveAssets();
    } catch (const std::exception& e) {
        candidate->SetSceneRuntime(nullptr);
        return FailLoad(requestedPath,
                        "Failed to prepare scene '" + requestedPath + "': " + e.what());
    } catch (...) {
        candidate->SetSceneRuntime(nullptr);
        return FailLoad(requestedPath,
                        "Failed to prepare scene '" + requestedPath + "'.");
    }

    // Preparation above is transactional. Only after it succeeds do we expose
    // the outgoing scene's unload event and destruction lifecycle. Keeping the
    // old World installed throughout both phases means unload subscribers and
    // OnDestroy code always observe the scene that is actually leaving.
    if (activeWorld_) {
        const std::string outgoingPath = currentScenePath_;
        const std::string outgoingName = activeWorld_->Name();
        SceneUnloadEvent unload;
        unload.sceneName = outgoingName;
        unload.scenePath = outgoingPath;
        try {
            EventBus::Publish(unload);
            activeWorld_->Shutdown();
        } catch (const std::exception& e) {
            candidate->SetSceneRuntime(nullptr);
            return FailLoad(requestedPath,
                            "Failed to unload scene '" + outgoingPath + "': " + e.what());
        } catch (...) {
            candidate->SetSceneRuntime(nullptr);
            return FailLoad(requestedPath,
                            "Failed to unload scene '" + outgoingPath + "'.");
        }
    }

    activeWorld_ = std::move(candidate);
    currentScenePath_ = requestedPath;
    lastError_.clear();

    // Awake/Start may request the next scene. That request belongs to the next
    // frame, so unlock after the irreversible swap and before starting it.
    commitGuard.Unlock();
    try {
        activeWorld_->StartPending();
    } catch (const std::exception& e) {
        // The outgoing World has already completed its destruction lifecycle,
        // so the swap is irreversible. Keep the new World active and report the
        // script lifecycle error without returning a false rollback signal to
        // callers that must rebind their World pointers.
        pendingScenePath_.clear();
        lastError_ = "Scene '" + requestedPath +
                     "' loaded, but a Start callback failed: " + e.what();
        Log::Error("SceneRuntime", lastError_);
    } catch (...) {
        pendingScenePath_.clear();
        lastError_ = "Scene '" + requestedPath +
                     "' loaded, but a Start callback failed.";
        Log::Error("SceneRuntime", lastError_);
    }

    SceneLoadEvent load;
    load.sceneName = activeWorld_->Name();
    load.scenePath = currentScenePath_;
    try {
        EventBus::Publish(load);
    } catch (const std::exception& e) {
        lastError_ = "Scene '" + requestedPath +
                     "' loaded, but a SceneLoadEvent subscriber failed: " + e.what();
        Log::Error("SceneRuntime", lastError_);
    } catch (...) {
        lastError_ = "Scene '" + requestedPath +
                     "' loaded, but a SceneLoadEvent subscriber failed.";
        Log::Error("SceneRuntime", lastError_);
    }
    return true;
}

World& SceneRuntime::ActiveWorld() {
    assert(activeWorld_ && "SceneRuntime has no active World");
    return *activeWorld_;
}

const World& SceneRuntime::ActiveWorld() const {
    assert(activeWorld_ && "SceneRuntime has no active World");
    return *activeWorld_;
}

bool SceneRuntime::Shutdown() {
    // Event subscribers and lifecycle callbacks run inside a commit. Destroying
    // the active World from one of those callbacks would invalidate the commit's
    // stack and could leave the runtime without either the old or new scene.
    // The owning application can call Shutdown again after the frame boundary.
    if (commitCallActive_ ||
        (activeWorld_ && activeWorld_->IsDispatchingCallbacks())) {
        Log::Warn("SceneRuntime",
                  "Ignoring Shutdown while scene callbacks are active.");
        return false;
    }

    pendingScenePath_.clear();
    committing_ = false;
    if (activeWorld_) {
        activeWorld_->Shutdown();
        activeWorld_.reset();
    }
    currentScenePath_.clear();
    return true;
}

bool SceneRuntime::FailLoad(const std::string& registeredPath, const std::string& error) {
    lastError_ = error;
    Log::Error("SceneRuntime", error);

    SceneLoadFailedEvent failed;
    failed.scenePath = registeredPath;
    failed.error = error;
    try {
        EventBus::Publish(failed);
    } catch (const std::exception& e) {
        Log::Error("SceneRuntime",
                   "SceneLoadFailedEvent subscriber threw: " + std::string(e.what()));
    } catch (...) {
        Log::Error("SceneRuntime", "SceneLoadFailedEvent subscriber threw.");
    }
    return false;
}

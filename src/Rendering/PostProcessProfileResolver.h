#pragma once

#include "Rendering/PostProcessProfile2D.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace molga {

class AssetDatabase;

enum class PostProcessProfileResolveStatus {
    Resolved,
    TransientPreview,
    LastGood,
    Missing,
    TypeMismatch,
    ImportFailed,
};

struct PostProcessProfileResolveResult {
    std::shared_ptr<const PostProcessProfile2D> profile;
    PostProcessProfileResolveStatus status = PostProcessProfileResolveStatus::Missing;
    std::string error;

    bool IsResolved() const { return profile != nullptr; }
    explicit operator bool() const { return IsResolved(); }
};

// GUID resolver shared by Game View, runtime, Scene View preview, and the
// asset inspector. Successful profiles are keyed by AssetDatabase content hash.
class PostProcessProfileResolver {
public:
    static PostProcessProfileResolver& Get();

    PostProcessProfileResolveResult Resolve(
        const std::string& guid,
        const AssetDatabase& database) const;
    PostProcessProfileResolveResult Resolve(const std::string& guid) const;

    bool SetTransientOverride(const std::string& guid,
                              const PostProcessProfile2D& profile,
                              std::string* errorOut = nullptr);
    void ClearTransientOverride(const std::string& guid);
    void ClearAllTransientOverrides();
    bool HasTransientOverride(const std::string& guid) const;

    void Invalidate(const std::string& guid);
    void ClearForTesting();

private:
    struct CacheEntry {
        std::string hash;
        std::shared_ptr<const PostProcessProfile2D> lastGood;
    };

    mutable std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, std::shared_ptr<const PostProcessProfile2D>>
        transient_;
};

} // namespace molga

#include "Rendering/PostProcessProfileResolver.h"

#include "Core/AssetDatabase.h"

namespace molga {
namespace {

void SetError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

} // namespace

PostProcessProfileResolver& PostProcessProfileResolver::Get() {
    static PostProcessProfileResolver resolver;
    return resolver;
}

PostProcessProfileResolveResult PostProcessProfileResolver::Resolve(
    const std::string& guid, const AssetDatabase& database) const {
    if (guid.empty()) {
        return {nullptr, PostProcessProfileResolveStatus::Missing,
                "post-process profile GUID is empty"};
    }
    const AssetRecord* record = database.Find(guid);
    if (!record) {
        return {nullptr, PostProcessProfileResolveStatus::Missing,
                "post-process profile GUID is unresolved: " + guid};
    }
    if (record->importer != "PostProcessProfileImporter") {
        return {nullptr, PostProcessProfileResolveStatus::TypeMismatch,
                "asset '" + guid + "' is not a post-process profile"};
    }

    const auto preview = transient_.find(guid);
    if (preview != transient_.end() && preview->second) {
        return {preview->second, PostProcessProfileResolveStatus::TransientPreview, {}};
    }

    auto cached = cache_.find(guid);
    if (record->importFailed) {
        if (cached != cache_.end() && cached->second.lastGood) {
            return {cached->second.lastGood, PostProcessProfileResolveStatus::LastGood,
                    record->importError};
        }
        return {nullptr, PostProcessProfileResolveStatus::ImportFailed,
                record->importError};
    }
    if (cached != cache_.end() && cached->second.lastGood &&
        !record->hash.empty() && cached->second.hash == record->hash) {
        return {cached->second.lastGood, PostProcessProfileResolveStatus::Resolved, {}};
    }

    PostProcessProfile2D profile;
    std::string error;
    const std::filesystem::path source = database.AbsoluteSourcePath(guid);
    if (!source.empty() && PostProcessProfile2D::LoadFromFile(source, profile, &error)) {
        auto shared = std::make_shared<const PostProcessProfile2D>(std::move(profile));
        cache_[guid] = CacheEntry{record->hash, shared};
        return {std::move(shared), PostProcessProfileResolveStatus::Resolved, {}};
    }
    if (cached != cache_.end() && cached->second.lastGood) {
        return {cached->second.lastGood, PostProcessProfileResolveStatus::LastGood,
                error.empty() ? "post-process profile source is missing" : error};
    }
    return {nullptr, PostProcessProfileResolveStatus::ImportFailed,
            error.empty() ? "post-process profile source is missing" : error};
}

PostProcessProfileResolveResult PostProcessProfileResolver::Resolve(
    const std::string& guid) const {
    return Resolve(guid, AssetDatabase::Get());
}

bool PostProcessProfileResolver::SetTransientOverride(
    const std::string& guid, const PostProcessProfile2D& profile,
    std::string* errorOut) {
    if (guid.empty()) {
        SetError(errorOut, "transient post-process preview requires a GUID");
        return false;
    }
    PostProcessProfile2D validated;
    std::string error;
    if (!PostProcessProfile2D::Deserialize(profile.Serialize(), validated, &error)) {
        SetError(errorOut, error);
        return false;
    }
    transient_[guid] =
        std::make_shared<const PostProcessProfile2D>(std::move(validated));
    SetError(errorOut, {});
    return true;
}

void PostProcessProfileResolver::ClearTransientOverride(const std::string& guid) {
    transient_.erase(guid);
}

void PostProcessProfileResolver::ClearAllTransientOverrides() {
    transient_.clear();
}

bool PostProcessProfileResolver::HasTransientOverride(const std::string& guid) const {
    return transient_.find(guid) != transient_.end();
}

void PostProcessProfileResolver::Invalidate(const std::string& guid) {
    cache_.erase(guid);
}

void PostProcessProfileResolver::ClearForTesting() {
    cache_.clear();
    transient_.clear();
}

} // namespace molga

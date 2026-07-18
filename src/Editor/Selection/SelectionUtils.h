#pragma once

#include "ECS/GameObject.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <vector>

namespace molga {

// Removes duplicates, missing IDs, and descendants whose selected ancestor is
// already in the set. Input order is retained so hierarchy/command results are
// deterministic.
inline std::vector<unsigned int> RootMostSelection(
    const std::vector<unsigned int>& ids,
    const std::function<GameObject*(unsigned int)>& resolve) {
    std::vector<unsigned int> ordered;
    std::unordered_set<unsigned int> selected;
    for (unsigned int id : ids) {
        if (id != 0 && selected.insert(id).second && resolve(id)) ordered.push_back(id);
    }

    std::vector<unsigned int> roots;
    roots.reserve(ordered.size());
    for (unsigned int id : ordered) {
        GameObject* object = resolve(id);
        bool hasSelectedAncestor = false;
        for (GameObject* parent = object ? object->GetParent() : nullptr;
             parent; parent = parent->GetParent()) {
            if (selected.count(parent->GetID()) != 0) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor) roots.push_back(id);
    }
    return roots;
}

inline std::vector<GameObject*> RootMostObjects(
    const std::vector<GameObject*>& objects) {
    std::vector<unsigned int> ids;
    ids.reserve(objects.size());
    for (GameObject* object : objects) if (object) ids.push_back(object->GetID());
    auto resolve = [&objects](unsigned int id) -> GameObject* {
        auto it = std::find_if(objects.begin(), objects.end(),
                               [id](GameObject* object) {
                                   return object && object->GetID() == id;
                               });
        return it == objects.end() ? nullptr : *it;
    };
    std::vector<GameObject*> roots;
    for (unsigned int id : RootMostSelection(ids, resolve)) roots.push_back(resolve(id));
    return roots;
}

// Appends exactly the rows a tree UI can display: the root itself and only the
// descendants whose complete ancestor chain is expanded.
inline void AppendVisibleHierarchyDfs(
    GameObject* root,
    const std::function<bool(const GameObject&)>& isExpanded,
    std::vector<unsigned int>& output) {
    if (!root) return;
    output.push_back(root->GetID());
    if (!isExpanded(*root)) return;
    for (GameObject* child : root->GetChildren()) {
        AppendVisibleHierarchyDfs(child, isExpanded, output);
    }
}

inline void AppendVisibleHierarchyForestDfs(
    const std::vector<GameObject*>& roots,
    const std::function<bool(const GameObject&)>& isExpanded,
    std::vector<unsigned int>& output) {
    for (GameObject* root : roots) {
        AppendVisibleHierarchyDfs(root, isExpanded, output);
    }
}

} // namespace molga

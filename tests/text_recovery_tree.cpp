#include "text_recovery_tree.h"

#include "text_dependency_authorities.h"
#include "text_dependency_test_support.h"

#include <iostream>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace molga::text_test {
namespace {

constexpr char kRecoveryTreeSlug[] = "text-make-recovery";

std::string CMakeCacheEntry(const std::filesystem::path& tree,
                            const std::string& key) {
    std::ifstream cache(tree / "CMakeCache.txt");
    REQUIRE_MESSAGE(cache.good(), (tree / "CMakeCache.txt").string());
    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(cache, line)) {
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return {};
}

// A run killed by a ctest timeout, a Ctrl-C, or a cancelled CI job never
// reaches its own cleanup, and nothing else collects what it left: `make clean`
// does not descend into a nested tree, and each one carries a complete nested
// ICU build. Sweeping every sibling of this slug also clears this pid's own
// slot, so the caller does not need to.
//
// This assumes one recovery run at a time per slug. RUN_SERIAL covers
// ctest-against-ctest, but a developer starting this binary by hand beside a
// running ctest will have their sweep delete the other run's in-flight tree and
// kill it mid-build. Accepted deliberately: the casualty is a throwaway tree,
// and pid-liveness checking would cost more than the leak it prevents.
void RemoveStaleTrees(const std::string& slug) {
    const std::string prefix = slug + "-";
    // Every match is collected before any is removed: unlinking entries while
    // readdir is still walking the same directory can skip siblings, which
    // would silently leave exactly the leak this is here to collect.
    std::vector<std::filesystem::path> stale;
    for (const auto& entry : std::filesystem::directory_iterator(BinaryRoot())) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            stale.push_back(entry.path());
        }
    }
    for (const auto& leaked : stale) {
        // Guarded on the resolved path but removed by the entry itself.
        // remove_all follows a symlink, so a link named with this prefix would
        // resolve to something that passes both guards and then destroy the
        // real directory it points at; unlinking the entry removes the link.
        // directory_iterator already guarantees it is a direct child here, so
        // resolution is what introduces the escape, not what prevents it.
        const auto resolved = std::filesystem::weakly_canonical(leaked);
        REQUIRE_MESSAGE(PathIsUnder(resolved, BinaryRoot()), resolved.string());
        REQUIRE(resolved != std::filesystem::weakly_canonical(BinaryRoot()));
        std::filesystem::remove_all(leaked);
    }
}

// A throwaway full build tree pinned to the generator whose byproduct handling
// these proofs are about. The slug is a parameter only so the sweep above and
// the tree name cannot disagree.
std::filesystem::path ConfigureUnixMakefilesTree(const std::string& slug) {
    const auto tree = BinaryRoot() / (slug + "-" + std::to_string(::getpid()));
    REQUIRE(PathIsUnder(tree, BinaryRoot()));
    RemoveStaleTrees(slug);
    std::filesystem::create_directories(tree);

    REQUIRE(RunWithoutShell({MOLGA_CMAKE_COMMAND,
                             "-S", MOLGA_SOURCE_DIR,
                             "-B", tree.string(),
                             "-G", "Unix Makefiles",
                             "-DCMAKE_BUILD_TYPE=Debug",
                             // box2d is the one dependency the project fetches
                             // over the network, and a virgin binary dir would
                             // re-clone it. Point it at the checkout this build
                             // tree already has so an offline run — or a GitHub
                             // outage — cannot fail a proof about ICU.
                             "-DFETCHCONTENT_SOURCE_DIR_BOX2D=" +
                                 (BinaryRoot() / "_deps/box2d-src").string()}) == 0);

    // Ninja gives every declared byproduct a real file-level rule, so the
    // repair path under test would never be reached. A tree that silently came
    // up on another generator voids the proof rather than weakening it.
    REQUIRE(CMakeCacheEntry(tree, "CMAKE_GENERATOR:INTERNAL") == "Unix Makefiles");
    return tree;
}

struct SharedTreeHolder {
    std::filesystem::path path;
    // Counted rather than latched to a bool. A REQUIRE throw aborts only the
    // case that tripped it — doctest runs the rest — so a single "retain" flag
    // cleared by each mark would let the cases *after* a failure clear it again
    // and delete the tree, along with the captured build logs inside it, which
    // is exactly the evidence the failing case needed and costs five minutes to
    // reproduce. Pairing acquisitions against marks needs no case count and
    // cannot be got wrong by adding a case: a case that took the tree and never
    // marked it keeps the tree, whatever ran afterwards.
    //
    // Every assertion in these cases is therefore also a REQUIRE: a bare CHECK
    // would fail the run and still let the case fall through to its mark.
    int acquisitions = 0;
    int marks = 0;

    ~SharedTreeHolder() {
        if (path.empty()) return;
        if (marks != acquisitions) {
            std::cerr << "retained make-recovery tree for inspection (" << marks
                      << " of " << acquisitions << " cases verified it): "
                      << path.string() << "\n";
            return;
        }
        // A destructor is implicitly noexcept, and the throwing overload of
        // remove_all would turn a filesystem error here — a permission change,
        // a vanished directory — into std::terminate *after* the run reported
        // success. A leaked throwaway tree is worth far less than a truthful
        // exit code, and the next run's slug sweep collects it anyway.
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

SharedTreeHolder& SharedTreeState() {
    static SharedTreeHolder holder;
    return holder;
}

}  // namespace

const std::filesystem::path& SharedRecoveryTree() {
    SharedTreeHolder& holder = SharedTreeState();
    if (holder.path.empty()) {
        holder.path = ConfigureUnixMakefilesTree(kRecoveryTreeSlug);
        MESSAGE("Make-generator recovery tree: " << holder.path.string());
        // Bring it to the state every case starts from. The HarfBuzz wrapper
        // pulls the whole ICU chain behind it, so this one build also leaves
        // the ICU case's first build a no-op.
        REQUIRE(BuildTreeTarget(holder.path, "molga_text_harfbuzz_raw_install") == 0);
    }
    ++holder.acquisitions;
    return holder.path;
}

void RequireSharedTreeEntry(const std::filesystem::path& tree) {
    INFO("shared tree entry check");
    RequireTextDependenciesVerified(tree);
}

void MarkSharedTreeVerified(const std::filesystem::path& tree) {
    {
        INFO("shared tree exit check");
        RequireTextDependenciesVerified(tree);
    }
    ++SharedTreeState().marks;
}

int BuildTreeTarget(const std::filesystem::path& tree, const std::string& target) {
    return RunWithoutShell(
        {MOLGA_CMAKE_COMMAND, "--build", tree.string(), "--target", target});
}

}  // namespace molga::text_test

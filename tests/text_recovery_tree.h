#pragma once

// The throwaway Unix Makefiles build tree the generator-recovery proofs run in,
// and its lifetime.
//
// Configuring one is about a minute and the nested ICU build about three more,
// so a tree per case would multiply four minutes by the number of proofs. One
// tree is shared instead, which imposes the rule that every case must leave it
// verified-good — which is also what makes the cases order-independent.
//
// Shared by text_make_recovery.cpp and text_harfbuzz_host_resolution.cpp.

#include <filesystem>
#include <string>

namespace molga::text_test {

// The one configured tree, brought to a verified-good state on first use.
// Exactly one call per test case: the holder counts acquisitions against marks
// to decide whether the tree survives the run, so a case that took the tree
// twice would let a failure elsewhere go uncounted.
const std::filesystem::path& SharedRecoveryTree();

// The entry check, first statement of every case: the tree as this case
// received it. Paired with the exit check inside MarkSharedTreeVerified so a
// case that inherits a tree the previous case broke says so, instead of failing
// as though it broke the tree itself.
void RequireSharedTreeEntry(const std::filesystem::path& tree);

// The exit check and the mark, in that order and inseparably. Verifying the
// whole tree here rather than at each call site is what stops a later case from
// silently dropping the postcondition that makes these cases
// order-independent. A case that trips a REQUIRE never reaches this, and the
// tree — including the captured build logs inside it — is then kept.
void MarkSharedTreeVerified(const std::filesystem::path& tree);

int BuildTreeTarget(const std::filesystem::path& tree, const std::string& target);

}  // namespace molga::text_test

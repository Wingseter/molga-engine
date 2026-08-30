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
// Exactly one call per test case: the holder pairs acquisitions against marks
// to decide whether the tree survives the run.
const std::filesystem::path& SharedRecoveryTree();

// Called at the end of a case that reached its own postcondition. A case that
// trips a REQUIRE never gets here, and the tree is then kept for inspection.
void MarkSharedTreeVerified();

int BuildTreeTarget(const std::filesystem::path& tree, const std::string& target);

}  // namespace molga::text_test

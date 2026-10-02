#pragma once

#include <cstddef>

namespace molga::text_test {

// The counting seam over the four ICU entry points the text runtime routes
// through: udata_setCommonData, udata_setFileAccess, u_init and u_cleanup.
//
// This translation unit links into molga_text_runtime_probe and nothing else.
// The editor, the development runtime and molga_core keep the production
// default table, and the private hook itself refuses a replacement once any
// lifecycle transition has happened, so the counts below can only ever
// describe a process that installed them before its first Initialize.
bool InstallCountingIcuRuntimeApi();

// Every routed ICU call this process has made.
std::size_t TotalIcuCalls() noexcept;

// Freezes IcuCallsBeforePublish() at the current total. Called exactly once,
// immediately after an Initialize published ready state.
void MarkReadyPublished() noexcept;

// Calls made before ready state was published. When no lifetime in this
// process ever reached ready, that is simply every call it made — which is the
// number every fail-closed case wants to see equal zero.
std::size_t IcuCallsBeforePublish() noexcept;

// Calls made strictly after the terminal u_cleanup returned. The counting
// u_cleanup forwarder marks that boundary itself, so no caller has to know
// when cleanup happened.
std::size_t IcuCallsAfterTerminal() noexcept;

// The exact `access` argument the routed udata_setFileAccess received, spelled
// as its UDataFileAccess enumerator; the empty string until that call happens.
//
// A count cannot see a weakened mode: forwarding UDATA_FILES_FIRST is still one
// call, so every icuCalls* assertion holds while the sealed package stops being
// the only source of data. The enumerator name rather than the number because
// the parent test links molga_core, which keeps ICU private and gives its
// consumers no ICU include path — it cannot name UDATA_NO_FILES itself.
const char* RoutedFileAccessArgumentName() noexcept;

}  // namespace molga::text_test

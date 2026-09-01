#include "TextRuntimeDependenciesTestAccess.h"

#include "Text/TextRuntimeDependencies.h"

namespace molga::text_test {
namespace {

std::size_t g_total          = 0;
std::size_t g_afterTerminal  = 0;
std::size_t g_beforePublish  = 0;
bool        g_publishMarked  = false;
bool        g_terminal       = false;

void Count() {
    ++g_total;
    if (g_terminal) ++g_afterTerminal;
}

const molga::text::detail::IcuRuntimeApi& Production() {
    return molga::text::detail::DefaultIcuRuntimeApi();
}

void CountingSetCommonData(const void* data, int* status) {
    Count();
    Production().setCommonData(data, status);
}

void CountingSetFileAccess(int access, int* status) {
    Count();
    Production().setFileAccess(access, status);
}

void CountingInit(int* status) {
    Count();
    Production().init(status);
}

void CountingCleanup() {
    Count();
    Production().cleanup();
    // Everything from here on is a call the design forbids outright, so it is
    // counted apart from the calls that legitimately built the lifetime.
    g_terminal = true;
}

}  // namespace

bool InstallCountingIcuRuntimeApi() {
    molga::text::detail::IcuRuntimeApi counting;
    counting.setCommonData = &CountingSetCommonData;
    counting.setFileAccess = &CountingSetFileAccess;
    counting.init          = &CountingInit;
    counting.cleanup       = &CountingCleanup;
    return molga::text::detail::InstallIcuRuntimeApi(counting);
}

std::size_t TotalIcuCalls() noexcept { return g_total; }

void MarkReadyPublished() noexcept {
    g_beforePublish = g_total;
    g_publishMarked = true;
}

std::size_t IcuCallsBeforePublish() noexcept {
    return g_publishMarked ? g_beforePublish : g_total;
}

std::size_t IcuCallsAfterTerminal() noexcept { return g_afterTerminal; }

}  // namespace molga::text_test

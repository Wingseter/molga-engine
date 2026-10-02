#include "TextRuntimeDependenciesTestAccess.h"

#include "Text/TextRuntimeDependencies.h"

#include <unicode/udata.h>

namespace molga::text_test {
namespace {

std::size_t g_total          = 0;
std::size_t g_afterTerminal  = 0;
std::size_t g_beforePublish  = 0;
bool        g_publishMarked  = false;
bool        g_terminal       = false;
const char* g_fileAccessName = "";

// Every UDataFileAccess spelled out, so a weakened mode is reported by name
// instead of vanishing into a call count. An unrecognised value is reported as
// unknown rather than silently mapped onto the sealed one.
const char* FileAccessName(int access) {
    switch (static_cast<UDataFileAccess>(access)) {
        case UDATA_FILES_FIRST:
            return "UDATA_FILES_FIRST";
        case UDATA_ONLY_PACKAGES:
            return "UDATA_ONLY_PACKAGES";
        case UDATA_PACKAGES_FIRST:
            return "UDATA_PACKAGES_FIRST";
        case UDATA_NO_FILES:
            return "UDATA_NO_FILES";
        default:
            return "UNKNOWN_UDATA_FILE_ACCESS";
    }
}

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
    // Recorded before the forward, so the argument is captured even if ICU
    // rejects it and the lifetime unwinds.
    g_fileAccessName = FileAccessName(access);
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

const char* RoutedFileAccessArgumentName() noexcept { return g_fileAccessName; }

}  // namespace molga::text_test

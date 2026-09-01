// The only main for session-backed text runtime tests. The session is
// initialized once before any case runs and cleaned up once immediately before
// the process returns, because a text runtime lifetime is terminal and cannot
// be restarted inside a process.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "TextRuntimeTestSession.h"
int main(int argc, char** argv) {
    doctest::Context context(argc, argv);
    TextRuntimeTestSession session(MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT);
    if (!session.Initialize()) return 4;
    TextRuntimeTestSession::Install(&session);
    const int result = context.run();
    if (!session.ShutdownAfterTests()) return 5;
    return result;
}

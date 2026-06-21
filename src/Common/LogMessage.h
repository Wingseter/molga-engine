#pragma once

#include <cstdint>
#include <string>
#include <thread>

namespace Log {

enum class Severity { Trace, Info, Warning, Error, Fatal };
enum class LogContext { Editor, Runtime, Build, ScriptCompiler, Importer };

// 모든 sink가 받는 구조화 로그 레코드. 값 타입(복사 가능) — thread 간 안전 전달용.
struct LogMessage {
    std::uint64_t   sequence   = 0;          // Emit이 채움(단조 증가)
    std::int64_t    timestampMs = 0;         // Emit이 채움(epoch ms)
    std::thread::id threadId{};              // Emit이 채움
    Severity        severity   = Severity::Info;
    LogContext      context    = LogContext::Editor;
    std::string     category;                // 기존 "tag" (예: "Renderer")
    std::string     message;
    std::string     sourceFile;              // 엔진 소스 위치(선택, __FILE__)
    int             sourceLine = 0;
    std::string     externalPath;            // 사용자 스크립트/에셋 경로(컴파일 진단)
    int             externalLine = 0;
    std::string     stack;                   // 선택적 멀티라인 stack/detail
    std::uint32_t   repeatCount = 1;         // 콘솔 collapse가 채움(발행 시 항상 1)
};

} // namespace Log

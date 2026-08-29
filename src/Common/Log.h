#pragma once

#include "Common/LogMessage.h"
#include <memory>
#include <string>
#include <vector>

namespace Log {

class ILogSink;

// 구조화 발행: 어느 thread에서도 호출 가능. sequence/timestamp/threadId를 채운 뒤
// 등록된 모든 sink로 fan-out 한다.
void Emit(const LogMessage& m);

// sink 레지스트리 (등록은 main thread에서 한다고 가정; fan-out 자체는 thread-safe).
void AddSink(std::shared_ptr<ILogSink> sink);
void RemoveSink(const std::shared_ptr<ILogSink>& sink);
void ClearSinks();

// 전역 sink 구성을 잠깐 바꿨다가 원상복구해야 하는 호출처(주로 테스트 scope 어댑터)를
// 위한 최소 API. SnapshotSinks는 현재 등록 목록의 복사본을 돌려주고, RestoreSinks는
// 그 목록으로 통째로 되돌린다. 둘 다 thread-safe이며 Emit의 fan-out 동작은 바뀌지 않는다.
std::vector<std::shared_ptr<ILogSink>> SnapshotSinks();
void RestoreSinks(std::vector<std::shared_ptr<ILogSink>> sinks);

// 기존 호출처(17파일)를 깨지 않는 편의 함수 — 내부적으로 Emit으로 위임한다.
void Info(const std::string& tag, const std::string& msg);
void Warn(const std::string& tag, const std::string& msg);
void Error(const std::string& tag, const std::string& msg);

} // namespace Log

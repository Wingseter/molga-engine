# Molga Engine: 플랫폼 지원 및 빌드 파이프라인 리서치 보고서

> 작성일: 2026-03-22
> 대상: Molga Engine (C++17, CMake, OpenGL 3.3 Core, macOS)
> 목적: Unity 수준의 플랫폼/툴링 시스템 도입을 위한 기술 조사

---

## 목차

1. [크로스 플랫폼 빌드](#1-크로스-플랫폼-빌드)
2. [WebGL/Emscripten 지원](#2-webglemscripten-지원)
3. [테스팅 프레임워크](#3-테스팅-프레임워크)
4. [CI/CD 파이프라인](#4-cicd-파이프라인)
5. [에셋 번들 / 패키징](#5-에셋-번들--패키징)
6. [로컬라이제이션 시스템](#6-로컬라이제이션-시스템)
7. [플러그인/확장 시스템](#7-플러그인확장-시스템)
8. [문서화 및 API 레퍼런스](#8-문서화-및-api-레퍼런스)
9. [버전 관리 통합](#9-버전-관리-통합)
10. [성능 모니터링](#10-성능-모니터링)
11. [우선순위 종합표](#11-우선순위-종합표)

---

## 1. 크로스 플랫폼 빌드

### 1.1 개요 및 중요성

크로스 플랫폼 빌드는 하나의 코드베이스에서 여러 OS/아키텍처 타겟을 생성하는 시스템이다. 인디 2D 게임 엔진에서 이것은 도달 가능한 유저 수를 직접적으로 결정하며, Steam 기준으로 Windows가 전체 유저의 약 96%, macOS가 약 2.5%, Linux가 약 1.5%를 차지한다. 모바일까지 포함하면 시장 규모가 근본적으로 달라진다.

### 1.2 Unity의 접근 방식

Unity는 다음 플랫폼을 지원한다:

| 플랫폼 | 그래픽스 API | 빌드 방식 |
|---------|-------------|-----------|
| Windows | DirectX 11/12, Vulkan, OpenGL | IL2CPP / Mono |
| macOS | Metal, OpenGL (deprecated) | IL2CPP |
| Linux | Vulkan, OpenGL | IL2CPP |
| iOS | Metal | IL2CPP + Xcode |
| Android | OpenGL ES 3.0+, Vulkan | Gradle + IL2CPP |
| WebGL | WebGL 2.0 | Emscripten + IL2CPP |

Unity의 핵심 설계 원칙:
- **Platform Abstraction Layer (PAL)**: 그래픽스, 오디오, 입력, 파일 I/O를 인터페이스 뒤에 숨김
- **Build Profiles**: 에디터 내에서 타겟 플랫폼별 설정 프로파일 관리
- **Player Settings**: 해상도, 아이콘, 번들 ID, 서명 등 플랫폼별 설정
- **Scripting Backend**: Mono (에디터/개발) / IL2CPP (배포)

### 1.3 Molga Engine 현재 상태 분석

현재 `Platform.h/cpp`에 기본적인 플랫폼 추상화가 존재한다:

```
현재 구현:
- PlatformType enum: Windows, macOS, Linux, Unknown
- 동적 라이브러리 로딩: dlopen/LoadLibrary 분기
- 실행 파일 경로: _NSGetExecutablePath / readlink / GetModuleFileName
- 작업 디렉터리 관리

미구현:
- 윈도우 시스템 추상화 (GLFW 직접 사용 중)
- 오디오 백엔드 추상화 (miniaudio가 부분적으로 처리)
- 파일 I/O 추상화
- 그래픽스 API 추상화 (OpenGL 3.3 Core 고정)
- 입력 시스템 추상화
```

### 1.4 권장 구현 방법

#### Phase 1: 플랫폼 추상화 레이어 강화 (필수)

```
src/Platform/
  Platform.h              -- 기존 (유지)
  Platform.cpp            -- 기존 (유지)
  PlatformWindow.h        -- 윈도우 생성/관리 인터페이스
  PlatformAudio.h         -- 오디오 백엔드 인터페이스
  PlatformFileSystem.h    -- 파일 I/O 추상화
  PlatformInput.h         -- 입력 장치 추상화

  // 플랫폼별 구현
  Desktop/
    DesktopWindow.cpp      -- GLFW 기반 (Windows/macOS/Linux 공통)
    DesktopFileSystem.cpp
  Mobile/
    iOSWindow.mm           -- UIKit + Metal/GLES
    AndroidWindow.cpp      -- NativeActivity + EGL
  Web/
    WebWindow.cpp           -- Emscripten + Canvas
```

핵심 인터페이스 설계 방향:

```cpp
// PlatformWindow.h -- 추상 윈도우 인터페이스
class IPlatformWindow {
public:
    virtual ~IPlatformWindow() = default;
    virtual bool Create(int width, int height, const char* title) = 0;
    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;
    virtual bool ShouldClose() = 0;
    virtual void SetFullscreen(bool enabled) = 0;
    virtual void GetSize(int& w, int& h) = 0;
};

// PlatformFileSystem.h -- 파일 시스템 추상화
class IPlatformFileSystem {
public:
    virtual ~IPlatformFileSystem() = default;
    virtual std::vector<uint8_t> ReadFile(const std::string& path) = 0;
    virtual bool WriteFile(const std::string& path, const void* data, size_t size) = 0;
    virtual bool FileExists(const std::string& path) = 0;
    virtual std::string GetAssetPath(const std::string& relative) = 0;
};
```

#### Phase 2: CMake 크로스 컴파일 설정

```cmake
# cmake/toolchains/windows-mingw.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# cmake/toolchains/emscripten.cmake
# Emscripten SDK가 자체 제공

# cmake/toolchains/ios.cmake
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
set(CMAKE_OSX_ARCHITECTURES "arm64")
```

CMakeLists.txt에 플랫폼 분기 추가:

```cmake
# 플랫폼 감지 및 설정
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(MOLGA_PLATFORM_WEB ON)
    set(MOLGA_GRAPHICS_API "WebGL")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MOLGA_PLATFORM_IOS ON)
    set(MOLGA_GRAPHICS_API "OpenGLES")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(MOLGA_PLATFORM_ANDROID ON)
    set(MOLGA_GRAPHICS_API "OpenGLES")
else()
    set(MOLGA_PLATFORM_DESKTOP ON)
    set(MOLGA_GRAPHICS_API "OpenGL")
endif()

# 플랫폼별 소스 파일 선택
if(MOLGA_PLATFORM_DESKTOP)
    set(PLATFORM_SOURCES
        src/Platform/Desktop/DesktopWindow.cpp
        src/Platform/Desktop/DesktopFileSystem.cpp
    )
elseif(MOLGA_PLATFORM_WEB)
    set(PLATFORM_SOURCES
        src/Platform/Web/WebWindow.cpp
        src/Platform/Web/WebFileSystem.cpp
    )
endif()
```

#### Phase 3: 최소 실행 가능 전략 (MVP)

인디 2D 엔진의 현실적 우선순위:

| 우선순위 | 플랫폼 | 이유 | 난이도 |
|----------|--------|------|--------|
| 1순위 | Windows | Steam 유저 96%, GLFW가 이미 지원 | 낮음 |
| 2순위 | Linux | Steam Deck 성장세, GLFW 지원 | 낮음 |
| 3순위 | WebGL | 데모/포트폴리오 배포에 적합 | 중간 |
| 4순위 | Android | 모바일 시장 접근 | 높음 |
| 5순위 | iOS | Apple 생태계 | 높음 |

**현실적 1차 목표: Desktop 3종 (Windows + macOS + Linux)**

GLFW가 이미 3개 OS를 지원하고, miniaudio도 크로스 플랫폼이므로 실제로 필요한 작업은:
1. Windows에서 MSVC/MinGW로 빌드 확인
2. Linux에서 GCC/Clang으로 빌드 확인
3. OpenGL 로더(glad)가 각 OS에서 동작 확인
4. 파일 경로 구분자(`/` vs `\`) 처리
5. 동적 라이브러리 확장자(`.dylib` vs `.dll` vs `.so`) 처리

### 1.5 권장 도구/라이브러리

| 도구 | 용도 | 비고 |
|------|------|------|
| **CMake 3.27+** | 빌드 시스템 (기존) | 크로스 컴파일 toolchain 파일 추가 |
| **GLFW 3.4** | 데스크톱 윈도우 관리 (기존) | Win/Mac/Linux 지원 완료 |
| **miniaudio** | 크로스 플랫폼 오디오 (기존) | 헤더 온리, 모든 플랫폼 지원 |
| **glad** | OpenGL 로더 (기존) | 데스크톱용 |
| **Emscripten SDK** | WebGL 빌드 | OpenGL ES 2.0/3.0 -> WebGL 자동 변환 |
| **vcpkg 또는 Conan** | 패키지 관리 | 선택 사항, 의존성 복잡해지면 도입 |

### 1.6 복잡도 및 우선순위

- **복잡도**: Large (플랫폼 추상화 설계 + 각 플랫폼 구현 + 테스트)
- **우선순위**: **Critical** -- Windows 미지원 시 배포 자체가 사실상 불가능
- **예상 작업량**: Desktop 3종 기준 약 2-4주, 모바일 포함 시 8-12주 추가

---

## 2. WebGL/Emscripten 지원

### 2.1 개요 및 중요성

WebGL 빌드를 통해 웹 브라우저에서 직접 게임을 실행할 수 있다. itch.io, Newgrounds 등의 웹 게임 플랫폼에 배포 가능하며, 설치 없이 즉시 플레이가 가능하므로 데모, 게임잼, 포트폴리오에 적합하다. 인디 개발자에게 특히 강력한 배포 채널이다.

### 2.2 Unity의 WebGL 빌드

Unity의 WebGL 파이프라인:
- C# 코드를 IL2CPP로 C++로 변환 후 Emscripten으로 WebAssembly 컴파일
- 그래픽스: Unity 렌더 파이프라인 -> WebGL 2.0 API 호출
- 오디오: Web Audio API로 변환
- 파일 I/O: IndexedDB 기반 가상 파일시스템
- 에셋 로딩: HTTP 비동기 요청 (UnityWebRequest)
- 빌드 결과물: `.wasm` + `.js` (글루 코드) + `.data` (에셋) + `index.html`

주요 제한사항:
- 멀티스레딩 제한 (SharedArrayBuffer 필요, COOP/COEP 헤더)
- 파일시스템 접근 불가 (가상 FS 사용)
- 소켓 제한 (WebSocket/WebRTC만 가능)
- 메모리 제한 (브라우저 힙 제한)

### 2.3 Emscripten 컴파일 파이프라인 상세

Emscripten은 C/C++ 코드를 WebAssembly + JavaScript 글루 코드로 변환한다.

```
C++17 소스코드
    |
    v
Emscripten (emcc/em++) -- Clang 기반 컴파일러
    |
    v
LLVM IR
    |
    v
WebAssembly (.wasm) + JavaScript glue (.js)
    |
    v
브라우저 런타임 (V8, SpiderMonkey 등)
```

CMake 연동 방법:

```bash
# Emscripten SDK 설치 후
source /path/to/emsdk/emsdk_env.sh

# CMake에서 Emscripten 툴체인 사용
cmake -B build_web \
  -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build_web
```

### 2.4 OpenGL 3.3 Core vs WebGL 차이점 (Molga Engine 영향 분석)

Molga Engine은 `#version 330 core` GLSL 셰이더를 사용 중이다. 이것이 WebGL 전환에서 가장 큰 기술적 과제이다.

| 항목 | OpenGL 3.3 Core (현재) | WebGL 2.0 (= OpenGL ES 3.0) | 필요한 변경 |
|------|----------------------|---------------------------|------------|
| GLSL 버전 | `#version 330 core` | `#version 300 es` | 셰이더 전부 수정 또는 전처리 |
| 정밀도 한정자 | 선택사항 | 필수 (`precision mediump float;`) | 프래그먼트 셰이더에 추가 |
| `in`/`out` 키워드 | 지원 | 지원 (ES 3.0) | 호환 |
| `texture()` 함수 | 지원 | 지원 (ES 3.0) | 호환 |
| `glGenVertexArrays` | 지원 | 지원 (WebGL 2.0) | 호환 |
| Uniform Block | 지원 | 지원 (WebGL 2.0) | 호환 |
| `glMapBuffer` | 지원 | 미지원 | `glBufferSubData` 사용 |
| 윈도우 컨텍스트 | GLFW | HTML Canvas + `emscripten_set_main_loop` | 메인 루프 구조 변경 필수 |
| 파일 로딩 | `std::ifstream` | 사전 패키징 또는 비동기 fetch | 에셋 로딩 재설계 |

### 2.5 필요한 핵심 변경사항

#### (1) 메인 루프 구조 변경

브라우저는 블로킹 `while` 루프를 허용하지 않는다. Emscripten의 메인 루프 콜백을 사용해야 한다:

```cpp
// 현재 구조 (추정)
while (!window.ShouldClose()) {
    Update();
    Render();
    window.SwapBuffers();
    window.PollEvents();
}

// WebGL 호환 구조
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void MainLoopCallback() {
    Update();
    Render();
    // SwapBuffers는 emscripten이 자동 처리
}

int main() {
    Init();
    emscripten_set_main_loop(MainLoopCallback, 0, 1);
    // 여기서 리턴하면 안 됨 (emscripten이 제어)
}
#else
// 기존 while 루프
#endif
```

#### (2) 셰이더 호환성 레이어

Molga Engine의 `default.vert`와 `default.frag`는 `#version 330 core`를 사용한다. 두 가지 접근법이 있다:

**방법 A: 셰이더 전처리기 (권장)**

```cpp
std::string Shader::PreprocessSource(const std::string& source, bool isFragment) {
    std::string result;
    #ifdef __EMSCRIPTEN__
        result = "#version 300 es\n";
        if (isFragment) {
            result += "precision mediump float;\n";
        }
        // #version 330 core 라인 제거하고 나머지 추가
    #else
        result = source; // 그대로 사용
    #endif
    return result;
}
```

**방법 B: 듀얼 셰이더 (관리 비용 높음)**

```
src/Shaders/
    GL330/default.vert    -- 데스크톱용
    GL330/default.frag
    GLES300/default.vert  -- WebGL/모바일용
    GLES300/default.frag
```

방법 A가 2D 엔진에서는 훨씬 실용적이다. 셰이더가 단순하기 때문이다.

#### (3) 에셋 로딩 변경

```cpp
// 파일시스템 기반 (데스크톱)
Texture tex("assets/player.png");

// WebGL: Emscripten 사전 패키징
// CMakeLists.txt에서:
// target_link_options(molga_runtime PRIVATE
//     "--preload-file" "${CMAKE_SOURCE_DIR}/assets@/assets")
//
// 이렇게 하면 assets 폴더가 .data 파일로 패키징되어
// 가상 파일시스템에 마운트됨. 기존 fopen/ifstream 코드 그대로 동작.
```

#### (4) GLFW 대체

Emscripten은 GLFW 에뮬레이션을 제공하지만 제한적이다. 두 가지 선택지:

- **Emscripten GLFW3 에뮬레이션**: `-s USE_GLFW=3` 플래그로 활성화. 기본 기능 동작하지만 일부 함수 미지원
- **SDL2**: Emscripten이 SDL2를 네이티브 수준으로 지원 (`-s USE_SDL=2`). 더 안정적이지만 GLFW에서 마이그레이션 필요

**권장**: Emscripten의 GLFW3 에뮬레이션 먼저 시도. Molga Engine이 GLFW의 기본 기능(윈도우 생성, 이벤트 루프, OpenGL 컨텍스트)만 사용한다면 대부분 동작한다.

### 2.6 CMake WebGL 빌드 설정

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(MOLGA_PLATFORM_WEB ON)

    # GLFW 에뮬레이션 사용
    target_link_options(molga_runtime PRIVATE
        "-s" "USE_GLFW=3"
        "-s" "USE_WEBGL2=1"        # WebGL 2.0 활성화
        "-s" "FULL_ES3=1"          # OpenGL ES 3.0 전체 에뮬레이션
        "-s" "ALLOW_MEMORY_GROWTH=1"
        "-s" "INITIAL_MEMORY=67108864"  # 64MB 초기 메모리
        "--preload-file" "${CMAKE_SOURCE_DIR}/assets@/assets"
        "--preload-file" "${CMAKE_SOURCE_DIR}/src/Shaders@/Shaders"
    )

    # HTML 셸 템플릿 (선택)
    target_link_options(molga_runtime PRIVATE
        "--shell-file" "${CMAKE_SOURCE_DIR}/web/shell.html"
    )

    # 출력 확장자를 .html로
    set_target_properties(molga_runtime PROPERTIES SUFFIX ".html")

    # glad 대신 Emscripten의 GL 사용
    # glad 라이브러리를 빌드에서 제외하고 Emscripten GL 헤더 사용
endif()
```

### 2.7 glad/OpenGL 로더 처리

Emscripten은 자체 OpenGL ES 헤더를 제공하므로 glad가 필요 없다:

```cpp
// src/Rendering/GLIncludes.h
#pragma once

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
    #include <emscripten.h>
#else
    #include <glad/glad.h>
#endif
```

모든 렌더링 소스에서 `<glad/glad.h>` 직접 include 대신 `GLIncludes.h`를 사용하도록 변경.

### 2.8 복잡도 및 우선순위

- **복잡도**: Medium-Large
  - 메인 루프 구조 변경: 1-2일
  - 셰이더 호환성 레이어: 1-2일
  - OpenGL 로더 추상화: 1일
  - 에셋 패키징: 1일
  - 테스트/디버깅: 3-5일
- **우선순위**: **High** -- 웹 배포는 인디 게임의 가시성을 극적으로 높임
- **예상 작업량**: 약 2-3주

---

## 3. 테스팅 프레임워크

### 3.1 개요 및 중요성

게임 엔진에서 테스팅은 리팩터링 안전성, 회귀 방지, 크로스 플랫폼 동작 보장의 핵심이다. 현재 Molga Engine은 4개의 CTest 기반 테스트(`test_types`, `test_collision`, `test_ecs`, `test_scene_serializer`)를 `<cassert>` 매크로로 작성하고 있다.

### 3.2 Unity의 테스트 러너

Unity Test Framework는 NUnit 기반이며 두 모드를 제공한다:

- **Edit Mode 테스트**: 에디터 환경에서 실행. MonoBehaviour 없이 순수 로직 테스트. 빠른 실행.
- **Play Mode 테스트**: 런타임 환경에서 실행. 게임 오브젝트 생성, 물리 시뮬레이션, 코루틴 대기 가능. 실제 게임 동작 테스트.

추가로:
- **Code Coverage**: 코드 커버리지 패키지로 커버리지 측정
- **Performance Testing**: Unity Performance Testing Extension으로 성능 벤치마크
- **Graphics Tests**: Graphics Test Framework로 렌더링 결과 비교

### 3.3 C++ 게임 엔진에 필요한 테스트 유형

| 테스트 유형 | 설명 | Molga 적용 대상 | 현재 상태 |
|------------|------|-----------------|-----------|
| **단위 테스트** | 개별 함수/클래스 격리 테스트 | ECS, Transform, Collision, Serializer | 4개 존재 (cassert) |
| **통합 테스트** | 다중 시스템 상호작용 | Scene 로드 -> ECS -> 렌더링 | 없음 |
| **시각적 회귀 테스트** | 렌더링 결과 이미지 비교 | Sprite, Tilemap, Animation 렌더링 | 없음 |
| **성능 벤치마크** | 프레임 시간, 메모리 사용량 | 렌더러, 물리, 입력 루프 | 없음 |
| **퍼즈 테스트** | 무작위 입력으로 크래시 탐지 | 파서, 직렬화기 | 없음 |

### 3.4 Google Test vs Catch2 비교

| 기준 | Google Test (gtest) | Catch2 v3 |
|------|--------------------:|----------:|
| **도입 용이성** | CMake `FetchContent`로 쉬움 | 헤더 온리(v2) / CMake(v3) |
| **문법** | `TEST(Suite, Name)`, `EXPECT_EQ` | `TEST_CASE("name")`, `REQUIRE()` |
| **BDD 스타일** | 미지원 | `SECTION` 블록으로 BDD 지원 |
| **Mocking** | Google Mock 내장 | 별도 라이브러리 필요 |
| **실행 속도** | 빠름 | v3에서 대폭 개선 |
| **CI 통합** | JUnit XML 출력 | JUnit XML, TAP 출력 |
| **커뮤니티** | 대형 프로젝트 표준 | 중소규모 프로젝트 인기 |
| **C++17 호환** | 완전 지원 | 완전 지원 |
| **벤치마크** | Google Benchmark 별도 | `BENCHMARK` 매크로 내장 |
| **CMake 통합** | `GTest::gtest` 타겟 | `Catch2::Catch2` 타겟 |

#### 권장: **Catch2 v3**

이유:
1. 헤더 포함이 간결하고 `SECTION` 기반 테스트 구성이 게임 엔진에 적합
2. 벤치마크가 내장되어 있어 별도 의존성 불필요
3. Molga Engine 규모(중소규모)에 적합
4. 기존 `cassert` 기반 테스트를 `REQUIRE()`로 자연스럽게 마이그레이션 가능

### 3.5 OpenGL 모킹 전략

OpenGL 함수를 직접 호출하는 코드의 단위 테스트는 GPU/디스플레이 서버가 필요해 CI에서 실행하기 어렵다.

**전략 1: 추상 렌더링 인터페이스 (장기적 권장)**

```cpp
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual uint32_t CreateTexture(int w, int h, const void* data) = 0;
    virtual void DeleteTexture(uint32_t id) = 0;
    virtual uint32_t CreateShader(const char* vert, const char* frag) = 0;
    virtual void DrawQuad(const RenderCommand& cmd) = 0;
};

class OpenGLRenderDevice : public IRenderDevice { /* 실제 구현 */ };
class NullRenderDevice : public IRenderDevice { /* 테스트용 */ };
```

**전략 2: 헤드리스 OpenGL 컨텍스트 (통합 테스트용)**

```cpp
// OSMesa (오프스크린 Mesa) 사용
#include <GL/osmesa.h>

void SetupHeadlessGL() {
    OSMesaContext ctx = OSMesaCreateContext(OSMESA_RGBA, nullptr);
    void* buffer = malloc(800 * 600 * 4);
    OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, 800, 600);
    // 이제 GL 함수 호출 가능 (소프트웨어 렌더링)
}
```

**전략 3: 레이어 분리 (현실적 권장)**

GL 의존 코드와 순수 로직을 분리하여 순수 로직만 단위 테스트:

```
테스트 가능 (GL 없이):
- ECS (GameObject, Component) -- 이미 테스트 중
- Physics/Collision -- 이미 테스트 중
- SceneSerializer -- 이미 테스트 중
- Animation (프레임 계산 로직)
- Camera2D (행렬 계산)
- Input (키 상태 관리)
- Audio (볼륨/재생 상태 로직)

GL 의존 (통합 테스트 필요):
- Shader 컴파일
- Texture 로딩/렌더링
- Renderer draw calls
- Tilemap 렌더링
```

### 3.6 시각적 회귀 테스트

렌더링 결과가 이전 버전과 동일한지 확인하는 테스트:

```
1. "골든 이미지" 생성:
   - 헤드리스 GL 컨텍스트에서 특정 장면 렌더링
   - FBO(Framebuffer Object)로 이미지 캡처
   - tests/golden/ 디렉터리에 PNG로 저장

2. 테스트 실행:
   - 동일 장면 렌더링
   - 골든 이미지와 픽셀 비교
   - 허용 오차(threshold) 내 차이 허용 (GPU별 미세 차이)

3. 도구:
   - stb_image_write (이미 stb 의존성 있음) -- 캡처
   - 커스텀 비교 함수 또는 ImageMagick compare
   - perceptualdiff -- 지각적 차이 비교
```

### 3.7 권장 테스트 디렉터리 구조

```
tests/
  CMakeLists.txt
  unit/
    test_ecs.cpp          -- 기존 마이그레이션
    test_collision.cpp     -- 기존 마이그레이션
    test_types.cpp         -- 기존 마이그레이션
    test_serializer.cpp    -- 기존 마이그레이션
    test_camera2d.cpp      -- 신규
    test_animation.cpp     -- 신규
    test_input.cpp         -- 신규
  integration/
    test_scene_load.cpp    -- Scene 로드 -> ECS 구성 확인
    test_script_reload.cpp -- 스크립트 핫리로드
  benchmark/
    bench_ecs.cpp          -- ECS 성능 벤치마크
    bench_collision.cpp    -- 충돌 검사 성능
  visual/
    test_render_sprite.cpp -- 스프라이트 렌더링 비교
    golden/                -- 골든 이미지
```

### 3.8 CMake 통합 (Catch2)

```cmake
# tests/CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

include(Catch)  # Catch2의 CMake 헬퍼

function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core Catch2::Catch2WithMain)
    catch_discover_tests(${TEST_NAME})  # CTest 자동 등록
endfunction()
```

기존 `cassert` 테스트를 Catch2로 마이그레이션하는 예시:

```cpp
// 기존
static void test_gameobject_creation() {
    auto obj = std::make_shared<GameObject>("Player");
    assert(obj->GetName() == "Player");
    assert(obj->IsActive());
}

// Catch2
TEST_CASE("GameObject creation", "[ecs][gameobject]") {
    auto obj = std::make_shared<GameObject>("Player");
    REQUIRE(obj->GetName() == "Player");
    REQUIRE(obj->IsActive());
    REQUIRE(obj->GetID() > 0);
}

TEST_CASE("GameObject components", "[ecs][component]") {
    auto obj = std::make_shared<GameObject>("Test");

    SECTION("Add and get component") {
        Transform* t = obj->AddComponent<Transform>(10.0f, 20.0f);
        REQUIRE(t != nullptr);
        REQUIRE(t->GetX() == Catch::Approx(10.0f));
    }

    SECTION("Has component check") {
        REQUIRE_FALSE(obj->HasComponent<Transform>());
        obj->AddComponent<Transform>();
        REQUIRE(obj->HasComponent<Transform>());
    }
}
```

### 3.9 복잡도 및 우선순위

- **복잡도**: Medium
  - Catch2 도입 + 기존 테스트 마이그레이션: 2-3일
  - 신규 단위 테스트 작성: 3-5일
  - 시각적 회귀 테스트: 5-7일 (헤드리스 GL 설정 포함)
- **우선순위**: **High** -- 크로스 플랫폼 확장 시 회귀 방지 필수
- **예상 작업량**: 기본 2-3주, 시각적 테스트 포함 시 4-5주

---

## 4. CI/CD 파이프라인

### 4.1 개요 및 중요성

지속적 통합/배포는 코드 변경마다 자동으로 빌드, 테스트, 아티팩트 생성을 수행한다. 크로스 플랫폼 엔진에서는 "macOS에서 수정한 코드가 Windows에서도 컴파일되는가?"를 매 커밋마다 확인해야 한다.

### 4.2 Unity의 Cloud Build

Unity Cloud Build의 기능:
- Git/SVN 저장소 모니터링
- 타겟 플랫폼별 자동 빌드 (Windows, macOS, iOS, Android, WebGL)
- 빌드 아티팩트 저장 및 배포
- 테스트 자동 실행
- Slack/이메일 알림
- 빌드 로그 및 에러 리포팅

### 4.3 GitHub Actions 기반 CI/CD 설계

Molga Engine이 GitHub에 호스팅되어 있으므로 GitHub Actions가 가장 자연스러운 선택이다.

#### 기본 멀티 플랫폼 빌드 워크플로우

```yaml
# .github/workflows/build.yml
name: Build & Test

on:
  push:
    branches: [main, phase*]
  pull_request:
    branches: [main]

jobs:
  build-macos:
    runs-on: macos-14        # Apple Silicon (M1)
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: brew install cmake

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --parallel $(sysctl -n hw.ncpu)

      - name: Test
        run: cd build && ctest --output-on-failure

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: molga-macos
          path: build/molga_runtime

  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure
        run: cmake -B build -G "Visual Studio 17 2022" -A x64

      - name: Build
        run: cmake --build build --config Release

      - name: Test
        run: cd build && ctest -C Release --output-on-failure

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: molga-windows
          path: build/Release/molga_runtime.exe

  build-linux:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            cmake build-essential \
            libglfw3-dev libgl1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev \
            libxcursor-dev libxi-dev

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --parallel $(nproc)

      - name: Test
        run: cd build && ctest --output-on-failure

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: molga-linux
          path: build/molga_runtime

  build-web:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup Emscripten
        uses: mymindstorm/setup-emsdk@v14
        with:
          version: 3.1.51

      - name: Configure
        run: emcmake cmake -B build_web -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build_web

      - name: Upload WebGL build
        uses: actions/upload-artifact@v4
        with:
          name: molga-webgl
          path: |
            build_web/molga_runtime.html
            build_web/molga_runtime.js
            build_web/molga_runtime.wasm
            build_web/molga_runtime.data
```

#### 릴리즈 자동화

```yaml
# .github/workflows/release.yml
name: Release

on:
  push:
    tags: ['v*']

jobs:
  build:
    # ... (위 빌드 잡 참조)

  release:
    needs: [build-macos, build-windows, build-linux, build-web]
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4

      - name: Create release packages
        run: |
          cd molga-windows && zip -r ../molga-windows.zip . && cd ..
          cd molga-macos && tar czf ../molga-macos.tar.gz . && cd ..
          cd molga-linux && tar czf ../molga-linux.tar.gz . && cd ..
          cd molga-webgl && zip -r ../molga-webgl.zip . && cd ..

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            molga-windows.zip
            molga-macos.tar.gz
            molga-linux.tar.gz
            molga-webgl.zip
```

### 4.4 CI 추가 기능

| 기능 | 도구 | 용도 |
|------|------|------|
| **코드 분석** | `cppcheck`, `clang-tidy` | 정적 분석, 버그 탐지 |
| **코드 포매팅** | `clang-format` | 코드 스타일 일관성 |
| **메모리 검사** | AddressSanitizer (`-fsanitize=address`) | 메모리 오류 탐지 |
| **커버리지** | `gcov` + `lcov` 또는 `llvm-cov` | 테스트 커버리지 리포트 |
| **캐싱** | `actions/cache` | CMake 빌드 캐시, 의존성 캐시 |

정적 분석 잡 예시:

```yaml
  static-analysis:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install tools
        run: sudo apt-get install -y cppcheck clang-tidy clang-format

      - name: Run cppcheck
        run: |
          cppcheck --enable=all --suppress=missingInclude \
                   --error-exitcode=1 \
                   -I src/ src/

      - name: Check formatting
        run: |
          find src/ -name "*.cpp" -o -name "*.h" | \
            xargs clang-format --dry-run --Werror
```

### 4.5 빌드 캐싱 전략

```yaml
      - name: Cache CMake build
        uses: actions/cache@v4
        with:
          path: build
          key: ${{ runner.os }}-cmake-${{ hashFiles('CMakeLists.txt', 'src/**') }}
          restore-keys: |
            ${{ runner.os }}-cmake-
```

### 4.6 복잡도 및 우선순위

- **복잡도**: Medium
  - 기본 빌드/테스트 워크플로우: 1-2일
  - 멀티 플랫폼 확장: 2-3일
  - 릴리즈 자동화: 1-2일
  - 정적 분석/포매팅: 1일
- **우선순위**: **Critical** -- 크로스 플랫폼 확장의 전제 조건
- **예상 작업량**: 약 1-2주

---

## 5. 에셋 번들 / 패키징

### 5.1 개요 및 중요성

게임 에셋(텍스처, 오디오, 셰이더, 씬 데이터)을 효율적으로 패키징하고 로딩하는 시스템이다. 현재 Molga Engine의 `GameBuilder`는 단순히 파일을 복사하는 방식이다. 이는 작은 게임에서는 충분하지만, 에셋이 늘어나면 로딩 시간, 디스크 사용량, 배포 크기가 문제가 된다.

### 5.2 Unity의 AssetBundle 시스템

Unity의 에셋 관리 계층:

```
Resources/ (빌드 시 포함, 런타임 로드)
    |
AssetBundle (패키징 단위, 압축, 버전 관리)
    |
Addressables (최신 시스템, 로컬/원격 에셋 통합 관리)
```

AssetBundle 특징:
- **압축 옵션**: LZMA (높은 압축률), LZ4 (빠른 로딩), 무압축
- **의존성 추적**: 에셋 간 참조 관계 관리
- **증분 빌드**: 변경된 에셋만 재빌드
- **CRC 검증**: 무결성 확인
- **스트리밍**: 필요한 에셋만 부분 로딩

Addressables의 추가 기능:
- 주소 기반 로딩 (파일 경로 대신 논리 주소)
- 원격 에셋 서버 지원
- 자동 메모리 관리 (참조 카운팅)
- 프로파일링 도구

### 5.3 Molga Engine 현재 상태

`GameBuilder`의 빌드 프로세스:

```
1. 출력 디렉터리 생성 (기존 삭제 후 재생성)
2. assets/ 폴더 전체 복사
3. src/Shaders/ 폴더 전체 복사
4. 씬 JSON 파일 복사
5. game.json 설정 파일 생성
6. 런타임 실행 파일 복사
```

문제점:
- 에셋 압축 없음 (원본 크기 그대로)
- 증분 빌드 없음 (매번 전체 복사)
- 에셋 무결성 검증 없음
- 개별 파일 단위 I/O (디스크 탐색 비용)

### 5.4 권장 구현: 단계별 접근

#### 단계 1: 에셋 팩 (단순 아카이브)

여러 에셋 파일을 하나의 팩 파일로 합쳐 디스크 I/O를 줄인다:

```
팩 파일 포맷 (.mpack):

[Header]
  magic: "MPACK\0\0\0" (8 bytes)
  version: uint32_t
  entry_count: uint32_t
  toc_offset: uint64_t        -- 목차 위치

[Data Section]
  [파일1 데이터] [파일2 데이터] [파일3 데이터] ...

[Table of Contents]
  [Entry 1]
    path_hash: uint64_t       -- 파일 경로 해시
    path_length: uint16_t
    path: char[]              -- 원본 경로
    offset: uint64_t          -- Data Section 내 위치
    size: uint64_t            -- 원본 크기
    compressed_size: uint64_t -- 압축 크기 (0 = 무압축)
    flags: uint32_t           -- 압축 방식 등
  [Entry 2] ...
```

```cpp
// src/Assets/AssetPack.h
class AssetPack {
public:
    bool Open(const std::string& path);
    void Close();

    bool Contains(const std::string& assetPath) const;
    std::vector<uint8_t> Read(const std::string& assetPath) const;

    // 스트리밍 지원
    size_t GetAssetSize(const std::string& assetPath) const;
    size_t ReadPartial(const std::string& assetPath,
                       void* buffer, size_t offset, size_t size) const;

private:
    struct Entry {
        uint64_t offset;
        uint64_t size;
        uint64_t compressedSize;
        uint32_t flags;
    };

    std::unordered_map<std::string, Entry> entries;
    FILE* file = nullptr;  // 또는 std::ifstream
};
```

#### 단계 2: 압축 지원

| 압축 알고리즘 | 압축률 | 해제 속도 | 용도 |
|-------------|--------|-----------|------|
| **LZ4** | 중간 (~2:1) | 매우 빠름 (>4GB/s) | 런타임 로딩 (기본 선택) |
| **zlib/deflate** | 높음 (~3:1) | 빠름 (~1GB/s) | 배포 패키지 |
| **zstd** | 매우 높음 (~3.5:1) | 빠름 (~1.5GB/s) | 최신 대안 (권장) |
| **무압축** | 없음 | 즉시 | 이미 압축된 에셋 (PNG, OGG) |

권장 라이브러리:

```
zstd (Facebook/Meta):
  - CMake FetchContent로 쉽게 통합
  - 딕셔너리 기반 압축으로 비슷한 에셋 그룹에 효과적
  - 압축/해제 속도 모두 우수
  - BSD 라이센스

lz4:
  - 해제 속도 최우선 시 선택
  - 매우 작은 라이브러리
  - BSD 라이센스
```

#### 단계 3: 가상 파일시스템 (VFS)

에셋의 실제 위치(디스크, 팩 파일, 메모리)를 추상화:

```cpp
class VirtualFileSystem {
public:
    // 마운트 포인트 등록
    void Mount(const std::string& mountPoint,
               std::unique_ptr<IFileProvider> provider);

    // 통합 인터페이스
    std::vector<uint8_t> ReadFile(const std::string& path);
    bool FileExists(const std::string& path);

private:
    struct MountEntry {
        std::string mountPoint;
        std::unique_ptr<IFileProvider> provider;
    };
    std::vector<MountEntry> mounts;
};

// 파일 제공자 인터페이스
class IFileProvider {
public:
    virtual ~IFileProvider() = default;
    virtual bool Exists(const std::string& relativePath) = 0;
    virtual std::vector<uint8_t> Read(const std::string& relativePath) = 0;
};

// 구현체들
class DiskFileProvider : public IFileProvider { /* 일반 파일시스템 */ };
class PackFileProvider : public IFileProvider { /* .mpack 파일 */ };
class MemoryFileProvider : public IFileProvider { /* 메모리 맵 */ };
```

사용 예:

```cpp
VirtualFileSystem vfs;

// 개발 중: 디스크에서 직접 로딩 (핫 리로드 가능)
vfs.Mount("/assets", std::make_unique<DiskFileProvider>("./assets"));

// 배포 시: 팩 파일에서 로딩
vfs.Mount("/assets", std::make_unique<PackFileProvider>("game.mpack"));

// 코드에서는 동일하게 사용
auto data = vfs.ReadFile("/assets/player.png");
```

### 5.5 에셋 팩 빌드 도구

`GameBuilder`를 확장하여 에셋 팩킹 기능 추가:

```cpp
class AssetPacker {
public:
    void AddFile(const std::string& virtualPath, const std::string& diskPath);
    void AddDirectory(const std::string& virtualPrefix, const std::string& diskDir);

    bool Pack(const std::string& outputPath, CompressionType compression);

private:
    struct PendingEntry {
        std::string virtualPath;
        std::string diskPath;
    };
    std::vector<PendingEntry> entries;
};
```

### 5.6 복잡도 및 우선순위

- **복잡도**: Medium-Large
  - 에셋 팩 기본 구현: 3-5일
  - 압축 통합: 2-3일
  - VFS: 3-5일
  - GameBuilder 통합: 2-3일
- **우선순위**: **Medium** -- 현재 규모에서는 파일 복사로 충분. 에셋이 100MB+가 되면 도입
- **예상 작업량**: 약 3-4주

---

## 6. 로컬라이제이션 시스템

### 6.1 개요 및 중요성

게임 텍스트를 다국어로 표시하는 시스템이다. 글로벌 시장 진출 시 필수이며, 특히 Steam은 한국어, 일본어, 중국어(간체/번체), 영어 등의 지원 여부가 검색 노출과 판매에 직접 영향을 미친다.

### 6.2 Unity의 Localization 패키지

Unity Localization 패키지 (com.unity.localization):

- **String Tables**: 키-값 쌍으로 번역 문자열 관리
- **Asset Tables**: 로케일별 다른 에셋(이미지, 오디오) 매핑
- **Locale System**: 시스템 로케일 감지, 수동 변경 지원
- **Pseudo-Localization**: 번역 전 UI 레이아웃 테스트 (문자 확장, RTL 시뮬레이션)
- **Smart Strings**: 변수 삽입, 복수형, 성별 변화 지원
- **Google Sheets 통합**: 번역 데이터 외부 동기화
- **CSV/XLIFF 가져오기/내보내기**: 번역가와 협업

### 6.3 권장 구현

#### 문자열 테이블 기반 시스템

```cpp
// src/Localization/Localization.h
class Localization {
public:
    static Localization& Get();

    // 로케일 관리
    bool LoadLocale(const std::string& localeCode);  // "ko", "en", "ja"
    void SetLocale(const std::string& localeCode);
    std::string GetCurrentLocale() const;
    std::vector<std::string> GetAvailableLocales() const;

    // 문자열 조회
    const std::string& GetString(const std::string& key) const;

    // 포맷팅 지원
    std::string Format(const std::string& key,
                       const std::unordered_map<std::string, std::string>& args) const;

private:
    std::string currentLocale;
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> tables;
    // tables[locale][key] = translated_string
};
```

#### 번역 데이터 형식 (JSON)

```json
// locales/ko.json
{
    "locale": "ko",
    "name": "한국어",
    "strings": {
        "menu.start": "게임 시작",
        "menu.settings": "설정",
        "menu.quit": "종료",
        "game.score": "점수: {score}",
        "game.lives": "목숨: {lives}개",
        "dialog.npc_greeting": "안녕하세요, {player_name}님!"
    }
}

// locales/en.json
{
    "locale": "en",
    "name": "English",
    "strings": {
        "menu.start": "Start Game",
        "menu.settings": "Settings",
        "menu.quit": "Quit",
        "game.score": "Score: {score}",
        "game.lives": "Lives: {lives}",
        "dialog.npc_greeting": "Hello, {player_name}!"
    }
}
```

#### CJK 폰트 지원

Molga Engine의 현재 `TextRenderer`는 8x8 비트맵 폰트(ASCII 32-126)를 사용한다. CJK 문자 지원을 위해서는 근본적인 업그레이드가 필요하다.

**권장: stb_truetype 또는 FreeType**

| 라이브러리 | 장점 | 단점 |
|-----------|------|------|
| **stb_truetype** | 헤더 온리, stb 이미 사용 중, 간단 | 힌팅 품질 낮음, 복잡한 스크립트 미지원 |
| **FreeType** | 산업 표준, 완벽한 힌팅, 모든 스크립트 | 외부 의존성, 복잡한 API |

CJK 폰트 렌더링 전략:

```
1. 폰트 아틀라스 (Font Atlas):
   - 게임 시작 시 또는 사전에 사용할 글리프를 텍스처로 래스터화
   - ASCII: 전체 미리 생성 (~95개 글리프)
   - CJK: 사용 빈도 높은 글자만 미리 생성 (약 3000-5000자)
   - 나머지: 런타임에 필요 시 동적 생성 및 캐싱

2. 동적 글리프 캐시:
   - LRU 캐시로 텍스처 아틀라스 관리
   - 새 글리프 요청 시 빈 영역에 래스터화
   - 아틀라스 가득 차면 가장 오래된 글리프 교체

3. 구현 단계:
   Phase 1: stb_truetype + 정적 아틀라스 (영문 + 기본 한글)
   Phase 2: 동적 글리프 캐시 (전체 CJK)
   Phase 3: 텍스트 레이아웃 엔진 (RTL, 합자 등)
```

#### RTL (오른쪽에서 왼쪽) 텍스트

아랍어, 히브리어 지원 시 필요하지만, 인디 2D 게임의 초기 단계에서는 우선순위가 낮다.

필요 시 도구: **ICU (International Components for Unicode)** 또는 **SheenBidi** (경량 BiDi 라이브러리)

### 6.4 에디터 통합

```
Editor에서의 지원:
- Inspector에서 문자열 키 선택 드롭다운
- 미리보기: 선택한 로케일로 텍스트 미리보기
- 번역 누락 경고: 키가 있지만 특정 로케일에 번역 없음
- CSV 내보내기/가져오기: 번역가와의 협업
```

### 6.5 복잡도 및 우선순위

- **복잡도**: Medium
  - 문자열 테이블 시스템: 2-3일
  - JSON 로더: 1일 (nlohmann/json 이미 사용 중)
  - TrueType 폰트 렌더링 (stb_truetype): 5-7일
  - CJK 동적 글리프 캐시: 3-5일
  - 에디터 통합: 3-5일
- **우선순위**: **Medium** -- 글로벌 출시 전 필요하지만 초기 개발에는 불필요
- **예상 작업량**: 약 3-4주

---

## 7. 플러그인/확장 시스템

### 7.1 개요 및 중요성

플러그인 시스템은 엔진의 핵심 코드를 수정하지 않고 기능을 확장할 수 있게 한다. 커뮤니티 생태계 형성, 엔진 모듈화, 프로젝트별 커스터마이징에 핵심적이다.

### 7.2 Unity의 Package Manager

Unity Package Manager (UPM):
- **Registry**: npm 스타일의 패키지 레지스트리
- **패키지 소스**: Unity Registry, Git URL, 로컬 경로, 타르볼
- **의존성 해결**: 자동 의존성 다운로드 및 버전 충돌 해결
- **Scoped Registry**: 사용자 정의 패키지 서버
- **패키지 구조**: `package.json`, Runtime/Editor 어셈블리 분리
- **버전 관리**: SemVer 기반

### 7.3 C++ 플러그인 아키텍처 설계

#### 접근법 1: 동적 라이브러리 (DLL/SO/DYLIB) 기반

Molga Engine은 이미 `Platform::LoadDynamicLibrary()`를 구현하고 있으며, 스크립트 핫 리로드에 사용 중이다. 이를 확장하면 된다.

```cpp
// src/Plugin/PluginInterface.h
// 플러그인이 구현해야 하는 인터페이스

#define MOLGA_PLUGIN_API_VERSION 1

struct PluginInfo {
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    uint32_t apiVersion;  // MOLGA_PLUGIN_API_VERSION
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo GetInfo() const = 0;
    virtual bool Initialize(class Engine* engine) = 0;
    virtual void Shutdown() = 0;
    virtual void Update(float deltaTime) = 0;
};

// 플러그인 DLL이 내보내야 하는 함수
extern "C" {
    typedef IPlugin* (*CreatePluginFunc)();
    typedef void (*DestroyPluginFunc)(IPlugin*);
}

// 플러그인 매크로 (플러그인 측에서 사용)
#define MOLGA_PLUGIN(ClassName) \
    extern "C" { \
        IPlugin* CreatePlugin() { return new ClassName(); } \
        void DestroyPlugin(IPlugin* p) { delete p; } \
    }
```

#### 접근법 2: 정적 링크 플러그인 (빌드 타임)

모바일/WebGL에서는 동적 로딩이 제한적이므로 정적 등록 방식도 필요:

```cpp
// 플러그인 레지스트리
class PluginRegistry {
public:
    static PluginRegistry& Get();

    // 동적 로딩
    bool LoadPlugin(const std::string& path);
    void UnloadPlugin(const std::string& name);

    // 정적 등록
    void RegisterPlugin(std::unique_ptr<IPlugin> plugin);

    // 관리
    IPlugin* GetPlugin(const std::string& name);
    std::vector<IPlugin*> GetAllPlugins();

    void InitializeAll(Engine* engine);
    void UpdateAll(float dt);
    void ShutdownAll();

private:
    struct LoadedPlugin {
        std::unique_ptr<IPlugin> plugin;
        void* libraryHandle = nullptr;  // 동적 로딩 시
    };
    std::vector<LoadedPlugin> plugins;
};
```

#### API 버전 관리

```cpp
// 버전 호환성 검사
bool PluginRegistry::LoadPlugin(const std::string& path) {
    void* handle = Platform::LoadDynamicLibrary(path.c_str());
    if (!handle) return false;

    auto createFunc = (CreatePluginFunc)Platform::GetSymbol(handle, "CreatePlugin");
    if (!createFunc) {
        Platform::CloseDynamicLibrary(handle);
        return false;
    }

    IPlugin* plugin = createFunc();
    PluginInfo info = plugin->GetInfo();

    // API 버전 호환성 검사
    if (info.apiVersion != MOLGA_PLUGIN_API_VERSION) {
        Log::Warn("Plugin '{}' API version mismatch: {} vs {}",
                  info.name, info.apiVersion, MOLGA_PLUGIN_API_VERSION);
        // 하위 호환 정책에 따라 거부 또는 경고
    }

    // ... 등록
}
```

#### 확장 포인트 (Extension Points)

엔진이 플러그인에 노출하는 API:

```cpp
// src/Plugin/EngineAPI.h
// 플러그인이 사용할 수 있는 엔진 기능

class EngineAPI {
public:
    // ECS
    virtual GameObject* CreateGameObject(const std::string& name) = 0;
    virtual void RegisterComponentType(const std::string& name,
                                        ComponentFactory factory) = 0;

    // 렌더링
    virtual void RegisterRenderPass(const std::string& name,
                                     IRenderPass* pass) = 0;

    // 입력
    virtual bool IsKeyPressed(int key) = 0;

    // 에셋
    virtual Texture* LoadTexture(const std::string& path) = 0;

    // 씬
    virtual void LoadScene(const std::string& path) = 0;

    // 로깅
    virtual void Log(const std::string& message) = 0;
};
```

### 7.4 패키지 매니페스트

```json
// plugins/my_plugin/plugin.json
{
    "name": "particle-effects",
    "version": "1.0.0",
    "apiVersion": 1,
    "author": "Developer Name",
    "description": "Advanced particle effects for Molga Engine",
    "dependencies": {
        "molga-core": ">=1.0.0"
    },
    "platforms": ["windows", "macos", "linux"],
    "entry": {
        "dynamic": "particle_effects.dylib",
        "source": "src/ParticlePlugin.cpp"
    }
}
```

### 7.5 복잡도 및 우선순위

- **복잡도**: Large
  - 플러그인 인터페이스 설계: 3-5일
  - 동적 로딩 통합 (기존 코드 활용): 2-3일
  - EngineAPI 설계/구현: 5-7일
  - 패키지 매니페스트/버전 관리: 3-5일
  - 에디터 통합 (플러그인 관리 UI): 3-5일
- **우선순위**: **Low** -- 엔진 자체 기능이 성숙한 후 도입
- **예상 작업량**: 약 4-6주

---

## 8. 문서화 및 API 레퍼런스

### 8.1 개요 및 중요성

API 문서화는 엔진 사용자(게임 개발자)가 기능을 효율적으로 사용하는 데 필수적이다. 잘 정리된 문서는 학습 곡선을 낮추고, 커뮤니티 성장을 촉진하며, 기여자 유입의 기본 조건이다.

### 8.2 Unity의 문서 시스템

Unity의 문서 구조:
- **Scripting API Reference**: 자동 생성, 모든 public 클래스/메서드 문서화
- **Manual**: 개념 설명, 튜토리얼, 워크플로우 가이드
- **에디터 내 도움말**: Inspector 필드 호버 시 툴팁, Help 메뉴에서 API 페이지 연결
- **Code Examples**: 각 API 항목에 실행 가능한 예제 코드
- **Version Dropdown**: 버전별 문서 전환
- **검색**: 전문 검색, 자동 완성

### 8.3 Doxygen 기반 API 문서화

C++ 프로젝트의 표준 문서화 도구인 Doxygen을 사용한다.

#### Doxygen 설정

```bash
# 초기 설정 파일 생성
doxygen -g Doxyfile
```

핵심 Doxyfile 설정:

```
# Doxyfile 주요 설정
PROJECT_NAME           = "Molga Engine"
PROJECT_NUMBER         = "1.0.0"
PROJECT_BRIEF          = "2D Game Engine"

# 입력 소스
INPUT                  = src/
RECURSIVE              = YES
FILE_PATTERNS          = *.h *.cpp

# 출력
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
HTML_OUTPUT            = docs/api

# C++ 설정
EXTRACT_ALL            = NO          # public만 추출
EXTRACT_PRIVATE        = NO
EXTRACT_STATIC         = NO

# 문서 품질
WARN_IF_UNDOCUMENTED   = YES
WARN_NO_PARAMDOC       = YES

# 다이어그램
HAVE_DOT               = YES         # Graphviz 설치 시
CLASS_DIAGRAMS         = YES
COLLABORATION_GRAPH    = YES
CALL_GRAPH             = YES

# 검색
SEARCHENGINE           = YES
```

#### Doxygen 주석 스타일

Molga Engine 코드에 추가할 문서화 주석 예시:

```cpp
/// @file GameObject.h
/// @brief ECS 시스템의 게임 오브젝트 클래스

/// @class GameObject
/// @brief 게임 월드에 존재하는 엔티티를 나타내는 핵심 클래스
///
/// GameObject는 Component를 포함하는 컨테이너이다.
/// Transform, SpriteRenderer 등의 컴포넌트를 추가하여 동작을 정의한다.
///
/// @code
/// auto player = std::make_shared<GameObject>("Player");
/// player->AddComponent<Transform>(100.0f, 200.0f);
/// player->AddComponent<SpriteRenderer>("player.png");
/// @endcode
///
/// @see Component, Transform, Scene
class GameObject {
public:
    /// @brief 지정된 이름으로 새 게임 오브젝트를 생성한다
    /// @param name 오브젝트 이름 (기본값: "GameObject")
    explicit GameObject(const std::string& name = "GameObject");

    /// @brief 지정된 타입의 컴포넌트를 추가한다
    /// @tparam T Component를 상속하는 컴포넌트 타입
    /// @tparam Args 생성자 인자 타입
    /// @param args 컴포넌트 생성자에 전달할 인자
    /// @return 추가된 컴포넌트의 포인터. 실패 시 nullptr
    ///
    /// @code
    /// auto* transform = obj->AddComponent<Transform>(10.0f, 20.0f);
    /// @endcode
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args);

    /// @brief 지정된 타입의 컴포넌트를 조회한다
    /// @tparam T 조회할 컴포넌트 타입
    /// @return 컴포넌트 포인터. 없으면 nullptr
    template<typename T>
    T* GetComponent();
};
```

### 8.4 추가 문서화 도구

| 도구 | 용도 | 통합 방식 |
|------|------|-----------|
| **Doxygen** | API 레퍼런스 자동 생성 | CI에서 자동 빌드 |
| **MkDocs** (또는 mdBook) | 사용자 매뉴얼, 튜토리얼 | Markdown 기반, GitHub Pages 배포 |
| **Doxygen + MkDocs 통합** | API + 매뉴얼 통합 사이트 | `docs/` 디렉터리 |

```
docs/
  Doxyfile               -- Doxygen 설정
  mkdocs.yml             -- MkDocs 설정 (사용자 매뉴얼)
  manual/
    index.md             -- 매뉴얼 홈
    getting-started.md   -- 시작 가이드
    ecs.md               -- ECS 시스템 설명
    rendering.md         -- 렌더링 파이프라인
    scripting.md         -- 스크립트 시스템
    building.md          -- 게임 빌드 방법
  api/                   -- Doxygen 출력 (자동 생성)
```

#### CI에서 문서 자동 빌드/배포

```yaml
# .github/workflows/docs.yml
name: Documentation

on:
  push:
    branches: [main]
    paths: ['src/**/*.h', 'docs/**']

jobs:
  build-docs:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install Doxygen
        run: sudo apt-get install -y doxygen graphviz

      - name: Generate API docs
        run: doxygen docs/Doxyfile

      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: docs/api/html
```

### 8.5 에디터 내 도움말

ImGui 기반 에디터에서의 도움말 통합:

```cpp
// Inspector에서 컴포넌트 필드 옆에 (?) 아이콘
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Transform의 X 좌표\n"
                      "월드 공간 기준 위치");
}

// 또는 Help 버튼으로 외부 문서 열기
if (ImGui::SmallButton("?")) {
    Platform::OpenURL("https://docs.molga.dev/api/Transform");
}
```

### 8.6 복잡도 및 우선순위

- **복잡도**: Small-Medium
  - Doxygen 설정 + 초기 주석: 2-3일
  - CI 통합 (GitHub Pages): 1일
  - MkDocs 매뉴얼 기본 구조: 2-3일
  - 에디터 내 툴팁: 1-2일
- **우선순위**: **Medium** -- 외부 사용자/기여자가 생기기 전 준비
- **예상 작업량**: 약 1-2주 (초기), 이후 지속적 유지

---

## 9. 버전 관리 통합

### 9.1 개요 및 중요성

게임 엔진의 씬 파일, 프리팹, 설정 파일이 Git에서 안전하게 관리되고, 병합 충돌이 최소화되어야 한다. 바이너리 에셋(텍스처, 오디오)은 특별한 처리가 필요하다.

### 9.2 Unity의 VCS 통합

Unity의 접근:
- **YAML 직렬화**: 씬/프리팹을 텍스트 기반 YAML로 저장 → Git diff/merge 가능
- **Smart Merge**: `UnityYAMLMerge` 도구로 씬 파일 3-way merge
- **Force Text**: 모든 에셋 메타 데이터를 텍스트 모드로 저장
- **.gitignore**: Library/, Temp/, Build/ 등 제외
- **Git LFS**: 텍스처, 오디오, 비디오 등 바이너리 에셋

문제점:
- YAML 씬 파일이 매우 큼 (수천 줄)
- 프리팹 내부 ID 기반으로 병합 충돌 빈번
- LFS 없이 바이너리 에셋 커밋 시 저장소 비대화

### 9.3 Molga Engine 현재 상태 및 분석

Molga Engine의 씬 직렬화는 **JSON 형식** (`nlohmann/json`)이다. 이것은 Unity의 YAML보다 VCS 친화적인 면이 있다:

```json
// scene.json (현재 형식, SceneSerializer 기반)
{
    "version": "1.0",
    "name": "Untitled Scene",
    "objects": [
        {
            "name": "Player",
            "id": 1,
            "active": true,
            "parentId": -1,
            "components": [
                {
                    "type": "Transform",
                    "enabled": true,
                    "x": 100.0,
                    "y": 200.0,
                    "rotation": 0.0,
                    "scaleX": 1.0,
                    "scaleY": 1.0
                }
            ]
        }
    ]
}
```

장점:
- JSON은 줄 기반 diff가 비교적 깔끔함
- nlohmann/json의 `dump(2)` 포맷팅으로 일관된 들여쓰기

개선 필요 사항:
- 오브젝트 정렬 순서 보장 (삽입 순서 유지)
- ID 기반 참조의 안정성 (씬 로드/세이브 시 ID 변경 방지)
- 바이너리 에셋 관리 전략

### 9.4 VCS 친화적 직렬화 강화

#### (1) JSON 키 정렬로 diff 안정성 확보

```cpp
// SceneSerializer에서 JSON 출력 시 키 정렬
// nlohmann/json은 기본적으로 ordered_map 사용 가능
using ordered_json = nlohmann::ordered_json;

// 또는 dump 시 정렬
std::string output = sceneJson.dump(2);  // 들여쓰기 2칸
```

#### (2) 안정적 ID 생성

```cpp
// UUID 기반 ID (충돌 없는 고유 식별자)
// 랜덤 정수 ID 대신 UUID 사용 시 병합 충돌 극적 감소

#include <random>
#include <sstream>
#include <iomanip>

std::string GenerateUUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (part1 >> 32) << "-"
       << std::setw(4) << ((part1 >> 16) & 0xFFFF) << "-"
       << std::setw(4) << (part1 & 0xFFFF) << "-"
       << std::setw(4) << (part2 >> 48) << "-"
       << std::setw(12) << (part2 & 0xFFFFFFFFFFFF);
    return ss.str();
}
```

#### (3) 컴포넌트별 한 줄 정책

깊이 중첩된 JSON보다 컴포넌트별로 명확히 구분되는 포맷이 diff에 유리하다.

### 9.5 .gitignore 설정

```gitignore
# Molga Engine .gitignore

# Build directories
build/
cmake-build-*/
out/

# IDE
.vscode/settings.json
.idea/
*.user
*.suo

# OS
.DS_Store
Thumbs.db

# Compiled shaders
*.spv

# Compiled scripts (hot-reload)
scripts/*.dylib
scripts/*.dll
scripts/*.so

# Runtime generated
imgui.ini
game_build/

# Temporary test artifacts
Testing/
test.png
test_build.cpp
```

### 9.6 Git LFS 설정

```bash
# .gitattributes
# 바이너리 에셋을 Git LFS로 추적

# 텍스처
*.png filter=lfs diff=lfs merge=lfs -text
*.jpg filter=lfs diff=lfs merge=lfs -text
*.bmp filter=lfs diff=lfs merge=lfs -text
*.psd filter=lfs diff=lfs merge=lfs -text

# 오디오
*.wav filter=lfs diff=lfs merge=lfs -text
*.mp3 filter=lfs diff=lfs merge=lfs -text
*.ogg filter=lfs diff=lfs merge=lfs -text

# 폰트
*.ttf filter=lfs diff=lfs merge=lfs -text
*.otf filter=lfs diff=lfs merge=lfs -text

# 씬/설정은 LFS 사용 안 함 (텍스트 diff 필요)
# *.json -filter -diff -merge text
```

### 9.7 커스텀 Merge Driver (고급)

씬 파일의 지능적 병합을 위한 커스텀 merge driver:

```
# .gitconfig (또는 .git/config)
[merge "molga-scene"]
    name = Molga Scene Merge Driver
    driver = python3 tools/scene_merge.py %A %O %B %P
```

간단한 씬 병합 도구:

```python
# tools/scene_merge.py
# JSON 씬 파일의 오브젝트를 ID 기반으로 병합
# 같은 ID의 오브젝트가 양쪽에서 수정되면 충돌 표시
```

### 9.8 복잡도 및 우선순위

- **복잡도**: Small
  - .gitignore 정리: 30분
  - .gitattributes + LFS 설정: 1시간
  - UUID 기반 ID: 1-2일
  - JSON 키 정렬: 1-2시간
  - 커스텀 merge driver: 2-3일 (선택사항)
- **우선순위**: **High** -- 팀 개발 시작 전 필수. 개인 프로젝트에서도 바이너리 관리 필요
- **예상 작업량**: 약 1주

---

## 10. 성능 모니터링

### 10.1 개요 및 중요성

게임 엔진의 성능 모니터링은 프레임 레이트 유지, 메모리 누수 탐지, 병목 지점 식별에 필수적이다. 특히 2D 엔진이라도 타일맵, 파티클, 대량 스프라이트 렌더링 시 성능 문제가 발생할 수 있다.

### 10.2 Unity의 성능 모니터링 도구

Unity가 제공하는 도구들:

| 도구 | 기능 |
|------|------|
| **Stats Window** | FPS, 배치 수, 삼각형 수, 메모리 사용량 실시간 표시 |
| **Profiler** | CPU/GPU 시간, 스크립트, 물리, 렌더링 프로파일링 |
| **Frame Debugger** | 드로우 콜 단위로 렌더링 순서 시각화 |
| **Memory Profiler** | 힙 메모리, 네이티브 메모리, 텍스처 메모리 상세 분석 |
| **Physics Debugger** | 콜라이더 시각화, 물리 연산 비용 |
| **GPU Profiler** | GPU 타임라인, 셰이더 비용 분석 |

### 10.3 Molga Engine 현재 상태

`StatsWindow.cpp`가 에디터에 존재하지만, 정확한 구현 수준을 확인해보면:

현재 `MolgaTime` 클래스로 delta time/FPS 계산을 하고 있을 것으로 추정된다. 본격적인 프로파일링 시스템은 아직 없다.

### 10.4 권장 구현: 계층별 접근

#### 계층 1: 기본 성능 카운터 (필수)

```cpp
// src/Core/Profiler.h
class Profiler {
public:
    static Profiler& Get();

    // 프레임 시작/종료
    void BeginFrame();
    void EndFrame();

    // 섹션 측정
    void BeginSection(const char* name);
    void EndSection(const char* name);

    // 카운터
    void IncrementCounter(const char* name, int value = 1);
    void SetGauge(const char* name, float value);

    // 조회
    float GetFrameTime() const;         // ms
    float GetFPS() const;
    float GetSectionTime(const char* name) const;  // ms
    int GetCounter(const char* name) const;
    float GetGauge(const char* name) const;

    // 히스토리
    const std::vector<float>& GetFrameTimeHistory() const;  // 최근 N 프레임

private:
    struct SectionData {
        std::chrono::high_resolution_clock::time_point start;
        float lastDuration = 0.0f;
        float accumulator = 0.0f;
        int callCount = 0;
    };

    std::unordered_map<std::string, SectionData> sections;
    std::unordered_map<std::string, int> counters;
    std::unordered_map<std::string, float> gauges;

    std::vector<float> frameTimeHistory;  // 최근 300 프레임
    static constexpr int HISTORY_SIZE = 300;
};

// RAII 헬퍼
class ProfileScope {
public:
    ProfileScope(const char* name) : name(name) {
        Profiler::Get().BeginSection(name);
    }
    ~ProfileScope() {
        Profiler::Get().EndSection(name);
    }
private:
    const char* name;
};

#define PROFILE_SCOPE(name) ProfileScope _ps_##__LINE__(name)
#define PROFILE_FUNCTION() ProfileScope _ps_##__LINE__(__FUNCTION__)
```

사용 예시:

```cpp
void Engine::Update(float dt) {
    PROFILE_FUNCTION();

    {
        PROFILE_SCOPE("Physics");
        physics.Update(dt);
    }

    {
        PROFILE_SCOPE("Scripts");
        scriptManager.UpdateAll(dt);
    }

    {
        PROFILE_SCOPE("Rendering");
        Profiler::Get().IncrementCounter("DrawCalls", 0);  // 리셋
        renderer.Render();
    }
}

// Renderer 내부
void Renderer::DrawSprite(const Sprite& sprite) {
    Profiler::Get().IncrementCounter("DrawCalls");
    Profiler::Get().IncrementCounter("Triangles", 2);
    // ... 실제 렌더링
}
```

#### 계층 2: GPU 타이밍 쿼리

```cpp
// src/Rendering/GPUProfiler.h
class GPUProfiler {
public:
    void BeginQuery(const char* name);
    void EndQuery(const char* name);
    float GetQueryResult(const char* name) const;  // ms

private:
    struct QueryPair {
        GLuint queries[2];   // begin, end
        bool resultReady = false;
        float lastResult = 0.0f;
    };
    std::unordered_map<std::string, QueryPair> queries;
};

// OpenGL Timer Query 사용
void GPUProfiler::BeginQuery(const char* name) {
    auto& q = queries[name];
    if (q.queries[0] == 0) {
        glGenQueries(2, q.queries);
    }
    glQueryCounter(q.queries[0], GL_TIMESTAMP);
}

void GPUProfiler::EndQuery(const char* name) {
    auto& q = queries[name];
    glQueryCounter(q.queries[1], GL_TIMESTAMP);
}

float GPUProfiler::GetQueryResult(const char* name) const {
    auto it = queries.find(name);
    if (it == queries.end()) return 0.0f;

    GLint available = 0;
    glGetQueryObjectiv(it->second.queries[1],
                       GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return it->second.lastResult;

    GLuint64 startTime, endTime;
    glGetQueryObjectui64v(it->second.queries[0],
                          GL_QUERY_RESULT, &startTime);
    glGetQueryObjectui64v(it->second.queries[1],
                          GL_QUERY_RESULT, &endTime);

    return (endTime - startTime) / 1000000.0f;  // ns -> ms
}
```

참고: `GL_TIMESTAMP`는 OpenGL 3.3 Core에서 지원된다. WebGL에서는 `EXT_disjoint_timer_query_webgl2` 확장이 필요하며, 보안 이유로 정밀도가 낮을 수 있다.

#### 계층 3: 메모리 추적

```cpp
// src/Core/MemoryTracker.h
class MemoryTracker {
public:
    static MemoryTracker& Get();

    // 카테고리별 메모리 추적
    void* Allocate(size_t size, const char* category);
    void Deallocate(void* ptr);

    // 조회
    size_t GetTotalAllocated() const;
    size_t GetCategoryUsage(const char* category) const;
    size_t GetAllocationCount() const;

    // 텍스처 메모리 (OpenGL)
    void TrackTexture(GLuint id, int width, int height, int channels);
    void UntrackTexture(GLuint id);
    size_t GetTextureMemory() const;

    // 리포트
    struct Report {
        size_t totalAllocated;
        size_t textureMemory;
        size_t audioMemory;
        size_t meshMemory;
        std::unordered_map<std::string, size_t> categoryBreakdown;
    };
    Report GetReport() const;
};

// 텍스처 메모리 추적 예시
void Texture::Load(const std::string& path) {
    // ... 기존 로딩 코드
    glTexImage2D(...);

    MemoryTracker::Get().TrackTexture(textureID, width, height, channels);
}

Texture::~Texture() {
    MemoryTracker::Get().UntrackTexture(textureID);
    glDeleteTextures(1, &textureID);
}
```

#### 계층 4: 에디터 StatsWindow 강화

```cpp
// Editor/Windows/StatsWindow.cpp 확장
void StatsWindow::Draw() {
    auto& profiler = Profiler::Get();

    // FPS 그래프 (ImGui::PlotLines)
    const auto& history = profiler.GetFrameTimeHistory();
    ImGui::PlotLines("Frame Time", history.data(), history.size(),
                     0, nullptr, 0.0f, 33.3f, ImVec2(0, 80));

    ImGui::Text("FPS: %.1f", profiler.GetFPS());
    ImGui::Text("Frame Time: %.2f ms", profiler.GetFrameTime());
    ImGui::Separator();

    // 시스템별 시간
    ImGui::Text("Physics: %.2f ms", profiler.GetSectionTime("Physics"));
    ImGui::Text("Scripts: %.2f ms", profiler.GetSectionTime("Scripts"));
    ImGui::Text("Rendering: %.2f ms", profiler.GetSectionTime("Rendering"));
    ImGui::Separator();

    // 렌더링 통계
    ImGui::Text("Draw Calls: %d", profiler.GetCounter("DrawCalls"));
    ImGui::Text("Triangles: %d", profiler.GetCounter("Triangles"));
    ImGui::Separator();

    // 메모리
    auto report = MemoryTracker::Get().GetReport();
    ImGui::Text("Total Memory: %.1f MB", report.totalAllocated / 1048576.0f);
    ImGui::Text("Texture Memory: %.1f MB", report.textureMemory / 1048576.0f);
    ImGui::Text("Audio Memory: %.1f MB", report.audioMemory / 1048576.0f);
}
```

### 10.5 외부 프로파일링 도구 통합

| 도구 | 플랫폼 | 용도 | 통합 방법 |
|------|--------|------|-----------|
| **Tracy** | 전체 | 프레임 프로파일러 | 헤더 온리, `TRACY_ENABLE` 매크로 |
| **RenderDoc** | 데스크톱 | GPU 프레임 캡처 | 외부 도구, 코드 변경 불필요 |
| **Instruments** | macOS | CPU/GPU/메모리 | Xcode 통합 |
| **Valgrind/ASan** | Linux | 메모리 오류 | 컴파일 플래그 |
| **Xcode GPU Frame Capture** | macOS | GPU 디버깅 | Xcode에서 실행 시 자동 |

**Tracy 통합 (강력히 권장)**:

Tracy는 나노초 정밀도의 실시간 프레임 프로파일러로, C++ 게임 엔진에서 사실상 표준이다.

```cmake
# Tracy 통합
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.11.1
)
FetchContent_MakeAvailable(tracy)

target_link_libraries(molga_core PUBLIC TracyClient)
target_compile_definitions(molga_core PUBLIC TRACY_ENABLE)
```

```cpp
// Tracy 사용
#include <tracy/Tracy.hpp>

void Engine::Update(float dt) {
    ZoneScoped;  // 자동으로 함수 이름으로 zone 생성

    {
        ZoneScopedN("Physics");
        physics.Update(dt);
    }

    FrameMark;  // 프레임 경계 표시
}
```

### 10.6 복잡도 및 우선순위

- **복잡도**: Medium
  - 기본 Profiler 클래스: 2-3일
  - GPU 타이밍 쿼리: 2일
  - 메모리 추적: 3-4일
  - StatsWindow 강화: 1-2일
  - Tracy 통합: 1일
- **우선순위**: **High** -- 성능 문제 디버깅에 필수. 초기부터 계측 코드 삽입 권장
- **예상 작업량**: 약 2-3주

---

## 11. 우선순위 종합표

### 구현 우선순위 매트릭스

| # | 시스템 | 복잡도 | 우선순위 | 예상 기간 | 의존성 |
|---|--------|--------|----------|-----------|--------|
| 4 | CI/CD 파이프라인 | Medium | **Critical** | 1-2주 | 없음 (즉시 시작 가능) |
| 1 | 크로스 플랫폼 빌드 | Large | **Critical** | 2-4주 | 없음 |
| 3 | 테스팅 프레임워크 | Medium | **High** | 2-3주 | 없음 |
| 9 | 버전 관리 통합 | Small | **High** | 1주 | 없음 |
| 10 | 성능 모니터링 | Medium | **High** | 2-3주 | 없음 |
| 2 | WebGL/Emscripten | Medium-Large | **High** | 2-3주 | #1 (플랫폼 추상화) |
| 8 | 문서화 / API 레퍼런스 | Small-Medium | **Medium** | 1-2주 | 없음 |
| 5 | 에셋 번들 / 패키징 | Medium-Large | **Medium** | 3-4주 | 없음 |
| 6 | 로컬라이제이션 | Medium | **Medium** | 3-4주 | 없음 |
| 7 | 플러그인/확장 시스템 | Large | **Low** | 4-6주 | #1 |

### 권장 구현 로드맵

```
Phase A (기반 인프라) -- 4-6주
├── CI/CD 파이프라인 (GitHub Actions)    ★ 최우선
├── 버전 관리 통합 (.gitignore, LFS)     ★ 즉시 적용 가능
├── 테스팅 프레임워크 (Catch2 도입)       ★ CI와 함께
└── 성능 모니터링 (기본 Profiler)         ★ 개발 초기부터

Phase B (플랫폼 확장) -- 4-6주
├── 크로스 플랫폼 빌드 (Windows + Linux)
├── WebGL/Emscripten 지원
└── 문서화 (Doxygen 기본 설정)

Phase C (콘텐츠 시스템) -- 6-8주
├── 에셋 번들 / 패키징
├── 로컬라이제이션 시스템
└── 문서화 (사용자 매뉴얼)

Phase D (생태계) -- 4-6주
├── 플러그인/확장 시스템
└── 고급 에셋 관리 (VFS, 스트리밍)
```

### 핵심 기술 스택 요약

| 범주 | 권장 도구/라이브러리 | 이유 |
|------|---------------------|------|
| 테스트 | Catch2 v3 | BDD 스타일, 벤치마크 내장, 경량 |
| CI/CD | GitHub Actions | GitHub 호스팅, 무료 tier, 멀티 플랫폼 runner |
| 프로파일링 | Tracy + 자체 Profiler | Tracy는 업계 표준, 자체 Profiler는 에디터 통합용 |
| 문서화 | Doxygen + MkDocs | API 자동 생성 + 매뉴얼 |
| 압축 | zstd (또는 LZ4) | 압축률/속도 균형 우수 |
| 폰트 | stb_truetype (초기), FreeType (장기) | stb는 이미 사용 중, 점진적 업그레이드 |
| 빌드 | CMake toolchain files | 기존 CMake 시스템과 자연스러운 통합 |
| VCS | Git LFS + .gitattributes | 바이너리 에셋 관리 표준 |

### 즉시 실행 가능 항목 (코드 변경 없음)

1. `.gitignore` 정리 -- 30분
2. `.gitattributes` + Git LFS 설정 -- 1시간
3. GitHub Actions 기본 빌드 워크플로우 -- 2-3시간
4. Doxygen 설정 파일 생성 -- 1시간

# Phase 5: 코드 품질 및 조직화

## 목표
소스 파일 디렉토리 재구성, include guard 통일, 매직 넘버 상수화, Shader uniform 캐싱, 네이밍 일관성 확보.

---

## 5.1 소스 디렉토리 재구성

### 현재 문제

`src/` 루트에 20개 이상의 파일이 분류 없이 배치:

```
src/
├── Audio.h/cpp          ← 오디오 시스템
├── Camera2D.h/cpp       ← 렌더링 관련
├── Collision.h/cpp      ← 물리 시스템
├── Input.h/cpp          ← 입력 시스템
├── Particle.h/cpp       ← 이펙트 시스템
├── Renderer.h/cpp       ← 렌더링 관련
├── Scene.h/cpp          ← 씬 관리
├── Shader.h/cpp         ← 렌더링 관련
├── Sprite.h/cpp         ← 렌더링 관련
├── SpriteSheet.h/cpp    ← 렌더링 관련
├── Texture.h/cpp        ← 렌더링 관련
├── TextRenderer.h/cpp   ← 렌더링 관련
├── Tilemap.h/cpp        ← 렌더링/물리 혼합
├── Time.h/cpp           ← 코어 시스템
├── UI.h/cpp             ← UI 시스템
├── Animation.h/cpp      ← 렌더링/애니메이션
├── linmath.h            ← 수학 유틸리티
├── main.cpp
└── runtime_main.cpp
```

### 변경 계획

```
src/
├── Core/                    (기존 유지 + 이동)
│   ├── Application.h/cpp
│   ├── Project.h/cpp
│   ├── GameBuilder.h/cpp
│   ├── SceneSerializer.h/cpp
│   ├── TextureManager.h/cpp
│   ├── Time.h/cpp           ← src/에서 이동
│   └── Scene.h/cpp          ← src/에서 이동
├── Rendering/               (신규)
│   ├── Renderer.h/cpp       ← src/에서 이동
│   ├── Shader.h/cpp         ← src/에서 이동
│   ├── Texture.h/cpp        ← src/에서 이동
│   ├── Sprite.h/cpp         ← src/에서 이동
│   ├── SpriteSheet.h/cpp    ← src/에서 이동
│   ├── Camera2D.h/cpp       ← src/에서 이동
│   ├── TextRenderer.h/cpp   ← src/에서 이동
│   ├── Animation.h/cpp      ← src/에서 이동
│   └── Tilemap.h/cpp        ← src/에서 이동
├── Systems/                 (신규)
│   ├── Audio.h/cpp          ← src/에서 이동
│   ├── Input.h/cpp          ← src/에서 이동
│   └── Particle.h/cpp       ← src/에서 이동
├── Physics/                 (신규)
│   └── Collision.h/cpp      ← src/에서 이동
├── UI/                      (신규)
│   └── UI.h/cpp             ← src/에서 이동
├── ECS/                     (기존 유지)
├── Editor/                  (기존 유지)
├── Scripting/               (기존 유지)
├── Platform/                (기존 유지)
├── Common/                  (기존 유지 + 추가)
│   ├── Types.h
│   └── Math.h               ← linmath.h 이동 및 이름 변경
├── Shaders/                 (기존 유지)
├── main.cpp
└── runtime_main.cpp
```

### 작업 순서
1. 새 디렉토리 생성
2. 파일 이동 (git mv)
3. `#include` 경로 전체 업데이트
4. `CMakeLists.txt` 소스 파일 경로 업데이트
5. 빌드 확인

### 대상 파일
- `CMakeLists.txt`
- 이동 대상 파일 전부
- 이동된 파일을 include하는 모든 파일

---

## 5.2 Include Guard 통일

### 현재 문제

모든 헤더 파일이 전통적 `#ifndef`/`#define` 가드 사용:

```cpp
// 현재 패턴 (모든 .h 파일)
#ifndef AUDIO_H
#define AUDIO_H
// ...
#endif
```

### 변경 계획

모든 헤더 파일을 `#pragma once`로 통일:

```cpp
// 변경 후
#pragma once
// ...
```

- 모든 주요 컴파일러(GCC, Clang, MSVC)에서 지원
- 파일 중복 include 방지가 더 안정적
- 가드 이름 충돌 가능성 제거

### 대상 파일
- 모든 `.h` 파일 (약 40개)

---

## 5.3 매직 넘버 상수화

### 현재 문제

| 파일 | 값 | 용도 |
|------|-----|------|
| Particle.cpp:49,64,205 | `6.28318f` | 2π (전체 원) |
| Collision.cpp:69,113 | `0.0001f` | 충돌 감지 엡실론 |
| Camera2D.cpp:30-31 | `0.1f`, `10.0f` | 줌 최소/최대 |
| Input.h:34-35 | `512`, `8` | 키/버튼 최대 수 |

### 변경 계획

```cpp
// src/Common/Constants.h
namespace Constants {
    constexpr float TWO_PI = 6.28318530718f;
    constexpr float PI = 3.14159265359f;
    constexpr float COLLISION_EPSILON = 0.0001f;

    namespace Camera {
        constexpr float MIN_ZOOM = 0.1f;
        constexpr float MAX_ZOOM = 10.0f;
    }

    namespace Input {
        constexpr int MAX_KEYS = 512;
        constexpr int MAX_MOUSE_BUTTONS = 8;
    }
}
```

### 대상 파일
- 새 파일: `src/Common/Constants.h`
- `src/Particle.cpp`
- `src/Collision.cpp`
- `src/Camera2D.h` / `src/Camera2D.cpp`
- `src/Input.h`

---

## 5.4 Shader Uniform Location 캐싱

### 현재 문제

```cpp
// Shader.cpp:31-32 - 매 프레임 uniform 위치 조회
void Shader::SetInt(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(programID, name), value);
}
```

`glGetUniformLocation()`이 매 프레임 호출되어 불필요한 GPU 쿼리 발생.

### 변경 계획

```cpp
class Shader {
    GLuint programID;
    mutable std::unordered_map<std::string, GLint> uniformCache;

    GLint GetUniformLocation(const char* name) const {
        auto it = uniformCache.find(name);
        if (it != uniformCache.end()) {
            return it->second;
        }
        GLint location = glGetUniformLocation(programID, name);
        uniformCache[name] = location;
        return location;
    }

public:
    void SetInt(const char* name, int value) const {
        glUniform1i(GetUniformLocation(name), value);
    }

    void SetFloat(const char* name, float value) const {
        glUniform1f(GetUniformLocation(name), value);
    }

    // 기타 Set 메서드 동일 패턴
};
```

### 대상 파일
- `src/Shader.h` / `src/Shader.cpp`

---

## 5.5 Renderer 상태 머신 추가

### 현재 문제

```cpp
// Begin() 없이 DrawSprite() 호출 가능 - 정의되지 않은 동작
// Begin() 두 번 연속 호출 가능 - 이전 상태 덮어씀
// End() 없이 다음 프레임 시작 가능
```

### 변경 계획

```cpp
class Renderer {
    enum class State { Idle, Drawing };
    State state = State::Idle;

public:
    void Begin(Shader* shader, Camera2D* camera) {
        assert(state == State::Idle && "Begin() called without matching End()");
        state = State::Drawing;
        // ...
    }

    void DrawSprite(Sprite* sprite) {
        assert(state == State::Drawing && "DrawSprite() called without Begin()");
        // ...
    }

    void End() {
        assert(state == State::Drawing && "End() called without Begin()");
        state = State::Idle;
        // ...
    }
};
```

### 대상 파일
- `src/Renderer.h` / `src/Renderer.cpp`

---

## 5.6 SceneManager 안전성 개선

### 현재 문제

```cpp
// Scene.cpp - 지연된 씬 전환
void SceneManager::ChangeScene(const std::string& name) {
    pendingScene = name;         // 마지막 호출만 유효
    sceneChangeRequested = true;
}
```

- 같은 프레임에 ChangeScene() 복수 호출 시 마지막만 반영
- 존재하지 않는 씬 이름 시 `std::cerr` 출력 후 무시
- 반환값 없어 호출자가 성공 여부 알 수 없음

### 변경 계획

```cpp
bool SceneManager::ChangeScene(const std::string& name) {
    if (scenes.find(name) == scenes.end()) {
        Log::Error("Scene not found: " + name);
        return false;
    }
    pendingScene = name;
    sceneChangeRequested = true;
    return true;
}
```

### 대상 파일
- `src/Scene.h` / `src/Scene.cpp`

---

## 5.7 네이밍 일관성

### 현재 문제

- 상수: `MAX_KEYS` (SCREAMING_SNAKE) vs `masterVolume` (camelCase 변수)
- struct: `Particle` vs `ParticleConfig` vs `ParticleEmitter` (일관적이나 struct vs class 혼용)
- 싱글톤 메서드: 모두 `Get()` (이미 일관적)

### 변경 계획

| 항목 | 규칙 |
|------|------|
| 클래스/구조체 | PascalCase |
| 멤버 변수 | camelCase |
| 상수/constexpr | SCREAMING_SNAKE_CASE |
| 메서드 | PascalCase (현재 프로젝트 컨벤션 유지) |
| 네임스페이스 | PascalCase |
| 파일명 | PascalCase (현재 패턴 유지) |

> 현재 코드베이스가 대체로 이 규칙을 따르고 있으므로, 소수 예외만 수정.

### 대상 파일
- 최소한의 수정 (기존 컨벤션이 대체로 일관적)

---

## 5.8 GameBuilder 경로 검증 추가

### 현재 문제

```cpp
// GameBuilder.cpp:83-94 - 소스 존재 여부 미확인
std::string assetsPath = "assets";
// 바로 copy 시도 - 소스 없으면 실패
```

### 변경 계획

```cpp
bool GameBuilder::CopyAssets(...) {
    if (!fs::exists(assetsPath)) {
        lastError = "Assets directory not found: " + assetsPath;
        return false;
    }
    // 기존 복사 로직
}
```

### 대상 파일
- `src/Core/GameBuilder.cpp`

---

## 체크리스트

- [x] 모든 헤더 `#pragma once` 전환 (~56개 헤더) ✅
- [x] Constants.h 생성 및 매직 넘버 교체 (PI, TWO_PI, COLLISION_EPSILON, Camera zoom) ✅
- [x] Shader uniform location 캐싱 구현 (`unordered_map` 기반) ✅
- [x] Renderer 상태 머신 assertion 추가 (Idle/Drawing 상태 검증) ✅
- [x] SceneManager::ChangeScene 반환값 `bool` 추가 및 씬 존재 검증 ✅
- [x] 소스 디렉토리 재구성 ✅
  - `src/Rendering/` — Renderer, Shader, Texture, Sprite, SpriteSheet, Camera2D, TextRenderer, Animation, Tilemap (9쌍)
  - `src/Systems/` — Audio, Input, Particle (3쌍)
  - `src/Physics/` — Collision (1쌍)
  - `src/UI/` — UI (1쌍)
  - `src/Core/` — Scene, MolgaTime 이동 (2쌍)
  - `src/Common/` — linmath.h 이동 (1파일)
- [x] git mv로 파일 이동 후 include 경로 업데이트 (~80개 include 변경) ✅
- [x] CMakeLists.txt 소스 경로 업데이트 (ENGINE_SOURCES 16개 항목) ✅
- [x] 빌드 확인 및 기능 테스트 (4/4 CTest 통과) ✅
- [N/A] GameBuilder 경로 검증 — 이미 모든 경로에 `fs::exists()` 검증 존재
- [N/A] 네이밍 일관성 (5.7) — 이미 PascalCase/camelCase/SCREAMING_SNAKE 일관적

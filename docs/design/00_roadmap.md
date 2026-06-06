# Molga Engine → Unity급 2D 게임 엔진 로드맵

> 작성일: 2026-03-22
> 현재 상태: Phase 0-5 리팩토링 완료 (ECS, Scene, Editor, 코드 품질, 디렉토리 구조화)
> 목표: Unity와 유사한 수준의 2D 게임 엔진

---

## 현재 엔진 보유 기능

| 영역 | 구현 완료 |
|------|-----------|
| **렌더링** | Sprite, SpriteSheet, Animation, Camera2D, TextRenderer, Particle, Tilemap, 단일 Shader |
| **ECS** | GameObject, Component (O(1) lookup), Transform 계층, ComponentFactory |
| **물리** | AABB/Circle 충돌 감지 (응답 없음) |
| **입력** | 기본 키보드/마우스 (GLFW) |
| **오디오** | miniaudio (재생/정지/일시정지) |
| **스크립팅** | C++ 스크립트 (컴파일, 핫리로드, builtin/dynamic 분리) |
| **에디터** | ImGui Docking, Hierarchy, Inspector, Project Browser, WindowManager, SceneOps, BuildManager |
| **씬** | SceneManager, JSON 직렬화, 부모-자식 복원 |
| **빌드** | CMake (molga_core + molga_engine + molga_runtime), CTest 4개 |
| **플랫폼** | macOS |

---

## 미구현 기능 전체 목록 (44개 시스템)

### A. 코어 엔진 (7개) → [01_core_engine.md](01_core_engine.md)

| # | 시스템 | 복잡도 | 예상 기간 | 설명 |
|---|--------|--------|-----------|------|
| A1 | **이벤트/메시징 시스템** | Small | 2-3일 | 타입 안전 이벤트 버스. 모든 시스템의 통신 기반 |
| A2 | **오브젝트 풀링** | Small | 1-2일 | 빈번한 생성/파괴 최적화 |
| A3 | **코루틴/태스크** | Medium | 3-5일 | C++17 빌더 패턴 기반 (StartCoroutine, WaitForSeconds 등) |
| A4 | **고급 입력 시스템** | Medium | 1-2주 | Input Action 매핑, 컴포지트 입력, 디바이스 추상화 |
| A5 | **에셋 관리 시스템** | Large | 2-3주 | GUID 기반 참조, AssetDatabase, 비동기 로딩 |
| A6 | **프리팹 시스템** | Large | 2-3주 | 직렬화 기반 인스턴스화, 오버라이드, 중첩 |
| A7 | **2D 물리 엔진** | Very Large | 4-6주 | Box2D 3.x 통합, Rigidbody2D, 힘/중력/관절/레이캐스트 |

### B. 렌더링 (8개) → [02_rendering.md](02_rendering.md)

| # | 시스템 | 복잡도 | 예상 기간 | 설명 |
|---|--------|--------|-----------|------|
| B1 | **커스텀 셰이더/머티리얼** | Medium | 1-1.5주 | ShaderManager, Material 시스템, 핫리로드 |
| B2 | **디버그/기즈모 렌더링** | Small | 3-5일 | 즉시 모드 디버그 드로잉 (라인, 사각형, 원) |
| B3 | **스프라이트 아틀라스** | Medium | 1-2주 | MaxRects 빈 패킹, 빌드타임 생성, UV 매핑 |
| B4 | **배치 렌더러** | Large | 2-3주 | 스프라이트 배칭 (10,000+ 목표), 파티클 인스턴싱 |
| B5 | **소팅 레이어** | Medium | 1-1.5주 | 64비트 정렬 키, 레이어/순서/텍스처 기반 정렬 |
| B6 | **카메라 시스템 고도화** | Medium | 1.5-2주 | 멀티 카메라, Pixel-Perfect, Cinemachine 스타일 추적 |
| B7 | **포스트 프로세싱** | Large | 2-3주 | Ping-Pong FBO, Bloom, Vignette, 색수차, Screen Shake |
| B8 | **2D 라이팅** | Very Large | 3-5주 | Point/Global/Spot Light, 노멀맵, 2D 그림자 |

### C. 에디터 (10개) → [03_editor.md](03_editor.md)

| # | 시스템 | 복잡도 | 예상 기간 | 설명 |
|---|--------|--------|-----------|------|
| C1 | **씬뷰 + 기즈모** | Very Large | 5-9일 | FBO 렌더링, 오브젝트 피킹, 이동/회전/스케일 핸들, 그리드, 스냅 |
| C2 | **Undo/Redo** | Large | 5-7일 | Command 패턴 + 직렬화 스냅샷 하이브리드 |
| C3 | **콘솔/로그 창** | Medium | 3일 | 기존 Log 연동, 필터링, 가상 스크롤 |
| C4 | **프로파일러** | Large | 5일 | RAII ProfileScope, 프레임 타임라인, 링 버퍼 |
| C5 | **멀티 오브젝트 편집** | Medium-Large | 5-7일 | SelectionManager, 혼합 값 표시, 일괄 수정 |
| C6 | **애니메이션 에디터** | Very Large | 9-13일 | Dope Sheet, 커브 에디터, 키프레임, 타임라인 |
| C7 | **드래그 & 드롭** | Medium | 3-4일 | ImGui D&D API 활용, 에셋→씬, 컴포넌트→오브젝트 |
| C8 | **컴포넌트 복사/붙여넣기** | Small-Medium | 2-3일 | Inspector 컨텍스트 메뉴, Serialize/Deserialize 활용 |
| C9 | **환경설정/프로젝트 설정** | Medium-Large | 5-8일 | Preferences vs Project Settings 분리, Tags & Layers, 충돌 매트릭스 |
| C10 | **에셋 임포트 파이프라인** | Large-Very Large | 8-12일 | GUID/Meta 파일, AssetDatabase, FileWatcher, Importer |

### D. 게임플레이 (9개) → [04_gameplay.md](04_gameplay.md)

| # | 시스템 | 복잡도 | 예상 기간 | 설명 |
|---|--------|--------|-----------|------|
| D1 | **태그 & 레이어** | Small | 3-5일 | 비트마스크 충돌 매트릭스, 문자열 태그, 소팅 레이어 |
| D2 | **오브젝트 라이프사이클** | Medium | 1-1.5주 | Instantiate/Destroy, 지연 파괴, DontDestroyOnLoad |
| D3 | **추가 빌트인 컴포넌트** | Large (합산) | 3-4주 | Rigidbody2D, 추가 콜라이더, AudioSource, Camera, ParticleSystem |
| D4 | **애니메이션 상태 머신** | Large | 2-3주 | FSM Animator, 파라미터, 조건 전환, 레이어 |
| D5 | **트위닝/이징** | Medium | 1-1.5주 | DOTween 스타일 체이닝, 30개 이징 함수, Sequence |
| D6 | **씬 관리 확장** | Medium-Large | 1.5-2주 | Additive 로딩, 비동기 로딩, 씬 언로딩 |
| D7 | **전역 게임 상태** | Medium | 1-1.5주 | GamePrefs (키-값), SaveSystem (슬롯 기반) |
| D8 | **2D 내비게이션** | Large | 2-3주 | 그리드 A*, Tilemap NavGrid 자동 생성, 장애물 회피 |
| D9 | **2D 특화 시스템** | 개별 Medium-Large | 필요 시 | Sprite Shape, Effectors, Sprite Mask, 2D IK |

### E. 플랫폼 & 툴링 (10개) → [05_platform.md](05_platform.md)

| # | 시스템 | 우선순위 | 복잡도 | 설명 |
|---|--------|----------|--------|------|
| E1 | **CI/CD 파이프라인** | Critical | Medium | GitHub Actions, 자동 빌드/테스트/아티팩트 |
| E2 | **크로스 플랫폼 빌드** | Critical | Large | Windows/Linux 지원 (GLFW/miniaudio 이미 크로스플랫폼) |
| E3 | **테스팅 프레임워크** | High | Medium | Catch2 v3 도입, 기존 cassert 마이그레이션 |
| E4 | **버전 관리 통합** | High | Small-Medium | .gitignore, Git LFS, UUID 기반 ID |
| E5 | **성능 모니터링** | High | Medium-Large | Tracy 프로파일러 통합, GPU 타이밍, 메모리 추적 |
| E6 | **WebGL/Emscripten** | High | Large | GLSL 전처리, 메인 루프 변경, glad 추상화 |
| E7 | **문서화/API 레퍼런스** | Medium | Medium | Doxygen, 에디터 내 툴팁 |
| E8 | **에셋 번들/패키징** | Medium | Large | 에셋 압축 (LZ4), 스트리밍, 가상 파일시스템 |
| E9 | **로컬라이제이션** | Low | Medium | 문자열 테이블, CJK 폰트 지원 |
| E10 | **플러그인/확장 시스템** | Low | Large | 동적 로딩, API 버저닝, 확장 포인트 |

---

## 종합 우선순위 로드맵

### Phase 6: 기반 인프라 (4-6주)

> 다른 모든 시스템의 전제 조건이 되는 기반 시스템들

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | A1 이벤트 시스템 | 2-3일 | 모든 시스템의 통신 기반 |
| 2 | D1 태그 & 레이어 | 3-5일 | 충돌 필터링, 렌더링 정렬의 기반 |
| 3 | B1 커스텀 셰이더/머티리얼 | 1-1.5주 | 라이팅, 포스트프로세싱의 전제 |
| 4 | B2 디버그/기즈모 렌더링 | 3-5일 | 이후 모든 개발의 디버깅 도구 |
| 5 | A2 오브젝트 풀링 | 1-2일 | 파티클, 물리 등 성능 기반 |
| 6 | D2 오브젝트 라이프사이클 | 1-1.5주 | Instantiate/Destroy 패턴 확립 |
| 7 | E1 CI/CD | 1주 | 크로스 플랫폼 확장 전제 |

### Phase 7: 렌더링 파이프라인 (6-10주)

> 성능 최적화와 비주얼 품질 향상

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | B3 스프라이트 아틀라스 | 1-2주 | 배치 렌더러의 전제 |
| 2 | B4 배치 렌더러 | 2-3주 | 핵심 성능 (10,000+ 스프라이트) |
| 3 | B5 소팅 레이어 | 1-1.5주 | 배치와 연동 정렬 |
| 4 | B6 카메라 시스템 | 1.5-2주 | 멀티 카메라, Pixel-Perfect |
| 5 | B7 포스트 프로세싱 | 2-3주 | FBO 인프라 구축 → 라이팅 전제 |
| 6 | B8 2D 라이팅 | 3-5주 | 가장 복잡하지만 비주얼 임팩트 최대 |

### Phase 8: 에디터 핵심 (4-6주)

> 게임 개발 워크플로우에 필수적인 에디터 기능

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | C1 씬뷰 + 기즈모 | 5-9일 | 에디터의 핵심. 오브젝트 조작 가능 |
| 2 | C2 Undo/Redo | 5-7일 | 안전한 편집의 기본 |
| 3 | C3 콘솔/로그 | 3일 | 디버깅 필수 도구 |
| 4 | C7 드래그 & 드롭 | 3-4일 | 에디터 사용성 대폭 향상 |
| 5 | C8 컴포넌트 복사/붙여넣기 | 2-3일 | 편의 기능 |
| 6 | C4 프로파일러 | 5일 | 성능 문제 진단 |

### Phase 9: 게임플레이 시스템 (8-12주)

> 실제 게임 제작에 필요한 핵심 시스템들

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | A7 2D 물리 엔진 (Box2D) | 4-6주 | 게임 역학의 핵심 |
| 2 | D3 추가 빌트인 컴포넌트 | 3-4주 | Rigidbody2D, 콜라이더, AudioSource 등 |
| 3 | D4 애니메이션 상태 머신 | 2-3주 | 캐릭터 애니메이션 관리 |
| 4 | A4 고급 입력 시스템 | 1-2주 | 입력 매핑/리바인딩 |
| 5 | D5 트위닝/이징 | 1-1.5주 | UI/게임 연출 |
| 6 | A3 코루틴 | 3-5일 | 시퀀스/타이밍 제어 |

### Phase 10: 에셋 파이프라인 & 고급 기능 (6-10주)

> 대규모 프로젝트 지원을 위한 인프라

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | A5 에셋 관리 시스템 | 2-3주 | GUID, AssetDatabase |
| 2 | C10 에셋 임포트 파이프라인 | 8-12일 | Meta 파일, Importer |
| 3 | A6 프리팹 시스템 | 2-3주 | 에셋 관리 위에 구축 |
| 4 | D6 씬 관리 확장 | 1.5-2주 | Additive/비동기 로딩 |
| 5 | C9 환경설정/프로젝트 설정 | 5-8일 | 프로젝트 설정 UI |

### Phase 11: 플랫폼 확장 (4-8주)

> 다중 플랫폼 배포 지원

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | E2 크로스 플랫폼 (Windows/Linux) | 2-3주 | 가장 큰 사용자 기반 |
| 2 | E3 테스팅 프레임워크 (Catch2) | 1주 | 회귀 방지 |
| 3 | E6 WebGL/Emscripten | 2-3주 | 웹 배포 |
| 4 | E4 버전 관리 통합 | 3-5일 | UUID, Git LFS |

### Phase 12: 고급 에디터 & 전문 시스템 (6-12주)

> 성숙한 엔진을 위한 고급 기능

| 순서 | 시스템 | 기간 | 이유 |
|------|--------|------|------|
| 1 | C5 멀티 오브젝트 편집 | 5-7일 | 대규모 씬 편집 |
| 2 | C6 애니메이션 에디터 | 9-13일 | 에디터 내 애니메이션 제작 |
| 3 | D8 2D 내비게이션 | 2-3주 | AI 경로 탐색 |
| 4 | D7 전역 게임 상태 | 1-1.5주 | 세이브/로드 |
| 5 | E5 성능 모니터링 (Tracy) | 1-2주 | 프로덕션 프로파일링 |
| 6 | D9 2D 특화 시스템 | 필요 시 | Sprite Shape, Mask, Effectors 등 |

---

## 타임라인 요약

```
Phase 6  [기반 인프라]     ████████              (4-6주)
Phase 7  [렌더링]              ████████████████    (6-10주)
Phase 8  [에디터 핵심]              ██████████      (4-6주)
Phase 9  [게임플레이]                  ████████████████████  (8-12주)
Phase 10 [에셋 파이프라인]                        ████████████████  (6-10주)
Phase 11 [플랫폼 확장]                                    ████████████  (4-8주)
Phase 12 [고급 기능]                                          ████████████████  (6-12주)
```

**MVP (게임 제작 가능)**: Phase 6-9 완료 → 약 22-34주 (5.5-8.5개월)
**전체 완성**: Phase 6-12 → 약 38-64주 (9.5-16개월)

---

## 선행 개선 사항 (Phase 6 시작 전)

코드베이스에서 새 시스템 도입 전 먼저 정비하면 좋은 4가지:

1. **Component에 `OnDestroy()` 콜백 추가** — 리소스 해제를 위한 라이프사이클 메서드
2. **Collider2D 공통 기반 클래스 추출** — 현재 BoxCollider2D가 직접 Component 상속. 다형성 충돌 처리를 위한 추상 베이스
3. **FixedUpdate 고정 시간 간격 루프** — 물리 시뮬레이션에 필수. 누적 방식 (accumulator pattern)
4. **Broad Phase 공간 분할** — 충돌 감지 성능을 위한 Uniform Grid 또는 Quadtree

---

## 시스템 간 의존성 맵

```
이벤트 시스템 (A1) ─────────────────────────────────────┐
  │                                                       │
  ├→ 물리 엔진 (A7) ←── FixedUpdate, Collider2D 기반     │
  │    └→ Rigidbody2D (D3)                                │
  │    └→ 추가 콜라이더 (D3)                              │
  │                                                       │
  ├→ 입력 시스템 (A4)                                     │
  │                                                       │
  ├→ 오브젝트 라이프사이클 (D2)                           │
  │    └→ 프리팹 (A6) ←── 에셋 관리 (A5)                 │
  │    └→ 풀링 (A2)                                       │
  │                                                       │
태그 & 레이어 (D1) ──────────────────────────────────────┤
  │                                                       │
  ├→ 소팅 레이어 (B5) ←── 배치 렌더러 (B4)              │
  │                        └── 스프라이트 아틀라스 (B3)    │
  │                                                       │
  ├→ 카메라 시스템 (B6) (culling mask)                    │
  │                                                       │
커스텀 셰이더 (B1) ──────────────────────────────────────┤
  │                                                       │
  ├→ 디버그 렌더링 (B2)                                   │
  ├→ 머티리얼 → 배치 렌더러 (B4)                          │
  ├→ 포스트 프로세싱 (B7) → 2D 라이팅 (B8)               │
  │                                                       │
에셋 관리 (A5) ──────────────────────────────────────────┤
  ├→ 에셋 임포트 (C10)                                    │
  ├→ 프리팹 (A6)                                          │
  └→ 씬 관리 확장 (D6)                                    │
                                                          │
독립 시스템 (의존성 없음):                                │
  - 코루틴 (A3)                                           │
  - 트위닝 (D5)                                           │
  - 전역 상태 (D7)                                        │
  - 콘솔/로그 (C3)                                        │
  - Undo/Redo (C2)                                        │
  - CI/CD (E1)                                            │
```

---

## 외부 라이브러리 권장

| 시스템 | 라이브러리 | 라이선스 | 용도 |
|--------|-----------|---------|------|
| 2D 물리 | [Box2D 3.x](https://github.com/erincatto/box2d) | MIT | 강체, 관절, 충돌 응답 |
| 프로파일링 | [Tracy](https://github.com/wolfpld/tracy) | BSD | CPU/GPU 프로파일링 |
| 테스팅 | [Catch2 v3](https://github.com/catchorg/Catch2) | BSL-1.0 | 유닛/통합 테스트 |
| GUID 생성 | [crossguid](https://github.com/graeme-hill/crossguid) | MIT | 크로스 플랫폼 UUID |
| 파일 감시 | [efsw](https://github.com/SpartanJ/efsw) | MIT | 에셋 파일 변경 감지 |
| WebGL | [Emscripten](https://emscripten.org/) | MIT | C++ → WebAssembly |

---

## 문서 구조

```
docs/ongoing/
├── 00_roadmap.md          ← 이 파일 (마스터 로드맵)
├── 01_core_engine.md      ← 코어 엔진 7개 시스템 상세 조사
├── 02_rendering.md        ← 렌더링 8개 시스템 상세 조사
├── 03_editor.md           ← 에디터 10개 시스템 상세 조사
├── 04_gameplay.md         ← 게임플레이 9개 시스템 상세 조사
└── 05_platform.md         ← 플랫폼/툴링 10개 시스템 상세 조사
```

각 상세 문서에는 시스템별로 다음 내용이 포함되어 있음:
- Unity 구현 방식 분석
- Molga Engine에 맞는 C++17 구현 설계 (코드 포함)
- 권장 라이브러리/알고리즘
- 복잡도 및 의존성
- 에디터 통합 방법

# Phase 6 구현 계획 리뷰 및 개선 제안 (Codex)

> 대상 문서: `docs/ongoing/phase6_implementation_plan.md`
> 검토 기준: 2026-03-23 현재 `src/`, `tests/`, `CMakeLists.txt`, `docs/design/MASTER_PLAN.md` 대조

## 결론

문서의 큰 방향은 맞다. 다만 현재 계획은 "새 시스템을 어떤 파일에 넣을지"는 잘 설명하지만, 실제 코드베이스의 소유권 모델, 렌더링 상태 모델, 테스트 인프라, 오브젝트 수명 계약과 부딪히는 지점을 과소평가하고 있다.

특히 바로 보정이 필요한 축은 5개다.

1. D2 `GameObjectManager`는 현재 `shared_ptr` 소유 구조와 병렬 공존하기 어렵다.
2. B1/B2는 렌더러의 `Begin/End` 소유권부터 바로잡지 않으면 그 위에 얹을 수 없다.
3. D1 `TagManager`는 문서상 독립 작업이지만, 실제로는 D2 수준의 수명 관리가 없으면 raw pointer 인덱스가 쉽게 깨진다.
4. A1 이벤트 큐와 D2 지연 파괴는 raw `GameObject*` 이벤트 payload 때문에 함께 설계해야 한다.
5. B1/B2 테스트는 현재 CTest 구조로는 "유닛 테스트"로 돌기 어렵다. OpenGL 컨텍스트가 필요하다.

---

## 1. 확인된 문제와 빠진 전제

### 1-1. 현재 상태 진단이 일부 기능을 "존재"와 "실제로 동작"을 혼동하고 있다

문서의 현재 상태 진단은 `Script 시스템 (FixedUpdate, LateUpdate, 핫리로드)`를 보유 상태로 적고 있다.

- `docs/ongoing/phase6_implementation_plan.md:60-66`

하지만 실제 코드를 보면 `LateUpdate()`와 `Start()`는 선언만 있고 엔트리포인트에서 호출되지 않는다.

- `src/Scripting/Script.h:19-29`
- `src/Scripting/Script.h:59-61`
- `src/main.cpp:169-191`
- `src/runtime_main.cpp:107-123`
- `rg` 결과 기준 `LateUpdateScripts()` 호출 지점 없음

즉 현재 기반은 "스크립트 라이프사이클 일부 보유"이지, 완성된 런타임 훅을 가진 상태가 아니다. 이 차이는 Phase 6에서 중요하다.

- A1 이벤트 구독/발행을 스크립트에 연결할 때 `Start` 훅 부재가 드러난다
- D2 `Instantiate()` 이후 초기화 타이밍 정의가 모호하다
- B2 디버그 표시를 스크립트에서 제어하려 할 때 `LateUpdate`/프레임 종료 훅이 없다

현재 상태 진단은 이 부분을 더 보수적으로 적는 편이 맞다.

### 1-2. D1은 실제로 "독립"이 아니다

문서는 D1 태그/레이어를 독립 작업처럼 두고 있다.

- `docs/ongoing/phase6_implementation_plan.md:82-88`
- `docs/ongoing/phase6_implementation_plan.md:1088-1104`

하지만 제안된 `TagManager`는 `GameObject*` raw pointer를 전역 인덱스에 저장한다.

- `docs/ongoing/phase6_implementation_plan.md:294-315`

현재 코드에서 오브젝트는 여러 군데가 각자 `std::vector<std::shared_ptr<GameObject>>`로 소유한다.

- `src/main.cpp:54-56`
- `src/runtime_main.cpp:80-83`
- `src/Scenes/GameScene.h:22-24`
- `src/Scenes/GameScene.h:34`

이 구조에서 D1만 먼저 넣으면 아래 문제가 바로 생긴다.

- 씬 종료나 벡터 `clear()` 시 TagManager raw pointer 정리 책임이 분산된다
- `GameObject` 소멸자가 태그 인덱스 정리를 모른다
- 에디터 `editorObjects`, 런타임 `gameObjects`, 씬 내부 `gameObjects`가 서로 다른 우주를 만든다

즉 D1을 안전하게 하려면 적어도 둘 중 하나가 먼저 필요하다.

- D2 수준의 중앙 수명 관리
- 혹은 D1 1차 버전에서는 `tag`, `layer` 필드와 직렬화만 넣고, 글로벌 인덱스는 뒤로 미루기

이 문서의 현재 의존성 표는 이 점을 반영하지 못한다.

### 1-3. `SortingLayer` 정렬 키 설계는 음수 `sortingOrder`와 충돌할 수 있다

문서는 정렬 키를 `(layerOrder << 16) | (orderInLayer & 0xFFFF)`로 제안한다.

- `docs/ongoing/phase6_implementation_plan.md:387-388`

현재 `SpriteRenderer`의 `sortingOrder`는 signed `int`다.

- `src/ECS/Components/SpriteRenderer.h:43-45`
- `src/ECS/Components/SpriteRenderer.h:68`

이 조합은 `orderInLayer`가 음수일 때 직관적 정렬을 보장하지 않는다. 예를 들어 `-1`은 마스킹 후 `65535`처럼 취급되어 같은 레이어 내에서 가장 뒤가 아니라 가장 앞으로 갈 수 있다.

권장안:

- bit-pack보다 `(layerOrder, orderInLayer, stableTieBreaker)` 튜플 비교
- 혹은 signed 16-bit bias를 명시적으로 적용

정렬 시스템은 Phase 7 배치 렌더러의 전제가 되므로, 여기서 정렬 키를 잘못 고정하면 뒤에서 다시 뜯게 된다.

### 1-4. B1/B2 이전에 렌더러의 `Begin/End` 소유권부터 정리해야 한다

현재 렌더링 경로는 이미 상태 모델이 엇갈려 있다.

- `src/main.cpp:197-207`
- `src/runtime_main.cpp:129-139`
- `src/ECS/Components/SpriteRenderer.cpp:54-56`
- `src/Rendering/Renderer.cpp:69-117`

`main.cpp`와 `runtime_main.cpp`는 루프 바깥에서 `renderer->Begin()`/`End()`를 잡고 있는데, `SpriteRenderer::RenderSprite()`도 내부에서 다시 `Begin()`/`End()`를 호출한다. 현재 `Renderer::Begin()`은 `assert(state == Idle)`를 갖고 있으므로, 이 설계는 디버그 빌드에서 바로 충돌할 수 있다.

즉 문서의 렌더링 현황 설명

- `docs/ongoing/phase6_implementation_plan.md:39-47`

은 현재 호출 구조를 충분히 반영하지 못하고 있다. B1/B2는 "셰이더/머티리얼 추가"보다 먼저 아래 계약을 정해야 한다.

- `Begin/End`는 호출자가 소유하는가
- `SpriteRenderer`는 `Sprite` 데이터만 준비하고 draw는 외부 패스가 하는가
- `Renderer::DrawSprite()`가 재질/유니폼 바인딩의 최종 소유자인가

이 정리가 없으면:

- Material이 `Apply()`해도 `Renderer::DrawSprite()`가 다시 `uColor/uUV/useTexture`를 덮어쓴다
- DebugDraw가 같은 프레임에 별도 셰이더/VAO/VBO를 잡을 때 상태 충돌이 난다

즉 B1/B2의 진짜 선행 조건은 "render pass ownership 정리"다.

### 1-5. `Material` 설계는 소유권과 직렬화 경로가 비어 있다

문서의 `Material`은 `Shader*`와 `Texture*` raw pointer를 non-owning으로 저장하고, 동시에 `Serialize/Deserialize()`를 제공한다.

- `docs/ongoing/phase6_implementation_plan.md:483-520`

하지만 현재 코드베이스에서 텍스처는 `texturePath` 기반으로 복원하고 있다.

- `src/ECS/Components/SpriteRenderer.cpp:59-86`

raw `Texture*`와 raw `Shader*`는 JSON에 그대로 저장할 수 없다. 따라서 현 설계는 "직렬화 지원"을 명시했지만 실제 복원 키가 없다.

권장안:

- `Material`은 런타임 객체와 자산 정의를 분리
- 직렬화는 `shaderName`, `texturePath`, 스칼라/색상 프로퍼티만 저장
- `Texture*`와 `Shader*`는 로드 시 매니저를 통해 resolve

또한 `SpriteRenderer`에 `Material* material`만 추가하면, 머티리얼의 소유자가 누구인지 모호하다.

- 씬 shared asset인가
- 객체별 인스턴스인가
- 에디터에서 수정 시 공유/복제 규칙은 무엇인가

이건 B1에서 반드시 먼저 정해야 하는 계약이다.

### 1-6. B1/B2 테스트는 현재 CTest 구조로는 "유닛 테스트"가 아니다

현재 테스트 타깃은 `molga_core`를 링크하지만 OpenGL 컨텍스트를 만들지 않는다.

- `tests/CMakeLists.txt:1-11`

반면 `Shader`, `Renderer`, `DebugDraw`, `Material::Apply()`는 GL 함수 호출이 필요하다.

- `src/Rendering/Shader.cpp:13-20`
- `src/Rendering/Shader.cpp:27-67`
- `src/Rendering/Renderer.cpp:69-110`

따라서 문서의 아래 테스트는 현재 구조로는 순수 unit test가 아니다.

- `docs/ongoing/phase6_implementation_plan.md:577-582`
- `docs/ongoing/phase6_implementation_plan.md:683-689`
- `docs/ongoing/phase6_implementation_plan.md:1119-1122`

권장안:

- `test_material_serialization.cpp`처럼 GL 없는 테스트와
- 숨김 GLFW 윈도우를 만드는 integration test를 분리

예시:

- unit: property map, JSON round-trip, sort key, queue expiry
- integration: shader compile, material apply, debug draw smoke

지금 문서의 "유닛 테스트" 표기는 테스트 인프라 현실과 맞지 않는다.

### 1-7. A1 이벤트 큐는 D2와 함께 수명 계약을 정의해야 한다

문서의 이벤트 payload는 raw `GameObject*`를 담는다.

- `docs/ongoing/phase6_implementation_plan.md:206-218`

동시에 D2는 프레임 끝에 destroy queue를 처리하는 구조다.

- `docs/ongoing/phase6_implementation_plan.md:898-934`
- `docs/ongoing/phase6_implementation_plan.md:937-945`

그런데 A1도 프레임 끝 `ProcessQueue()`를 전제로 한다.

- `docs/ongoing/phase6_implementation_plan.md:237-241`

즉 아래 순서가 정의되지 않으면 문제가 생긴다.

- 이벤트 큐 먼저 처리 후 destroy queue 처리
- destroy queue 먼저 처리 후 이벤트 큐 처리

후자의 경우 queued event 안의 `GameObject*`가 이미 무효일 수 있다. 전자의 경우는 "곧 파괴될 오브젝트"를 이벤트가 보게 된다.

권장안:

- 이벤트 payload는 raw pointer 대신 `GameObjectID` 또는 stable handle 사용
- 혹은 이벤트/파괴 처리 순서를 문서에 명시
- 필요하면 `DestroyRequestedEvent`와 실제 destroy commit을 분리

이건 A1과 D2를 따로 설계할 수 없다는 뜻이다.

### 1-8. A1 구독 해제 API는 사용성 면에서 약하다

문서의 EventBus는 `Unsubscribe<EventT>(SubscriptionID id)`를 제안한다.

- `docs/ongoing/phase6_implementation_plan.md:140-147`

이 API는 해제할 때도 이벤트 타입을 알고 있어야 한다. 그런데 실전에서는 구독 핸들만 들고 generic하게 해제하거나, RAII로 scope 종료 시 자동 해제하는 경우가 많다.

권장안:

- `Unsubscribe(SubscriptionID id)` 비템플릿 API 제공
- `ScopedSubscription` 추가
- publish 중 subscribe/unsubscribe가 일어나도 안전하도록 snapshot 또는 pending mutation 큐 도입

이 부분을 미리 잡아야 D2, 에디터 툴, 향후 스크립트 이벤트 구독이 편해진다.

### 1-9. D2 `Clone()` 설계 예시는 현재 API와 맞지 않고, ID도 잘못 복제된다

문서의 Clone 예시는 존재하지 않는 오버로드를 호출한다.

- `docs/ongoing/phase6_implementation_plan.md:879-887`
- 현재 `SceneSerializer` 시그니처는 `DeserializeGameObject(const std::string&)`만 제공
- `src/Core/SceneSerializer.h:19-23`

또한 현재 `DeserializeGameObject()`는 JSON 안의 `id`를 그대로 복원한다.

- `src/Core/SceneSerializer.cpp:199-203`

그래서 문서에 적힌 "새 ID 자동 할당"은 현재 코드와 정반대다. 지금 구조에서 JSON round-trip clone을 하면 원본과 동일 ID를 가진 오브젝트가 생긴다.

이건 D2의 핵심 버그 후보다.

권장안:

- clone 시 JSON에서 `id`와 `parentId`를 제거하거나
- `DeserializeGameObject(json, preserveIDs=false)` 옵션을 추가
- 자식까지 복제할 경우 ID remap 표를 같이 생성

이 문제는 문서상 예시 수준이 아니라 실제 구현 리스크다.

### 1-10. D2 `GameObjectManager`는 현재 소유권 모델과 병렬 공존하기 어렵다

문서는 Phase 6에서는 "기존 코드 유지 + 새 API 병렬 제공"을 제안한다.

- `docs/ongoing/phase6_implementation_plan.md:943-945`

하지만 현재 오브젝트는 이미 여러 컨테이너가 소유한다.

- `main.cpp`의 `editorObjects`
- `runtime_main.cpp`의 `gameObjects`
- `GameScene` 내부 `gameObjects`
- `SceneManager`는 씬만 관리하고, 씬 내부 오브젝트는 모른다

참고:

- `src/main.cpp:54-56`
- `src/runtime_main.cpp:80-83`
- `src/Scenes/GameScene.h:22-24`
- `src/Scenes/GameScene.h:34`
- `src/Core/Scene.cpp:31-50`

여기에 또 다른 중앙 `std::vector<std::shared_ptr<GameObject>> objects`를 두면 "같은 GameObject를 누가 최종 소유하는가"가 모호해진다.

문서대로 병렬 제공하면 바로 생기는 문제:

- `FindByID`는 manager-owned object만 찾고 scene-owned object는 놓친다
- `DestroyAllNonPersistent()`가 실제 활성 씬 오브젝트를 모두 못 본다
- editor/runtime/sample objects와 scene objects가 서로 다른 registry를 가진다

권장안:

- D2는 중앙 소유권 이전을 전제로 하거나
- 최소한 `GameObjectRegistry` 수준의 읽기 전용 인덱스와 `DestroyQueue`를 분리
- "manager owns objects"와 "manager indexes external owners"를 명확히 구분

병렬 제공은 구현 난이도를 낮추는 것이 아니라, 오히려 split-brain 상태를 만들 가능성이 크다.

### 1-11. D2는 부모-자식 파괴 계약도 같이 정의해야 한다

현재 `GameObject` 소멸자는 부모에서는 자신을 제거하지만, 자신의 자식들에 대해서는 단순 `children.clear()`만 수행한다.

- `src/ECS/GameObject.cpp:13-22`

즉 부모가 먼저 파괴되면 자식의 `parent` raw pointer는 dangling 될 수 있다. Phase 6 문서의 D2 섹션에는 이 계층 파괴 규칙이 없다.

여기서는 최소한 아래 셋 중 하나를 정해야 한다.

- 부모 파괴 시 자식도 함께 cascade destroy
- 부모 파괴 시 자식은 자동 orphan 처리
- persistent child가 있으면 별도 정책

이 규칙이 없으면 `DontDestroyOnLoad`, scene unload, delayed destroy에서 부모-자식 트리가 쉽게 깨진다.

### 1-12. A2 `ObjectPool<T>`는 재사용 정책이 빠져 있다

현재 제안은 메모리 슬롯 재사용만 다루고, 객체 상태 reset은 다루지 않는다.

- `docs/ongoing/phase6_implementation_plan.md:724-776`

이 구조로는 풀에서 반환된 객체가 이전 프레임 상태를 그대로 들고 나온다. 파티클, 발사체, 임시 이펙트에는 거의 항상 `OnAcquire`/`OnRelease` 혹은 reset 콜백이 필요하다.

권장안:

- `Acquire(initFn)` / `Release(resetFn)` 지원
- 혹은 `IPoolable` 인터페이스 (`OnAcquire`, `OnRelease`)

단순 컨테이너로 끝내면 결국 실제 사용처마다 수동 reset 코드가 흩어진다.

### 1-13. E1 CI는 초반에 넣는 것이 맞지만, 현재 설계는 아티팩트와 GL 테스트 분리를 더 써야 한다

좋은 점:

- 현재 `.github/workflows`는 비어 있다
- CI를 초반에 넣자는 방향은 맞다

확인 결과:

- `.github/` 자체가 현재 없다
- `CMakeLists.txt`는 `external/glfw`를 `add_subdirectory`로 사용한다
- `brew install glfw`는 불필요할 가능성이 높다

참고:

- `docs/ongoing/phase6_implementation_plan.md:1005-1007`
- `docs/ongoing/phase6_implementation_plan.md:1041-1045`
- `CMakeLists.txt:6-7`

추가 개선안:

- 아티팩트는 `build/molga_engine` 단일 파일보다 실행 바이너리 + copied shaders/assets 포함 디렉터리를 올리는 편이 낫다
- GL context가 필요한 integration tests는 별도 job 또는 별도 label로 분리
- `ctest --output-on-failure` 외에 `--verbose` 또는 test timeout 정책 추가

---

## 2. 더 나은 실행 순서

현재 문서 순서는:

1. A1
2. B1
3. D1
4. A2
5. B2
6. D2
7. E1

권장 순서는 다소 다르다.

1. E1 CI/CD
2. 렌더러 상태/패스 소유권 정리
3. A1 이벤트 시스템
4. D1 1차: `tag/layer` 필드 + 직렬화만
5. B1 셰이더/머티리얼
6. B2 디버그 렌더링
7. D2 오브젝트 라이프사이클
8. D1 2차: `TagManager`/검색 인덱스
9. A2 오브젝트 풀링

이 순서를 권장하는 이유는 명확하다.

- E1은 초반에 깔아야 이후 작업 회귀를 잡는다
- B1/B2는 현재 렌더링 상태 계약 정리 없이는 안전하게 못 들어간다
- D1의 raw pointer 인덱스는 D2 없이 먼저 넣기 위험하다
- A2는 독립적이지만 실제 사용처가 정해진 뒤 넣어도 늦지 않다

---

## 3. 더 나은 범위 분할

### D1은 두 단계로 쪼개는 편이 안전하다

1차:

- `GameObject::tag`, `GameObject::layer`
- `SceneSerializer` 저장/로드
- `SpriteRenderer::sortingLayerName`

2차:

- `TagManager` 글로벌 인덱스
- `FindWithTag`, `FindAllWithTag`
- lifecycle hook 기반 자동 등록/해제

이렇게 쪼개면 D1을 D2 앞에 일부 진행할 수 있다.

### B1은 `Material`보다 먼저 `RenderSprite` 계약을 고쳐야 한다

실질적인 Step 0:

- `SpriteRenderer`는 draw command를 만들고
- `Renderer`는 active pass 안에서만 그리게 구조 분리
- `Begin/End` 호출권을 한 곳으로 모으기

그 다음에야 `Material::Apply()` 위치가 안정된다.

### D2는 "중앙 소유권"과 "조회/파괴 서비스"를 분리해야 한다

추천 분해:

- `GameObjectRegistry`: 이름/ID/태그 조회 인덱스
- `DestroyQueue`: 프레임 끝 파괴 예약
- `Prefab/CloneService`: 복제

처음부터 `GameObjectManager` 하나에 전부 넣으면 책임이 너무 커진다.

---

## 4. 권장 테스트 보강

문서의 테스트 목록은 방향은 좋지만, 실제 리스크를 잡으려면 아래 케이스가 추가로 필요하다.

- queued event 안의 `GameObject*`가 destroy queue와 같이 있을 때 유효성 검증
- `sortingOrder` 음수/양수 혼합 시 정렬 순서 검증
- `Clone()` 시 새 ID 부여와 parent remap 검증
- 부모 오브젝트 파괴 시 자식 orphan/cascade 정책 검증
- `Material` 직렬화에서 `shaderName`, `texturePath` 복원 검증
- hidden GLFW window 기반 shader/material/debug smoke test
- `SpriteRenderer`가 outer render pass 안에서 중첩 `Begin/End` 없이 동작하는지 검증

특히 B1/B2는 GL 없는 unit test와 GL 있는 integration test를 반드시 분리하는 편이 낫다.

---

## 5. 최종 제안

이 문서는 방향성은 좋지만, 현재 그대로 구현하면 아래 네 지점에서 다시 설계를 되감게 될 가능성이 높다.

1. D1 raw pointer 인덱스와 D2 수명 관리의 불일치
2. B1/B2가 현재 렌더러 상태 모델과 충돌
3. D2 clone/ownership 설계가 현재 `SceneSerializer`/`shared_ptr` 구조와 불일치
4. GL 의존 테스트를 unit test로 가정한 검증 계획

Phase 6을 더 안정적으로 만들려면:

1. CI를 가장 먼저 넣고
2. 렌더링 상태 계약을 B1 직전에 별도 정리하고
3. D1을 필드/직렬화 단계와 인덱스 단계로 나누고
4. D2는 중앙 소유권 이전 없이 "병렬 제공"하지 않는 편이 낫다

이렇게 수정하면 Phase 6은 단순 기능 추가가 아니라, 이후 Phase 7~9가 실제로 올라갈 수 있는 기반이 된다.

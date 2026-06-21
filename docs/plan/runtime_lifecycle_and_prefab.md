# 런타임 Instantiate/Destroy + Unity식 Prefab 시스템 — 구현 계획

> 작성일: 2026-06-14
> 대상 로드맵 항목: D2(오브젝트 라이프사이클), A6(프리팹), A2(오브젝트 풀링 토대)
> 범위 결정: **Phase 1 런타임 생명주기 → Phase 2 Unity식 Prefab** (단계 분리, Phase 1 단독 출시 가능)

## Context

Molga Engine은 물리·렌더링·입력·이벤트·스크립팅·에디터까지 갖췄지만, **플레이 중 오브젝트를 생성·파괴하는 런타임 프리미티브가 없다.** 스크립트가 도달할 수 있는 것은 `GetGameObject()->GetWorld()` 까지이고, `World`에는 로드 시점에만 쓰는 `Add()`만 있다(지연 파괴 큐 없음, 스폰 API 없음). 이 공백 때문에 총알·적 스폰·아이템 드롭·웨이브 등 사실상 모든 게임 로직을 짤 수 없다.

목표:
- **Phase 1 — 런타임 생명주기**: `World`에 지연 큐 기반 `Instantiate`/`Destroy`를 넣고 `Script`에 편의 API를 노출. 이것만으로 슈터/스포너 제작이 가능해진다(단독 출시 가능).
- **Phase 2 — Unity식 Prefab**: `.prefab` 에셋 + 인스턴스 링크 + 프로퍼티 오버라이드(Apply/Revert) + 중첩 프리팹. Phase 1의 "서브트리 클론(fresh-id remap)" 코어 위에 쌓는다.

### 설계 결정(확정)
- Destroy는 **서브트리 전체**로 캐스케이드, `delay` 인자 지원, 지연 큐로 안전하게 flush.
- Instantiate는 객체를 **즉시 생성·반환**하되 `objects_` 병합/`Start()`/`ResolveAssets()`는 flush 시점에.
- 인스턴스 ID는 항상 **새로 발급**(원본 ID 보존 금지).
- 프리팹 식별은 **GUID**(파일 내부 저장) + `PrefabRegistry`가 GUID↔경로 매핑.
- 오버라이드 granularity는 **컴포넌트 JSON 키 단위(per-property)** — nlohmann::json 키 diff로 reflection 없이 구현.
- 씬은 프리팹 인스턴스를 **stripped(링크+오버라이드)** 로 저장하고 로드 시 프리팹에서 복원 → "원본 수정 → 인스턴스 자동 갱신" 보장.

---

## 탐색으로 확인된 재사용 자산 (그대로 활용)

- `GameObject::CollectSubtree(out)` — DFS(부모 먼저) 서브트리 수집. 클론/프리팹 직렬화의 입력. (`src/ECS/GameObject.cpp`)
- `SceneSerializer::SerializeScene/DeserializeScene` — `parentId` 기반 2-pass 부모 복원. 단, **DeserializeScene는 ID를 보존**하므로 Instantiate용으로는 remap 변형이 필요. (`src/Core/SceneSerializer.cpp`)
- `ComponentFactory::Get().Create(typeName, obj)` + 스크립트는 `ScriptManager::Get().CreateScript()` fallback — 타입명→컴포넌트 복원 경로. (`src/ECS/ComponentFactory.h`)
- `GameObject::nextID`(static) / `SetID()` — `SetID`가 `nextID`를 bump해 충돌 방지. 새 `GameObject()`는 자동으로 fresh id.
- `GameObject::NotifyDestroy()` — idempotent, `OnDisable→OnDestroy→OnDetach` 순서 보장. Destroy flush가 호출.
- 에셋 경로 패턴: 컴포넌트가 `path` 문자열 저장 → `Serialize`/`Deserialize` → `ResolveAssets()`에서 `PathService::Get().ResolveAsset(path)`로 지연 로드 (`SpriteRenderer` 참조). 프리팹 참조도 동일 컨벤션.
- 싱글톤 레지스트리 패턴: `ProjectSettings::Get()`, `ScriptManager::Get()`, `TextureManager::Get()` → `PrefabRegistry::Get()` 동일 형태.
- 명령 패턴: `ICommand`(`Execute/Undo/Name`) + `CommandHistory::Execute()` (`src/Editor/Commands/`). 에디터 프리팹 작업은 여기에 신규 커맨드로 추가.
- 게임 루프 안전 flush 지점: `FixedStep → Update → LateUpdate → [Render] → EventBus::ProcessQueue()`. 런타임 `src/runtime_main.cpp`, 에디터 플레이 `src/main.cpp` 동일 구조. **LateUpdate 직후 ~ ProcessQueue 직전**이 deferred flush 자리.
- 플레이 월드: `SceneDocument`가 `editWorld_`/`playWorld_` 보유, `EnterPlay()`에서 `editWorld_.Clone()`. (`src/Editor/SceneDocument.h`)

---

## Phase 1 — 런타임 Instantiate / Destroy (엔진 프리미티브)

### 1.1 서브트리 클론 코어 (SceneSerializer 확장)
`src/Core/SceneSerializer.h/.cpp`에 추가:
- `static nlohmann::json SerializeSubtree(const GameObject* root)` — `CollectSubtree`로 모은 객체들을 `SerializeScene`과 동일한 `gameObjects` 배열 형태로 직렬화(루트의 부모는 끊어 `parentId=-1`).
- `static GameObject* DeserializeSubtreeRemapped(const nlohmann::json& subtree, World& world, std::unordered_map<unsigned,unsigned>& idRemap)` — `DeserializeScene`과 동일한 2-pass지만 **각 객체를 fresh id로 생성**하고 `localId(저장된 id) → newId`를 `idRemap`에 기록. `parentId` 링크는 remap을 거쳐 복원. 루트 포인터 반환.
  - 이 remap map은 Phase 2에서 오버라이드 타깃(prefab localId)→씬 객체를 잇는 다리로 재사용.

### 1.2 World 지연 큐 + 생명주기 API
`src/Core/World.h/.cpp`:
- 내부 상태 추가:
  - `std::vector<std::shared_ptr<GameObject>> pendingAdds_;`
  - `struct PendingDestroy { unsigned id; float delay; };  std::vector<PendingDestroy> pendingDestroys_;`
- 신규 메서드:
  - `GameObject* Instantiate(const GameObject* original);` / 오버로드 `(original, const Vector2& worldPos)`, `(original, GameObject* parent)` — `SerializeSubtree(original)` → `DeserializeSubtreeRemapped`로 fresh-id 복제 → `pendingAdds_`에 push → 루트 포인터 즉시 반환. position/parent 오버로드는 복제 루트의 Transform/SetParent 후처리.
  - `void Destroy(GameObject* obj, float delay = 0.0f);` — id를 `pendingDestroys_`에 등록(중복 무시). delay는 프레임마다 감산.
  - `void FlushDeferred(float dt);` — (a) `pendingDestroys_` delay 감산 후 만료분 처리: 대상 + 서브트리(`CollectSubtree`)를 `NotifyDestroy()` 호출하고 `objects_`에서 erase-remove(부모 children 정리는 `~GameObject()`가 수행). (b) `pendingAdds_`를 `objects_`로 이동하고 새 객체에 한해 `ResolveAssets()` + `StartScripts()` 호출(다음 Update 전에 Start 보장, Unity 시맨틱).
- `Clone()`은 기존대로(편집→플레이 스냅샷, ID 보존) 유지 — Instantiate 경로와 구분.

### 1.3 게임 루프 배선
- `src/runtime_main.cpp`: `world.LateUpdate(dt)` 직후 `world.FlushDeferred(dt);` 호출(렌더 전).
- `src/main.cpp` 플레이 모드: `ActiveWorld().LateUpdate(...)` 직후 동일 호출.
- (Phase 1에서는 에디터 편집 모드 생성/삭제는 기존 커맨드 유지, 신규 UI 없음.)

### 1.4 Script 편의 API
`src/Scripting/Script.h/.cpp`:
- `GameObject* Instantiate(const GameObject* original);` + position/parent 오버로드 → `GetGameObject()->GetWorld()->Instantiate(...)` 위임(world null 가드).
- `void Destroy(GameObject* obj, float delay = 0.0f);` 및 자기 자신 `void Destroy(float delay = 0.0f)` → `world->Destroy(...)`.
- 전역 상태 도입 없이 `World*` 경유만 사용.

### 1.5 데모 빌트인 스크립트(검증용)
`src/Scripting/BuiltinScripts.h/.cpp`에 `Spawner`(N초마다 지정 객체 복제) + 복제 대상에 붙일 `SelfDestruct`(수명 후 `Destroy(this)`) 추가 — 수동 검증과 사용 예시 겸용.

### Phase 1 검증
- 신규 `tests/test_world_lifecycle.cpp`(`tests/CMakeLists.txt` 등록):
  - Instantiate가 컴포넌트/서브트리를 복제하고 **새 id**를 부여(원본 id와 다름, World에서 둘 다 조회됨).
  - position/parent 오버로드 동작.
  - `Destroy`가 flush 시 서브트리 전체를 제거하고 `OnDestroy` 1회 호출(카운터 컴포넌트로 확인).
  - `delay` Destroy가 지정 프레임 수 후 제거.
  - Update 중 Instantiate/Destroy 호출 시 **이터레이터 무효화 없이** 다음 flush에 반영.
- `ctest` 통과. 런타임에서 `Spawner` 데모로 스폰/소멸 육안 확인.

---

## Phase 2 — Unity식 Prefab (에셋 + 인스턴스 링크 + 오버라이드)

### 2a. 프리팹 에셋 + 레지스트리 + 인스턴스화(by GUID)
- `.prefab` 포맷: `SerializeSubtree` 결과 + 헤더 `{ "guid": "<128bit hex>", "version": "1.0", "gameObjects": [...] }`. GUID는 `std::random_device` 기반 생성(신규 의존성 없음).
- `src/Core/PrefabRegistry.h/.cpp` (신규, 싱글톤 `Get()`):
  - 프로젝트 `Assets/` 스캔으로 `.prefab` 수집 → `guid↔path` 양방향 맵, 파싱된 JSON 캐시.
  - `GameObject* Instantiate(const std::string& guid, World& world, std::unordered_map<unsigned,unsigned>& idRemap)` — 캐시 JSON을 `DeserializeSubtreeRemapped`로 fresh-id 복제(Phase 1 코어 재사용).
  - `bool SavePrefab(guid, json)` / 변경 시 재스캔.
- `src/ECS/Components/PrefabInstance.h/.cpp` (신규 컴포넌트, 인스턴스 루트에 부착):
  - 보유: `std::string prefabGuid`, `nlohmann::json modifications`(오버라이드 목록), 그리고 transient `idRemap`(prefab localId↔scene id, 로드 시 재구축).
  - `Serialize`: `prefabGuid` + `modifications`만 기록(컴포넌트 자체는 가벼움).
- **SceneSerializer 분기(핵심 통합 지점)** `src/Core/SceneSerializer.cpp`:
  - 저장: `PrefabInstance`를 가진 객체(서브트리)는 **stripped** 로 — 전체 컴포넌트 데이터 대신 `{ "prefabInstance": { guid, rootId, modifications } }` 한 엔트리만 기록.
  - 로드: `prefabInstance` 엔트리를 만나면 `PrefabRegistry::Instantiate(guid,...)`로 서브트리 복원 → `modifications` 적용 → `PrefabInstance` 링크 컴포넌트 부착(remap 보관). rootId는 인스턴스 루트 id로 사용.

### 2b. 오버라이드 diff / Apply / Revert
- 오버라이드 모델: modification = `{ target:<prefab localId>, component:"<TypeName>", key:"<jsonKey>", value:<json> }`.
- diff 알고리즘(`src/Core/PrefabUtil.h/.cpp` 자유함수 또는 PrefabRegistry 내): 인스턴스 각 객체/컴포넌트 JSON과 프리팹 원본 JSON을 **키 단위 비교** → 다르거나 추가된 키를 modification으로 산출. (구조 변경=컴포넌트/자식 추가·삭제는 2b 범위에서 "추가"만 우선 지원, 삭제는 후속.)
- 적용(로드): 프리팹 JSON에서 시작 → modification마다 `componentJson[key]=value` → 컴포넌트 `Deserialize`.
- **Revert**: 해당 인스턴스 `modifications` 비우고 프리팹에서 재복원.
- **Apply**: 인스턴스의 현재 키 값들을 프리팹 JSON에 써넣고 `SavePrefab` → 열려 있는 다른 인스턴스들 재-resolve(자동 갱신).

### 2c. 에디터 통합 (명령 + 인스펙터 + 하이어라키)
- 신규 커맨드 `src/Editor/Commands/PrefabCommands.h/.cpp` (`ICommand` 상속, `CommandHistory` 경유):
  - `CreatePrefabFromObjectCommand` — 선택 객체를 `.prefab`로 저장(GUID 발급) + 씬 객체를 프리팹 인스턴스로 전환.
  - `InstantiatePrefabCommand` — 프리팹을 씬에 인스턴스화.
  - `ApplyPrefabCommand` / `RevertPrefabCommand` / `UnpackPrefabCommand`.
- `src/Editor/Windows/InspectorWindow.cpp` — 프리팹 인스턴스 선택 시 상단에 프리팹 헤더(Open/Apply/Revert) 표시, **오버라이드된 키는 강조(굵게/색)**; modification 집합을 링크에서 조회.
- `src/Editor/Windows/HierarchyWindow.cpp` — 프리팹 인스턴스 루트에 구분 아이콘/색(`UIRegistry` 아이콘 시스템 활용). 우클릭 메뉴에 Create Prefab/Apply/Revert.
- `src/Editor/Windows/ProjectBrowserWindow.cpp` — `.prefab` 표시 + 컨텍스트 메뉴 "Instantiate into Scene"(드래그&드롭은 엔진 미구현이라 메뉴 기반).

### 2d. 중첩 프리팹 (가장 복잡, 마지막)
- 프리팹 JSON이 내부에 또 다른 `prefabInstance` 블록을 포함 가능 → `DeserializeSubtreeRemapped`/resolve를 **재귀** 적용. 중첩 인스턴스의 오버라이드는 상위 프리팹의 modification으로 누적. (리스크 큼: 2a/2b 안정화 후 착수, 별도 검증.)

### Phase 2 검증
- 신규 `tests/test_prefab.cpp`:
  - 객체→`.prefab` 생성 시 GUID 포함, 레지스트리가 GUID로 조회.
  - GUID Instantiate가 **독립** 인스턴스 생성(서로 fresh id).
  - 프로퍼티 오버라이드 후 씬 저장→재로드: 오버라이드 키는 유지, 비오버라이드 키는 프리팹 값 추종.
  - 프리팹 원본 수정→재로드: 인스턴스 자동 갱신(비오버라이드 키).
  - Revert가 오버라이드 제거, Apply가 프리팹에 반영(다른 인스턴스 갱신).
  - (2d) 중첩 프리팹 인스턴스화/오버라이드 라운드트립.
- 에디터 수동: Create Prefab → 인스턴스 2개 배치 → 한쪽 프로퍼티 변경(인스펙터 강조 확인) → 프리팹 원본 편집 시 양쪽 반영, 변경한 쪽은 오버라이드 유지 → Apply/Revert 동작.
- `ctest` 전체 통과, `-Wall -Wextra -Wpedantic` 경고 없음.

---

## 작업 순서 / 의존성
1. **1.1 서브트리 클론 코어** (이후 모든 것의 토대)
2. 1.2 World 지연 큐 → 1.3 루프 배선 → 1.4 Script API → 1.5 데모 → **Phase 1 검증·커밋(단독 출시 가능)**
3. 2a 에셋+레지스트리+stripped 직렬화 → 2b diff/Apply/Revert → 2c 에디터 UI → 2d 중첩 → **Phase 2 검증·커밋**

## 신규/수정 파일 요약
- 신규: `src/Core/PrefabRegistry.{h,cpp}`, `src/Core/PrefabUtil.{h,cpp}`, `src/ECS/Components/PrefabInstance.{h,cpp}`, `src/Editor/Commands/PrefabCommands.{h,cpp}`, `tests/test_world_lifecycle.cpp`, `tests/test_prefab.cpp`
- 수정: `src/Core/SceneSerializer.{h,cpp}`(SerializeSubtree/DeserializeSubtreeRemapped + prefab 분기), `src/Core/World.{h,cpp}`(지연 큐/생명주기), `src/Scripting/Script.{h,cpp}`(편의 API), `src/Scripting/BuiltinScripts.{h,cpp}`, `src/runtime_main.cpp`·`src/main.cpp`(FlushDeferred 배선), `src/Editor/Windows/{Inspector,Hierarchy,ProjectBrowser}Window.cpp`, `CMakeLists.txt`·`tests/CMakeLists.txt`(신규 소스/테스트 등록).

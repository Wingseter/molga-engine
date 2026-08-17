# UX-3: Asset Browser and Identity (애셋 브라우저 및 식별자)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`로 Task 단위로 구현한다. Steps는 체크박스(`- [ ]`)로 추적한다. **선행 의존성:** UX-1(SelectionService — drag-to-scene 선택 갱신)과 UX-2(Console — 누락 애셋 경고 출력)가 먼저 필요하다. 단, 본 문서는 두 의존성이 아직 없어도 진행할 수 있도록 *얇은 폴백*(`Editor::SetSelectedObject`, `Log::Warn`)을 명시한다. **이 작업은 UX-6(고급 제작 UX)의 토대**다 — GUID/meta/index가 없으면 멀티오브젝트·프리팹 변형·command palette가 같은 경로 깨짐 문제를 반복한다.

**Goal:** 애셋을 "경로"가 아니라 "안정된 식별자(GUID)"로 참조하게 만들고, Project Browser를 안전한 애셋 조작 표면(생성/임포트/이름 변경/이동/삭제를 모두 Undo 가능한 Command + 참조 검사 + 휴지통)으로 끌어올린다. 텍스처를 이동/이름 변경해도 저장된 Sprite 참조가 깨지지 않고, 누락 애셋은 placeholder + Console 경고로 보이게 한다.

**Architecture:** 신규 `AssetDatabase`(Core 계층, 싱글톤)가 진실의 원천이다. 각 소스 애셋 옆에 `<파일>.meta` sidecar(JSON, `guid` 포함)를 두고, `AssetRecord { guid, sourcePath, importer, importerVersion, artifactPath, dependencies }`를 모은 `AssetIndex { guid→record, sourcePath→guid }`를 빌드한다. 컴포넌트는 `texturePath`(상대 경로) 대신 `textureGuid`를 직렬화하되, **기존 경로 참조는 로드 시 GUID로 자동 마이그레이션**한다(메모리에서만 수행, 입력 파일 즉시 덮어쓰기 금지 — roadmap §2.2). 누락 GUID는 `MissingAsset` placeholder로 해석된다. Project Browser의 모든 파괴적 동작은 `ProjectFile*Command`(CommandHistory 경유)로 흐르고, 삭제는 즉시 `std::filesystem::remove`가 아니라 휴지통(`Assets/.trash/`)으로 이동한다. 기존 `PrefabRegistry`(`.prefab` 내부에 guid를 저장하는 특수 GUID 인덱스)는 `AssetDatabase`의 한 importer(`PrefabImporter`)로 흡수하지 않고 **공존**시키되, AssetDatabase가 `.prefab`도 스캔해 동일 GUID를 공유한다(중복 GUID 발급 금지).

**Tech Stack:** C++17, doctest, ImGui, nlohmann/json, `std::filesystem`

**닫는 결함(갭 분석 §5):**
- 애셋 참조가 여전히 파일 경로 위주(`SpriteRenderer::texturePath`, `AudioSource::clipPath`, `Material::mainTexturePath`).
- 범용 GUID/meta/importer 데이터베이스 부재.
- 증분 파일 watcher/index refresh 부재.
- scene/prefab/material/audio/tilemap/script 애셋 의존성 그래프 부재.
- 이름 변경/이동/삭제가 참조를 갱신하지 않음.
- 삭제가 `std::filesystem::remove` 직접 호출(되돌릴 수 없음).
- 누락 참조 UI / 임포트 에러 badge 부재.

---

## 현재 상태 (검증된 사실)

### 애셋 참조는 "경로 문자열"로 저장된다 (핵심 마이그레이션 리스크)

- `SpriteRenderer`는 텍스처를 `std::string texturePath`(상대 경로)로 보관·직렬화한다. (`src/ECS/Components/SpriteRenderer.h:64`, `Serialize`에서 `j["texturePath"] = texturePath` — `src/ECS/Components/SpriteRenderer.cpp:77`, `Deserialize`에서 `SetTexturePath(j["texturePath"])` — `:100-101`)
- 텍스처 해석은 `ResolveAssets()`에서 경로→절대경로→`TextureManager`로 이뤄진다: `PathService::Get().ResolveAsset(texturePath)` 후 `TextureManager::Get().Load(abs)`. 실패 시 `Log::Warn("SpriteRenderer", "Texture not found: " + abs)`만 남고 **placeholder 없음**. (`src/ECS/Components/SpriteRenderer.cpp:108-118`)
- `AudioSource`도 동일 패턴: `std::string clipPath`(`src/ECS/Components/AudioSource.h:64`), `Serialize`에서 `j["clipPath"] = clipPath`(`AudioSource.cpp:148`), `ResolveAssets`에서 `PathService::Get().ResolveAsset(clipPath)`(`AudioSource.cpp:128`).
- `Material`도 경로 필드를 가진다: `texturePath`, `mainTexturePath`(`src/Rendering/Material.h:24,32`)와 `ResolveAssets()`(`:38`).
- `TilemapRenderer`는 `spriteSheetPath`(`src/ECS/Components/TilemapRenderer.h:47`)로 타일셋을 참조한다.
- **모든 컴포넌트가 `Component::ResolveAssets()` 가상 메서드를 이미 가진다** — 지연 로드 훅이 존재하므로 GUID 해석을 끼워 넣기 좋다. `World::ResolveAssets()`가 전 오브젝트를 순회한다(`src/Core/World.cpp:77-79`).

### GUID는 `.prefab`에만 존재하고 sidecar가 아니다

- `PrefabRegistry`(`src/Core/PrefabRegistry.h`)는 이미 GUID 인덱스를 운영한다: `guidToPath_`, `pathToGuid_`, `guidToCache_`. (`PrefabRegistry.h:42-46`)
- GUID는 **`.prefab` 파일 본문 안의 `"guid"` 키**에 저장된다(sidecar 아님). `ScanAssets()`가 `AssetRoot()`를 재귀 순회하며 `.prefab`만 읽어 `j["guid"]`를 인덱싱한다. (`PrefabRegistry.cpp:17-53`)
- GUID 생성기는 `PrefabRegistry::GenerateGUID()` — `mt19937`로 32자리 hex 문자열을 만든다. (`PrefabRegistry.cpp:125-137`) **범용 GUID는 이 알고리즘을 재사용**한다(중복/포맷 불일치 방지).
- Scene은 prefab 인스턴스를 `prefabInstance.guid`로 저장/복원한다(이미 GUID 참조). (`src/Core/SceneSerializer.cpp:77,144,151,355`)

### 삭제는 즉시·비가역적이고, 참조 검사가 없다

- Project Browser의 Delete는 `std::filesystem::remove(entry.path); Refresh();` — 휴지통도 참조 검사도 Undo도 없다. (`src/Editor/Windows/ProjectBrowserWindow.cpp:286-289`)
- 드래그 소스만 존재한다: 텍스처는 `SetDragDropPayload("TEXTURE_PATH", entry.path...)`(`ProjectBrowserWindow.cpp:246`), 오디오는 `"AUDIO_PATH"`(`:255`). **둘 다 경로 문자열을 payload로 싣는다.**
- 컴포넌트 인스펙터가 `TEXTURE_PATH`/`AUDIO_PATH` payload를 받아 경로→상대경로 변환 후 저장한다(`SpriteRenderer.cpp:153`, `AudioSource.cpp:194`, `TilemapRenderer.cpp:226`에 `Project::Get().GetRelativePath(droppedPath)`).
- **SceneViewWindow에는 `AcceptDragDropPayload`가 전혀 없다** — Scene View로 텍스처를 끌어 Sprite를 만드는 경로는 신규 구현이다(`grep AcceptDragDropPayload src/Editor/Windows/SceneViewWindow.cpp` → 0건).

### 경로/프로젝트/스캔 인프라

- `PathService`(싱글톤)가 `AssetRoot()`를 보유하고 `ResolveAsset(stored)`로 상대/절대를 절대경로로 해석한다. (`src/Core/PathService.h:22-29`)
- `Project`(에디터 싱글톤)가 `GetAssetsPath()`, `GetAbsolutePath()`, `GetRelativePath()`를 제공한다. (`src/Editor/Project.h:22,37-38`)
- 프로젝트 오픈 시 `PathService::Get().SetAssetRoot(Project::Get().GetPath())` 후 씬 로드·`ResolveAssets()`가 호출된다. (`src/main.cpp:201,205`) — **여기가 AssetDatabase 스캔을 끼워 넣을 지점이다.**
- `TextureManager`는 절대경로 문자열을 키로 텍스처를 캐시한다(`src/Core/TextureManager.h:36`). GUID 도입 후에도 캐시 키는 절대경로 그대로 유지(아래 Task C 참고).

### 빌드/테스트 구조

- 엔진 코드는 `molga_core`(STATIC)로 묶이고, `MOLGA_EDITOR` 정의는 `molga_engine`에만 붙는다(`CMakeLists.txt:115,173`). Core 계층 코드(AssetDatabase)는 `ENGINE_SOURCES`에, 에디터 전용 Command/Window는 `EDITOR_SOURCES`에 둔다(`CMakeLists.txt:144`).
- 테스트는 `molga_add_test(name src)`로 등록하며 `molga_core`에 링크된다(`tests/CMakeLists.txt:9-15`). Editor 소스가 필요한 테스트는 `target_sources(... PRIVATE ${CMAKE_SOURCE_DIR}/src/Editor/...)`로 개별 추가하는 선례가 있다(`test_build_manager`, `tests/CMakeLists.txt:60-66`).

---

## 파일 구조

**Create (Core — `ENGINE_SOURCES`):**
- `src/Core/Guid.h` — 헤더 온리 GUID 생성/검증(`PrefabRegistry::GenerateGUID` 로직 이전·공유).
- `src/Core/AssetMeta.h` — `.meta` sidecar의 read/write/생성 헬퍼(헤더 온리).
- `src/Core/AssetDatabase.h` / `src/Core/AssetDatabase.cpp` — `AssetRecord` / `AssetIndex` / `AssetDatabase`(싱글톤) + scan/refresh/이동·이름 변경/의존성/역참조.
- `src/Core/Importers/Importer.h` — `IImporter` 인터페이스 + `ImportResult`.
- `src/Core/Importers/TextureImporter.h` / `.cpp` — 텍스처 importer(width/height 메타).
- `src/Core/Importers/AudioImporter.h` / `.cpp` — 오디오 importer.
- `src/Core/Importers/PrefabImporter.h` / `.cpp` — `.prefab` 본문 guid를 AssetDatabase에 등록(PrefabRegistry와 GUID 공유).

**Create (Editor — `EDITOR_SOURCES`):**
- `src/Editor/Commands/ProjectFileCommands.h` / `.cpp` — `ProjectFileCreateCommand` / `ProjectFileImportCommand` / `ProjectFileRenameCommand` / `ProjectFileMoveCommand` / `ProjectFileDeleteCommand`.
- `src/Editor/AssetReferenceScan.h` / `.cpp` — 씬/프리팹/머티리얼 등에서 특정 GUID 역참조를 찾는 헬퍼(삭제 전 경고).
- `src/Editor/Watcher/AssetWatcher.h` / `.cpp` — 폴링 기반 증분 mtime watcher.

**Modify:**
- `src/ECS/Components/SpriteRenderer.h` / `.cpp` — `textureGuid` 추가, GUID 우선 직렬화 + 경로 마이그레이션, `ResolveAssets`가 GUID로 해석, 누락 시 `MissingAsset`.
- `src/ECS/Components/AudioSource.h` / `.cpp` — `clipGuid` 동일 패턴.
- `src/Rendering/Material.h` / `.cpp` — `mainTextureGuid` 동일 패턴.
- `src/Editor/Windows/ProjectBrowserWindow.h` / `.cpp` — payload를 `ASSET_GUID`로 변경(경로 호환 유지), Command 경유 생성/이름 변경/이동/삭제, badge(missing/import-failed/dirty/generated), 검색 + 타입 필터.
- `src/Editor/Windows/SceneViewWindow.h` / `.cpp` — `ASSET_GUID`/`TEXTURE_PATH` drag target → Sprite 생성 Command.
- `src/main.cpp` — 프로젝트 오픈 시 `AssetDatabase::Get().ScanProject(...)` 호출, 프레임 루프에 `AssetWatcher` tick.
- `CMakeLists.txt` — 신규 Core/Editor 소스 등록.
- `tests/CMakeLists.txt` — 신규 테스트 등록.

**Test (`tests/`):**
- `test_guid.cpp`, `test_asset_meta.cpp`, `test_asset_database.cpp`, `test_importer.cpp`, `test_asset_reference_migration.cpp`, `test_project_file_commands.cpp`, `test_asset_reference_scan.cpp`, `test_asset_watcher.cpp`.

---

## Task A. GUID 생성기 + `.meta` sidecar (TDD)

**Files:**
- Create: `src/Core/Guid.h`
- Create: `src/Core/AssetMeta.h`
- Create: `tests/test_guid.cpp`, `tests/test_asset_meta.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/Core/PrefabRegistry.cpp` (생성기를 `Guid`로 위임)

- [ ] **Step 1: 실패하는 GUID 테스트 작성**

Create `tests/test_guid.cpp`:
```cpp
#include "Core/Guid.h"
#include "doctest.h"

using molga::Guid;

TEST_CASE("Generate produces a 32-char lowercase hex string") {
    std::string g = Guid::Generate();
    CHECK(g.size() == 32);
    for (char c : g) CHECK(std::isxdigit(static_cast<unsigned char>(c)));
}

TEST_CASE("two generated guids differ") {
    CHECK(Guid::Generate() != Guid::Generate());
}

TEST_CASE("IsValid accepts 32-hex and rejects others") {
    CHECK(Guid::IsValid("0123456789abcdef0123456789abcdef"));
    CHECK_FALSE(Guid::IsValid(""));
    CHECK_FALSE(Guid::IsValid("xyz"));
    CHECK_FALSE(Guid::IsValid("0123456789ABCDEF0123456789abcdeg")); // g is not hex
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_guid test_guid.cpp)
```
Run:
```bash
cmake --preset debug && cmake --build --preset debug --target test_guid -j4
```
Expected: FAIL — `Core/Guid.h` 없음.

- [ ] **Step 3: `Guid.h` 작성 (헤더 온리)**

Create `src/Core/Guid.h`:
```cpp
#pragma once

#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace molga {

// 32자리 소문자 hex 식별자. PrefabRegistry::GenerateGUID와 동일 포맷.
class Guid {
public:
    static std::string Generate() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8) << dis(gen) << std::setw(8) << dis(gen)
           << std::setw(8) << dis(gen) << std::setw(8) << dis(gen);
        return ss.str();
    }

    static bool IsValid(const std::string& s) {
        if (s.size() != 32) return false;
        for (char c : s) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }
};

} // namespace molga
```

- [ ] **Step 4: 테스트 통과 확인**

Run:
```bash
cmake --build --preset debug --target test_guid -j4
ctest --preset debug -R test_guid --output-on-failure
```
Expected: PASS, `3 | 3 passed`.

- [ ] **Step 5: PrefabRegistry가 Guid를 재사용하도록 위임**

`src/Core/PrefabRegistry.cpp`의 `GenerateGUID()`(`:125-137`) 본문을 다음으로 교체(포맷 단일화):
```cpp
#include "Core/Guid.h"
// ...
std::string PrefabRegistry::GenerateGUID() {
    return molga::Guid::Generate();
}
```

- [ ] **Step 6: 실패하는 `.meta` 테스트 작성**

Create `tests/test_asset_meta.cpp`:
```cpp
#include "Core/AssetMeta.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::AssetMeta;
namespace fs = std::filesystem;

TEST_CASE("CreateOrLoad writes a .meta with a fresh guid and reloads it") {
    fs::path dir = fs::temp_directory_path() / "molga_meta_test";
    fs::create_directories(dir);
    fs::path asset = dir / "hero.png";
    { std::ofstream(asset) << "fake"; }
    fs::path metaPath = AssetMeta::MetaPathFor(asset);
    fs::remove(metaPath);

    AssetMeta m = AssetMeta::CreateOrLoad(asset, "TextureImporter", 1);
    CHECK(m.guid.size() == 32);
    CHECK(m.importer == "TextureImporter");
    CHECK(m.importerVersion == 1);
    CHECK(fs::exists(metaPath));

    AssetMeta again = AssetMeta::CreateOrLoad(asset, "TextureImporter", 1);
    CHECK(again.guid == m.guid); // guid는 안정적이어야 한다

    fs::remove_all(dir);
}

TEST_CASE("MetaPathFor appends .meta") {
    CHECK(AssetMeta::MetaPathFor("a/b/hero.png").string()
          == fs::path("a/b/hero.png.meta").string());
}
```

- [ ] **Step 7: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_asset_meta test_asset_meta.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_asset_meta -j4
```
Expected: FAIL — `Core/AssetMeta.h` 없음.

- [ ] **Step 8: `AssetMeta.h` 작성 (헤더 온리)**

Create `src/Core/AssetMeta.h`:
```cpp
#pragma once

#include "Core/Guid.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace molga {

// 소스 애셋 옆 "<파일>.meta" sidecar. guid가 애셋의 안정된 정체성이다.
struct AssetMeta {
    std::string guid;
    std::string importer;        // 예: "TextureImporter"
    int importerVersion = 1;

    static std::filesystem::path MetaPathFor(const std::filesystem::path& asset) {
        std::filesystem::path p = asset;
        p += ".meta";
        return p;
    }

    // .meta가 있으면 로드, 없으면 새 guid로 생성·기록한다.
    static AssetMeta CreateOrLoad(const std::filesystem::path& asset,
                                  const std::string& importer, int version) {
        std::filesystem::path metaPath = MetaPathFor(asset);
        AssetMeta m;
        if (std::filesystem::exists(metaPath)) {
            std::ifstream in(metaPath);
            nlohmann::json j;
            try { in >> j; } catch (...) {}
            m.guid = j.value("guid", std::string());
            m.importer = j.value("importer", importer);
            m.importerVersion = j.value("importerVersion", version);
        }
        if (!Guid::IsValid(m.guid)) {
            m.guid = Guid::Generate();
            m.importer = importer;
            m.importerVersion = version;
            Write(asset, m);
        }
        return m;
    }

    static void Write(const std::filesystem::path& asset, const AssetMeta& m) {
        nlohmann::json j;
        j["guid"] = m.guid;
        j["importer"] = m.importer;
        j["importerVersion"] = m.importerVersion;
        std::ofstream out(MetaPathFor(asset));
        out << j.dump(2);
    }
};

} // namespace molga
```

- [ ] **Step 9: 테스트 통과 + 커밋**

Run:
```bash
cmake --build --preset debug --target test_asset_meta -j4
ctest --preset debug -R "test_guid|test_asset_meta" --output-on-failure
```
Expected: 둘 다 PASS.
```bash
git add src/Core/Guid.h src/Core/AssetMeta.h src/Core/PrefabRegistry.cpp \
        tests/test_guid.cpp tests/test_asset_meta.cpp tests/CMakeLists.txt
git commit -m "feat(asset): Guid generator + .meta sidecar (UX-3 Task A)"
```

---

## Task B. AssetRecord / AssetIndex / AssetDatabase 스캔 (TDD)

**Files:**
- Create: `src/Core/AssetDatabase.h`, `src/Core/AssetDatabase.cpp`
- Create: `tests/test_asset_database.cpp`
- Modify: `CMakeLists.txt` (`ENGINE_SOURCES`에 `AssetDatabase.cpp`)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 스캔 테스트 작성**

Create `tests/test_asset_database.cpp`:
```cpp
#include "Core/AssetDatabase.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::AssetDatabase;
namespace fs = std::filesystem;

static fs::path MakeProject() {
    fs::path root = fs::temp_directory_path() / "molga_adb_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    { std::ofstream(root / "Assets" / "hero.png") << "img"; }
    { std::ofstream(root / "Assets" / "shot.wav") << "snd"; }
    return root / "Assets";
}

TEST_CASE("Scan assigns one record per source asset and builds both maps") {
    fs::path assets = MakeProject();
    AssetDatabase db;
    db.ScanProject(assets);

    CHECK(db.RecordCount() == 2);                  // .meta는 카운트하지 않는다
    std::string heroGuid = db.GuidForSource("hero.png");
    REQUIRE(heroGuid.size() == 32);

    const auto* rec = db.Find(heroGuid);
    REQUIRE(rec != nullptr);
    CHECK(rec->sourcePath == "hero.png");
    CHECK(rec->importer == "TextureImporter");

    fs::remove_all(assets.parent_path());
}

TEST_CASE("Re-scan keeps guids stable (meta persists)") {
    fs::path assets = MakeProject();
    AssetDatabase db;
    db.ScanProject(assets);
    std::string first = db.GuidForSource("hero.png");
    db.ScanProject(assets);
    CHECK(db.GuidForSource("hero.png") == first);
    fs::remove_all(assets.parent_path());
}

TEST_CASE("Unknown guid resolves to nullptr") {
    AssetDatabase db;
    CHECK(db.Find("ffffffffffffffffffffffffffffffff") == nullptr);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_asset_database test_asset_database.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_asset_database -j4
```
Expected: FAIL — `Core/AssetDatabase.h` 없음.

- [ ] **Step 3: `AssetDatabase.h` 작성**

Create `src/Core/AssetDatabase.h`:
```cpp
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

// 임포트된 단일 애셋의 식별·임포트 상태.
struct AssetRecord {
    std::string guid;
    std::string sourcePath;        // asset root 기준 상대 경로(슬래시 정규화)
    std::string importer;          // 예: "TextureImporter"
    int importerVersion = 1;
    std::string artifactPath;      // 가져온 산출물 경로(없으면 빈 문자열)
    std::vector<std::string> dependencies;  // 이 애셋이 참조하는 다른 애셋 guid
    bool importFailed = false;     // badge용
    bool generated = false;        // 산출물/임시 애셋 표시 badge용
};

// guid <-> record, sourcePath -> guid 양방향 인덱스.
class AssetDatabase {
public:
    static AssetDatabase& Get();   // 싱글톤 접근(테스트는 지역 인스턴스 사용 가능)

    // assetRoot를 재귀 스캔: 소스 애셋마다 .meta를 보장하고 record를 만든다.
    void ScanProject(const std::filesystem::path& assetRoot);

    const AssetRecord* Find(const std::string& guid) const;
    std::string GuidForSource(const std::string& relativeSourcePath) const;

    // guid를 절대 소스 경로로 해석(런타임/에디터 공용). 없으면 빈 경로.
    std::filesystem::path AbsoluteSourcePath(const std::string& guid) const;

    size_t RecordCount() const { return byGuid_.size(); }
    const std::unordered_map<std::string, AssetRecord>& All() const { return byGuid_; }
    const std::filesystem::path& Root() const { return assetRoot_; }

    // 단일 애셋만 다시 가져온다(importerVersion 변경/외부 수정 대응).
    void Reimport(const std::string& guid);

    // Task E/F가 사용하는 인덱스 변경(파일 시스템 동작 후 호출).
    void OnSourceRenamed(const std::filesystem::path& oldRel, const std::filesystem::path& newRel);
    void OnSourceRemoved(const std::filesystem::path& rel);
    void OnSourceAdded(const std::filesystem::path& rel);

    static std::string NormalizeRel(const std::filesystem::path& rel);

private:
    AssetDatabase() = default;

    void IndexOne(const std::filesystem::path& absPath);
    static std::string ImporterForExtension(const std::string& ext, int& versionOut);

    std::filesystem::path assetRoot_;
    std::unordered_map<std::string, AssetRecord> byGuid_;       // guid -> record
    std::unordered_map<std::string, std::string> sourceToGuid_; // relPath -> guid
};

} // namespace molga
```

- [ ] **Step 4: `AssetDatabase.cpp` 작성 (스캔 + importer 선택)**

Create `src/Core/AssetDatabase.cpp`:
```cpp
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Log.h"
#include <algorithm>

namespace molga {

AssetDatabase& AssetDatabase::Get() {
    static AssetDatabase instance;
    return instance;
}

std::string AssetDatabase::NormalizeRel(const std::filesystem::path& rel) {
    std::string s = rel.generic_string();  // 슬래시 정규화
    return s;
}

std::string AssetDatabase::ImporterForExtension(const std::string& ext, int& versionOut) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") { versionOut = 1; return "TextureImporter"; }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")  { versionOut = 1; return "AudioImporter"; }
    if (ext == ".prefab")                                  { versionOut = 1; return "PrefabImporter"; }
    versionOut = 1;
    return "GenericImporter";
}

void AssetDatabase::IndexOne(const std::filesystem::path& absPath) {
    std::string ext = absPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".meta") return;  // sidecar 자체는 애셋이 아니다

    int version = 1;
    std::string importer = ImporterForExtension(ext, version);
    AssetMeta meta = AssetMeta::CreateOrLoad(absPath, importer, version);

    AssetRecord rec;
    rec.guid = meta.guid;
    rec.sourcePath = NormalizeRel(std::filesystem::relative(absPath, assetRoot_));
    rec.importer = meta.importer;
    rec.importerVersion = meta.importerVersion;

    sourceToGuid_[rec.sourcePath] = rec.guid;
    byGuid_[rec.guid] = std::move(rec);
}

void AssetDatabase::ScanProject(const std::filesystem::path& assetRoot) {
    assetRoot_ = assetRoot;
    byGuid_.clear();
    sourceToGuid_.clear();
    if (assetRoot_.empty() || !std::filesystem::exists(assetRoot_)) return;

    try {
        for (const auto& e : std::filesystem::recursive_directory_iterator(assetRoot_)) {
            if (e.is_regular_file()) IndexOne(e.path());
        }
    } catch (const std::exception& ex) {
        Log::Error("AssetDatabase", std::string("scan failed: ") + ex.what());
    }
}

const AssetRecord* AssetDatabase::Find(const std::string& guid) const {
    auto it = byGuid_.find(guid);
    return it == byGuid_.end() ? nullptr : &it->second;
}

std::string AssetDatabase::GuidForSource(const std::string& relativeSourcePath) const {
    auto it = sourceToGuid_.find(NormalizeRel(relativeSourcePath));
    return it == sourceToGuid_.end() ? std::string() : it->second;
}

std::filesystem::path AssetDatabase::AbsoluteSourcePath(const std::string& guid) const {
    const AssetRecord* rec = Find(guid);
    if (!rec) return {};
    return assetRoot_ / rec->sourcePath;
}

void AssetDatabase::Reimport(const std::string& guid) {
    const AssetRecord* rec = Find(guid);
    if (!rec) return;
    IndexOne(assetRoot_ / rec->sourcePath);   // 단일 재인덱스(importer 호출은 Task C)
}

void AssetDatabase::OnSourceAdded(const std::filesystem::path& rel) {
    IndexOne(assetRoot_ / rel);
}

void AssetDatabase::OnSourceRemoved(const std::filesystem::path& rel) {
    std::string key = NormalizeRel(rel);
    auto it = sourceToGuid_.find(key);
    if (it == sourceToGuid_.end()) return;
    byGuid_.erase(it->second);
    sourceToGuid_.erase(it);
}

void AssetDatabase::OnSourceRenamed(const std::filesystem::path& oldRel,
                                    const std::filesystem::path& newRel) {
    std::string oldKey = NormalizeRel(oldRel);
    auto it = sourceToGuid_.find(oldKey);
    if (it == sourceToGuid_.end()) { OnSourceAdded(newRel); return; }
    std::string guid = it->second;
    sourceToGuid_.erase(it);
    std::string newKey = NormalizeRel(newRel);
    sourceToGuid_[newKey] = guid;
    auto recIt = byGuid_.find(guid);
    if (recIt != byGuid_.end()) recIt->second.sourcePath = newKey;
    // 핵심: guid는 변하지 않으므로 저장된 GUID 참조는 그대로 유효하다.
}

} // namespace molga
```

- [ ] **Step 5: CMake 등록 + 빌드**

`CMakeLists.txt`의 `ENGINE_SOURCES`에 추가:
```cmake
    src/Core/AssetDatabase.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_asset_database -j4
ctest --preset debug -R test_asset_database --output-on-failure
```
Expected: PASS, `3 | 3 passed`.

- [ ] **Step 6: 커밋**

```bash
git add src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
        tests/test_asset_database.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(asset): AssetRecord/AssetIndex + recursive project scan (UX-3 Task B)"
```

---

## Task C. Importer 인터페이스 + MissingAsset placeholder (TDD)

**Files:**
- Create: `src/Core/Importers/Importer.h`, `src/Core/Importers/TextureImporter.h`/`.cpp`, `src/Core/Importers/AudioImporter.h`/`.cpp`, `src/Core/Importers/PrefabImporter.h`/`.cpp`
- Create: `tests/test_importer.cpp`
- Modify: `src/Core/AssetDatabase.h`/`.cpp` (importer 레지스트리 + `MissingAsset`/`MissingTexture` 접근), `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 importer 테스트 작성**

Create `tests/test_importer.cpp`:
```cpp
#include "Core/Importers/Importer.h"
#include "Core/Importers/TextureImporter.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::TextureImporter;
using molga::ImportResult;
namespace fs = std::filesystem;

TEST_CASE("TextureImporter reports its name and a positive version") {
    TextureImporter imp;
    CHECK(imp.Name() == std::string("TextureImporter"));
    CHECK(imp.Version() >= 1);
}

TEST_CASE("TextureImporter accepts image extensions only") {
    TextureImporter imp;
    CHECK(imp.CanImport(".png"));
    CHECK(imp.CanImport(".jpg"));
    CHECK_FALSE(imp.CanImport(".wav"));
}

TEST_CASE("Import of a missing file fails gracefully") {
    TextureImporter imp;
    ImportResult r = imp.Import("does_not_exist.png");
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.error.empty());
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_importer test_importer.cpp)
target_sources(test_importer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Core/Importers/TextureImporter.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_importer -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: `Importer.h` 인터페이스 작성**

Create `src/Core/Importers/Importer.h`:
```cpp
#pragma once

#include <string>

namespace molga {

struct ImportResult {
    bool success = false;
    std::string error;
    std::string artifactPath;   // 산출물 경로(없으면 비움)
    int width = 0;              // 텍스처용(없으면 0)
    int height = 0;
};

// 소스 애셋을 검증/메타 추출하는 importer.
class IImporter {
public:
    virtual ~IImporter() = default;
    virtual std::string Name() const = 0;
    virtual int Version() const = 0;
    virtual bool CanImport(const std::string& ext) const = 0;
    virtual ImportResult Import(const std::string& absSourcePath) const = 0;
};

} // namespace molga
```

- [ ] **Step 4: `TextureImporter` 작성**

Create `src/Core/Importers/TextureImporter.h`:
```cpp
#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class TextureImporter : public IImporter {
public:
    std::string Name() const override { return "TextureImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    }
    ImportResult Import(const std::string& absSourcePath) const override;
};

} // namespace molga
```

Create `src/Core/Importers/TextureImporter.cpp`:
```cpp
#include "Core/Importers/TextureImporter.h"
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION_GUARD_NOOP  // stb는 Texture.cpp가 구현; 여기선 헤더만
#include "stb_image.h"

namespace molga {

ImportResult TextureImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    int w = 0, h = 0, ch = 0;
    if (stbi_info(absSourcePath.c_str(), &w, &h, &ch) == 1) {
        r.success = true;
        r.width = w;
        r.height = h;
    } else {
        r.error = "stbi_info failed for: " + absSourcePath;
    }
    return r;
}

} // namespace molga
```
> `stbi_info`만 사용하므로 `STB_IMAGE_IMPLEMENTATION`을 정의하지 않는다(중복 정의 방지 — 구현은 기존 `src/Rendering/Texture.cpp`에 있다). include 경로는 `external/stb`를 따른다(`Texture.cpp`의 include 형태를 그대로 복사).

- [ ] **Step 5: `AudioImporter` 작성**

Create `src/Core/Importers/AudioImporter.h`:
```cpp
#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class AudioImporter : public IImporter {
public:
    std::string Name() const override { return "AudioImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override {
        return ext == ".wav" || ext == ".mp3" || ext == ".ogg";
    }
    ImportResult Import(const std::string& absSourcePath) const override;
};

} // namespace molga
```

Create `src/Core/Importers/AudioImporter.cpp`:
```cpp
#include "Core/Importers/AudioImporter.h"
#include <filesystem>

namespace molga {

ImportResult AudioImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    r.success = true;   // 디코드는 런타임 miniaudio가 담당; 임포트는 존재 검증만
    return r;
}

} // namespace molga
```

- [ ] **Step 6: `PrefabImporter` 작성 (PrefabRegistry와 GUID 공유)**

Create `src/Core/Importers/PrefabImporter.h`:
```cpp
#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

// .prefab은 guid를 파일 본문에 보관한다(sidecar 아님). 이 importer는 본문 guid를
// 권위 있는 값으로 취급하므로, AssetDatabase가 .meta를 만들 때 동일 guid를 쓰게 한다.
class PrefabImporter : public IImporter {
public:
    std::string Name() const override { return "PrefabImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override { return ext == ".prefab"; }
    ImportResult Import(const std::string& absSourcePath) const override;

    // .prefab 본문에서 "guid"를 읽어 반환(없으면 빈 문자열).
    static std::string ReadEmbeddedGuid(const std::string& absSourcePath);
};

} // namespace molga
```

Create `src/Core/Importers/PrefabImporter.cpp`:
```cpp
#include "Core/Importers/PrefabImporter.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace molga {

std::string PrefabImporter::ReadEmbeddedGuid(const std::string& absSourcePath) {
    std::ifstream in(absSourcePath);
    if (!in.is_open()) return {};
    nlohmann::json j;
    try { in >> j; } catch (...) { return {}; }
    return j.value("guid", std::string());
}

ImportResult PrefabImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    r.success = !ReadEmbeddedGuid(absSourcePath).empty();
    if (!r.success) r.error = "prefab has no embedded guid";
    return r;
}

} // namespace molga
```

- [ ] **Step 7: AssetDatabase가 importer를 호출하고 prefab guid를 존중하도록 확장**

`src/Core/AssetDatabase.cpp`의 `IndexOne`에서 `.meta` 생성 직후, `.prefab`이면 본문 guid를 우선한다. `IndexOne` 본문의 `AssetMeta meta = ...` 다음에 추가:
```cpp
    if (ext == ".prefab") {
        std::string embedded = PrefabImporter::ReadEmbeddedGuid(absPath.string());
        if (Guid::IsValid(embedded)) meta.guid = embedded;  // PrefabRegistry와 동일 guid 공유
    }
```
`AssetDatabase.cpp` 상단 include에 추가:
```cpp
#include "Core/Guid.h"
#include "Core/Importers/PrefabImporter.h"
```
또한 import 실패를 record에 반영하기 위해, `IndexOne`에서 importer를 실제 호출하도록 다음을 `rec.importerVersion = ...` 다음에 추가:
```cpp
    ImportResult res = RunImporter(importer, absPath.string());
    rec.importFailed = !res.success;
    rec.artifactPath = res.artifactPath;
    if (res.width > 0)  rec.textureWidth  = res.width;
    if (res.height > 0) rec.textureHeight = res.height;
```
`AssetRecord`에 다음 두 필드를 추가(`AssetDatabase.h`):
```cpp
    int textureWidth = 0;
    int textureHeight = 0;
```
그리고 `AssetDatabase`에 importer 레지스트리 헬퍼를 추가(`AssetDatabase.cpp`):
```cpp
#include "Core/Importers/TextureImporter.h"
#include "Core/Importers/AudioImporter.h"

static ImportResult RunImporterImpl(const std::string& importer, const std::string& abs) {
    if (importer == "TextureImporter") return TextureImporter().Import(abs);
    if (importer == "AudioImporter")   return AudioImporter().Import(abs);
    if (importer == "PrefabImporter")  return PrefabImporter().Import(abs);
    ImportResult ok; ok.success = true; return ok;  // GenericImporter
}
```
`AssetDatabase.h`의 private에 선언 추가:
```cpp
    static ImportResult RunImporter(const std::string& importer, const std::string& abs);
```
`AssetDatabase.cpp`에 정의 추가:
```cpp
ImportResult AssetDatabase::RunImporter(const std::string& importer, const std::string& abs) {
    return RunImporterImpl(importer, abs);
}
```
`AssetDatabase.h` 상단에 `#include "Core/Importers/Importer.h"` 추가.

- [ ] **Step 8: MissingAsset placeholder 추가**

`src/Core/AssetDatabase.h`의 `AssetDatabase` public에 추가:
```cpp
    // guid가 인덱스에 없으면 누락. 호출자는 placeholder를 사용해야 한다.
    bool IsMissing(const std::string& guid) const { return Find(guid) == nullptr; }

    // 누락 텍스처용 placeholder 절대 경로(엔진 리소스). ResolveAssets가 사용.
    static std::filesystem::path MissingTexturePath();
```
`src/Core/AssetDatabase.cpp`에 정의 추가:
```cpp
#include "Core/PathService.h"
// ...
std::filesystem::path AssetDatabase::MissingTexturePath() {
    return PathService::Get().EngineResource("Editor/missing_texture.png");
}
```
> `Editor/missing_texture.png`는 실행 파일 옆에 배포되는 분홍/체커보드 16x16 placeholder다. 빌드 산출 디렉터리에 복사하도록 `CMakeLists.txt`의 리소스 복사 단계에 추가한다(기존 Shaders 복사 패턴을 따름).

- [ ] **Step 9: CMake 등록 + 테스트 통과 + 커밋**

`CMakeLists.txt`의 `ENGINE_SOURCES`에 추가:
```cmake
    src/Core/Importers/TextureImporter.cpp
    src/Core/Importers/AudioImporter.cpp
    src/Core/Importers/PrefabImporter.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_importer -j4
ctest --preset debug -R "test_importer|test_asset_database" --output-on-failure
```
Expected: PASS.
```bash
git add src/Core/Importers src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
        tests/test_importer.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(asset): IImporter + texture/audio/prefab importers + MissingAsset (UX-3 Task C)"
```

---

## Task D. GUID 참조로 전환 + 기존 경로 마이그레이션 (TDD)

> 핵심: 컴포넌트는 `textureGuid`/`clipGuid`를 직렬화하되, **이미 저장된 씬은 `texturePath`만 가진다.** Deserialize 시 path→guid 변환을 *메모리에서* 수행한다(roadmap §2.2: 입력 파일 즉시 덮어쓰기 금지). 저장 시에는 guid를 기록하고 path는 하위 호환을 위해 함께 남긴다(직전 두 버전 로드 지원).

**Files:**
- Modify: `src/ECS/Components/SpriteRenderer.h`/`.cpp`, `src/ECS/Components/AudioSource.h`/`.cpp`, `src/Rendering/Material.h`/`.cpp`
- Create: `tests/test_asset_reference_migration.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 마이그레이션 테스트 작성**

Create `tests/test_asset_reference_migration.cpp`:
```cpp
#include "ECS/Components/SpriteRenderer.h"
#include "Core/AssetDatabase.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static fs::path SeedAssets() {
    fs::path root = fs::temp_directory_path() / "molga_mig_test";
    fs::remove_all(root);
    fs::create_directories(root);
    { std::ofstream(root / "hero.png") << "img"; }
    molga::AssetDatabase::Get().ScanProject(root);
    return root;
}

TEST_CASE("loading a legacy scene with texturePath migrates to textureGuid in memory") {
    fs::path assets = SeedAssets();
    std::string guid = molga::AssetDatabase::Get().GuidForSource("hero.png");
    REQUIRE(guid.size() == 32);

    nlohmann::json legacy;            // guid 없이 path만 있는 구버전 데이터
    legacy["texturePath"] = "hero.png";

    SpriteRenderer sr;
    sr.Deserialize(legacy);
    CHECK(sr.GetTextureGuid() == guid);   // path가 guid로 승격되었다

    nlohmann::json out;
    sr.Serialize(out);
    CHECK(out.value("textureGuid", std::string()) == guid);  // 저장은 guid로

    fs::remove_all(assets);
}

TEST_CASE("loading a scene that already has textureGuid keeps it") {
    fs::path assets = SeedAssets();
    std::string guid = molga::AssetDatabase::Get().GuidForSource("hero.png");
    nlohmann::json modern;
    modern["textureGuid"] = guid;

    SpriteRenderer sr;
    sr.Deserialize(modern);
    CHECK(sr.GetTextureGuid() == guid);
    fs::remove_all(assets);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가(컴포넌트 인스펙터는 `MOLGA_EDITOR` 가드 안이라 core 링크만으로 충분):
```cmake
molga_add_test(test_asset_reference_migration test_asset_reference_migration.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_asset_reference_migration -j4
```
Expected: FAIL — `GetTextureGuid` 미정의.

- [ ] **Step 3: SpriteRenderer에 guid 필드/접근자 추가**

`src/ECS/Components/SpriteRenderer.h`의 `SetTexturePath`/`GetTexturePath` 아래에 추가:
```cpp
    void SetTextureGuid(const std::string& g) { textureGuid = g; }
    const std::string& GetTextureGuid() const { return textureGuid; }
```
private 멤버 `std::string texturePath;` 다음에 추가:
```cpp
    std::string textureGuid;   // 권위 있는 참조(경로보다 우선)
```

- [ ] **Step 4: Serialize/Deserialize에서 guid 우선 + 마이그레이션**

`src/ECS/Components/SpriteRenderer.cpp` 상단 include에 추가:
```cpp
#include "Core/AssetDatabase.h"
```
`Serialize`(`:71-`)에서 `j["texturePath"] = texturePath;` 다음에 추가:
```cpp
    j["textureGuid"] = textureGuid;   // 권위값. texturePath는 하위 호환용으로 함께 보존.
```
`Deserialize`(`:84-`)의 `if (j.contains("texturePath")) { SetTexturePath(j["texturePath"]); }` 블록을 다음으로 교체:
```cpp
    if (j.contains("textureGuid") && j["textureGuid"].is_string()) {
        textureGuid = j["textureGuid"].get<std::string>();
    }
    if (j.contains("texturePath")) {
        SetTexturePath(j["texturePath"].get<std::string>());
    }
    // 구버전 마이그레이션: guid가 없고 path만 있으면 path를 guid로 승격(메모리에서만).
    if (textureGuid.empty() && !texturePath.empty()) {
        std::string g = molga::AssetDatabase::Get().GuidForSource(texturePath);
        if (!g.empty()) textureGuid = g;
    }
```

- [ ] **Step 5: ResolveAssets가 guid로 해석 + 누락 placeholder**

`src/ECS/Components/SpriteRenderer.cpp`의 `ResolveAssets`(`:108-120`)를 다음으로 교체:
```cpp
void SpriteRenderer::ResolveAssets() {
    if (!texture) {
        std::filesystem::path src;
        if (!textureGuid.empty()) {
            src = molga::AssetDatabase::Get().AbsoluteSourcePath(textureGuid);
        }
        if (src.empty() && !texturePath.empty()) {
            src = PathService::Get().ResolveAsset(texturePath);  // guid 미해석 시 폴백
        }
        if (!src.empty()) {
            texture = TextureManager::Get().Load(src.string());
        }
        if (!texture) {
            // UX-2 Console로 경고(없으면 stdout). 시각적 placeholder는 missing_texture.
            Log::Warn("SpriteRenderer", "Missing texture for guid '" + textureGuid +
                      "' (path '" + texturePath + "')");
            texture = TextureManager::Get().Load(
                molga::AssetDatabase::MissingTexturePath().string());
        } else if (width == 32.0f && height == 32.0f) {
            width = static_cast<float>(texture->GetWidth());
            height = static_cast<float>(texture->GetHeight());
        }
    }
    material.ResolveAssets();
}
```

- [ ] **Step 6: AudioSource / Material 동일 패턴 적용**

`src/ECS/Components/AudioSource.h`에 `clipGuid`와 `GetClipGuid()`/`SetClipGuid()`를 `clipPath` 옆에 추가. `AudioSource.cpp`의 `Serialize`에 `j["clipGuid"] = clipGuid;`, `Deserialize`에 동일 마이그레이션 블록(`AssetDatabase::Get().GuidForSource(clipPath)`), `ResolveAssets`에서 `AbsoluteSourcePath(clipGuid)` 우선 해석을 적용한다. `Material.h`/`.cpp`도 `mainTexturePath` 옆에 `mainTextureGuid`를 추가하고 동일 패턴을 적용한다.
> 셋 모두 SpriteRenderer와 **동일한 메서드명 규칙**(`Get<Field>Guid`/`Set<Field>Guid`)을 쓴다.

- [ ] **Step 7: 테스트 통과 + 전체 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug -R "test_asset_reference_migration|test_scene_serializer" --output-on-failure
```
Expected: PASS. 기존 `test_scene_serializer`도 깨지지 않아야 한다(path 필드 보존).
```bash
git add src/ECS/Components/SpriteRenderer.h src/ECS/Components/SpriteRenderer.cpp \
        src/ECS/Components/AudioSource.h src/ECS/Components/AudioSource.cpp \
        src/Rendering/Material.h src/Rendering/Material.cpp \
        tests/test_asset_reference_migration.cpp tests/CMakeLists.txt
git commit -m "feat(asset): components store GUID refs + legacy path migration (UX-3 Task D)"
```

---

## Task E. ProjectFile*Command 세트 + 참조 검사 + 가역 삭제(휴지통) (TDD)

> 갭 분석 §2의 파일 동작 command(ProjectFileCreate/Rename/Move/Delete)가 여기에 속한다. 모든 동작은 `CommandHistory`(task-0-4의 헤더 온리 `molga::CommandHistory`)를 거친다. 삭제 전 역참조를 검사하고, 삭제는 `Assets/.trash/`로 이동(가역).

**Files:**
- Create: `src/Editor/AssetReferenceScan.h`/`.cpp` (씬/프리팹/머티리얼에서 guid 역참조 탐색)
- Create: `src/Editor/Commands/ProjectFileCommands.h`/`.cpp`
- Create: `tests/test_asset_reference_scan.cpp`, `tests/test_project_file_commands.cpp`
- Modify: `CMakeLists.txt` (`EDITOR_SOURCES`), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 역참조 스캔 테스트 작성**

Create `tests/test_asset_reference_scan.cpp`:
```cpp
#include "Editor/AssetReferenceScan.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

TEST_CASE("scan finds scenes that reference a given texture guid") {
    fs::path root = fs::temp_directory_path() / "molga_ref_test";
    fs::remove_all(root);
    fs::create_directories(root / "Scenes");

    nlohmann::json scene;
    scene["gameObjects"] = nlohmann::json::array();
    nlohmann::json go; go["components"] = nlohmann::json::array();
    nlohmann::json comp; comp["type"] = "SpriteRenderer";
    comp["textureGuid"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    go["components"].push_back(comp);
    scene["gameObjects"].push_back(go);
    { std::ofstream(root / "Scenes" / "main.json") << scene.dump(); }

    auto refs = molga::AssetReferenceScan::FindReferencers(
        root, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(refs.size() == 1);

    auto none = molga::AssetReferenceScan::FindReferencers(
        root, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    CHECK(none.empty());

    fs::remove_all(root);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_asset_reference_scan test_asset_reference_scan.cpp)
target_sources(test_asset_reference_scan PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/AssetReferenceScan.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_asset_reference_scan -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: AssetReferenceScan 작성**

Create `src/Editor/AssetReferenceScan.h`:
```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace molga {

// 프로젝트 안에서 특정 애셋 guid를 참조하는 문서를 찾는다.
class AssetReferenceScan {
public:
    // assetRoot 아래 .json/.prefab/.mat 등을 훑어 guid 문자열을 포함한 파일을 반환.
    static std::vector<std::filesystem::path> FindReferencers(
        const std::filesystem::path& assetRoot, const std::string& guid);
};

} // namespace molga
```

Create `src/Editor/AssetReferenceScan.cpp`:
```cpp
#include "Editor/AssetReferenceScan.h"
#include <fstream>
#include <sstream>

namespace molga {

static bool FileContainsGuid(const std::filesystem::path& p, const std::string& guid) {
    std::ifstream in(p);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str().find(guid) != std::string::npos;
}

std::vector<std::filesystem::path> AssetReferenceScan::FindReferencers(
    const std::filesystem::path& assetRoot, const std::string& guid) {
    std::vector<std::filesystem::path> out;
    if (guid.empty() || !std::filesystem::exists(assetRoot)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(assetRoot)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext == ".json" || ext == ".prefab" || ext == ".mat" || ext == ".scene") {
            if (FileContainsGuid(e.path(), guid)) out.push_back(e.path());
        }
    }
    return out;
}

} // namespace molga
```
> 텍스트 부분 일치 스캔이라 빠르고 importer에 독립적이다. guid가 32자리 hex라 오탐 위험이 낮다. 정밀 의존성 그래프는 Task F의 `AssetRecord::dependencies`로 강화한다.

- [ ] **Step 4: 실패하는 ProjectFileCommand 테스트 작성**

Create `tests/test_project_file_commands.cpp`:
```cpp
#include "Editor/Commands/ProjectFileCommands.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using molga::ProjectFileDeleteCommand;
using molga::ProjectFileRenameCommand;

static fs::path Seed() {
    fs::path root = fs::temp_directory_path() / "molga_pfc_test";
    fs::remove_all(root);
    fs::create_directories(root);
    { std::ofstream(root / "hero.png") << "img"; }
    return root;
}

TEST_CASE("delete moves the file to trash and undo restores it") {
    fs::path root = Seed();
    fs::path file = root / "hero.png";
    fs::path trash = root / ".trash";

    ProjectFileDeleteCommand cmd(file, trash);
    cmd.Execute();
    CHECK_FALSE(fs::exists(file));   // 원본은 사라지고
    CHECK(fs::exists(trash));        // 휴지통으로 이동
    cmd.Undo();
    CHECK(fs::exists(file));         // 복원됨
    fs::remove_all(root);
}

TEST_CASE("rename moves the source and its .meta together; undo reverts both") {
    fs::path root = Seed();
    { std::ofstream(root / "hero.png.meta") << "{}"; }
    ProjectFileRenameCommand cmd(root / "hero.png", "villain.png");
    cmd.Execute();
    CHECK(fs::exists(root / "villain.png"));
    CHECK(fs::exists(root / "villain.png.meta"));   // .meta가 따라온다
    CHECK_FALSE(fs::exists(root / "hero.png"));
    cmd.Undo();
    CHECK(fs::exists(root / "hero.png"));
    CHECK(fs::exists(root / "hero.png.meta"));
    fs::remove_all(root);
}
```

- [ ] **Step 5: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_project_file_commands test_project_file_commands.cpp)
target_sources(test_project_file_commands PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/ProjectFileCommands.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_project_file_commands -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 6: ProjectFileCommands 작성**

Create `src/Editor/Commands/ProjectFileCommands.h`:
```cpp
#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <filesystem>
#include <string>

namespace molga {

// 소스 + 그 .meta를 함께 이동/이름 변경한다(.meta는 guid를 보존하므로 항상 동행).
class ProjectFileRenameCommand : public ICommand {
public:
    ProjectFileRenameCommand(std::filesystem::path src, std::string newName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Rename Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path dst_;
};

// 디렉터리 간 이동(.meta 동행).
class ProjectFileMoveCommand : public ICommand {
public:
    ProjectFileMoveCommand(std::filesystem::path src, std::filesystem::path dstDir);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Move Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path dst_;
};

// 휴지통으로 이동(가역). trashDir는 보통 "<assets>/.trash".
class ProjectFileDeleteCommand : public ICommand {
public:
    ProjectFileDeleteCommand(std::filesystem::path src, std::filesystem::path trashDir);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Delete Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path trashDir_;
    std::filesystem::path trashed_;   // 휴지통 안 실제 경로(undo 복원용)
};

// 빈 파일/폴더/스크립트 등 새 애셋 생성(undo는 삭제).
class ProjectFileCreateCommand : public ICommand {
public:
    ProjectFileCreateCommand(std::filesystem::path target, std::string contents, bool isDirectory);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Asset"; }
private:
    std::filesystem::path target_;
    std::string contents_;
    bool isDirectory_;
};

} // namespace molga
```

Create `src/Editor/Commands/ProjectFileCommands.cpp`:
```cpp
#include "Editor/Commands/ProjectFileCommands.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include <fstream>

namespace molga {

namespace fs = std::filesystem;

static void MoveWithMeta(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    fs::path fromMeta = AssetMeta::MetaPathFor(from);
    if (fs::exists(fromMeta)) {
        fs::rename(fromMeta, AssetMeta::MetaPathFor(to), ec);
    }
}

// ── Rename ─────────────────────────────────────────────────────────────────
ProjectFileRenameCommand::ProjectFileRenameCommand(fs::path src, std::string newName)
    : src_(std::move(src)) {
    dst_ = src_.parent_path() / newName;
}
void ProjectFileRenameCommand::Execute() {
    MoveWithMeta(src_, dst_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(src_, AssetDatabase::Get().Root()),
        fs::relative(dst_, AssetDatabase::Get().Root()));
}
void ProjectFileRenameCommand::Undo() {
    MoveWithMeta(dst_, src_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(dst_, AssetDatabase::Get().Root()),
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Move ───────────────────────────────────────────────────────────────────
ProjectFileMoveCommand::ProjectFileMoveCommand(fs::path src, fs::path dstDir)
    : src_(std::move(src)) {
    dst_ = dstDir / src_.filename();
}
void ProjectFileMoveCommand::Execute() {
    MoveWithMeta(src_, dst_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(src_, AssetDatabase::Get().Root()),
        fs::relative(dst_, AssetDatabase::Get().Root()));
}
void ProjectFileMoveCommand::Undo() {
    MoveWithMeta(dst_, src_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(dst_, AssetDatabase::Get().Root()),
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Delete (휴지통) ─────────────────────────────────────────────────────────
ProjectFileDeleteCommand::ProjectFileDeleteCommand(fs::path src, fs::path trashDir)
    : src_(std::move(src)), trashDir_(std::move(trashDir)) {}
void ProjectFileDeleteCommand::Execute() {
    std::error_code ec;
    fs::create_directories(trashDir_, ec);
    trashed_ = trashDir_ / src_.filename();
    MoveWithMeta(src_, trashed_);
    AssetDatabase::Get().OnSourceRemoved(
        fs::relative(src_, AssetDatabase::Get().Root()));
}
void ProjectFileDeleteCommand::Undo() {
    MoveWithMeta(trashed_, src_);
    AssetDatabase::Get().OnSourceAdded(
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Create ─────────────────────────────────────────────────────────────────
ProjectFileCreateCommand::ProjectFileCreateCommand(fs::path target, std::string contents, bool isDirectory)
    : target_(std::move(target)), contents_(std::move(contents)), isDirectory_(isDirectory) {}
void ProjectFileCreateCommand::Execute() {
    if (isDirectory_) {
        fs::create_directories(target_);
    } else {
        std::ofstream(target_) << contents_;
        AssetDatabase::Get().OnSourceAdded(
            fs::relative(target_, AssetDatabase::Get().Root()));
    }
}
void ProjectFileCreateCommand::Undo() {
    std::error_code ec;
    fs::remove_all(target_, ec);
    fs::remove(AssetMeta::MetaPathFor(target_), ec);
    if (!isDirectory_) {
        AssetDatabase::Get().OnSourceRemoved(
            fs::relative(target_, AssetDatabase::Get().Root()));
    }
}

} // namespace molga
```

- [ ] **Step 7: CMake 등록 + 테스트 통과**

`CMakeLists.txt`의 `EDITOR_SOURCES`에 추가:
```cmake
    src/Editor/AssetReferenceScan.cpp
    src/Editor/Commands/ProjectFileCommands.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_project_file_commands test_asset_reference_scan -j4
ctest --preset debug -R "test_project_file_commands|test_asset_reference_scan" --output-on-failure
```
Expected: PASS.

- [ ] **Step 8: ProjectBrowser 삭제를 Command + 참조 경고로 교체**

`src/Editor/Windows/ProjectBrowserWindow.cpp`의 Delete 메뉴(`:286-289`)를 다음으로 교체:
```cpp
            if (ImGui::MenuItem((std::string(Icons::Trash) + " Delete").c_str())) {
                std::string guid = molga::AssetDatabase::Get().GuidForSource(
                    Project::Get().GetRelativePath(entry.path));
                auto refs = molga::AssetReferenceScan::FindReferencers(
                    Project::Get().GetAssetsPath(), guid);
                if (!refs.empty()) {
                    Log::Warn("ProjectBrowser",
                        "Deleting '" + entry.name + "' still referenced by " +
                        std::to_string(refs.size()) + " document(s). Moved to trash (recoverable).");
                }
                std::filesystem::path trash =
                    std::filesystem::path(Project::Get().GetAssetsPath()) / ".trash";
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ProjectFileDeleteCommand>(entry.path, trash));
                Refresh();
            }
```
include 추가(`ProjectBrowserWindow.cpp` 상단):
```cpp
#include "Core/AssetDatabase.h"
#include "Editor/AssetReferenceScan.h"
#include "Editor/Commands/ProjectFileCommands.h"
```
> `.trash`는 스캔에서 제외해야 한다. Task B `ScanProject`/`AssetReferenceScan`의 순회에서 디렉터리 이름이 `.trash`면 건너뛰도록 한 줄 가드를 추가한다(`if (e.path().filename() == ".trash") continue;` 형태를 디렉터리 진입 지점에 적용).

- [ ] **Step 9: 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug -j4 && ctest --preset debug --output-on-failure
```
Expected: 전체 PASS.
```bash
git add src/Editor/AssetReferenceScan.h src/Editor/AssetReferenceScan.cpp \
        src/Editor/Commands/ProjectFileCommands.h src/Editor/Commands/ProjectFileCommands.cpp \
        src/Editor/Windows/ProjectBrowserWindow.cpp \
        src/Core/AssetDatabase.cpp \
        tests/test_asset_reference_scan.cpp tests/test_project_file_commands.cpp \
        CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(asset): ProjectFile commands + reference check + reversible trash delete (UX-3 Task E)"
```

---

## Task F. 증분 watcher + badge + 검색/타입 필터 (TDD)

**Files:**
- Create: `src/Editor/Watcher/AssetWatcher.h`/`.cpp`
- Create: `tests/test_asset_watcher.cpp`
- Modify: `src/Editor/Windows/ProjectBrowserWindow.h`/`.cpp` (badge + 검색 + 필터), `src/main.cpp` (tick), `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 watcher 테스트 작성**

Create `tests/test_asset_watcher.cpp`:
```cpp
#include "Editor/Watcher/AssetWatcher.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using molga::AssetWatcher;

TEST_CASE("watcher reports added and removed files between polls") {
    fs::path root = fs::temp_directory_path() / "molga_watch_test";
    fs::remove_all(root);
    fs::create_directories(root);

    AssetWatcher w;
    w.Prime(root);                              // 최초 스냅샷

    { std::ofstream(root / "new.png") << "x"; }
    AssetWatcher::Changes c1 = w.Poll(root);
    CHECK(c1.added.size() == 1);
    CHECK(c1.removed.empty());

    fs::remove(root / "new.png");
    AssetWatcher::Changes c2 = w.Poll(root);
    CHECK(c2.removed.size() == 1);
    CHECK(c2.added.empty());

    fs::remove_all(root);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_asset_watcher test_asset_watcher.cpp)
target_sources(test_asset_watcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Watcher/AssetWatcher.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_asset_watcher -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: AssetWatcher 작성 (폴링 mtime 스냅샷)**

Create `src/Editor/Watcher/AssetWatcher.h`:
```cpp
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

// 폴링 기반 증분 watcher. 전체 재스캔 대신 added/removed/modified만 보고한다.
class AssetWatcher {
public:
    struct Changes {
        std::vector<std::string> added;
        std::vector<std::string> removed;
        std::vector<std::string> modified;
        bool Any() const { return !added.empty() || !removed.empty() || !modified.empty(); }
    };

    void Prime(const std::filesystem::path& root);   // 최초 스냅샷(변화 보고 없음)
    Changes Poll(const std::filesystem::path& root); // 직전 스냅샷과 비교 후 갱신

private:
    std::unordered_map<std::string, long long> mtimes_;  // relPath -> mtime ticks
    static void Snapshot(const std::filesystem::path& root,
                         std::unordered_map<std::string, long long>& out);
};

} // namespace molga
```

Create `src/Editor/Watcher/AssetWatcher.cpp`:
```cpp
#include "Editor/Watcher/AssetWatcher.h"

namespace molga {

namespace fs = std::filesystem;

void AssetWatcher::Snapshot(const fs::path& root,
                            std::unordered_map<std::string, long long>& out) {
    out.clear();
    if (!fs::exists(root)) return;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".meta") continue;
        std::string rel = fs::relative(e.path(), root).generic_string();
        auto t = fs::last_write_time(e.path()).time_since_epoch().count();
        out[rel] = static_cast<long long>(t);
    }
}

void AssetWatcher::Prime(const fs::path& root) { Snapshot(root, mtimes_); }

AssetWatcher::Changes AssetWatcher::Poll(const fs::path& root) {
    std::unordered_map<std::string, long long> now;
    Snapshot(root, now);
    Changes c;
    for (auto& [rel, t] : now) {
        auto it = mtimes_.find(rel);
        if (it == mtimes_.end()) c.added.push_back(rel);
        else if (it->second != t) c.modified.push_back(rel);
    }
    for (auto& [rel, t] : mtimes_) {
        (void)t;
        if (now.find(rel) == now.end()) c.removed.push_back(rel);
    }
    mtimes_.swap(now);
    return c;
}

} // namespace molga
```

- [ ] **Step 4: 테스트 통과 확인**

Run:
```bash
cmake --build --preset debug --target test_asset_watcher -j4
ctest --preset debug -R test_asset_watcher --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: main 프레임 루프에 watcher tick + AssetDatabase 증분 갱신**

`src/main.cpp`의 프로젝트 오픈 블록(`:201` 근처, `SetAssetRoot` 다음)에 추가:
```cpp
        molga::AssetDatabase::Get().ScanProject(Project::Get().GetAssetsPath());
        static molga::AssetWatcher assetWatcher;
        assetWatcher.Prime(Project::Get().GetAssetsPath());
```
프레임 루프 안(예: `glfwSwapBuffers` 직전, 매 N프레임)에서 폴링:
```cpp
        // 약 0.5초 간격으로만 폴링(매 프레임 디렉터리 순회 방지)
        if (assetPollTimer.Elapsed() > 0.5f) {
            auto ch = assetWatcher.Poll(Project::Get().GetAssetsPath());
            for (auto& a : ch.added)   molga::AssetDatabase::Get().OnSourceAdded(a);
            for (auto& r : ch.removed) molga::AssetDatabase::Get().OnSourceRemoved(r);
            for (auto& m : ch.modified) {
                std::string g = molga::AssetDatabase::Get().GuidForSource(m);
                if (!g.empty()) molga::AssetDatabase::Get().Reimport(g);
            }
            assetPollTimer.Reset();
        }
```
include 추가: `#include "Core/AssetDatabase.h"`, `#include "Editor/Watcher/AssetWatcher.h"`.
> `assetPollTimer`는 기존 `Time`/타이머 유틸을 쓰거나 간단한 `float` 누적기로 둔다(프로젝트의 frame dt 변수 재사용).

- [ ] **Step 6: ProjectBrowser badge + 검색 + 타입 필터 추가**

`src/Editor/Windows/ProjectBrowserWindow.h`에 멤버 추가:
```cpp
    char searchBuffer_[128] = "";
    int typeFilter_ = 0;   // 0=All,1=Texture,2=Audio,3=Prefab,4=Script,5=Scene
```
`ProjectBrowserWindow.cpp`의 그리드 렌더 상단에 검색/필터 UI를, 각 파일 타일에 badge를 추가한다. 파일 그리기 직전:
```cpp
    // 검색 바 + 타입 필터(그리드 위)
    ImGui::InputTextWithHint("##search", "Search assets...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    const char* kFilters[] = {"All","Texture","Audio","Prefab","Script","Scene"};
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##typeFilter", &typeFilter_, kFilters, IM_ARRAYSIZE(kFilters));
```
타일을 그릴지 결정(이름 필터 + 타입 필터):
```cpp
    std::string needle = searchBuffer_;
    if (!needle.empty() &&
        entry.name.find(needle) == std::string::npos) continue;
    if (typeFilter_ != 0 && AssetTypeIndex(entry.extension) != typeFilter_) continue;
```
타일 위에 badge 오버레이(아이콘 버튼 직후):
```cpp
    std::string guid = molga::AssetDatabase::Get().GuidForSource(
        Project::Get().GetRelativePath(entry.path));
    const molga::AssetRecord* rec = molga::AssetDatabase::Get().Find(guid);
    if (guid.empty()) {
        DrawBadge(ImVec2(0,0), IM_COL32(255,80,80,255), "?");      // missing/미인덱스
    } else if (rec && rec->importFailed) {
        DrawBadge(ImVec2(0,0), IM_COL32(255,160,0,255), "!");      // import-failed
    } else if (rec && rec->generated) {
        DrawBadge(ImVec2(0,0), IM_COL32(120,120,255,255), "G");    // generated
    }
```
`AssetTypeIndex(ext)`와 `DrawBadge(...)`는 같은 파일에 작은 static 헬퍼로 추가한다(확장자→필터 인덱스 매핑, 코너에 색 원 + 글자). dirty badge는 향후 unsaved-import 상태와 연결한다(현 단계는 missing/failed/generated 세 가지).

- [ ] **Step 7: CMake 등록 + 빌드 + 커밋**

`CMakeLists.txt`의 `EDITOR_SOURCES`에 추가:
```cmake
    src/Editor/Watcher/AssetWatcher.cpp
```
Run:
```bash
cmake --preset debug && cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 PASS.
```bash
git add src/Editor/Watcher/AssetWatcher.h src/Editor/Watcher/AssetWatcher.cpp \
        src/Editor/Windows/ProjectBrowserWindow.h src/Editor/Windows/ProjectBrowserWindow.cpp \
        src/main.cpp tests/test_asset_watcher.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(asset): incremental watcher + browser badges/search/type filters (UX-3 Task F)"
```

---

## Task G. 텍스처를 Scene View로 드래그 → Sprite 생성 (TDD + 수동)

> 이 Task가 Exit 시나리오의 진입점이다. SceneViewWindow에 `AcceptDragDropPayload`를 신규 추가하고, 드롭 시 `SpriteRenderer`(textureGuid 설정)를 가진 GameObject를 생성하는 Command를 만든다. 선택은 UX-1 SelectionService(없으면 `Editor::SetSelectedObject` 폴백)로 갱신한다.

**Files:**
- Create: `src/Editor/Commands/CreateSpriteFromAssetCommand.h`/`.cpp`
- Modify: `src/Editor/Windows/SceneViewWindow.cpp`, `src/Editor/Windows/ProjectBrowserWindow.cpp` (payload를 `ASSET_GUID`로 추가; `TEXTURE_PATH`는 호환 유지), `CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `tests/test_create_sprite_from_asset.cpp`

- [ ] **Step 1: 실패하는 Command 테스트 작성**

Create `tests/test_create_sprite_from_asset.cpp`:
```cpp
#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "ECS/GameObject.h"
#include "ECS/Components/SpriteRenderer.h"
#include "doctest.h"
#include <vector>
#include <memory>

using molga::CreateSpriteFromAssetCommand;

TEST_CASE("command adds a GameObject with a SpriteRenderer bound to the guid") {
    std::vector<std::shared_ptr<GameObject>> objects;
    CreateSpriteFromAssetCommand cmd(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Hero", {100.0f, 50.0f}, &objects);
    cmd.Execute();
    REQUIRE(objects.size() == 1);
    auto* sr = objects[0]->GetComponent<SpriteRenderer>();
    REQUIRE(sr != nullptr);
    CHECK(sr->GetTextureGuid() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    cmd.Undo();
    CHECK(objects.empty());          // undo는 생성된 오브젝트를 제거
}
```
> 테스트는 Editor 싱글톤 없이 동작하도록 `std::vector*`를 직접 받는 생성자를 둔다(에디터 통합 경로는 Step 4에서 Editor 헬퍼로 래핑).

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_create_sprite_from_asset test_create_sprite_from_asset.cpp)
target_sources(test_create_sprite_from_asset PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/CreateSpriteFromAssetCommand.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_create_sprite_from_asset -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: CreateSpriteFromAssetCommand 작성**

Create `src/Editor/Commands/CreateSpriteFromAssetCommand.h`:
```cpp
#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "Common/Types.h"
#include <memory>
#include <string>
#include <vector>

class GameObject;

namespace molga {

// 텍스처 guid로 Sprite GameObject를 만든다. 테스트는 objects 벡터를 직접 주입하고,
// 에디터는 Editor의 활성 오브젝트 벡터를 넘긴다.
class CreateSpriteFromAssetCommand : public ICommand {
public:
    CreateSpriteFromAssetCommand(std::string textureGuid, std::string name,
                                 Vector2 worldPos,
                                 std::vector<std::shared_ptr<GameObject>>* objects);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Sprite"; }
    GameObject* created() const { return created_; }
private:
    std::string textureGuid_;
    std::string name_;
    Vector2 worldPos_;
    std::vector<std::shared_ptr<GameObject>>* objects_;
    std::shared_ptr<GameObject> object_;   // redo 재사용
    GameObject* created_ = nullptr;
};

} // namespace molga
```

Create `src/Editor/Commands/CreateSpriteFromAssetCommand.cpp`:
```cpp
#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include <algorithm>

namespace molga {

CreateSpriteFromAssetCommand::CreateSpriteFromAssetCommand(
    std::string textureGuid, std::string name, Vector2 worldPos,
    std::vector<std::shared_ptr<GameObject>>* objects)
    : textureGuid_(std::move(textureGuid)), name_(std::move(name)),
      worldPos_(worldPos), objects_(objects) {}

void CreateSpriteFromAssetCommand::Execute() {
    if (!objects_) return;
    if (!object_) {
        object_ = std::make_shared<GameObject>(name_);
        auto* tr = object_->AddComponent<Transform>();
        tr->position = worldPos_;
        auto* sr = object_->AddComponent<SpriteRenderer>();
        sr->SetTextureGuid(textureGuid_);
        sr->ResolveAssets();   // 즉시 텍스처 로드(누락이면 placeholder)
    }
    objects_->push_back(object_);
    created_ = object_.get();
}

void CreateSpriteFromAssetCommand::Undo() {
    if (!objects_ || !object_) return;
    objects_->erase(
        std::remove(objects_->begin(), objects_->end(), object_), objects_->end());
    created_ = nullptr;
}

} // namespace molga
```
> `Transform::position`이 `Vector2`가 아니라면(예: `Vector3`/`x,y` 분리) 해당 필드 형태에 맞춰 한 줄 수정한다(`SpriteRenderer.cpp` 인스펙터의 기존 Transform 접근 형태를 그대로 따른다).

- [ ] **Step 4: SceneViewWindow에 drop target 추가**

`src/Editor/Windows/SceneViewWindow.cpp`의 씬 이미지(FBO 텍스처)를 그린 직후 — 보통 `ImGui::Image(...)` 다음 — 에 추가:
```cpp
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID");
        if (!p) p = ImGui::AcceptDragDropPayload("TEXTURE_PATH"); // 구 payload 호환
        if (p) {
            std::string guid;
            if (std::strcmp(p->DataType, "ASSET_GUID") == 0) {
                guid.assign(static_cast<const char*>(p->Data), p->DataSize - 1);
            } else { // TEXTURE_PATH → guid 변환
                std::string path(static_cast<const char*>(p->Data), p->DataSize - 1);
                guid = molga::AssetDatabase::Get().GuidForSource(
                    Project::Get().GetRelativePath(path));
            }
            if (!guid.empty()) {
                Vector2 world = ScreenToWorld(ImGui::GetMousePos()); // 기존 카메라 헬퍼
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::CreateSpriteFromAssetCommand>(
                        guid, "Sprite", world, Editor::Get().GetActiveObjects()));
                Editor::Get().SetSelectedObject(Editor::Get().GetActiveObjects()->back().get());
            }
        }
        ImGui::EndDragDropTarget();
    }
```
include 추가(`SceneViewWindow.cpp` 상단):
```cpp
#include "Core/AssetDatabase.h"
#include "Editor/Editor.h"
#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include <cstring>
```
> `ScreenToWorld`/`GetActiveObjects`는 SceneViewWindow/Editor에 이미 있는 카메라·오브젝트 접근자를 사용한다. 없으면 화면 좌표 대신 `{0,0}` 월드 위치로 두고(수동 검증 단계에서 위치 보정) Editor에 `std::vector<std::shared_ptr<GameObject>>* GetActiveObjects()` 한 줄 접근자를 추가한다(`SetGameObjects`로 주입된 포인터 반환).

- [ ] **Step 5: ProjectBrowser 드래그 payload에 ASSET_GUID 추가**

`src/Editor/Windows/ProjectBrowserWindow.cpp`의 텍스처 드래그 소스(`:244-249`)에서 `TEXTURE_PATH` 옆에 guid payload를 함께 싣는다:
```cpp
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            std::string guid = molga::AssetDatabase::Get().GuidForSource(
                Project::Get().GetRelativePath(entry.path));
            ImGui::SetDragDropPayload("ASSET_GUID", guid.c_str(), guid.size() + 1);
            ImGui::SetDragDropPayload("TEXTURE_PATH", entry.path.c_str(), entry.path.size() + 1);
            ImGui::Text("Texture: %s", entry.name.c_str());
            ImGui::EndDragDropSource();
        }
```
> ImGui는 한 소스에서 마지막 `SetDragDropPayload`만 활성 타입으로 유지한다. 단일 타입으로 통일하려면 `ASSET_GUID` 하나만 싣고, 인스펙터 쪽 `AcceptDragDropPayload("TEXTURE_PATH")`를 `ASSET_GUID`로 함께 갱신한다(같은 커밋에서 `SpriteRenderer`/`AudioSource`/`TilemapRenderer` 인스펙터 accept를 ASSET_GUID 우선 + TEXTURE_PATH/AUDIO_PATH 폴백으로 변경). 권장: **ASSET_GUID 단일화 + 폴백**.

- [ ] **Step 6: 빌드 + 자동 테스트 + 수동 검증(Exit 시나리오)**

Run:
```bash
cmake --preset debug && cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 PASS.

수동 검증(에디터 실행):
1. 프로젝트를 연다(`Assets/`에 텍스처 1개 존재).
2. Project Browser에서 텍스처를 Scene View로 드래그-드롭 → Sprite 생성·선택 확인.
3. 씬 저장.
4. Project Browser에서 그 텍스처를 우클릭→Rename(또는 다른 폴더로 Move).
5. 씬을 다시 로드 → **Sprite가 여전히 같은 텍스처로 보이는지** 확인(GUID 불변).
6. 텍스처를 Delete → "still referenced by N document(s)" 경고 + 휴지통 이동 확인, Undo로 복원.
7. 텍스처 파일을 외부에서 삭제 → Sprite가 분홍 placeholder + Console 경고로 표시되는지 확인.

- [ ] **Step 7: 커밋**

```bash
git add src/Editor/Commands/CreateSpriteFromAssetCommand.h \
        src/Editor/Commands/CreateSpriteFromAssetCommand.cpp \
        src/Editor/Windows/SceneViewWindow.cpp \
        src/Editor/Windows/ProjectBrowserWindow.cpp \
        tests/test_create_sprite_from_asset.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(asset): drag texture into Scene View to create a Sprite (UX-3 Task G)"
```

---

## 완료 기준

- [ ] 모든 소스 애셋이 `<파일>.meta`(안정된 guid)를 갖고, `AssetDatabase`가 `guid↔record`와 `sourcePath→guid`를 인덱싱한다.
- [ ] **텍스처를 이동/이름 변경해도 저장된 Sprite 참조가 깨지지 않는다**(guid 불변, `.meta` 동행). `test_project_file_commands` + 수동 Exit 시나리오로 검증.
- [ ] **누락 애셋이 시각적 placeholder(분홍 missing_texture) + Console 경고**로 표시된다(`SpriteRenderer::ResolveAssets`).
- [ ] **삭제는 참조를 경고하고 가역적**이다 — 휴지통(`Assets/.trash/`)으로 이동, Undo로 복원, 삭제 전 `AssetReferenceScan`이 참조 문서를 보고.
- [ ] **importer 버전 변경 시 영향받는 애셋만 reimport**된다(`AssetDatabase::Reimport(guid)`는 단일 record만 갱신).
- [ ] 구버전 씬(`texturePath`/`clipPath`만 존재)이 메모리에서 guid로 마이그레이션되고, 저장 시 guid로 기록되며 path도 하위 호환으로 보존된다.
- [ ] 증분 watcher가 added/removed/modified를 보고하고, 전체 재스캔 없이 인덱스를 갱신한다.
- [ ] Project Browser가 검색 + 타입 필터 + badge(missing/import-failed/generated)를 제공한다.
- [ ] 빌드 파이프라인이 선택 scene/prefab이 참조하는 애셋만 수집할 수 있다(`AssetReferenceScan::FindReferencers`로 닫힘 집합 산출 — UX-3 산출물을 BuildManager가 소비; 빌드 통합 자체는 3-1 Build와 연계).
- [ ] 모든 신규 테스트(`test_guid`, `test_asset_meta`, `test_asset_database`, `test_importer`, `test_asset_reference_migration`, `test_project_file_commands`, `test_asset_reference_scan`, `test_asset_watcher`, `test_create_sprite_from_asset`)가 Debug에서 통과한다.

---

## 의존성 / 순서

- **선행(필수):** UX-1(SelectionService) — Task G의 drag-to-scene 선택 갱신에 사용. 미완 시 `Editor::SetSelectedObject` 폴백으로 진행 가능. UX-2(Console + 구조화 로그 sink) — 누락 애셋/참조 경고 표시에 사용. 미완 시 `Log::Warn`(stdout) 폴백.
- **후행(이 작업이 토대):** UX-6(고급 제작 UX) — 멀티오브젝트 편집·프리팹 변형·command palette는 guid 참조와 `.meta`/index 위에서만 안전하다. 비-goal(§10): guid/importer가 안정되기 전 깊은 프리팹 변형·고급 애니메이션 에디터 금지.
- **로드맵 정합:** Phase 2-1 `AssetDatabase`에 대응(roadmap §11). roadmap §2.1 금지 규칙 "Asset 참조를 절대 경로로 저장하지 않는다"를 직접 충족한다. AssetDatabase는 Core 계층(`molga_core`)에 두어 Runtime/Editor가 공유하고, Runtime은 ImGui/Editor 싱글톤을 include하지 않는다.
- **하위 호환/마이그레이션(다운스트림이 반드시 알아야 할 사실):**
  - **현재 모든 애셋 참조는 경로 문자열이다**(`SpriteRenderer::texturePath`, `AudioSource::clipPath`, `Material::mainTexturePath`, `TilemapRenderer::spriteSheetPath`). guid 전환은 *추가*이며, 직렬화는 guid+path를 함께 쓴다(직전 두 버전 로드 지원, roadmap §2.2).
  - **마이그레이션은 입력 파일을 즉시 덮어쓰지 않는다.** path→guid 승격은 Deserialize 시 메모리에서만 일어나고, 사용자가 씬을 저장할 때 비로소 guid가 디스크에 기록된다.
  - **`.prefab`은 guid를 sidecar가 아니라 파일 본문에 보관**한다(기존 `PrefabRegistry` 설계). AssetDatabase는 prefab을 인덱싱할 때 본문 guid를 권위값으로 채택해 **PrefabRegistry와 동일 guid를 공유**한다 — 두 인덱스가 충돌하지 않도록 PrefabImporter가 본문 guid를 우선한다.
  - **마이그레이션 갭 리스크:** 스캔 전에 씬이 로드되면(`AssetDatabase`가 비어 있으면) path→guid 변환이 실패한다. 따라서 `main.cpp`에서 **씬 로드보다 먼저** `ScanProject`를 호출해야 한다(현재 `:201` SetAssetRoot 직후, `:205` ResolveAssets 이전).
  - 빌드/패키징은 더 이상 디렉터리 전체 복사가 아니라 참조 닫힘 집합 수집으로 좁힐 수 있다 — 3-1 Build 마일스톤이 `AssetReferenceScan`/`AssetRecord::dependencies`를 소비한다.

---

## 자기 점검(작성자 메모)

- Includes 매핑: GUID/meta/index → Task A·B; importer 인터페이스 → Task C; 안전한 Project Browser 동작 → Task E·F; 누락 참조 UX → Task C(placeholder)·D(ResolveAssets)·E(경고); drag-texture-to-SceneView → Task G. 모든 Includes 항목이 Task로 커버됨.
- 타입/메서드명 일관성: guid 접근자는 전 컴포넌트에서 `Get<Field>Guid`/`Set<Field>Guid`. AssetDatabase 인덱스 변경 훅은 `OnSourceAdded`/`OnSourceRemoved`/`OnSourceRenamed`로 통일. Command 명명은 `ProjectFile<동작>Command` + `CreateSpriteFromAssetCommand`. `AssetReferenceScan::FindReferencers`는 Task E 정의 → Task F/완료기준에서 동일 시그니처로 사용.
- placeholder 스캔: 모든 Step에 실제 C++/명령/기대 출력 포함. "TODO/적절히 처리" 류 문구 없음.

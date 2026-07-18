# 게임 제작 P1 구현 계획 — 캐릭터 스테이지 저작 파이프라인

> 작성일: 2026-07-16
>
> 상태: **5/5 완료** (2026-07-16)
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 목표 상태 표기: 최종 패키징 E2E와 전체 검증 게이트 통과 전에는 P1을 부분 완료로만 기록하고, 통과 후에만 **5/5 완료**로 갱신한다.

### 완료 기록

- 공통 asset catalog v2, importer registry, 재귀 dependency validator와 원자적 asset 편집 경로를 구현했다.
- immutable multi-quad 렌더 계약 위에 texture/sprite, Animator, Tilemap, Particle, Audio의 다섯 축을 통합했다.
- point-filtered idle/run/jump 캐릭터, 3-layer terrain tilemap, textured particle burst, Music/SFX bus와 BGM crossfade를 하나의 패키징 fixture에서 검증했다.
- Debug/Release/ASan/UBSan에서 각각 CTest **70/70**이 통과했으며, 모든 preset에서 `smoke_end_to_end`가 통과했다.
- 최종 `git diff --check`도 통과했으며 기준 갭 분석을 P1 **5/5 완료**로 갱신했다.

---

## 1. 목표

P1은 서로 분리된 다섯 기능을 추가하는 작업이 아니라, 실제 캐릭터 중심 2D 스테이지를 반복 저작하고 패키징할 수 있는 하나의 제작 파이프라인을 완성하는 마일스톤이다.

1. 텍스처 임포트 설정과 안정적인 스프라이트 슬라이스
2. `AnimationClip2D` 에셋과 `Animator2D` FSM
3. 네이티브 타일 팔레트, 다중 레이어, 4방향 오토타일, 청크 배칭
4. 텍스처·곡선 기반 CPU 파티클
5. 고정 오디오 버스와 페이드·BGM 크로스페이드

완료 산출물은 다음을 한 스테이지에서 함께 사용하는 패키징된 2D 플랫폼 게임이다.

- point-filtered spritesheet의 idle/run/jump 상태 전환
- 배경·충돌·전경 타일 레이어와 4방향 terrain 오토타일
- 텍스처 atlas를 사용하는 결정적 particle burst
- Music/SFX 버스, one-shot 효과음, 두 BGM 사이 crossfade
- 에디터 저장 → Play clone → 패키징 → 별도 런타임 실행까지 동일한 GUID 참조와 시각·청각 결과

### 1.1 성공 기준

- 다섯 축의 데이터가 씬·프리팹·에셋 파일과 catalog를 왕복해도 손실되지 않는다.
- 레거시 씬·프리팹·catalog v1·path 기반 API가 계속 읽히고 기존 시각 크기와 top-left 원점이 보존된다.
- 빌드 전에 모든 신규 에셋 의존성을 재귀 검증하며, 누락·타입 불일치·순환 참조는 명확한 참조 체인과 함께 실패한다.
- 타일과 파티클은 기존 `SpriteBatcher`를 사용하고 2,048 quad 경계를 지킨다.
- 실패한 텍스처 재임포트는 기존 `Texture*` 주소와 마지막 정상 GPU 리소스를 보존한다.
- 오디오 voice는 disable, scene unload, Play Stop, 엔진 종료 뒤 남지 않는다.
- Debug/Release/ASan/UBSan 전체 CTest, 패키징 smoke, `git diff --check`가 모두 통과한다.

### 1.2 비목표

다음은 P1 범위에 포함하지 않는다.

- 텍스처 압축, 플랫폼별 override, atlas packing/extrusion, nine-slice
- Animator 그래프 에디터, AnyState, blend tree, sprite crossfade, animation event, Marrow 통합
- weighted tile variant, 8방향/47-tile terrain, 회전·비균일/음수 scale 타일 페인팅
- GPU particle simulation, particle collision, noise, sub-emitter
- 임의 오디오 버스 그래프, ducking, DSP effect, snapshot
- 기존 `Rendering::Animation`, `SpriteSheet`, legacy Tilemap 경로의 즉시 삭제
- 신규 외부 라이브러리 도입

---

## 2. 현재 기준선과 설계 원칙

### 2.1 현재 기반

이미 존재하며 확장해야 하는 기반은 다음과 같다.

- `AssetMeta`의 GUID/importer/version sidecar와 `AssetDatabase`의 GUID↔path 인덱스
- catalog schema v1 저장·런타임 로드와 path 기반 레거시 참조 승격
- `TextureManager`의 `Texture*` 캐시와 OpenGL 3.3 텍스처
- 단일 quad를 값으로 소유하는 `RenderCommand`, 2,048 sprite 제한의 `SpriteBatcher`
- 씬/프리팹 직렬화, Play용 edit/play `World` 분리, component snapshot Undo/Redo
- fallback 렌더 경로를 쓰는 legacy `TilemapRenderer`와 `ParticleSystem`
- miniaudio 기반 전역 오디오와 `AudioSource`
- staging 패키징, asset catalog, CTest preset, 패키징 E2E smoke

### 2.2 공통 원칙

- **GUID가 권위값이다.** path는 레거시 호환과 사용자 표시용으로만 유지한다.
- **기존 입력은 묵시적으로 파괴하지 않는다.** 마이그레이션은 메모리에서 수행하고, 사용자가 저장하거나 명시적으로 변환할 때만 새 schema를 기록한다.
- **재임포트는 트랜잭션이다.** CPU decode, 유효성 검사, 임시 GPU/decoder 준비가 모두 성공한 뒤에만 last-good를 교체한다.
- **런타임 포인터 수명을 안정화한다.** component는 재임포트 때문에 무효화되는 raw owner를 갖지 않는다.
- **저작 변경은 명령 단위다.** asset 내용과 `.meta` 변경은 원자 저장과 Undo/Redo를 거치며, reimport는 명시적인 command 결과다.
- **렌더 데이터는 제출 뒤 불변이다.** queue가 소비할 때까지 component 내부 vector나 임시 메모리를 참조하지 않는다.
- **한 프레임의 결정은 결정적이다.** Animator transition 우선순위, particle RNG, tile terrain 갱신 범위를 저장 계약으로 고정한다.
- **오류는 조용히 다른 타입으로 해석하지 않는다.** 누락과 타입 불일치는 placeholder를 쓸 수 있는 편집 렌더와 패키징 실패를 구분한다.

---

## 3. 공통 아키텍처

### 3.1 의존성 흐름

| 소유 데이터 | 직접 의존성 | 최종 런타임 자원 |
|---|---|---|
| Scene/Prefab | animator controller, tileset, texture, audio GUID | component runtime state |
| `.animator` | `.animclip` GUID | `Animator2D` FSM |
| `.animclip` | texture GUID + stable slice ID | `ResolvedSprite` frame |
| `.tileset` | texture GUID + stable slice ID | chunk quad/collision data |
| ParticleSystem v2 | texture GUID + stable slice IDs | emitter quad pool |
| AudioSource/API | audio GUID | `AudioService` voice |
| texture/audio `.meta` | importer settings | GL texture / decoder configuration |

의존성 validator는 위 그래프를 scene/prefab root부터 깊이 우선으로 방문한다. 각 edge는 기대 importer/type을 포함하며, 오류에는 `scene → prefab → controller → clip → texture`처럼 전체 참조 체인을 기록한다.

### 3.2 예정 파일 구조

구현 중 이름은 저장소 관례에 맞게 조정할 수 있지만 책임 경계는 유지한다.

공통 Core/Rendering:

- 수정: `src/Core/AssetMeta.h`
- 수정: `src/Core/AssetDatabase.h`, `src/Core/AssetDatabase.cpp`
- 수정: `src/Core/Importers/Importer.h`
- 생성: `src/Core/Importers/ImporterRegistry.h`, `.cpp`
- 생성: `src/Core/AssetDependencyValidator.h`, `.cpp`
- 생성: `src/Core/SpriteAsset.h`, `.cpp`
- 수정: `src/Rendering/RenderQueue.h`
- 수정: `src/Rendering/SpriteBatcher.h`, `.cpp`
- 수정: `src/Rendering/RenderSystem2D.cpp`
- 수정: `src/Rendering/Texture.h`, `.cpp`
- 수정: `src/Rendering/Framebuffer.h`, `.cpp`
- 수정: `src/Core/TextureManager.h`, `.cpp`

Editor 공통:

- 생성: `src/Editor/Commands/AssetContentCommand.h`, `.cpp`
- 수정: `src/Editor/Windows/ProjectBrowserWindow.*`
- 수정: `src/Editor/Windows/InspectorWindow.*`
- 수정: `src/Editor/GameBuilder.*`
- 수정: `src/Editor/Watcher/AssetWatcher.*`

기능별:

- 생성: `src/Animation/AnimationClip2D.*`, `AnimatorController2D.*`
- 생성: `src/ECS/Components/Animator2D.*`
- 생성: `src/Editor/Windows/AnimationWindow.*`
- 생성: `src/Tilemap/TileSet.*`, `TilemapLayer.*`, `TilePaintCommand.*`
- 수정: `src/ECS/Components/TilemapRenderer.*`
- 생성: `src/Editor/Windows/TilePaletteWindow.*`
- 수정: `src/Systems/Particle.*`, `src/ECS/Components/ParticleSystem.*`
- 생성 또는 수정: `src/Systems/AudioService.*`, `src/ECS/Components/AudioSource.*`
- 수정: `src/Core/ProjectSettings.*`
- 수정: `CMakeLists.txt`, `tests/CMakeLists.txt`

---

## 4. 저장 계약

모든 신규 JSON schema는 알 수 없는 필드를 읽을 때 무시하되, importer-owned `.meta`는 다시 쓸 때 알 수 없는 root/settings 키를 보존한다. 잘못된 필드 타입은 무조건 예외를 밖으로 던지는 대신 진단 가능한 validation 오류로 변환한다.

### 4.1 AssetMeta schema v2

텍스처 예시:

```json
{
  "schemaVersion": 2,
  "guid": "0123456789abcdef0123456789abcdef",
  "importer": "TextureImporter",
  "importerVersion": 2,
  "settings": {
    "filter": "Nearest",
    "wrapU": "Clamp",
    "wrapV": "Clamp",
    "mipmaps": false,
    "colorSpace": "SRGB",
    "pixelsPerUnit": 16.0,
    "spriteMode": "Multiple",
    "defaultPivot": [0.5, 0.5],
    "slices": [
      {
        "id": "11111111111111111111111111111111",
        "name": "Idle 0",
        "rect": [0, 0, 16, 16],
        "pivot": [0.5, 0.5]
      }
    ]
  }
}
```

규칙:

- `schemaVersion`이 없으면 v1로 간주한다.
- 신규 texture meta 기본값은 `Linear`, `Clamp/Clamp`, mipmap off, `SRGB`, PPU 1, `Single`, center pivot이다.
- 기존 v1 texture meta는 화면 색과 크기를 보존하기 위해 메모리상 `LegacyLinear`, PPU 1, full-rect single slice로 해석한다.
- `rect`는 `[x, y, width, height]`이고 원본 이미지의 top-left pixel 좌표를 사용하는 정수 범위다.
- `pivot`은 rect 내부 normalized 좌표 `[0, 1]`이고 `(0,0)`은 top-left, `(1,1)`은 bottom-right다.
- slice `id`가 참조 권위값이다. 이름과 rect를 바꿔도 유지할 수 있다.
- reslice는 같은 grid cell 또는 명시적으로 매칭된 기존 slice의 ID를 보존하고, 삭제된 ID를 임의의 새 slice에 재사용하지 않는다.
- `Single`도 stable slice ID 하나를 가진다. 빈 slice ID는 저장 전에 생성한다.
- importer/version migration이나 설정 수정 시 root와 `settings`의 알 수 없는 키를 그대로 보존한다.

오디오 예시:

```json
{
  "schemaVersion": 2,
  "guid": "fedcba9876543210fedcba9876543210",
  "importer": "AudioImporter",
  "importerVersion": 2,
  "settings": {
    "loadMode": "DecodeOnLoad"
  }
}
```

`loadMode`는 `DecodeOnLoad` 또는 `Streaming`이다. decoder header에서 읽은 duration, channel count, sample rate는 source-derived metadata이므로 `.meta`가 아니라 catalog의 generic `metadata` object에 기록한다.

### 4.2 Asset catalog schema v2

```json
{
  "schemaVersion": 2,
  "assetRootMode": "packageRoot",
  "records": [
    {
      "guid": "0123456789abcdef0123456789abcdef",
      "sourcePath": "Assets/Textures/player.png",
      "importer": "TextureImporter",
      "importerVersion": 2,
      "artifactPath": "",
      "hash": "0123456789abcdef",
      "settings": {},
      "dependencies": [],
      "importFailed": false,
      "importError": "",
      "generated": false,
      "width": 128,
      "height": 64,
      "metadata": {}
    }
  ]
}
```

규칙:

- writer는 항상 v2를 기록한다.
- reader는 v1과 v2를 모두 읽는다. v1에 없는 필드는 안전한 기본값을 사용한다.
- GUID, normalized source path, importer/type이 잘못된 record는 catalog load 오류로 보고한다.
- 중복 GUID와 중복 source path는 마지막 항목으로 덮어쓰지 않고 load/scan 오류로 처리한다.
- `dependencies`는 GUID 배열이며 importer가 추출한 직접 의존성만 저장한다. transitive closure는 validator가 계산한다.
- import 실패 record도 catalog에 남기되 패키징 dependency closure에 포함되면 빌드를 실패시킨다.
- 저장 순서는 normalized source path 또는 GUID로 정렬해 deterministic diff를 만든다.
- catalog 쓰기는 임시 파일 flush 후 rename하는 원자 저장을 사용한다.
- AudioImporter의 derived metadata는 `{ "durationSeconds": 1.25, "channels": 2, "sampleRate": 48000, "loadMode": "DecodeOnLoad" }`처럼 generic `metadata`에 저장한다.

### 4.3 Importer registry 계약

```cpp
struct ImportResult {
    bool success = false;
    std::string error;
    std::string artifactPath;
    std::vector<std::string> dependencies;
    nlohmann::json metadata;
};

class IImporter {
public:
    virtual std::string Name() const = 0;
    virtual int Version() const = 0;
    virtual bool CanImport(const std::string& lowerExtension) const = 0;
    virtual ImportResult Import(const std::string& sourcePath) const = 0;
    virtual ImportResult Import(const std::string& sourcePath,
                                const nlohmann::json& settings) const = 0;
};
```

registry 규칙:

- built-in importer 등록은 한 곳에서 수행한다.
- extension→importer와 importer name→instance 조회가 같은 registry를 사용한다.
- meta에 기록된 importer가 현재 extension과 호환되지 않으면 묵시적으로 GenericImporter로 실행하지 않고 migration 또는 오류를 선택한다.
- GenericImporter도 명시적으로 등록하며 알 수 없는 형식을 성공으로 가장하지 않는다. 단순 opaque asset 정책은 별도 결과로 표현한다.
- reimport는 record를 먼저 파괴하지 않는다. 새 결과가 준비된 뒤 status/settings/metadata를 commit한다.

### 4.4 공통 SpriteRef 계약

```cpp
struct SpriteRef {
    std::string textureGuid;
    std::string sliceId;

    bool Empty() const;
};

struct ResolvedSprite {
    Texture* texture = nullptr;
    Vector4 uv;              // u0, v0, u1, v1
    Vector2 pivotNormalized;
    Vector2 pivotOffset;     // world/local units from top-left
    Vector2 nativeSize;      // pixel size / PPU
    Vector2 pixelSize;
};
```

규칙:

- texture GUID가 없거나 importer type이 다르면 해석 실패다.
- slice ID가 없으면 해당 texture의 single/full slice만 명시적으로 선택할 수 있다. Multiple texture의 임의 첫 slice fallback은 금지한다.
- pixel rect→UV 변환은 stb/OpenGL vertical flip을 resolver 한 곳에서 처리한다.
- `ResolvedSprite`는 texture import revision과 함께 캐시할 수 있으나 reimport 성공 시 무효화한다.
- 누락 slice는 편집 화면에서 placeholder와 진단을 제공하고 패키징 dependency validation에서는 실패한다.

### 4.5 AnimationClip2D 에셋

확장자: `.animclip`

```json
{
  "schemaVersion": 1,
  "textureGuid": "0123456789abcdef0123456789abcdef",
  "loop": true,
  "frames": [
    { "sliceId": "11111111111111111111111111111111", "durationSeconds": 0.1 },
    { "sliceId": "22222222222222222222222222222222", "durationSeconds": 0.1 }
  ]
}
```

규칙:

- 한 clip은 하나의 texture GUID만 참조한다.
- frame duration은 유한한 양수여야 한다. 잘못된 값은 validation 오류이며 runtime은 무한 loop 없이 안전하게 정지한다.
- 빈 frame 목록은 저장할 수 있지만 재생 불가 진단을 낸다.
- frame은 index가 아니라 stable slice ID를 참조한다.

### 4.6 AnimatorController2D 에셋

확장자: `.animator`

```json
{
  "schemaVersion": 1,
  "parameters": [
    { "id": "grounded", "name": "Grounded", "type": "Bool", "default": true },
    { "id": "speed", "name": "Speed", "type": "Float", "default": 0.0 },
    { "id": "jump", "name": "Jump", "type": "Trigger" }
  ],
  "states": [
    {
      "id": "idle",
      "name": "Idle",
      "clipGuid": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "speed": 1.0
    }
  ],
  "defaultStateId": "idle",
  "transitions": [
    {
      "id": "idle_to_run",
      "fromStateId": "idle",
      "toStateId": "run",
      "hasExitTime": false,
      "exitTime": 0.0,
      "conditions": [
        { "parameterId": "speed", "op": "Greater", "value": 0.01 }
      ]
    }
  ]
}
```

규칙:

- parameter/state/transition ID는 이름 변경과 배열 재정렬에도 유지되는 stable ID다.
- parameter type은 `Bool`, `Int`, `Float`, `Trigger`다.
- bool은 `Equals`, int/float는 `Equals`, `NotEqual`, `Greater`, `Less`, Trigger는 `Set` 조건을 지원한다.
- transition 배열 순서가 우선순위다. 같은 frame에 여러 조건이 맞아도 첫 transition 하나만 수행한다.
- transition이 실제 선택됐을 때 그 transition 조건에 사용된 Trigger만 소비한다. 평가했지만 선택되지 않은 transition의 Trigger는 유지한다.
- exit time은 source state의 normalized time 기준이다. loop state는 현재 loop 구간을 기준으로 판단한다.
- default state, state clip, transition endpoint, parameter type 불일치는 asset validation 오류다.

### 4.7 TileSet 에셋

확장자: `.tileset`

```json
{
  "schemaVersion": 1,
  "cellSize": { "x": 16, "y": 16 },
  "tiles": [
    {
      "id": "grass_center",
      "sprite": {
        "textureGuid": "0123456789abcdef0123456789abcdef",
        "sliceId": "33333333333333333333333333333333"
      },
      "solid": true,
      "terrainId": "grass"
    }
  ],
  "terrains": [
    {
      "id": "grass",
      "name": "Grass",
      "rules": {
        "0": "grass_isolated",
        "15": "grass_center"
      }
    }
  ]
}
```

terrain mask 규칙:

- 4-bit mask는 `N=1`, `E=2`, `S=4`, `W=8`이다.
- 같은 terrain ID의 직교 이웃만 bit를 켠다.
- rule value는 tile stable ID다.
- 각 terrain은 필요한 0~15 mask를 명시할 수 있다. rule이 없는 mask는 현재 tile을 유지하고 진단 badge를 표시한다.
- `solid`는 P1에서 full-cell collision만 뜻한다.

### 4.8 TilemapRenderer schema v2

씬/프리팹 component 예시:

```json
{
  "type": "TilemapRenderer",
  "schemaVersion": 2,
  "tileSetGuid": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "width": 128,
  "height": 64,
  "chunkSize": 32,
  "layers": [
    {
      "id": "collision",
      "name": "Collision",
      "visible": true,
      "locked": false,
      "collisionEnabled": true,
      "opacity": 1.0,
      "sortingOffset": 0,
      "cellsRle": [
        { "count": 32, "tileId": "", "terrainId": "" },
        { "count": 4, "tileId": "grass_center", "terrainId": "grass" }
      ]
    }
  ]
}
```

규칙:

- cell 데이터는 layer별 row-major 순서이며 run count 합이 항상 `width * height`다.
- 빈 cell은 빈 tile/terrain ID로 표현한다.
- layer ID는 stable하고 이름·순서 변경과 무관하다.
- opacity는 `[0,1]`, sorting offset은 component sorting order에 더한다.
- 내부 chunk 크기는 v2에서 32×32로 고정한다. 저장값이 있더라도 32 외 값은 validation 오류다.
- 기존 path 기반 단층 tilemap은 기존 schema로 계속 읽고 렌더한다. 자동 저장 마이그레이션하지 않는다.
- v2 전환은 `Create TileSet & Convert` 명령으로만 수행하며 원본 snapshot을 Undo할 수 있어야 한다.

### 4.9 ParticleSystem schema v2

```json
{
  "type": "ParticleSystem",
  "schemaVersion": 2,
  "durationSeconds": 1.0,
  "loop": false,
  "simulationSpace": "World",
  "seed": 12345,
  "sprites": [
    { "textureGuid": "0123456789abcdef0123456789abcdef", "sliceId": "44444444444444444444444444444444" }
  ],
  "frameMode": "Random",
  "blendMode": "Additive",
  "particleLifetime": { "min": 0.4, "max": 0.8 },
  "sizeOverLife": {
    "keys": [
      { "time": 0.0, "value": 1.0 },
      { "time": 1.0, "value": 0.0 }
    ]
  },
  "colorOverLife": {
    "keys": [
      { "time": 0.0, "color": [1.0, 1.0, 1.0, 1.0] },
      { "time": 1.0, "color": [1.0, 0.5, 0.0, 0.0] }
    ]
  }
}
```

규칙:

- `durationSeconds`는 emitter의 emission duration이고 particle lifetime과 독립이다.
- frame mode `Start`는 첫 sprite, `Random`은 spawn 때 한 번 결정, `OverLife`는 normalized age를 sprite 목록에 매핑한다.
- `FloatCurve`와 `ColorGradient` key time은 `[0,1]`이며 정렬·중복 시간 처리 규칙을 loader에서 정규화한다.
- key 사이 값은 piecewise-linear interpolation이다. key가 없으면 안전 기본값, 한 key면 상수다.
- 기존 start/end size/color는 time 0/1 두 key로 메모리 migration한다.
- 같은 serialized config, seed, emit/update 호출 순서는 동일한 particle 결과를 만든다.
- edit preview의 runtime pool/RNG/time은 직렬화하지 않는다.

### 4.10 오디오 mixer 저장 계약

`ProjectSettings/game.json`의 고정 구조:

```json
{
  "audioMixer": {
    "Master": { "volume": 1.0, "muted": false },
    "Music":  { "volume": 1.0, "muted": false },
    "SFX":    { "volume": 1.0, "muted": false },
    "Voice":  { "volume": 1.0, "muted": false },
    "UI":     { "volume": 1.0, "muted": false }
  }
}
```

규칙:

- 계층은 `Master/{Music,SFX,Voice,UI}`로 고정한다.
- volume은 `[0,1]` linear gain으로 clamp한다.
- child 최종 gain은 `Master * child * voice gain`이며 engine-level master gain은 항상 1이다.
- 기존 `AudioSource`에 bus가 없으면 `Master`로 읽는다. 새 component 기본값은 `SFX`다.
- mute는 저장 volume을 바꾸지 않는다.
- 잘못된 bus 문자열은 runtime에서 임의 생성하지 않고 `Master` fallback과 진단을 사용한다.

---

## 5. 공개 런타임 API

### 5.1 Sprite/texture

```cpp
ResolvedSprite ResolveSprite(const SpriteRef& ref);

class SpriteRenderer : public Component {
public:
    void SetSprite(const SpriteRef& sprite);              // authored, serialized
    const SpriteRef& GetSprite() const;
    void SetRuntimeSpriteOverride(const SpriteRef& sprite); // Animator, not serialized
    void ClearRuntimeSpriteOverride();

    void SetSizeMode(SpriteSizeMode mode);                // Custom | Native
    SpriteSizeMode GetSizeMode() const;
    void SetSize(const Vector2& size);                     // Custom size
    Vector2 GetRenderedSize() const;
};
```

- Animator는 runtime override만 건드린다.
- Animator `Stop`, component disable, controller 제거 시 authored sprite로 복구한다.
- legacy `textureGuid`/`texturePath` sprite는 기존 custom pixel size와 top-left origin을 유지한다.
- 새로 생성한 SpriteRef object만 PPU/native size와 pivot을 기본 적용한다.

### 5.2 Animator2D

```cpp
class Animator2D : public Component {
public:
    bool Play(const std::string& stateId, float normalizedTime = 0.0f);
    void Stop();
    void Pause();
    void Resume();

    bool SetBool(const std::string& parameterId, bool value);
    bool GetBool(const std::string& parameterId, bool& out) const;
    bool SetInt(const std::string& parameterId, int value);
    bool GetInt(const std::string& parameterId, int& out) const;
    bool SetFloat(const std::string& parameterId, float value);
    bool GetFloat(const std::string& parameterId, float& out) const;
    bool SetTrigger(const std::string& parameterId);
    bool ResetTrigger(const std::string& parameterId);

    std::string CurrentStateId() const;
    float NormalizedTime() const;
    bool IsPlaying() const;
    bool IsPaused() const;
    void SetSpeed(float speed);
    float GetSpeed() const;
};
```

평가 phase:

```text
Script Update → Animator2D Evaluate → Script LateUpdate → render collection
```

- 큰 `dt`는 여러 frame duration과 loop를 정확히 소비한다.
- 0/음수/NaN duration은 무한 반복 없이 오류 상태로 정지한다.
- controller/clip/slice/SpriteRenderer 누락은 crash하지 않고 상태·diagnostic을 유지한다.
- transition 이후 새 state의 frame 0을 같은 animation phase에 적용한다.

### 5.3 TilemapRenderer

```cpp
class TilemapRenderer : public Component {
public:
    bool Resize(int width, int height);
    Vector2 CellToWorld(int x, int y) const;
    CellCoord WorldToCell(const Vector2& world) const;

    LayerId AddLayer(std::string name);
    bool RemoveLayer(LayerId id);
    bool MoveLayer(LayerId id, int newIndex);
    const TileLayer* GetLayer(LayerId id) const;

    TileCell GetCell(LayerId layer, int x, int y) const;
    bool SetCell(LayerId layer, int x, int y, const TileCell& cell);
    bool SetCells(LayerId layer, const std::vector<CellEdit>& edits);
    bool SetTerrain(LayerId layer, int x, int y, TerrainId terrain);
    void RefreshTerrainNeighborhood(LayerId layer, int x, int y);
};
```

- 잠긴 layer의 mutation API는 false를 반환한다.
- batch edit는 affected chunk/revision과 collision revision을 한 번만 갱신한다.
- terrain 변경은 대상 cell과 N/E/S/W 다섯 cell만 재평가한다.
- world/cell 변환은 회전 0, 양의 uniform scale만 지원한다.

### 5.4 ParticleSystem

```cpp
class ParticleSystem : public Component {
public:
    void Play();
    void Pause();
    void Stop();       // emission stop + playback reset policy
    void Clear();      // active particles 즉시 free pool로 반환
    void Emit(int count);

    bool IsPlaying() const;
    bool IsPaused() const;
    int ActiveParticleCount() const;
};
```

`Stop`과 `Clear`의 차이를 테스트로 고정한다. `Pause` 중 emitter time과 particle age는 모두 진행하지 않는다.

### 5.5 AudioService

```cpp
enum class AudioBus { Master, Music, SFX, Voice, UI };

struct VoiceHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
    bool IsValid() const;
};

class AudioService {
public:
    VoiceHandle PlayOneShot(const std::string& audioGuid,
                            AudioBus bus = AudioBus::SFX,
                            float volume = 1.0f);
    VoiceHandle PlayMusic(const std::string& audioGuid,
                          float fadeInSeconds = 0.0f);
    VoiceHandle CrossFadeMusic(const std::string& audioGuid,
                               float durationSeconds);
    void StopMusic(float fadeOutSeconds = 0.0f);

    void SetBusVolume(AudioBus bus, float volume);
    float GetBusVolume(AudioBus bus) const;
    void SetBusMuted(AudioBus bus, bool muted);
    bool GetBusMuted(AudioBus bus) const;
    void FadeBus(AudioBus bus, float targetVolume, float durationSeconds);

    bool IsAlive(VoiceHandle handle) const;
    size_t LiveVoiceCount() const;
};
```

- handle은 slot generation을 검증하며 stale handle 조작은 no-op이다.
- `AudioSource`는 `ma_sound`를 직접 소유하지 않고 `VoiceHandle`만 가진다.
- fade/crossfade는 audio update tick에서 진행하며 duration ≤ 0은 즉시 적용한다.
- 새 music 재생 성공 전 기존 music을 파괴하지 않는다.
- no-device backend도 같은 voice/fade/lifetime state machine을 실행한다.

---

## 6. 에디터 저작 계약

### 6.1 Asset selection과 Inspector

- Project Browser의 file selection을 Editor의 asset selection model에 연결한다.
- Inspector target은 GameObject와 Asset을 명시적으로 구분한다.
- Inspector lock은 target kind와 stable identity를 함께 저장한다.
- texture 선택 시 import settings, slice 목록, preview, Apply/Revert/Reimport를 표시한다.
- `.animclip`, `.animator`, `.tileset`, audio asset은 각 전용 editor 또는 asset Inspector를 연다.
- drag/drop payload는 importer/type을 검증한 GUID를 전달한다.

### 6.2 AssetContentCommand

한 asset 편집 transaction은 다음을 포함한다.

1. 대상 source 또는 `.meta`의 before bytes
2. 저장할 after bytes
3. 임시 파일 write/flush/rename
4. 성공한 파일 commit 뒤 명시적 reimport
5. Undo 시 before bytes 원자 복원과 reimport
6. Redo 시 동일 after bytes 재적용

실패한 write/reimport는 command history에 성공한 변경처럼 남기지 않는다. scene dirty와 asset dirty 표시는 구분하되, 저장되지 않은 asset 편집을 닫을 때 사용자에게 경고한다.

### 6.3 Texture slicing

- Grid slicing: cell width/height, offset, spacing, row/column 범위
- Manual slicing: rect create/move/resize, name, pivot
- ID-preserving reslice preview: 유지/추가/삭제될 ID를 Apply 전에 표시
- rect overlap은 허용하되 bounds 밖, 0 size, 중복 ID는 금지
- preview는 nearest/linear와 pivot 위치를 실제 import setting으로 표시

### 6.4 Animation Window

- clip frame 목록 추가/삭제/정렬, slice 선택, frame duration, loop
- scrub time과 play/pause preview
- preview는 선택 GameObject의 직렬화 상태를 바꾸지 않는 runtime copy 사용
- controller parameter/state/transition을 표 형태로 편집
- transition 순서 이동이 runtime priority 순서를 직접 바꾼다.
- graph canvas는 P1에 포함하지 않는다.

### 6.5 Tile Palette

- active TileSet, active layer, tile/terrain 선택
- pencil, erase, rectangle, flood fill, eyedropper
- Scene View grid, cell hover, brush ghost preview
- locked/hidden layer 상태 표시와 mutation 차단
- transform 계약 위반 시 Inspector와 Scene View 경고, 모든 paint tool 차단
- 한 mouse drag 전체를 sparse before/after 하나의 `TilePaintCommand`로 기록
- flood fill은 map bounds와 원래 cell identity를 기준으로 종료를 보장

### 6.6 Particle preview

- edit mode preview는 serialized component를 복사한 별도 runtime emitter를 사용한다.
- preview tick/Play/Stop/Burst는 scene dirty와 Undo history를 만들지 않는다.
- inspector 값 변경 시 preview copy만 안전하게 재생성한다.
- Play mode 진입/종료, selection 변경, component 제거 때 preview resource를 해제한다.

### 6.7 Audio settings

- Project Settings에 다섯 고정 bus의 volume/mute를 표시한다.
- audio asset Inspector에 header metadata와 DecodeOnLoad/Streaming을 표시한다.
- 손상 source는 last-known metadata를 정상 상태처럼 표시하지 않고 import error를 노출한다.

---

## 7. 렌더와 리소스 수명 계약

### 7.1 Multi-quad RenderCommand

기존 단일 sprite producer를 깨지 않도록 단일 quad 필드를 유지하고, 추가 geometry를 선택적으로 사용한다.

```cpp
struct QuadGeometry {
    std::vector<Vertex2D> vertices; // size % 4 == 0
};

struct RenderBounds {
    AABB worldBounds;
};

struct RenderCommand {
    SortKey sortKey;
    BatchKey batchKey;

    bool isBatchableSprite = false;
    std::array<Vertex2D, 4> vertices; // legacy/single quad

    std::shared_ptr<const QuadGeometry> quadGeometry;
    std::optional<RenderBounds> viewBounds;
    std::function<void(Renderer*)> fallbackRender;
};
```

규칙:

- `quadGeometry`가 있으면 그것이 batch geometry이며 제출 뒤 수정하지 않는다.
- geometry가 없으면 기존 `vertices` 한 개를 처리한다.
- view bounds가 없으면 기존처럼 항상 제출한다.
- camera와 bounds가 있으면 완전히 바깥인 command를 batcher에 넘기지 않는다.
- 한 command가 2,048 quad를 넘으면 같은 BatchKey를 유지한 채 내부 flush한다.
- geometry 수명은 queue clear 이후까지 연장할 필요가 없지만 component vector를 직접 참조해서는 안 된다.

### 7.2 타일 청크

- layer마다 32×32 cell chunk를 갖는다.
- cell/tile/opacity 변경은 해당 chunk만 dirty로 만든다.
- terrain neighbor 갱신으로 바뀐 이웃이 다른 chunk면 두 chunk 모두 dirty다.
- visible layer의 non-empty visible chunk만 command를 제출한다.
- 동일 texture/blend/order의 연속 command는 기존 batcher에서 합쳐진다.
- chunk geometry는 texture/slice import revision 변경 시 재생성한다.

### 7.3 파티클 batch

- emitter 하나는 active particle을 sorting policy에 따라 quad geometry로 snapshot한다.
- sprite 목록이 여러 texture를 참조하면 texture별 연속 command로 나누거나 P1 validation에서 단일 texture를 요구한다. 구현은 draw order를 보존하는 쪽을 선택하고 테스트로 고정한다.
- Alpha/Additive는 BatchKey blend mode에 반영한다.
- 2,048 quad마다 flush되며 command/fallback 직접 draw를 사용하지 않는다.

### 7.4 Texture 재임포트

성공 경로:

1. source를 임시 CPU buffer로 decode한다.
2. settings와 slice rect를 검증한다.
3. 임시 GL texture를 올리고 오류를 검사한다.
4. 기존 `Texture` 객체 내부의 GL handle/dimensions/settings revision을 교체한다.
5. 이전 GL handle을 안전하게 삭제한다.
6. sprite resolver와 chunk cache를 revision으로 무효화한다.

실패 경로:

- 기존 `Texture` 객체, GL handle, dimensions, revision을 바꾸지 않는다.
- catalog record에는 현재 import failure/error를 기록한다.
- 아직 last-good가 없는 최초 import만 placeholder를 사용한다.
- decode 실패 객체를 성공처럼 TextureManager cache에 넣지 않는다.

### 7.5 색 공간

- `SRGB` color texture는 sRGB internal format으로 upload한다.
- `LegacyLinear`는 기존 GL_RGBA/GL_RGB sampling 결과를 보존한다.
- Scene View FBO color attachment와 final framebuffer의 sRGB enable/disable 규칙을 명시적으로 맞춘다.
- data texture와 font atlas는 linear data로 유지한다.
- framebuffer 전환 뒤 `GL_FRAMEBUFFER_SRGB` 상태가 누출되지 않도록 render pass가 소유한다.

---

## 8. 구현 마일스톤과 체크리스트

각 마일스톤은 독립적으로 merge 가능한 회귀 테스트를 가져야 한다. 단, P1 진행률은 아래 하위 마일스톤 완료 수가 아니라 최종 E2E gate로 판정한다.

### M0. 계획·기준선 고정

- [x] 상세 P1 계획 문서를 작성한다.
- [x] 현재 Debug/Release/ASan/UBSan 및 packaging smoke 기준선을 기록한다.
- [x] 기존 scene/prefab/catalog v1 fixture를 보존용 golden input으로 고정한다.
- [x] 신규 파일과 component/importer 등록 위치를 CMake 목록에 반영할 순서를 정한다.
- [x] 공통 고충돌 파일의 작업 소유권을 정한다: AssetDatabase, RenderQueue, SpriteRenderer, Inspector, GameBuilder.

완료 조건:

- 구현 전 기준선이 재현 가능하고 기존 dirty worktree를 덮어쓰지 않는다.

### M1. 공통 asset 기반

- [x] `AssetMeta`가 raw JSON과 importer settings를 보존하도록 확장한다.
- [x] v1→v2 texture/audio meta migration을 구현한다.
- [x] `.meta`와 catalog 원자 저장 helper를 추가한다.
- [x] hard-coded importer if chain을 `ImporterRegistry`로 교체한다.
- [x] importer settings 입력과 `ImportResult` dependencies, generic metadata, error를 추가한다.
- [x] scan/reimport를 prepare→commit transaction으로 바꾼다.
- [x] duplicate GUID/source path를 탐지한다.
- [x] catalog v2 writer와 v1/v2 reader를 구현한다.
- [x] scene/prefab 및 신규 asset importer의 직접 dependency 추출을 구현한다.
- [x] missing/type mismatch/cycle을 찾는 재귀 dependency validator를 구현한다.
- [x] `GameBuilder`가 staging 복사 전에 validator를 호출하도록 연결한다.
- [x] Project Browser asset selection과 Inspector asset target을 연결한다.
- [x] `AssetContentCommand`와 명시적 reimport/Undo/Redo를 구현한다.
- [x] watcher가 source 변경과 meta 변경을 구분하고 필요한 reimport를 한 번만 실행한다.

테스트:

- [x] meta v1→v2 GUID 안정성
- [x] root/settings unknown-key roundtrip
- [x] atomic write 실패 시 원본 보존
- [x] registry extension/name 선택과 importer version migration
- [x] catalog v1 load, v2 roundtrip, deterministic ordering
- [x] duplicate GUID/source path 거부
- [x] dependency missing/type mismatch/cycle과 전체 chain 메시지
- [x] asset content Execute/Undo/Redo와 reimport 횟수

완료 조건:

- 후속 네 기능의 GUID asset을 같은 registry/catalog/validator에 추가할 수 있고 v1 package가 계속 로드된다.

### M2. 공통 multi-quad 렌더 기반

- [x] immutable `QuadGeometry`와 optional view bounds를 `RenderCommand`에 추가한다.
- [x] 기존 single quad command 호환 경로를 유지한다.
- [x] `SpriteBatcher`에 multi-quad append와 2,048 경계 flush를 구현한다.
- [x] `RenderSystem2D`가 camera view bounds로 command를 cull한다.
- [x] batch stats가 command 수, submitted quad 수, culled command/chunk 수, draw call을 구분한다.
- [x] SpriteRenderer, TextRenderer, UI의 기존 vertex 방향과 결과를 회귀 고정한다.
- [x] fallback command 전후 flush와 sort semantics를 보존한다.

테스트:

- [x] single/multi command 값 수명과 queue copy
- [x] 0, 1, 2,047, 2,048, 2,049 quad 경계
- [x] BatchKey 변경과 fallback 사이 flush
- [x] view bounds inside/intersect/outside
- [x] 기존 SpriteRenderer/Text/UI UV·색·정렬 회귀

완료 조건:

- tile/particle이 직접 GL draw 없이 geometry command만 제출할 수 있다.

### M3. 텍스처 설정·SpriteRef·안전한 재임포트

- [x] `TextureImportSettings`, slice, `SpriteRef`, `ResolvedSprite` 타입을 구현한다.
- [x] texture importer v2 기본값과 legacy migration을 구현한다.
- [x] Nearest/Linear, U/V wrap, mipmap, LegacyLinear/SRGB upload를 연결한다.
- [x] stable slice ID를 보존하는 grid/manual reslice 알고리즘을 구현한다.
- [x] pixel rect→UV, pivot, PPU/native size resolver를 구현한다.
- [x] `Texture` 내부 handle 교체와 TextureManager GUID/revision 기반 reimport를 구현한다.
- [x] 실패한 decode/upload가 last-good와 pointer address를 보존하도록 한다.
- [x] SpriteRenderer에 authored SpriteRef/runtime override와 Native/Custom size를 추가한다.
- [x] legacy textureGuid/path serialization과 기존 top-left/custom pixel size를 유지한다.
- [x] 새 sprite 생성 command가 PPU/pivot/native size를 적용한다.
- [x] Texture asset Inspector, preview, grid/manual slicing, Apply/Revert/Reimport를 구현한다.
- [x] Scene View FBO와 runtime final framebuffer 색 공간을 연결한다.

테스트:

- [x] PNG/JPEG UV 방향과 full/sliced rect
- [x] grid reslice ID 유지, 추가, 삭제, rename
- [x] pivot corner/center와 PPU/native size
- [x] legacy scene 크기/top-left 불변
- [x] filter/wrap/mipmap GL 설정 contract
- [x] SRGB/LegacyLinear internal format과 framebuffer state
- [x] 성공 reimport의 pointer 안정성/revision 증가
- [x] 실패 reimport의 handle/dimension/revision/last-good 보존
- [x] scene/prefab/Play clone SpriteRef roundtrip

완료 조건:

- point-filtered spritesheet를 editor에서 slice하고 GUID+slice ID만으로 패키지 런타임에서 동일하게 해석한다.

### M4. AnimationClip2D·Animator2D FSM

- [x] `.animclip`과 `.animator` importer/serializer/validator를 등록한다.
- [x] per-frame duration을 처리하는 clip player를 구현한다.
- [x] typed parameter storage와 state/transition FSM을 구현한다.
- [x] transition 저장 순서, exit time, Trigger 선택 소비 semantics를 구현한다.
- [x] Animator 전용 phase를 Update 이후, LateUpdate 이전에 추가한다.
- [x] SpriteRenderer runtime override를 적용하고 Stop/disable에서 authored sprite로 복구한다.
- [x] 공개 Play/Pause/Resume/Stop/parameter/query/speed API를 노출한다.
- [x] controller/clip/slice/renderer 누락과 잘못된 duration을 안전하게 처리한다.
- [x] scene/prefab serializer와 component factory에 Animator2D를 등록한다.
- [x] Animation Window clip scrub/preview와 controller 표 편집을 구현한다.

테스트:

- [x] 서로 다른 frame duration과 정확한 boundary
- [x] 큰 `dt`의 multi-frame/loop 소비
- [x] loop/non-loop 종료 상태
- [x] Bool/Int/Float/Trigger 조건과 type mismatch
- [x] exit time 전후
- [x] 여러 transition 중 첫 항목 하나만 선택
- [x] 선택된 transition Trigger만 소비
- [x] Play/Stop/Pause/Resume/speed/normalized time
- [x] authored sprite 복구
- [x] 누락 controller/clip/slice/SpriteRenderer 안전성
- [x] scene/prefab/Play clone roundtrip

완료 조건:

- idle/run/jump controller가 스크립트 parameter 입력으로 결정적으로 전환되고 편집 상태를 오염시키지 않는다.

### M5. TileSet·Tilemap v2·Tile Palette

- [x] `.tileset` importer/serializer와 SpriteRef dependencies를 구현한다.
- [x] stable tile/layer ID, layer property, row-major RLE를 구현한다.
- [x] resize/world-cell/layer/cell/batch edit 공개 API를 구현한다.
- [x] NESW 16 mask terrain resolver와 5-cell neighborhood 갱신을 구현한다.
- [x] 32×32 chunk dirty tracking과 immutable geometry cache를 구현한다.
- [x] camera bounds와 layer visibility로 visible chunk만 제출한다.
- [x] layer collision revision과 horizontal solid-run collider 갱신을 구현한다.
- [x] pencil/erase/rectangle/flood fill/eyedropper Tile Palette를 구현한다.
- [x] sparse before/after `TilePaintCommand`로 한 drag를 하나의 Undo step으로 만든다.
- [x] locked layer, grid, active layer, ghost preview를 구현한다.
- [x] transform 계약 경고와 painting 차단을 구현한다.
- [x] legacy path 기반 단층 tilemap 로드/렌더를 유지한다.
- [x] 명시적 `Create TileSet & Convert`와 Undo를 구현한다.

테스트:

- [x] layer stable ID/order/property와 RLE roundtrip
- [x] malformed RLE 거부
- [x] 16개 NESW mask와 경계 cell
- [x] terrain 변경 시 다섯 cell 외 불변
- [x] pencil/erase/rectangle/flood fill 한 stroke Undo/Redo
- [x] locked layer mutation 차단
- [x] 32×32 chunk boundary와 dirty chunk 수
- [x] 128×64 map view culling과 visible chunk 제출 수
- [x] 2,048 batch 경계와 draw call 상한
- [x] collision revision별 horizontal run 갱신
- [x] legacy load/render 및 explicit conversion Undo
- [x] 회전/비균일/음수 scale paint 차단

완료 조건:

- 배경·충돌·전경 3-layer 스테이지를 palette로 저작하고 수정된 보이는 chunk만 재생성·제출한다.

### M6. ParticleSystem schema v2와 edit preview

- [x] `FloatCurve`, `ColorGradient`, SpriteRef frame 목록을 구현한다.
- [x] legacy start/end 값을 0/1 key로 migration한다.
- [x] emitter duration과 particle lifetime을 분리한다.
- [x] local/world simulation space를 구현한다.
- [x] emitter별 seed와 deterministic RNG를 구현한다.
- [x] active/free pool로 spawn/update/death를 정리한다.
- [x] Start/Random/OverLife frame mode를 구현한다.
- [x] Alpha/Additive multi-quad command를 제출한다.
- [x] Play/Pause/Stop/Clear/Emit semantics를 정리한다.
- [x] 직렬화와 분리된 edit preview runtime copy를 구현한다.

테스트:

- [x] legacy schema migration과 roundtrip
- [x] 같은 seed/call sequence의 동일 결과
- [x] curve/gradient edge/interpolation
- [x] duration 종료 뒤 기존 particle 생존
- [x] Pause/Stop/Clear/Emit 상태
- [x] local/world emitter 이동 차이
- [x] atlas slice UV와 frame mode
- [x] Alpha/Additive BatchKey
- [x] 2,048 quad 경계
- [x] edit preview가 serialized JSON/scene dirty/Undo를 바꾸지 않음

완료 조건:

- 캐릭터 jump/landing 이벤트에서 재현 가능한 textured burst가 배칭되어 렌더된다.

### M7. AudioService·고정 mixer·fade

- [x] 기존 전역 재생과 `AudioSource` 소유권을 단일 `AudioService`로 통합한다.
- [x] generation 검증 `VoiceHandle` slot pool을 구현한다.
- [x] `Master/{Music,SFX,Voice,UI}` routing과 ProjectSettings 저장을 구현한다.
- [x] engine master gain을 1로 유지하고 Master group에서 한 번만 적용한다.
- [x] AudioImporter가 실제 decoder header를 열어 손상 파일을 거부한다.
- [x] duration/channel/sample rate와 DecodeOnLoad/Streaming을 catalog/meta에 연결한다.
- [x] GUID 기반 one-shot/music/crossfade/stop/bus/fade API를 구현한다.
- [x] `AudioSource` output bus 직렬화와 legacy/new 기본값을 구현한다.
- [x] no-device backend에서 동일 state machine을 실행한다.
- [x] disable/scene unload/Play Stop/shutdown 해제 순서를 고정한다.

테스트:

- [x] 유효/손상 WAV fixture와 header metadata
- [x] DecodeOnLoad/Streaming meta/catalog roundtrip
- [x] no-device one-shot 중첩과 stale handle
- [x] 각 bus routing, Master 단일 적용, mute/volume
- [x] bus fade의 시작/중간/끝
- [x] music crossfade의 두 voice gain과 완료 후 old voice 해제
- [x] 새 music load 실패 시 기존 music 유지
- [x] AudioSource legacy Master/new SFX 기본값
- [x] disable/scene unload/Play Stop/shutdown 후 live voice 0

완료 조건:

- 스테이지 BGM과 효과음이 독립 bus로 재생되고 전환 시 누수 없이 crossfade된다.

### M8. 통합 캐릭터 스테이지와 문서 완료

- [x] P1 fixture project에 point-filtered spritesheet와 stable slices를 만든다.
- [x] idle/run/jump clip과 Animator controller를 연결한다.
- [x] 배경·충돌·전경 Tilemap v2와 terrain을 저작한다.
- [x] 캐릭터 physics/state parameter와 particle burst를 연결한다.
- [x] Music/SFX bus와 두 BGM crossfade를 연결한다.
- [x] scene/prefab→asset dependency closure를 패키징 전에 검증한다.
- [x] 패키지 runtime에서 시각·애니메이션·collision·particle·audio proof counter를 검증한다.
- [x] 기존 P0 smoke와 legacy fixtures가 그대로 통과하는지 확인한다.
- [x] 관련 subsystem 문서의 오래된 상태를 실제 구현과 일치하도록 고친다.
- [x] 모든 gate 통과 후에만 갭 분석 P1을 5/5로 갱신한다.

완료 조건:

- 아래 10절의 최종 완료 게이트를 모두 통과한다.

---

## 9. 테스트 계획

### 9.1 테스트 계층

| 계층 | 목적 | 예시 |
|---|---|---|
| 순수 unit | JSON/schema, FSM, terrain, curve, RNG, fade 수학 | GL/audio device 없이 실행 |
| component roundtrip | scene/prefab/Play clone 직렬화 | `World`, `SceneSerializer` |
| headless contract | GL batch counter, no-device audio | hidden context/backend |
| editor command | asset/tile Undo, preview 격리 | command history/dirty marker |
| packaging integration | dependency closure와 catalog/runtime 해석 | fixture project build |
| process E2E | 패키지 executable proof | smoke report/counters |

### 9.2 실제 회귀 테스트 배치

기존 suite의 호환 fixture를 유지하면서 관련 계약을 다음 파일에 통합했다.

- 공통 asset/meta/catalog/importer: `tests/test_asset_meta.cpp`, `test_asset_database.cpp`, `test_asset_catalog.cpp`, `test_importer.cpp`
- Animator: `tests/test_animation.cpp`
- TileSet/Tilemap v2/paint/chunk/collision: `tests/test_tilemap.cpp`
- Particle v2: `tests/test_particle.cpp`
- AudioService/importer/bus/fade/lifetime: `tests/test_audio.cpp`
- 패키지 script host API: `tests/test_runtime_script_package_loader.cpp`, `tests/dummy_valid_lib.cpp`
- 통합 stage: `tests/smoke/create_fixture.cmake`, `tests/smoke/run_end_to_end.cmake`

### 9.3 성능 회귀 기준

벽시계 시간은 CI 편차가 크므로 P1 완료 판정에 사용하지 않는다. deterministic counter를 사용한다.

- 128×64 tilemap은 camera에 보이는 32×32 chunk만 command로 제출한다.
- 수정되지 않은 chunk의 geometry revision은 증가하지 않는다.
- 동일 texture/order/blend의 `quadCount`개는 draw call이 `ceil(quadCount / 2048)`를 넘지 않는다.
- particle 2,049개는 동일 BatchKey일 때 정확히 두 batch 이내다.
- culled chunk/particle command는 uploaded vertex와 submitted sprite counter를 늘리지 않는다.
- collision은 변경된 collision-enabled layer revision에 대해서만 rebuild한다.

### 9.4 호환성 회귀

- catalog v1 package load
- textureGuid/texturePath legacy SpriteRenderer
- 기존 SpriteRenderer 32×32 default와 texture pixel auto-size 동작
- legacy single-layer TilemapRenderer
- legacy ParticleSystem start/end serialization
- bus 필드 없는 AudioSource
- `Rendering::Animation`, `SpriteSheet` 빌드 및 기존 테스트
- 기존 TextRenderer/UI single-quad batch와 font atlas linear sampling

### 9.5 검증 명령

개별 개발 단계:

```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```

최종 gate:

```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure

cmake --build --preset release -j4
ctest --preset release --output-on-failure

cmake --build --preset asan -j4
ctest --preset asan --output-on-failure

cmake --build --preset ubsan -j4
ctest --preset ubsan --output-on-failure

git diff --check
```

저장소 preset 이름이나 CI 명령이 변경되면 같은 의미의 공식 명령으로 대체하고 결과를 문서에 기록한다.

---

## 10. 최종 완료 게이트

P1은 다음 항목을 **전부** 만족하기 전에는 완료가 아니다.

### 데이터와 호환성

- [x] meta v1→v2에서 GUID와 기존 화면 결과가 보존된다.
- [x] `.meta` unknown key가 편집·migration·reimport 뒤에도 보존된다.
- [x] catalog v1 reader와 v2 roundtrip이 통과한다.
- [x] 기존 scene/prefab/path API와 legacy Tilemap/Particle/AudioSource가 읽힌다.

### 에셋과 패키징

- [x] dependency missing/type mismatch/cycle이 packaging 전에 실패한다.
- [x] 오류가 root부터 실패 edge까지 참조 chain을 제공한다.
- [x] import 실패 asset이 dependency closure에 있으면 빌드가 실패한다.
- [x] package 안의 catalog/settings/dependencies가 runtime에서 동일하게 해석된다.

### 런타임 기능

- [x] idle/run/jump Animator FSM이 typed parameter와 transition priority 계약을 만족한다.
- [x] 3-layer tilemap terrain/collision/render가 일치한다.
- [x] textured particle burst가 같은 seed로 재현된다.
- [x] Music/SFX routing, bus fade, BGM crossfade가 동작한다.
- [x] scene unload와 shutdown 뒤 live audio voice가 0이다.

### 렌더와 리소스 안전성

- [x] 실패한 texture reimport가 pointer와 last-good GPU resource를 보존한다.
- [x] SRGB/LegacyLinear와 Scene View/runtime framebuffer 결과가 테스트 계약을 만족한다.
- [x] visible chunk culling과 2,048 quad draw-call 상한을 만족한다.
- [x] ASan/UBSan에서 queue geometry, preview, voice lifetime 오류가 없다.

### 에디터 저작

- [x] Texture slicing, Animation Window, Tile Palette, Particle preview, Audio settings가 Project Browser selection에서 접근 가능하다.
- [x] asset/meta 편집과 한 tile stroke가 Undo/Redo된다.
- [x] preview가 scene dirty나 serialized state를 오염시키지 않는다.
- [x] 잘못된 tilemap transform에서는 경고와 painting 차단이 동작한다.

### 전체 검증과 상태 갱신

- [x] Debug 전체 CTest 통과
- [x] Release 전체 CTest 통과
- [x] ASan 전체 CTest 통과
- [x] UBSan 전체 CTest 통과
- [x] 모든 preset의 packaging smoke 통과
- [x] `git diff --check` 통과
- [x] 패키징된 P1 캐릭터 스테이지 E2E 통과
- [x] 관련 subsystem 문서 상태 정정
- [x] 마지막 단계에서 갭 분석 P1 상태를 0/5에서 5/5로 갱신

---

## 11. 병렬화와 통합 순서

안전한 병렬 작업을 위해 다음 barrier를 지킨다.

1. **M1 공통 asset 계약을 먼저 통합한다.** importer registration, meta/catalog v2, dependency API가 이후 모든 asset 형식의 기준이다.
2. **M2 렌더 계약을 먼저 통합한다.** tile/particle은 공통 multi-quad API가 확정된 뒤 fallback 제거를 진행한다.
3. **M3 SpriteRef/SpriteRenderer 계약을 먼저 확정한다.** Animator는 authored/runtime override API 위에 구현한다.
4. M3 기반이 안정되면 M4 Animator, M5 Tilemap, M6 Particle을 병렬 진행할 수 있다.
5. M7 Audio는 M1 catalog/settings가 통합된 뒤 렌더 작업과 독립적으로 진행할 수 있다.
6. M8은 모든 기능 branch를 단순 합치는 단계가 아니라 dependency closure, scene lifecycle, render/audio counters를 함께 검증하는 통합 단계다.

고충돌 파일과 통합 소유권:

| 파일/영역 | 충돌 가능 작업 | 통합 원칙 |
|---|---|---|
| `AssetMeta`, `AssetDatabase`, `Importer.h` | texture, animation, tile, audio | M1 소유자가 공통 계약 통합 |
| `RenderQueue`, `SpriteBatcher`, `RenderSystem2D` | tile, particle, text/UI 회귀 | M2 소유자가 호환 API 유지 |
| `SpriteRenderer` | texture/pivot, Animator override | M3 API 통합 후 Animator 연결 |
| `Texture`, `TextureManager` | sprite, tile, particle, UI/Material | raw pointer 호환과 revision을 한 구현에서 관리 |
| `InspectorWindow`, `ProjectBrowserWindow` | 모든 asset editor | asset target shell을 먼저 만들고 기능별 panel 등록 |
| `GameBuilder`, smoke fixture | validator, audio, 모든 E2E proof | M8에서 단일 통합 소유자가 정리 |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | 모든 신규 파일/테스트 | 기능별 작은 추가 후 최종 정렬 |

---

## 12. 구현 중 결정 기록

다음 항목은 구현 도중 임의로 흩어져 결정하지 말고 이 문서 또는 인접 설계 문서에 기록한다.

- slice ID 생성 형식과 reslice matching 우선순위
- float transition equality epsilon
- missing terrain mask fallback의 정확한 진단/렌더 정책
- multi-texture particle draw-order 정책
- particle `Stop` 시 살아 있는 particle 처리
- fade interpolation이 linear gain인지 perceptual/dB인지 여부(P1 기본은 linear gain)
- Streaming audio의 seek/loop 제약
- Scene View FBO와 default framebuffer별 sRGB state table
- asset command 실패를 `CommandHistory`에 push하지 않는 인터페이스 변경 방식

결정 변경은 해당 unit test와 migration/호환성 항목을 함께 갱신해야 한다.

---

## 13. 상태 보고 규칙

- 하위 기능이 개별 통과하면 “P1 texture 기반 완료”, “P1 Animator 기반 완료”처럼 부분 상태로 기록한다.
- 다섯 기능 중 일부가 구현됐다는 이유로 갭 분석의 P1 숫자를 올리지 않는다.
- E2E 이전에는 `0/5 (구현 중)` 또는 동등한 표현을 유지한다.
- 최종 gate의 실제 명령, 통과 수, sanitizer와 packaging 결과를 기록한 뒤 한 번에 `5/5 완료`로 갱신한다.
- 제외 범위는 완료 보고에서 구현된 것처럼 표현하지 않는다.

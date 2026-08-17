# 02. 사운드 (Audio) 구현 계획

> 작성일: 2026-06-14
> 상태 갱신: 2026-07-16 — `AudioSource` 통합과 P1 고정 bus/fade까지 완료
> 범위: `AudioSource`/`AudioListener` ECS 컴포넌트, 직렬화, 런타임 재생, (이후) 공간 음향
> 관련 문서: [`00_overview.md`](00_overview.md)

> **현재 구현 기준:** `AudioService`가 generation-safe voice를 소유하고 `Master/{Music,SFX,Voice,UI}` bus,
> GUID one-shot/music/crossfade, bus fade, importer metadata와 no-device 검증을 제공한다. 아래 본문은 2026-06-14의
> 최초 계획 기록이며, 현재 계약과 잔여 범위는 [P1 구현 계획](../2026-07-16_game_production_p1_plan.md)을 따른다.

---

## 1. 최초 상태 (2026-06-14 코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 전역 오디오 API | `src/Systems/Audio.h` (static 클래스) | ✅ 동작 |
| 효과음 | `Audio::LoadSound/PlaySound/StopSound` | ✅ |
| 음악(스트리밍) | `Audio::LoadMusic/PlayMusic/Pause/Resume/SetMusicVolume` | ✅ |
| 마스터 볼륨 | `Audio::SetMasterVolume/GetMasterVolume` | ✅ |
| 백엔드 | `external/miniaudio` (`ma_engine`, `ma_sound`, 커스텀 deleter) | ✅ |

**핵심 격차:**
- 전부 **전역 static API**다. ECS 컴포넌트가 없어 **씬 데이터로 저장·편집할 수 없다.**
- 오브젝트별 사운드(발소리, 폭발음)를 에디터에서 붙일 방법이 없다.
- **공간 음향(거리 감쇠, 패닝) 없음** — miniaudio는 지원하나 래핑되지 않음.
- 믹서 그룹/버스, 사운드 에셋 참조(경로 기반만) 없음.
- `main.cpp:14`에서 include되지만 컴포넌트 기반 사용 흐름 없음.

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `AudioSource`를 붙이고 사운드 파일을 지정 → playOnAwake/loop/volume/pitch 설정.
- 스크립트에서 `audioSource->Play()` 호출 또는 충돌 이벤트에 반응해 재생.
- 씬에 `AudioListener`(보통 카메라)를 두고 거리 기반 음량을 얻는다(2차).
- 저장/로드/빌드에서 사운드 참조와 설정이 보존된다.

---

## 3. 설계

### 3.1 신규 타입

```
AudioClip (참조)  — 경로 기반 핸들. 1차에는 string path + Audio 캐시 키로 충분.
                    (이후 AssetDatabase GUID로 승격)

AudioSource : Component (src/ECS/Components/AudioSource.{h,cpp})
  - clipPath(string), volume, pitch, loop, playOnAwake, spatial(bool)
  - maxDistance, minDistance (공간 음향, 2차)
  - Play() / Stop() / Pause() / IsPlaying()
  - OnEnable: playOnAwake면 Play
  - Update(dt): spatial이면 listener 거리로 음량 계산 후 적용
  - OnDestroy: 재생 중지/핸들 해제
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/ResolveAssets/OnInspectorGUI

AudioListener : Component (src/ECS/Components/AudioListener.{h,cpp})
  - 씬에 1개(보통 메인 카메라). 위치를 공간 음향 계산 기준으로 제공.
  - 여러 개면 경고 후 첫 번째 사용.
```

### 3.2 `Audio` 정적 API와의 관계

- 1차에는 `AudioSource`가 내부적으로 **기존 `Audio` static API를 호출**(`LoadSound`+`PlaySound`).
  → 빠르게 동작. 단 인스턴스별 동시 재생/정밀 제어 한계.
- 2차에 miniaudio `ma_sound`를 **`AudioSource` 인스턴스마다 보유**하도록 확장
  → 개별 pitch/pan/3D, 동시 다중 재생. `Audio`는 `ma_engine` 소유 + 믹서로 역할 축소.

### 3.3 통합 지점

- `ResolveAssets()`에서 `Audio::LoadSound(clipPath, clipPath)` 지연 로드(GL과 무관하나 동일 훅 재사용).
- `Update(dt)`는 `World::Update`(`src/Core/World.h:27`)가 모든 컴포넌트에 자동 호출.
- 공간 음향용 listener 위치는 `World`에서 `AudioListener`를 찾아 `PhysicsWorld`처럼 프레임당 1회 캐시.
- **Play/Stop(에디터) 전환 시:** Stop 시 모든 `AudioSource` 정지 필요 → World 폐기 경로에 훅.

### 3.4 직렬화

- `AudioSource`: clipPath, volume, pitch, loop, playOnAwake, spatial, min/maxDistance
- `REGISTER_COMPONENT(AudioSource)`, `REGISTER_COMPONENT(AudioListener)` 필수

### 3.5 에디터

- `AudioSource` Inspector: 파일 선택(또는 경로 입력), volume/pitch 슬라이더, loop/playOnAwake 체크,
  spatial 토글 + min/maxDistance, ▶ 미리듣기 버튼
- Project Browser에서 `.wav/.mp3/.ogg` 더블클릭/드래그 → `AudioSource.clipPath` 지정(드래그&드롭은 이후)

---

## 4. 최초 작업 체크리스트 (역사적 기록)

**1차: 컴포넌트화 (전역 API 위에)**
- [ ] `AudioSource` 컴포넌트 + 등록 + 직렬화 + `ResolveAssets`(로드) + Inspector
- [ ] playOnAwake / loop / volume 적용 (기존 `Audio::PlaySound` 경유)
- [ ] 스크립트에서 `Play()/Stop()` 호출 경로
- [ ] Play→Stop 전환 시 전체 사운드 정지
- [ ] 단위/스모크 테스트: 직렬화 라운드트립, playOnAwake 동작

**2차: 인스턴스 사운드 + 공간 음향**
- [ ] `AudioSource`별 `ma_sound` 보유로 전환(개별 pitch/pan)
- [ ] `AudioListener` 컴포넌트 + 거리 감쇠/패닝
- [ ] 동시 다중 재생 검증

**3차: 믹서(이후)**
- [ ] 믹서 그룹/버스(Master/SFX/Music) + 그룹 볼륨
- [ ] `AudioClip`을 AssetDatabase GUID로 승격

---

## 5. 최초 완료 기준 (역사적 기록)

- [ ] 오브젝트에 `AudioSource`를 붙이고 클립을 지정하면 Play에서 소리가 난다.
- [ ] playOnAwake/loop/volume가 저장·로드·빌드에서 보존된다.
- [ ] 충돌 이벤트나 스크립트에서 효과음을 재생할 수 있다.
- [ ] Stop 시 모든 재생이 멈추고, 빌드 런타임에서 동일하게 동작한다.

---

## 6. 의존성·위험·결정 필요

- **결정:** 1차를 "전역 API 위 얇은 컴포넌트"로 빠르게 갈지, 처음부터 인스턴스 `ma_sound`로 갈지.
- **위험:** 다수 동시 효과음은 전역 API 모델에서 충돌 가능 → 2차 전환 시점을 게임 요구로 판단.
- **연동(선택):** 충돌음은 [`01_physics.md`](01_physics.md) 이벤트에, 발소리는 [`08_input_actions.md`](08_input_actions.md)에 연동 가능.

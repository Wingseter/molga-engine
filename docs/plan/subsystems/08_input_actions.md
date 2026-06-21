# 08. 입력 액션 맵 (Input Action Map) 구현 계획

> 작성일: 2026-06-14
> 범위: 액션 기반 입력 추상화, 바인딩 리맵, 게임패드, ImGui 캡처 분리
> 관련 문서: [`00_overview.md`](00_overview.md)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 입력 시스템 | `src/Systems/Input.h` (static, 키/마우스/스크롤, GetKey/Down/Up) | ✅ 동작 |
| 프레임 갱신 | `Input::Update()` (`src/main.cpp:219`에서 호출) | ✅ |
| 스크롤 | `Input::GetScrollX/Y` + GLFW 콜백 | ✅ |

**핵심 격차:**
- 게임 코드가 **GLFW 키 코드를 직접 사용**(`GetKey(GLFW_KEY_SPACE)`). 리바인딩 불가.
- **액션 맵 없음** — "Jump"/"Fire" 같은 의미 단위가 없어, 키 변경 시 코드 전역 수정 필요.
- **게임패드 미지원**(GLFW joystick 미사용), 데드존/축 없음.
- **ImGui 캡처 분리 없음** — 텍스트 입력 중에도 게임 입력이 먹힐 수 있음(gap analysis §4.5).
- 스크롤 소비 타이밍 이슈 가능성(gap analysis §4.5: `glfwPollEvents` 위치).

> **우선순위 주의:** 기존 `Input`만으로도 게임 동작은 가능하다. 액션 맵은 **편의·리바인딩·게임패드**를
> 위한 것으로, 다른 서브시스템보다 우선순위가 낮다([`00_overview.md`](00_overview.md) §4 순서 9).

---

## 2. 목표 (완료의 정의)

- "Jump", "MoveX", "Fire" 같은 **액션**을 정의하고 키/버튼/축을 바인딩한다.
- 게임 코드는 `Input::GetAction("Jump")` / `GetAxis("MoveX")`로 키 코드와 무관하게 동작.
- 키보드와 게임패드를 같은 액션에 바인딩(디바이스 추상화).
- 바인딩을 설정 파일로 저장/로드(리맵).
- ImGui가 입력을 캡처 중이면 게임 입력을 차단.

---

## 3. 설계

### 3.1 신규 타입

```
InputAction
  - name(string), type { Button, Axis }
  - bindings: 키/마우스버튼/게임패드버튼·축 목록(positive/negative 키 페어로 축 합성)

InputActionMap (src/Systems/InputActions.{h,cpp})
  - Register(action) / LoadFromFile / SaveToFile
  - Update(): 디바이스 상태 폴 → 액션 값/엣지 계산
  - GetAction(name) bool / GetActionDown / GetActionUp
  - GetAxis(name) float (데드존 적용)

기존 Input은 "raw 디바이스 레이어"로 유지. InputActionMap이 그 위에서 의미 부여.
게임패드는 Input에 glfwGetGamepadState 래핑 추가.
```

### 3.2 통합 지점

- `Input::Update()` 직후 `InputActionMap::Update()` 호출(`src/main.cpp:219` 인근).
- **ImGui 캡처 분리:** `ImGui::GetIO().WantCaptureKeyboard/Mouse`를 확인해 게임 입력 게이팅
  (에디터/런타임 공통 헬퍼). 텍스트 필드 포커스 시 게임 입력 무시.
- 기본 액션 맵 에셋을 프로젝트에 두고 런타임이 로드.

### 3.3 직렬화

- 액션·바인딩을 **프로젝트 설정/별도 에셋**(`input.json` 등)으로 저장. (씬 컴포넌트가 아님)
- 게임패드 매핑은 GLFW 표준 게임패드 DB 사용.

### 3.4 에디터

- (이후) "Input Settings" 창: 액션 추가, 바인딩 편집, "키 누르기 대기" 리바인딩 UI
  ([`docs/design/03_editor.md`](../../design/03_editor.md) C9 환경설정과 연계)

---

## 4. 작업 체크리스트

**1차: 액션 맵(키보드)**
- [ ] `InputAction`/`InputActionMap` + Button/Axis
- [ ] 기본 액션 세트(Move/Jump/Fire) + 코드에서 `GetAction/GetAxis` 사용 경로
- [ ] 바인딩 JSON 로드/세이브
- [ ] `Input::Update` 직후 액션 갱신 통합

**2차: 게임패드 + ImGui 분리**
- [ ] `Input`에 `glfwGetGamepadState` 래핑 + 데드존
- [ ] 게임패드 버튼/축을 액션에 바인딩
- [ ] ImGui `WantCaptureKeyboard/Mouse` 게이팅
- [ ] 스크롤 소비 타이밍 이슈 점검(gap analysis §4.5)

**3차: 리바인딩 UI(이후)**
- [ ] Input Settings 창 + "키 대기" 리맵
- [ ] 다중 컨트롤 스킴/플레이어

---

## 5. 완료 기준

- [ ] 게임 코드가 키 코드 없이 `GetAction("Jump")`/`GetAxis("MoveX")`로 동작.
- [ ] 키보드·게임패드가 같은 액션을 구동.
- [ ] 바인딩을 파일로 저장/로드해 리맵이 유지된다.
- [ ] 에디터 텍스트 입력 중 게임 입력이 차단된다.

---

## 6. 의존성·위험·결정 필요

- **우선순위:** 게임 동작 자체엔 필수 아님 → 다른 서브시스템 이후 권장.
- **위험:** 게임패드 디바이스/플랫폼 편차 — GLFW 표준 매핑으로 1차 한정.
- **연동:** 리바인딩 UI는 환경설정/프로젝트 설정(에디터 C9)과 함께.

# Molga Engine 리팩토링 계획서 (Refactoring Plan)

## 1. 개요 (Overview)
현재 **Molga Engine**은 C++17과 OpenGL 기반으로 만들어진 2D 게임 엔진입니다. 초기 아키텍처는 기능 구현에 초점을 맞추어 빠르게 개발되었으나, 장기적인 확장성과 유지보수성을 위해(특히 에디터와 런타임의 분리 및 확장) 다음과 같은 핵심적인 구조적 개선(Refactoring)이 필요합니다.

---

## 2. 주요 문제점 및 개선 방향

### 2.1. 과도한 싱글톤(Singleton) 사용 (전역 상태 문제)
- **문제점:**
  현재 `Application`, `Project`, `GameBuilder`, `TextureManager`, `TextRenderer`, `ScriptManager` 등 엔진의 핵심 시스템/매니저 클래스들이 대부분 `static Instance::Get()` 형태의 싱글톤 패턴을 취하고 있습니다. 
  이는 클래스 간의 숨겨진 의존성을 만들며, 순환 참조를 야기할 수 있고 멀티스레드 환경이나 유닛 테스트 시 큰 제약으로 작용합니다.
- **해결 방안:**
  - **Engine Context / Service Locator 생성:** 전역 싱글톤 대신 엔진의 모든 하위 시스템 포인터를 소유하고 관리하는 중앙 집중형 레지스트리(컨텍스트) 객체를 만듭니다.
  - **Dependency Injection (의존성 주입):** 서브 시스템 간 참조가 필요할 때 명시적으로 컨텍스트나 타겟 매니저의 포인터를 주입받아 사용하도록 변경합니다.

### 2.2. 단일 책임 원칙(SRP) 위배 (Component 구조)
- **문제점:**
  `Component.h`를 살펴보면 순수 게임 상태 처리(`Update`, `Render`), 직렬화/역직렬화(`Serialize`, `Deserialize`), 그리고 에디터 UI 렌더링(`OnInspectorGUI`)이 하나의 클래스에 강하게 결합(Tight Coupling)되어 있습니다.
  이는 컴포넌트의 역할이 너무 비대하며 에디터 UI 라이브러리(ImGui)와 JSON 라이브러리가 엔진 코어에 침투하는 문제를 낳습니다.
- **해결 방안:**
  - **데이터와 렌더링 로직 분리:** 컴포넌트 본체에서 `OnInspectorGUI()`를 제거하고, `Editor` 레이어 하위의 패널(`InspectorWindow` 등)에서 컴포넌트 타입을 구분해 UI를 그리도록 위임합니다.
  - **직렬화 로직 분리:** 가능하면 `SceneSerializer` 또는 `ComponentSerializer` 전용 유틸리티에서 직렬화를 전담합니다.

### 2.3. 에디터(Editor)와 런타임(Runtime)의 강한 결합
- **문제점:**
  `Application.cpp` 의 메인 루프 안에 ImGui 관련 에디터 메뉴 및 `Stats` 윈도우 렌더링 코드가 하드코딩되어 있습니다. 이렇게 되면 추후 배포용 런타임(Standalone Player) 빌드 시 에디터 코드를 말끔히 덜어내는 것이 매우 어렵습니다.
- **해결 방안:**
  - **Layer / LayerStack 아키텍처 도입:** `Application`은 단순히 레이어 스택을 순회하면서 메시지(Event), `Update()`, `Render()`를 전달하는 호스트 역할만 수행하도록 순화(Purify)합니다.
  - 이를 통해 `EditorLayer` 와 런타임 용 `GameLayer`를 완전히 분리하여 빌드 타겟에 따라 컴포넌트를 조립할 수 있게 만듭니다.

### 2.4. 전통적 OOP 기반 ECS 모델의 한계
- **문제점:**
  현재 사용중인 `GameObject`와 `Component`는 유니티(Unity) 초기와 유사한 고전적 객체 지향적(OOP) 설계를 따릅니다. 이는 포인터 배열을 통해 힙 메모리 사방에 흩어져 있으므로 대량의 오브젝트 처리 시 CPU Cache Miss가 빈번하게 발생하여 구조적으로 느려질 수밖에 없습니다.
- **해결 방안:**
  - (선택 사항) **EnTT 같은 데이터 지향(Data-Oriented) ECS 라이브러리 도입:** 엔티티를 더이상 객체가 아닌 순수 ID(uint32_t)로 취급하고, 컴포넌트는 Contiguous Memory(연속된 배열)에 꽉 채워(Packed Array) 배치합니다. 시스템 로직은 별도의 클래스나 함수로 분리됩니다.

---

## 3. 단계별 리팩토링 로드맵

### Phase 1: 아키텍처 디커플링 (Layer 기반 분리)
1. `Layer` 인터페이스 및 `LayerStack` 시스템 도입.
2. `Application.cpp` 내의 하드코딩 된 ImGui 및 메인 메뉴 렌더링 코드를 별도의 `EditorLayer` 클래스로 추출.
3. `Application` 클래스의 역할을 순수 윈도우 생성, 이벤트 전달 및 Layer 스택 순회로 축소.

### Phase 2: 싱글톤 의존성 제거 및 Service Locator 구축
1. `EngineContext` (혹은 `Core`) 클래스를 설계하여 `TextureManager`, `ScriptManager`, `FontManager` 등을 래핑.
2. 각 클래스에서 `Manager::Get()`을 직접 호출하던 코드들을 `EngineContext`에서 받아오는 방식으로 변경.
3. `Project`와 `GameBuilder`는 코어 로직과 별도로 분리되는 Editor 패키지로 편입 고려.

### Phase 3: ECS 클래스의 SRP(Single Responsibility Principle) 보완
1. `Component` 클래스에서 가상 함수 `OnInspectorGUI()`를 제거.
2. `Editor/Windows/InspectorWindow.cpp`에서 Type Reflection 패턴이나 `dynamic_cast` 트리/방문자 패턴을 이용해 컴포넌트 별 UI 출력 코드를 한곳으로 밀집시킴.
3. 동일하게 `nlohmann::json` 을 직접 다루는 `Serialize` 함수를 외부 컴포넌트 특화 Serializer로 분리.

### Phase 4: Data-Oriented ECS 적용 (선택형 장기 마일스톤)
1. `EnTT` 라이브러리 연동 테스트.
2. `Transform`, `SpriteRenderer` 등의 데이터들을 EnTT 레지스트리 기반으로 재단.
3. System (PhysicsSystem, RenderSystem) 작성 후, 기존 `GameObject::Update()` 루프를 System 호출 기반으로 전환.

---

## 4. 기대 효과 (Impact)
* **모듈화 편의성:** 런타임 배포 시 에디터 로직(ImGui, Inspect)이 완전히 배제되어 바이너리 사이즈 감소 및 보안 향상.
* **유지보수성:** 싱글톤 의존성 지옥에서 해방되어 특정 시스템 단위 교체 및 Unit Test 구축이 매우 쉬워집니다.
* **성능 확장성:** Layer 도입으로 인한 렌더 패스 최적화 가능성, 장기적으로 EnTT 도입 시 대규모 유닛(총알 등) 연산에서 압도적 성능 이점을 취할 수 있습니다.

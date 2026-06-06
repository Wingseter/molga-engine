# Phase 6: 기반 인프라 구현 계획서 리뷰 및 개선 제안 (Gemini)

`docs/ongoing/phase6_implementation_plan.md` 문서를 분석하여 발견한 잠재적 문제점 및 성능/구조적 개선 아이디어를 정리했습니다. 전반적인 아키텍처는 훌륭하게 설계되어 있으나, 일부 기능에서 런타임 병목이나 안정성 문제가 존재할 수 있습니다.

---

## 🚨 1. 치명적 성능 병목 (Critical Performance Bottlenecks)

### 1-1. D2: JSON 직렬화를 이용한 `Clone()` 메커니즘
* **상황**: `GameObjectManager::Clone()`에서 기존 오브젝트를 JSON 문자열로 직렬화(Serialize)한 뒤, 무거운 파일 I/O 포맷인 JSON을 다시 파싱(Deserialize)하여 새 객체를 생성하는 방식을 제안하고 있습니다.
* **문제점**: 런타임(게임 도중)에 JSON 문자열 생성 및 파싱 연산은 **극심한 CPU 메모리 할당 및 파싱 오버헤드**를 유발합니다. 동적으로 총알을 복사하거나 풀에서 꺼낼 때 이 방식을 쓰면 프레임 드랍(Spike)이 심각하게 발생합니다. A2(오브젝트 풀링) 최적화의 효과를 사라지게 합니다.
* **해결 제안**:
  `Component`, `GameObject` 수준에 순수 C++ 메모리 복사 및 인스턴싱 기반의 `virtual Component* Clone()` 메서드를 구현하여 깊은 복사(Deep Copy)를 직접 수행해야 합니다. JSON 직렬화/역직렬화 복제는 에디터 모드 한정으로만 사용해야 안전합니다.

### 1-2. B1: Material의 매 프레임 `std::string` 문자열 비용
* **상황**: `Material` 클래스가 내부적으로 속성(property)을 `std::unordered_map<std::string, float>` 형태 등으로 보유하고 있고 반복문으로 `Apply()`에서 GPU에 전송합니다.
* **문제점**: 렌더링 파이프라인(매 프레임 여러 번 호출)에서 uniform location을 매번 문자열을 통해 해싱 룩업(hash lookup)하고 `glGetUniformLocation`을 호출하면 엄청난 병목이 생깁니다.
* **해결 제안**:
  `Apply()` 최적화를 위해 머티리얼 속성 세팅 시(또는 셰이더 컴파일 시) 해당 문자열 이름에 대한 Uniform Location(정수 ID)을 미리 캐싱해 두고, 렌더링 루프에서는 오직 O(1) 정수 배열 접근만 이뤄지도록 바꾸어야 합니다. `StringHash` 방식도 좋습니다.

---

## 🛠 2. 구조적 최적화 및 안정성 (Architectural Improvements)

### 2-1. A2: Object Pool의 O(n) 선형 탐색 불필요
* **상황**: 풀에서 반환할 객체를 찾기 위해 `std::vector<bool> active;` 를 전부 배열 순회(`O(n)`)하며 가용 여부를 체크합니다. (현재 규모 수백 개라 쓸 만하다고 문서에 명시됨)
* **문제점**: `std::vector<bool>`은 비트 패킹 때문에 반복(iterating) 속도가 무거우며, 단순히 가용 항목을 찾으려 O(n) 탐색을 허용할 이유가 없습니다.
* **해결 제안**:
  가용 상태인 객체 포인터를 담아두는 O(1) 스택 기반 `Free-List` 모델이 구현 난이도조차 더 낮고 성능이 훨씬 빠릅니다. 선형 탐색 대신 아래 형태를 강력히 제안합니다:
  ```cpp
  std::vector<T*> available;
  T* Acquire() {
      if (available.empty()) Expand();
      T* obj = available.back();
      available.pop_back();
      return obj;
  }
  void Release(T* obj) { available.push_back(obj); }
  ```

### 2-2. A1: Event/Messaging '취소(Consume/Cancellation)' 기능 부재
* **상황**: 이벤트 큐에서 Publish된 조건에 맞는 모든 구독자에게 이벤트를 우선순위대로 전달합니다.
* **문제점**: GUI 엔진이나 물리에서 흔히 발생하는 일련의 상호작용 과정에서, "이벤트가 윗단에서 처리되었으므로 아랫단에는 전달을 막아주세요(Consume)"같은 분기가 필요할 때가 아주 많습니다.
* **해결 제안**:
  `EventBus::Publish(EventT& event)`로 비-상수(non-const) 참조를 넘기게 하여, `event.handled = true`인 경우 다음 핸들러로의 루프를 중지(`break`)하는 취소 기능을 설계에 반영하는 것이 장기적으로 좋습니다.

### 2-3. D1: Sorting Layer의 음수 인덱스 비트마스크 위험성
* **상황**: `GetSortKey`에서 `(layerOrder << 16) | (orderInLayer & 0xFFFF)` 비트 조합을 사용합니다.
* **문제점**: 2D 게임 개발 편의상 `orderInLayer` 값으로 주로 음수 (예: `-1` 등 배경 표현)가 많이 들어옵니다. 음수는 2의 보수로 표현되어 `& 0xFFFF` 마스크 씌우고 비트 조합 시 예상치 못한 거대한 양수값으로 해석되어서 정렬이 망가질 수 있습니다.
* **해결 제안**:
  `orderInLayer`에 32768(2^15)을 더하는 양수 오프셋 방식 공간을 확보하거나, 구조체 비교 연산자 (`bool operator<()`) 자체를 활용하여 비트 시프트 연산과 2의 보수에 얽힌 버그를 차단하는 편이 안전합니다.

---

## 💡 3. CI/CD 및 기타 고려 사항

* **E1: CI / CD 머신리스 환경**: GitHub Actions Mac (또는 Linux) 워크플로우에서 X11/Wayland/Cocoa 윈도우 시스템이 없을 경우 `glfwInit()` 또는 윈도우 생성 부분에서 에러를 방출하여 유닛 테스트가 강제 실패할 수 있습니다. 윈도우 없이 동작 가능한 헤드리스(Headless) Mocking이나, `glad` 로드 전 래이캐스트 등 비렌더링 엔진 로직만 분리해 유닛 테스트를 구성할 수 있도록 준비가 필요합니다.

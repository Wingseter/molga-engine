# Molga Engine 리팩토링 계획 - 개요

> 참고 자료: `docs/codex/refactoring-plan-2026-03-08.md`, `docs/gemini/refactoring_plan.md`의 분석 결과를 반영하여 통합 정리.

## 문서 구성

| 문서 | 내용 |
|------|------|
| `00_overview.md` | 전체 리팩토링 요약 및 우선순위 (현재 문서) |
| `01_phase0_baseline.md` | Phase 0: 기준선 확보 (빌드 인프라, 테스트) |
| `02_memory_safety.md` | Phase 1: 메모리 안전성 및 RAII |
| `03_architecture.md` | Phase 2: 아키텍처 통일 및 부트스트랩 정리 |
| `04_ecs_refactor.md` | Phase 3: ECS / 씬 데이터 모델 개선 |
| `05_editor_refactor.md` | Phase 4: 에디터 구조 개선 |
| `06_code_quality.md` | Phase 5: 코드 품질 및 조직화 |

---

## 현재 코드베이스 진단 요약

### 심각도별 이슈 수

| 심각도 | 이슈 수 | 핵심 영역 |
|--------|---------|-----------|
| CRITICAL | ~~10~~ 6 | ~~메모리 누수~~✅, ~~RAII 위반~~✅, ~~부트스트랩 3중 분리~~✅, ~~직렬화 데이터 손실~~✅ |
| HIGH | ~~13~~ 9 | ~~ECS 성능~~✅, 에디터 God Class, 글로벌 상태, 경로 불일치, 핫리로드 버그 |
| MEDIUM | 10 | 디렉토리 구조, 네이밍 불일치, 매직 넘버, 플랫폼 코드, 레거시+ECS 혼합 |
| LOW | 4 | 포맷팅, include guard 스타일, 상수 추출 |

### 핵심 문제 TOP 7

1. **~~부트스트랩 3중 분리~~** ✅ Phase 2에서 해결 — Application 삭제, Bootstrap 모듈 추출로 2→1 통합.

2. **Raw Pointer 수동 관리** - `new`/`delete` 패턴이 main.cpp, runtime_main.cpp, Audio.cpp, REGISTER_SCRIPT 전반에 산재. 예외 발생 시 메모리 누수.

3. **싱글톤 과잉 사용** - 12개 이상 클래스가 싱글톤. 3가지 패턴 혼재. 숨겨진 의존성, 테스트 불가, 멀티스레드 제약.

4. **~~직렬화 데이터 손실~~** ✅ Phase 3에서 해결 — id 복원(`SetID`), 부모-자식 관계 2-pass 직렬화, `ComponentFactory` 자동 등록으로 스크립트 컴포넌트 복원 가능, `enabled` 상태 직렬화 추가.

5. **경로 규칙 불일치** - Project는 `Assets`/`Scenes` (대문자), GameBuilder는 `assets`/`scenes` (소문자). ~~셰이더 경로도 에디터와 런타임 불일치.~~ ✅ 셰이더 경로 통일 완료 (Phase 2). 대소문자 케이싱 버그는 Phase 4에서 수정 예정.

6. **ECS가 아닌 EC 패턴 + 레거시 혼합** - System 레이어 없음. GameScene에서 playerSprite와 ECS Transform을 이중 관리. ~~`dynamic_cast` 기반 O(n) 조회.~~ ✅ `ComponentTypeID` + `unordered_map` 기반 O(1) 조회로 전환 완료 (Phase 3).

7. **Editor God Class** - Editor 클래스가 15개 이상 책임. 도킹 윈도우 이름 불일치. Scene View가 placeholder. HierarchyWindow의 생성/삭제 미구현.

---

## 리팩토링 로드맵

```
Phase 0 (선행) ✅      Phase 1 (Critical) ✅  Phase 2 (Critical) ✅
기준선 확보 완료       메모리 안전성 완료     아키텍처 통일 완료
- molga_core ✅         - unique_ptr 전환 ✅   - Bootstrap 추출 ✅
- CTest + 4 tests ✅   - RAII 래퍼 ✅         - Application 삭제 ✅
- 빌드 경고 해소 ✅    - 에러 경로 수정 ✅    - 경로 상수화 ✅
- Time.h 이름충돌 ✅   - goto 제거 ✅         - 글로벌 변수 축소 ✅
     │                      │                 - 셰이더 경로 통일 ✅
     ▼                      ▼                      │
Phase 3 (High) ✅                                   ▼
ECS/씬 모델 개선 완료  Phase 4 (High)         Phase 5 (Medium)
- O(1) 컴포넌트 조회 ✅ - God Class 분리       코드 품질
- 생명주기 콜백 ✅      - Layer/LayerStack      - 디렉토리 재구성
- id 복원 ✅            - 싱글톤 통일          - pragma once 통일
- enabled 직렬화 ✅     - EngineContext         - 매직 넘버 상수화
- ComponentFactory ✅   - 핫리로드 수정
- 부모-자식 직렬화 ✅   - Scene View 실제 구현
```

### Phase별 예상 영향 범위

| Phase | 변경 파일 수 | 파괴적 변경 | 의존성 |
|-------|------------|------------|--------|
| Phase 0 | 12 ✅ 완료 | 없음 (인프라) | 없음 |
| Phase 1 | 10 ✅ 완료 | 낮음 | Phase 0 |
| Phase 2 | 12 ✅ 완료 | 중간 | Phase 1 |
| Phase 3 | 11 ✅ 완료 | 높음 (ECS/씬 API 변경) | Phase 2 |
| Phase 4 | 8-12 | 중간 | Phase 2 (경로 상수 의존) |
| Phase 5 | 20+ | 낮음 (파일 이동) | Phase 1-4 완료 후 권장 |

---

## 리팩토링 원칙

1. **기능 유지**: 리팩토링은 기존 동작을 변경하지 않는다
2. **점진적 적용**: 각 Phase를 작게 쪼개서 병합 가능한 단위로 진행
3. **검증 의무**: 각 단계는 "빌드 성공 + 최소 1개의 검증 루틴 추가"를 완료 조건으로 설정
4. **하위 호환**: API 변경 시 기존 코드와의 호환성을 고려
5. **데이터 모델 우선**: ECS 성능 개선은 씬 직렬화와 데이터 규칙을 먼저 고정한 후 진행
6. **테스트 없이 동시 변경 금지**: Scene/ECS/스크립트 직렬화를 테스트 없이 동시에 수정하지 않음

## 비권장 접근

- 처음부터 ECS 전체를 다시 쓰는 것
- ~~Application을 유지한 채 main.cpp와 runtime_main.cpp도 계속 따로 키우는 것~~ → Phase 2에서 Application 삭제, Bootstrap 추출로 해결
- ~~테스트 없이 Scene/ECS/스크립트 직렬화를 동시에 뜯는 것~~ → Phase 3에서 단계별 테스트와 함께 완료 (lifecycle, ID, enabled, hierarchy 테스트 추가)
- 경로 규칙 정리 전에 패키저 기능부터 더 붙이는 것

# Marrow 2D Skeletal Animation Integration Plan for molga-engine

이 문서는 독자적인 2D 스켈레탈 애니메이션 런타임인 **Marrow (Maroow)**를 **molga-engine**에 연동하여 실시간으로 뼈대 애니메이션을 로드하고 재생 및 렌더링하기 위한 구체적인 기술 사양과 상세 개발 계획을 설명합니다.

---

## 1. 개요 및 목표
* **목표**: `Marrow` 프로젝트의 빌드 결과물(`libmarrow_runtime.a` 및 헤더)을 `molga-engine`에 의존성으로 추가하고, 에셋 로드부터 실시간 정점 변환 연산 및 OpenGL 드로우 콜까지 이어지는 스켈레탈 애니메이션 파이프라인을 구축합니다.
* **통합 결과물**: 게임 에디터 및 런타임 내에서 `GameObject`에 `MarrowRenderer` 컴포넌트를 부착하여 `.mskl` 및 `.matl` 애니메이션을 재생할 수 있도록 합니다.

---

## 2. 의존성 및 빌드 설정 (CMake)

`molga-engine`이 로컬 디렉토리에 있는 `Maroow` 라이브러리를 찾고 사용할 수 있도록 빌드 환경을 설정합니다.

### 2.1 디렉토리 구조 가정
* `molga-engine`: `/Users/kwon/Workspace/C/molga-engine`
* `Maroow` (런타임): `/Users/kwon/Workspace/C/Maroow`

### 2.2 CMakeLists.txt 수정안
[CMakeLists.txt](file:///Users/kwon/Workspace/C/molga-engine/CMakeLists.txt)의 `molga_core` 정적 라이브러리 빌드 설정 부분에 Marrow 라이브러리 경로 및 Include 디렉토리를 포함시킵니다.

```diff
# ── molga_core static library ──────────────────────────────────────────────────
add_library(molga_core STATIC ${ENGINE_SOURCES})
target_include_directories(molga_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
+   /Users/kwon/Workspace/C/Maroow/include
)
target_link_libraries(molga_core PUBLIC glad glfw)
+ target_link_libraries(molga_core PUBLIC /Users/kwon/Workspace/C/Maroow/build/libmarrow_runtime.a)
target_link_libraries(molga_core PRIVATE molga_warnings)
```

> [!NOTE]
> `Maroow` 빌드가 릴리즈인지 디버그인지에 따라 라이브러리 타겟의 성능 및 어서션이 달라질 수 있습니다. 개발 시에는 `/Users/kwon/Workspace/C/Maroow/build/` 내의 정적 라이브러리 빌드본을 지정합니다.

---

## 3. 에셋 파이프라인 및 리소스 로더 설계

Marrow 애니메이션은 주로 두 가지 리소스를 필요로 합니다:
1. **스켈레톤 정의 파일 (`.mskl`)**: 본(Bone) 계층 구조, 기본 트랜스폼, 애니메이션 키프레임 및 물리 제약 조건 데이터를 포함한 JSON/바이너리 데이터.
2. **아틀라스 머티리얼 파일 (`.matl`)**: 개별 스프라이트 이미지의 패킹 정보 및 매핑 텍스처를 정의.

### 3.1 리소스 매니저 연동
`molga-engine`의 기존 리소스 로딩 시스템([TextureManager.cpp](file:///Users/kwon/Workspace/C/molga-engine/src/Core/TextureManager.cpp) 등)을 확장하거나, `MarrowRenderer` 내에서 안전하게 로드할 수 있도록 스마트 포인터 기반으로 자산을 관리합니다.

```cpp
#include <marrow/runtime/skeleton.hpp>
#include <marrow/runtime/atlas.hpp>
#include <memory>

class MarrowAsset {
public:
    std::shared_ptr<marrow::runtime::SkeletonData> skeletonData;
    std::shared_ptr<marrow::runtime::Atlas> atlas;
    unsigned int openGLTextureID{0}; // molga-engine에서 바인딩하여 사용할 아틀라스 텍스처 ID
};
```

---

## 4. ECS 컴포넌트 설계 (`MarrowRenderer`)

`molga-engine`은 컴포넌트 기반 아키텍처(ECS)를 사용합니다. `src/ECS/Components/` 아래에 `MarrowRenderer` 컴포넌트를 설계합니다.

### 4.1 `MarrowRenderer.h` 설계안
[src/ECS/Components/SpriteRenderer.h](file:///Users/kwon/Workspace/C/molga-engine/src/ECS/Components/SpriteRenderer.h)를 참고하여 뼈대 애니메이션 전용 컴포넌트를 정의합니다.

```cpp
#pragma once
#include "ECS/Component.h"
#include "ECS/Components/Transform.h"
#include <string>
#include <memory>
#include <marrow/runtime/skeleton.hpp>
#include <marrow/runtime/animation_state.hpp>

class MarrowRenderer : public Component {
public:
    MarrowRenderer(GameObject* owner);
    virtual ~MarrowRenderer();

    // 초기 에셋 및 텍스처 설정
    bool Load(const std::string& skeletonMsklPath, const std::string& atlasMatlPath, const std::string& texturePath);

    // 애니메이션 제어
    void SetAnimation(int trackIndex, const std::string& animationName, bool loop);
    void SetMix(const std::string& fromName, const std::string& toName, float duration);

    // ECS 생명주기 메서드
    void Update(float deltaTime) override;
    void Render(); // Renderer에 최종 버텍스 드로우 요청

private:
    // Marrow Runtime 객체들
    std::unique_ptr<marrow::runtime::Skeleton> skeleton;
    std::unique_ptr<marrow::runtime::AnimationState> animationState;
    std::shared_ptr<marrow::runtime::SkeletonData> skeletonData;
    std::shared_ptr<marrow::runtime::Atlas> atlas;

    // OpenGL 연동용
    unsigned int textureID{0}; 
    Transform* transformComp{nullptr}; // 오브젝트의 전역 Transform 컴포넌트 캐싱
};
```

### 4.2 `MarrowRenderer.cpp` 생명주기 및 업데이트 로직 구현
* **Load**: 파일 경로로부터 뼈대 데이터를 메모리에 인스턴스화하고 애니메이션 상태 제어 객체를 생성합니다.
* **Update**: 프레임 경과 시간(`deltaTime`)만큼 애니메이션 트랙의 시간 값을 올리고, 이를 실제 뼈대 인스턴스에 적용한 뒤, 월드 기준 본 트랜스폼 연산을 전파시킵니다.

```cpp
#include "MarrowRenderer.h"
#include "ECS/GameObject.h"
#include "Core/TextureManager.h"
#include <iostream>

MarrowRenderer::MarrowRenderer(GameObject* owner) : Component(owner) {
    transformComp = owner->GetComponent<Transform>();
}

MarrowRenderer::~MarrowRenderer() {
    // 내부 리소스 메모리 해제
}

bool MarrowRenderer::Load(const std::string& skeletonMsklPath, const std::string& atlasMatlPath, const std::string& texturePath) {
    // 1. Marrow Skeleton Data 및 Atlas 파싱 로드
    // (예시 코드: 실 구현 시 marrow::runtime의 파일로드 헬퍼 API 적용)
    skeletonData = marrow::runtime::load_skeleton_data_from_file(skeletonMsklPath);
    atlas = marrow::runtime::load_atlas_from_file(atlasMatlPath);
    
    if (!skeletonData || !atlas) {
        std::cerr << "Failed to load Marrow Assets: " << skeletonMsklPath << std::endl;
        return false;
    }

    // 2. 개별 인스턴스 상태 및 애니메이션 제어기 생성
    skeleton = std::make_unique<marrow::runtime::Skeleton>(skeletonData.get());
    animationState = std::make_unique<marrow::runtime::AnimationState>(skeletonData.get());

    // 3. molga-engine 텍스처 시스템을 통해 아틀라스 PNG 바인딩
    textureID = TextureManager::GetTexture(texturePath)->GetID();

    return true;
}

void MarrowRenderer::SetAnimation(int trackIndex, const std::string& animationName, bool loop) {
    if (animationState) {
        animationState->set_animation(trackIndex, animationName, loop);
    }
}

void MarrowRenderer::Update(float deltaTime) {
    if (!animationState || !skeleton) return;

    // 1. 애니메이션 타이머 진행 및 키프레임 값을 본(Bone)들에 적용
    animationState->update(deltaTime);
    animationState->apply(*skeleton);

    // 2. 뼈대들의 부모-자식 관계에 기반한 월드 트랜스폼 갱신 (핵심 연산)
    skeleton->update_world_transforms();
}
```

---

## 5. OpenGL 기반 렌더링 파이프라인 연동

뼈대 정적/동적 연산이 완료된 후, 슬롯들의 최종 월드 위치 및 정점(Vertex) 좌표 정보를 OpenGL 버퍼에 밀어 넣어 화면에 표시합니다.

### 5.1 슬롯 순회 및 정점 가공
Marrow는 슬롯(Slot) 단위로 정렬된 드로우 오더(Draw Order)를 제공합니다. 드로우 오더를 정방향으로 순회하며 투명도(Alpha) 및 z-ordering에 대응합니다.

```cpp
void MarrowRenderer::Render() {
    if (!skeleton || !atlas || textureID == 0) return;

    // 1. 셰이더 및 아틀라스 텍스처 바인딩
    // molga-engine의 Renderer 셰이더 및 전역 설정을 이용해 드로우를 준비합니다.
    glBindTexture(GL_TEXTURE_2D, textureID);

    const auto& drawOrder = skeleton->get_draw_order();
    
    // 2. 드로우 오더 순회하며 그리 그리기 시작
    for (auto* slot : drawOrder) {
        auto* attachment = slot->get_attachment();
        if (!attachment) continue;

        // Attachment 타입 판별 (RegionAttachment vs MeshAttachment)
        if (attachment->is_region()) {
            auto* region = static_cast<marrow::runtime::RegionAttachment*>(attachment);
            
            // 4개의 꼭짓점 좌표 계산 (본 트랜스폼 반영)
            std::array<float, 8> worldVertices;
            region->compute_world_vertices(*slot, worldVertices.data());

            // UV 좌표 획득
            const float* uvs = region->get_uvs();

            // 4개 정점을 molga-engine의 배치 렌더러에 던지거나 즉시 그리기(Immediate Draw) 수행
            // 예시: Renderer::DrawQuad(worldVertices, uvs, slot->color);
        }
        else if (attachment->is_mesh()) {
            auto* mesh = static_cast<marrow::runtime::MeshAttachment*>(attachment);
            
            std::vector<float> worldVertices(mesh->get_world_vertices_count());
            mesh->compute_world_vertices(*slot, worldVertices.data());

            const float* uvs = mesh->get_uvs();
            const unsigned short* indices = mesh->get_triangles();
            int indexCount = mesh->get_triangles_count();

            // 메시 형태의 삼각면 그리기
            // Renderer::DrawMesh(worldVertices, uvs, indices, indexCount, slot->color);
        }
    }
}
```

> [!IMPORTANT]
> **성능 최적화 (Batch Rendering)**: 슬롯마다 개별 드로우 콜(`glDrawElements`)을 날리는 방식은 성능에 큰 악영향을 줍니다. 동일한 텍스처 아틀라스를 사용하므로, 한 스켈레톤 내 모든 슬롯의 삼각면 정점 정보를 동적 정점 버퍼(Dynamic VBO)에 합산하여 **단 한 번의 드로우 콜**로 렌더링되도록 배치 버퍼링을 설계해야 합니다.

---

## 6. 테스트 및 검증 시나리오

1. **에셋 변환 검증**: 
   - `Maroow` 프로젝트의 `spine_to_marrow` 도구를 통해 테스트 에셋인 `spineboy.json`을 `spineboy.mskl`, `spineboy.matl`로 안정적으로 변환할 수 있는지 검사합니다.
2. **에디터 뷰포트 연동**: 
   - `molga-engine` 에디터의 씬 계층 구조(Hierarchy Window)에서 빈 GameObject를 생성하고 `MarrowRenderer` 컴포넌트를 붙여 인스펙터 창에서 애니메이션 리스트가 잡히는지 확인합니다.
3. **런타임 동작 확인**:
   - `player_idle.marrow` 애니메이션이 정상 속도로 끊김 없이 반복 재생되는지 렌더링 뷰포트에서 확인합니다.
   - 트랙 전환 시의 부드러운 애니메이션 블렌딩(SetMix)이 시각적으로 잘 구현되는지 검증합니다.

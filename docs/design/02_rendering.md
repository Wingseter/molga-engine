# Molga Engine - 렌더링 시스템 리서치 보고서

> 조사 일자: 2026-03-22
> 대상: Unity 수준의 2D 렌더링에 필요한 8개 시스템
> 엔진 사양: C++17, OpenGL 3.3

## 현재 상태 분석

코드베이스를 분석한 결과, 현재 Molga Engine의 렌더링 파이프라인은 다음과 같은 구조이다:

- **Renderer**: 단일 Quad VAO/VBO 기반. `DrawSprite()` 호출마다 `glDrawArrays(GL_TRIANGLES, 0, 6)` 실행 -- 즉 **스프라이트 1개 = Draw Call 1회**
- **Shader**: 단일 셰이더(`default.vert/frag`). uniform 캐시 있음. `model`, `projection`, `uColor`, `uUV`, `useTexture` uniform 사용
- **Sprite**: position/size/rotation/color/uv + Texture 포인터. `GetModelMatrix()`로 매 프레임 4x4 모델 행렬 계산
- **SpriteRenderer**: ECS 컴포넌트. `sortingOrder` 필드 존재하나 실제 정렬 시스템 미구현
- **Camera2D**: position/zoom/rotation. 단일 카메라. 뷰/프로젝션 행렬 생성
- **ParticleEmitter**: CPU 기반 파티클. 파티클마다 개별 `Renderer::DrawSprite()` 호출

핵심 병목: **스프라이트당 1 Draw Call + 스프라이트당 uniform 업데이트 + 텍스처 바인딩 변경**. 이 구조로는 수백 개 이상의 스프라이트에서 심각한 성능 저하가 발생한다.

---

## 1. Batch Rendering / Draw Call 최적화

### 1.1 무엇이며 왜 필수인가

Batch Rendering은 동일한 렌더링 상태(셰이더, 텍스처, 블렌딩 모드)를 공유하는 다수의 스프라이트를 **하나의 Draw Call**로 묶어 GPU에 제출하는 기법이다. 현재 Molga Engine은 스프라이트 하나마다 다음 과정을 반복한다:

```
glUniformMatrix4fv(model)     // CPU→GPU 전송
glUniform4f(uColor)           // CPU→GPU 전송
glUniform4f(uUV)              // CPU→GPU 전송
glBindTexture(...)            // 상태 변경
glDrawArrays(GL_TRIANGLES, 0, 6)  // Draw Call
```

1,000개 스프라이트라면 1,000번의 Draw Call과 4,000번 이상의 uniform 업데이트가 발생한다. OpenGL 드라이버에서 Draw Call은 CPU 측의 가장 큰 병목이다. 일반적으로 데스크탑에서 Draw Call 예산은 프레임당 500-3,000회 수준이며, 이를 넘기면 CPU-bound 상태가 된다.

### 1.2 Unity의 구현 방식

Unity는 세 가지 배칭 전략을 사용한다:

- **Dynamic Batching**: 런타임에 동일 머티리얼을 사용하는 작은 메쉬들을 하나의 메쉬로 합침. 정점 수 제한(300 이하)이 있고 CPU 오버헤드가 있어 2D에서는 SRP Batcher에 밀림
- **Static Batching**: 움직이지 않는 오브젝트들을 빌드 타임에 하나의 큰 메쉬로 합침. 메모리 사용량 증가 대신 Draw Call 극적 감소
- **SRP Batcher**: Scriptable Render Pipeline에서 사용. 셰이더 variant별로 persistent CBUFFER를 유지하여 CPU→GPU 데이터 전송을 최소화. 동일 shader variant면 배칭 가능

Unity 2D에서는 주로 **Sprite Renderer의 Dynamic Batching**이 사용되며, 동일 텍스처/머티리얼의 스프라이트들이 자동으로 하나의 배치로 합쳐진다.

### 1.3 OpenGL 3.3 구현 방안

**권장 아키텍처: Texture Atlas 기반 Dynamic Sprite Batcher**

#### 핵심 데이터 구조

```cpp
// 배치용 정점 구조체 -- 인터리브 형식
struct BatchVertex {
    float x, y;        // 월드 좌표 (이미 변환된 상태)
    float u, v;         // 아틀라스 내 텍스처 좌표
    float r, g, b, a;   // 정점 컬러
};

class SpriteBatcher {
public:
    static constexpr int MAX_SPRITES = 10000;
    static constexpr int MAX_VERTICES = MAX_SPRITES * 4;
    static constexpr int MAX_INDICES = MAX_SPRITES * 6;

    void Init();
    void Begin(Shader* shader, Camera2D* camera);
    void Submit(const SpriteRenderData& data);  // 스프라이트 추가
    void End();   // 실제 렌더링 수행
    void Flush(); // 현재 배치 제출

private:
    GLuint vao, vbo, ebo;
    BatchVertex* vertexBuffer;       // CPU 측 정점 배열
    BatchVertex* vertexBufferPtr;    // 현재 쓰기 위치
    int spriteCount = 0;
    GLuint currentTextureID = 0;
    Shader* currentShader = nullptr;
};
```

#### 초기화 (VBO 설정)

```cpp
void SpriteBatcher::Init() {
    // 인덱스 버퍼 미리 생성 (쿼드 패턴은 항상 동일)
    uint32_t indices[MAX_INDICES];
    uint32_t offset = 0;
    for (int i = 0; i < MAX_INDICES; i += 6) {
        indices[i + 0] = offset + 0;
        indices[i + 1] = offset + 1;
        indices[i + 2] = offset + 2;
        indices[i + 3] = offset + 2;
        indices[i + 4] = offset + 3;
        indices[i + 5] = offset + 0;
        offset += 4;
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    // 동적 VBO -- GL_DYNAMIC_DRAW
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_VERTICES * sizeof(BatchVertex),
                 nullptr, GL_DYNAMIC_DRAW);

    // position (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BatchVertex),
                          (void*)offsetof(BatchVertex, x));
    glEnableVertexAttribArray(0);

    // texcoord (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BatchVertex),
                          (void*)offsetof(BatchVertex, u));
    glEnableVertexAttribArray(1);

    // color (location 2)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                          sizeof(BatchVertex),
                          (void*)offsetof(BatchVertex, r));
    glEnableVertexAttribArray(2);

    // 정적 인덱스 버퍼
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    vertexBuffer = new BatchVertex[MAX_VERTICES];
}
```

#### Submit / Flush 흐름

```cpp
void SpriteBatcher::Submit(const SpriteRenderData& data) {
    // 텍스처가 바뀌거나 배치가 꽉 차면 Flush
    GLuint texID = data.texture ? data.texture->GetID() : 0;
    if (texID != currentTextureID || spriteCount >= MAX_SPRITES) {
        Flush();
        currentTextureID = texID;
    }

    // CPU에서 모델 변환 적용 -> 월드 좌표 정점 4개 계산
    // 이것이 핵심: GPU에 model uniform 보내는 대신
    // CPU에서 직접 4개 정점을 변환한다
    float cos_r = cosf(data.rotation);
    float sin_r = sinf(data.rotation);
    float hw = data.width * 0.5f;
    float hh = data.height * 0.5f;

    // 4개 코너 (로컬 → 월드 변환)
    float corners[4][2] = {
        {-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}
    };

    for (int i = 0; i < 4; i++) {
        float rx = corners[i][0] * cos_r - corners[i][1] * sin_r;
        float ry = corners[i][0] * sin_r + corners[i][1] * cos_r;

        vertexBufferPtr->x = rx + data.x;
        vertexBufferPtr->y = ry + data.y;
        vertexBufferPtr->u = data.uvs[i * 2];
        vertexBufferPtr->v = data.uvs[i * 2 + 1];
        vertexBufferPtr->r = data.color[0];
        vertexBufferPtr->g = data.color[1];
        vertexBufferPtr->b = data.color[2];
        vertexBufferPtr->a = data.color[3];
        vertexBufferPtr++;
    }
    spriteCount++;
}

void SpriteBatcher::Flush() {
    if (spriteCount == 0) return;

    // VBO 서브데이터 업로드 (변경된 부분만)
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLsizeiptr size = (uint8_t*)vertexBufferPtr - (uint8_t*)vertexBuffer;
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, vertexBuffer);

    // 텍스처 바인딩
    if (currentTextureID) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTextureID);
    }

    // 단일 Draw Call로 모든 스프라이트 렌더링
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, spriteCount * 6,
                   GL_UNSIGNED_INT, nullptr);

    // 리셋
    spriteCount = 0;
    vertexBufferPtr = vertexBuffer;
}
```

#### 파티클 인스턴싱 (Instanced Rendering)

파티클 시스템은 동일한 쿼드 메쉬를 수천 번 그리므로 **인스턴스 렌더링**이 적합하다. OpenGL 3.3은 `glDrawArraysInstanced`와 `glVertexAttribDivisor`를 완전 지원한다.

```cpp
struct ParticleInstance {
    float x, y;          // 위치
    float size;          // 크기
    float rotation;      // 회전
    float r, g, b, a;    // 색상
    float u0, v0, u1, v1; // UV (아틀라스 내)
};

// 인스턴스 버퍼 설정
GLuint instanceVBO;
glGenBuffers(1, &instanceVBO);
glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
glBufferData(GL_ARRAY_BUFFER,
             MAX_PARTICLES * sizeof(ParticleInstance),
             nullptr, GL_STREAM_DRAW);

// location 2 ~ 5에 인스턴스 속성 바인딩
// glVertexAttribDivisor(loc, 1)로 인스턴스당 한 번 읽도록 설정

// 렌더링: 단 1회의 Draw Call로 모든 파티클
glDrawArraysInstanced(GL_TRIANGLES, 0, 6, activeParticleCount);
```

#### 핵심 최적화 포인트

| 기법 | 효과 | 비고 |
|------|------|------|
| **텍스처별 정렬 후 배칭** | Flush 횟수 최소화 | 동일 아틀라스 사용 시 1 Draw Call |
| **glBufferSubData** 대신 **glMapBufferRange** | 버퍼 동기화 비용 감소 | `GL_MAP_INVALIDATE_BUFFER_BIT` 사용 |
| **더블/트리플 버퍼링** | GPU-CPU 동기화 방지 | 3개 VBO를 로테이션 |
| **인덱스 기반 쿼드** | 정점 수 33% 감소 (6→4/쿼드) | EBO 미리 생성 |
| **CPU 측 변환** | model uniform 제거 | 스프라이트 4개 정점을 CPU에서 직접 변환 |

#### 배치 셰이더 (batch.vert) 변경사항

```glsl
#version 330 core
layout (location = 0) in vec2 aPos;       // 이미 월드 좌표
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;  // projView (model 없음!)

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
```

### 1.4 성능 목표 및 측정 기준

| 지표 | 현재 | 목표 |
|------|------|------|
| 스프라이트 10,000개 Draw Call | 10,000 | 1-10 (아틀라스에 따라) |
| uniform 업데이트/프레임 | 40,000+ | 2 (projection 1회 + 텍스처 수) |
| CPU 렌더링 시간 (10K 스프라이트) | ~15-20ms | ~1-2ms |

### 1.5 복잡도 및 의존성

- **예상 복잡도**: Large (2-3주)
- **의존성**: Sprite Atlas 시스템 (동시 구현 권장), Sorting Layer (정렬 후 배칭)
- **영향 범위**: Renderer, SpriteRenderer, ParticleEmitter 전면 리팩토링

---

## 2. Sorting Layers & Render Order

### 2.1 무엇이며 왜 필수인가

2D 게임에서 렌더링 순서는 깊이(depth)의 역할을 한다. 배경은 먼저 그리고, 캐릭터는 나중에, UI는 가장 마지막에 그려야 한다. 제대로 된 정렬 시스템 없이는:

- 스프라이트가 뒤죽박죽 겹쳐 보임
- 반투명 스프라이트가 올바르게 블렌딩되지 않음 (뒤에서 앞으로 그려야 알파 블렌딩이 올바름)
- 배칭 시 정렬 순서를 보장할 수 없음

현재 Molga Engine의 `SpriteRenderer`에는 `sortingOrder` 필드가 있지만 실제로 이를 사용해 정렬하는 시스템이 없다. 렌더링은 씬의 오브젝트 순서에 의존한다.

### 2.2 Unity의 구현 방식

Unity는 3단계 정렬 체계를 사용한다:

1. **Sorting Layer** (최상위): "Background", "Default", "Foreground", "UI" 등의 명명된 레이어. 정수 ID로 관리되며, 레이어 순서가 가장 높은 우선순위
2. **Order in Layer** (레이어 내 순서): 같은 Sorting Layer 내에서의 정수 순서. 값이 클수록 나중에(위에) 그려짐
3. **Camera Depth**: 여러 카메라 사용 시 카메라별 렌더링 순서. depth 값이 작은 카메라가 먼저 렌더링

최종 정렬 키: `(CameraDepth, SortingLayerID, OrderInLayer, RendererPriority)`

투명 오브젝트는 back-to-front (뒤→앞) 순서로, 불투명 오브젝트는 front-to-back (앞→뒤, early-z 활용) 순서로 정렬한다.

### 2.3 OpenGL 3.3 구현 방안

#### 정렬 키 설계

```cpp
// 64비트 정렬 키로 모든 정보 인코딩
// |-- layer(8bit) --|-- order(16bit) --|-- texID(24bit) --|-- depth(16bit) --|
struct SortKey {
    uint64_t key;

    static SortKey Create(int8_t layer, int16_t order,
                          uint32_t textureID, uint16_t depth = 0) {
        SortKey sk;
        sk.key = ((uint64_t)(layer + 128) << 56)    // layer [-128,127] → [0,255]
               | ((uint64_t)(order + 32768) << 40)   // order 정규화
               | ((uint64_t)(textureID & 0xFFFFFF) << 16) // 텍스처 ID (배칭 최적화)
               | (uint64_t)depth;
        return sk;
    }

    bool operator<(const SortKey& other) const {
        return key < other.key;
    }
};
```

핵심 설계 의도: 정렬 키에 **텍스처 ID를 포함**시켜서 같은 레이어/순서 내에서 동일 텍스처끼리 자연스럽게 모이도록 한다. 이것이 **정렬과 배칭의 연결 고리**이다.

#### SortingLayer 관리자

```cpp
class SortingLayerManager {
public:
    // 기본 레이어 등록
    void Init() {
        RegisterLayer("Background", -100);
        RegisterLayer("Default", 0);
        RegisterLayer("Foreground", 100);
        RegisterLayer("UI", 200);
    }

    void RegisterLayer(const std::string& name, int order);
    int GetLayerOrder(const std::string& name) const;
    const std::vector<std::string>& GetLayerNames() const;

private:
    struct LayerInfo {
        std::string name;
        int order;
    };
    std::vector<LayerInfo> layers; // order로 정렬 유지
};
```

#### SpriteRenderer 컴포넌트 확장

```cpp
class SpriteRenderer : public Component {
    // ... 기존 멤버 ...
    std::string sortingLayerName = "Default";  // 추가
    int sortingOrder = 0;                       // 기존

    SortKey GetSortKey() const {
        int layerOrder = SortingLayerManager::Get()
                         .GetLayerOrder(sortingLayerName);
        GLuint texID = texture ? texture->GetID() : 0;
        return SortKey::Create(layerOrder, sortingOrder, texID);
    }
};
```

#### 렌더링 파이프라인에서의 정렬

```cpp
void RenderSystem::Render() {
    // 1. 모든 활성 SpriteRenderer를 수집
    std::vector<RenderCommand> commands;
    commands.reserve(activeRenderers.size());

    for (auto* sr : activeRenderers) {
        RenderCommand cmd;
        cmd.sortKey = sr->GetSortKey();
        cmd.spriteData = sr->GetRenderData();
        commands.push_back(cmd);
    }

    // 2. 정렬 키로 정렬 (std::sort, 또는 radix sort for >1000)
    std::sort(commands.begin(), commands.end(),
              [](const RenderCommand& a, const RenderCommand& b) {
                  return a.sortKey < b.sortKey;
              });

    // 3. 정렬된 순서대로 Batcher에 Submit
    batcher.Begin(shader, camera);
    for (const auto& cmd : commands) {
        batcher.Submit(cmd.spriteData);
    }
    batcher.End();
}
```

#### 투명도 정렬 전략

2D 게임에서 대부분의 스프라이트는 투명 영역을 포함하므로 back-to-front 정렬이 기본이다. 정렬 키 자체가 이미 layer/order 기준 back-to-front이므로 추가 작업이 필요 없다. 단, 같은 정렬 키를 가진 스프라이트 간의 Z-fighting을 방지하기 위해 y좌표 기반 보조 정렬을 제공하면 아이소메트릭 게임 등에서 유용하다.

```cpp
// Y-sort 모드 (SpriteRenderer에 옵션으로)
enum class SortMode {
    Fixed,      // sortingOrder만 사용
    YSort,      // y좌표 기준 (아래쪽 = 앞)
    Custom      // 사용자 정의 비교 함수
};
```

### 2.4 복잡도 및 의존성

- **예상 복잡도**: Medium (1-1.5주)
- **의존성**: 없음 (독립 구현 가능하나, Batch Renderer와 함께 설계해야 효율적)
- **영향 범위**: SpriteRenderer 컴포넌트, RenderSystem, 에디터 Inspector

---

## 3. 2D Lighting System

### 3.1 무엇이며 왜 필수인가

2D 라이팅은 평면적인 스프라이트에 깊이감, 분위기, 시각적 다이나미즘을 부여한다. 라이팅 없이는 모든 스프라이트가 동일한 밝기로 렌더링되어 시각적으로 평면적이다. 공포/스텔스/탐험 장르에서는 라이팅이 핵심 게임플레이 요소이기도 하다.

### 3.2 Unity의 구현 방식 (URP 2D Lighting)

Unity의 Universal Render Pipeline에서 2D Lighting은 다음을 제공한다:

- **Point Light 2D**: 한 지점에서 원형으로 퍼지는 빛. 내부/외부 반경, 강도, 색상, 감쇠(falloff) 커스터마이징
- **Global Light 2D**: 씬 전체를 균일하게 비추는 앰비언트 라이트
- **Spot Light 2D**: 원뿔 형태로 퍼지는 방향성 빛
- **Freeform Light 2D**: 다각형 형태의 자유 형상 빛
- **Shadow Caster 2D**: 2D 오브젝트가 빛을 차단하여 그림자 생성. 실시간 소프트 섀도 지원
- **Normal Map 지원**: 스프라이트에 노멀맵을 적용하여 빛에 의한 요철 표현

Unity 내부적으로는 라이트별로 **라이트 텍스처(Light Render Texture)**를 렌더링한 뒤, 최종 장면과 곱셈(Multiply) 블렌딩하는 방식을 사용한다.

### 3.3 OpenGL 3.3 구현 방안

#### 2D에서의 Forward vs Deferred 비교

| 방식 | 장점 | 단점 | 2D 적합성 |
|------|------|------|-----------|
| **Forward** | 구현 간단, 투명도 자연 처리 | 라이트 수 증가 시 패스 증가 | 라이트 <10개 시 적합 |
| **Deferred** (라이트맵 방식) | 라이트 수에 무관한 성능 | FBO 추가 필요, 투명도 처리 복잡 | 라이트 >10개 시 적합 |
| **2D 라이트맵 합성** | 2D에 최적, 구현 적절 | G-Buffer 불필요 | **권장** |

**권장 방식: 라이트맵 합성 (Light Map Compositing)**

이 방식은 전통적인 3D deferred와 다르다. G-Buffer를 만드는 대신:

1. 장면을 일반적으로 렌더링 (diffuse pass)
2. 별도의 **라이트맵 FBO**에 모든 2D 라이트를 Additive 블렌딩으로 렌더링
3. 최종 합성 시 장면 텍스처와 라이트맵 텍스처를 **Multiply** 블렌딩

#### 전체 파이프라인

```
[Pass 1: Scene Render] → sceneFBO (색상 텍스처)
[Pass 2: Light Map]    → lightFBO (라이트맵 텍스처)
[Pass 3: Composite]    → 화면 (sceneFBO * lightFBO)
[Pass 4: UI]           → 화면 위에 라이팅 영향 없이 렌더링
```

#### FBO 설정

```cpp
class RenderTarget {
public:
    void Create(int width, int height) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // 색상 텍스처
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                     width, height, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTexture, 0);

        // completeness 체크
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)
               == GL_FRAMEBUFFER_COMPLETE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    void Unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    GLuint GetTexture() const { return colorTexture; }

private:
    GLuint fbo = 0;
    GLuint colorTexture = 0;
};
```

#### 2D 라이트 구조체 및 렌더링

```cpp
struct Light2D {
    enum class Type { Point, Global, Spot };

    Type type = Type::Point;
    float x, y;                    // 위치
    float innerRadius = 50.0f;     // 내부 반경 (100% 강도)
    float outerRadius = 200.0f;    // 외부 반경 (감쇠)
    float intensity = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;

    // Spot light 전용
    float direction = 0.0f;        // 라디안
    float angle = 0.785f;          // 원뿔 반각 (45도)
};
```

#### 라이트맵 셰이더 (light_point.frag)

```glsl
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform vec2 uLightPos;        // 화면 좌표
uniform float uInnerRadius;
uniform float uOuterRadius;
uniform float uIntensity;
uniform vec3 uLightColor;
uniform vec2 uResolution;

void main() {
    vec2 fragPos = gl_FragCoord.xy;

    // 라이트까지 거리
    float dist = length(fragPos - uLightPos);

    // smoothstep 감쇠
    float attenuation = 1.0 - smoothstep(uInnerRadius, uOuterRadius, dist);
    attenuation *= uIntensity;

    FragColor = vec4(uLightColor * attenuation, attenuation);
}
```

#### 노멀맵 지원

스프라이트에 노멀맵을 적용하면 2D이면서도 빛의 방향에 반응하는 표면 질감을 표현할 수 있다.

```glsl
// normal_lit.frag -- 노멀맵이 있는 스프라이트용
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D uDiffuse;     // 기본 텍스처
uniform sampler2D uNormalMap;   // 노멀맵
uniform vec2 uLightPos;         // 라이트 위치 (월드)
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform vec2 uSpritePos;        // 스프라이트 위치

void main() {
    vec4 diffuseColor = texture(uDiffuse, TexCoord);

    // 노멀맵에서 법선 읽기 (탄젠트 공간, [0,1] → [-1,1])
    vec3 normal = texture(uNormalMap, TexCoord).rgb * 2.0 - 1.0;

    // 라이트 방향 (2D, z=0.5로 가정한 높이)
    vec3 lightDir = normalize(vec3(uLightPos - uSpritePos, 0.5));

    // Lambert diffuse
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 finalColor = diffuseColor.rgb * uLightColor
                      * diff * uLightIntensity;
    FragColor = vec4(finalColor, diffuseColor.a);
}
```

#### 2D 섀도 캐스팅

2D 그림자는 1D shadow map 방식이 효율적이다:

1. 라이트 위치에서 360도 방향으로 레이캐스트
2. 각 방향에서 가장 가까운 차폐물까지의 거리를 1D 텍스처에 기록
3. 프래그먼트가 라이트에서의 거리가 shadow map 값보다 크면 그림자

```
[Shadow Caster 메쉬 수집]
→ [1D Shadow Map FBO에 각도별 최소 거리 렌더링]
→ [라이트 셰이더에서 shadow map 참조하여 그림자 판정]
```

이 방식은 라이트당 1D 텍스처(해상도 360-720 정도)만 필요하므로 매우 가볍다.

### 3.4 복잡도 및 의존성

- **예상 복잡도**: Very Large (3-5주)
  - 기본 포인트/글로벌 라이트: Large (2주)
  - 노멀맵 지원: Medium (1주)
  - 섀도 캐스팅: Large (2주)
- **의존성**: Post-Processing (FBO 인프라 공유), Custom Shader System (다중 셰이더 필수)
- **영향 범위**: 렌더링 파이프라인 전면 변경, 새로운 FBO 관리, 라이트 컴포넌트 ECS 통합

---

## 4. Post-Processing Effects

### 4.1 무엇이며 왜 필수인가

포스트 프로세싱은 장면이 렌더링된 후 최종 이미지에 적용되는 화면 전체 이펙트이다. 게임의 시각적 품질을 극적으로 향상시키며, 분위기 연출(Bloom으로 네온 효과, Vignette로 집중 효과, Color Grading으로 톤 조정)에 필수적이다. 또한 Screen Shake 같은 게임 피드백 효과도 여기서 처리한다.

### 4.2 Unity의 구현 방식

Unity의 Post-Processing Stack / URP Volume 시스템:

- **Volume 기반**: 월드에 Volume을 배치하여 카메라가 진입하면 해당 이펙트 활성화. 글로벌/로컬 Volume 지원
- **이펙트 체인**: Bloom → Tone Mapping → Color Grading → Vignette → ... 순서로 연결
- **Override 시스템**: 각 이펙트의 파라미터를 Volume별로 오버라이드 가능
- **블렌딩**: 여러 Volume의 가중 혼합

주요 2D 이펙트:
- **Bloom**: 밝은 영역이 번져 보이는 효과. HDR 추출 → 다운샘플 → 가우시안 블러 → 업샘플 → 합성
- **Color Grading**: LUT(Look-Up Table) 기반 색상 보정
- **Vignette**: 화면 가장자리를 어둡게
- **Chromatic Aberration**: 색수차 (RGB 채널 오프셋)
- **Film Grain**: 노이즈 오버레이

### 4.3 OpenGL 3.3 구현 방안

#### 핵심 인프라: Ping-Pong Framebuffer

포스트 프로세싱 체인의 기본 원리: FBO A에 장면 렌더링 → 이펙트 1을 FBO B에 적용 → 이펙트 2를 FBO A에 적용 → ... → 최종 결과를 화면에 렌더링. 이 A↔B 교대 방식을 Ping-Pong이라 한다.

```cpp
class PostProcessPipeline {
public:
    void Init(int width, int height) {
        // 장면 렌더 타겟
        sceneFBO.Create(width, height, GL_RGBA16F); // HDR 지원

        // Ping-Pong FBO 2개
        pingPongFBO[0].Create(width, height, GL_RGBA16F);
        pingPongFBO[1].Create(width, height, GL_RGBA16F);

        // 전체 화면 쿼드 (NDC)
        float quadVerts[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f,
        };
        // VAO/VBO 설정 ...
    }

    // 장면을 FBO에 렌더링하는 Begin/End
    void BeginScene() { sceneFBO.Bind(); }
    void EndScene() { sceneFBO.Unbind(); }

    // 이펙트 체인 실행 후 최종 출력
    void Execute() {
        GLuint currentInput = sceneFBO.GetTexture();
        int pingPongIndex = 0;

        for (auto& effect : activeEffects) {
            if (!effect->IsEnabled()) continue;

            pingPongFBO[pingPongIndex].Bind();
            glClear(GL_COLOR_BUFFER_BIT);

            effect->Apply(currentInput);
            RenderFullscreenQuad();

            currentInput = pingPongFBO[pingPongIndex].GetTexture();
            pingPongIndex = 1 - pingPongIndex; // 핑퐁 전환
        }

        // 최종: 기본 프레임버퍼에 출력
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        blitShader->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentInput);
        RenderFullscreenQuad();
    }

private:
    RenderTarget sceneFBO;
    RenderTarget pingPongFBO[2];
    std::vector<std::unique_ptr<PostProcessEffect>> activeEffects;
    GLuint quadVAO, quadVBO;
};
```

#### Bloom 구현 (다운샘플 + 가우시안 블러 + 업샘플)

Bloom은 가장 복잡하지만 시각적 효과가 큰 포스트 프로세싱이다.

```
[HDR 장면] → [밝기 추출 (threshold)] → [다운샘플 x4-5] → [가우시안 블러]
→ [업샘플 x4-5 (바이리니어 + additive)] → [원본과 합성]
```

밝기 추출 셰이더 (bright_extract.frag):
```glsl
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D uScene;
uniform float uThreshold;

void main() {
    vec4 color = texture(uScene, TexCoord);
    float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > uThreshold) {
        FragColor = color;
    } else {
        FragColor = vec4(0.0);
    }
}
```

2-Pass 가우시안 블러 셰이더 (gaussian_blur.frag):
```glsl
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D uImage;
uniform bool uHorizontal;
uniform vec2 uTexelSize;  // 1.0 / textureSize

// 5-tap 가우시안 가중치 (9-tap 이상은 성능 vs 품질 트레이드오프)
const float weight[5] = float[](
    0.2270270270, 0.1945945946, 0.1216216216,
    0.0540540541, 0.0162162162
);

void main() {
    vec3 result = texture(uImage, TexCoord).rgb * weight[0];

    if (uHorizontal) {
        for (int i = 1; i < 5; i++) {
            result += texture(uImage,
                TexCoord + vec2(uTexelSize.x * i, 0.0)).rgb * weight[i];
            result += texture(uImage,
                TexCoord - vec2(uTexelSize.x * i, 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 5; i++) {
            result += texture(uImage,
                TexCoord + vec2(0.0, uTexelSize.y * i)).rgb * weight[i];
            result += texture(uImage,
                TexCoord - vec2(0.0, uTexelSize.y * i)).rgb * weight[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
```

Bloom은 다운샘플 레벨마다 블러 2패스 (수평+수직) + 업샘플 패스가 필요하므로 추가 FBO가 다수 필요하다. 일반적으로 4-5단계 다운샘플.

```cpp
class BloomEffect : public PostProcessEffect {
    static constexpr int MIP_LEVELS = 5;

    RenderTarget mipChain[MIP_LEVELS];  // 해상도 절반씩 감소
    RenderTarget blurTemp;               // 블러 임시 버퍼

    Shader* brightExtractShader;
    Shader* gaussianBlurShader;
    Shader* bloomCompositeShader;

    float threshold = 1.0f;
    float intensity = 1.0f;
};
```

#### Screen Shake 구현

Screen Shake는 포스트 프로세싱이 아닌 카메라 오프셋으로 구현하는 것이 더 적절하지만, UV 오프셋 방식도 가능하다:

```cpp
class ScreenShake {
public:
    void Trigger(float intensity, float duration, float frequency = 30.0f) {
        this->intensity = intensity;
        this->duration = duration;
        this->frequency = frequency;
        timer = duration;
    }

    void Update(float dt) {
        if (timer <= 0) return;
        timer -= dt;

        float decay = timer / duration; // 선형 감쇠
        float t = (duration - timer) * frequency;
        offsetX = sinf(t * 6.28f) * intensity * decay;
        offsetY = cosf(t * 4.37f) * intensity * decay * 0.7f;
        // 두 축에 다른 주파수 사용하여 자연스럽게
    }

    // Camera2D::GetViewMatrix()에서 이 오프셋을 적용
    float offsetX = 0, offsetY = 0;

private:
    float intensity = 0, duration = 0, frequency = 0;
    float timer = 0;
};
```

#### Vignette 셰이더 (vignette.frag)

```glsl
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D uScene;
uniform float uIntensity;  // 0.0 ~ 1.0
uniform float uSmoothness; // 가장자리 부드러움

void main() {
    vec4 color = texture(uScene, TexCoord);
    vec2 uv = TexCoord * (1.0 - TexCoord);
    float vignette = uv.x * uv.y * 15.0;
    vignette = clamp(pow(vignette, uSmoothness), 0.0, 1.0);
    color.rgb *= mix(1.0, vignette, uIntensity);
    FragColor = color;
}
```

#### Chromatic Aberration 셰이더

```glsl
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D uScene;
uniform float uOffset;  // 0.001 ~ 0.01

void main() {
    vec2 dir = TexCoord - vec2(0.5);
    float r = texture(uScene, TexCoord + dir * uOffset).r;
    float g = texture(uScene, TexCoord).g;
    float b = texture(uScene, TexCoord - dir * uOffset).b;
    float a = texture(uScene, TexCoord).a;
    FragColor = vec4(r, g, b, a);
}
```

### 4.4 복잡도 및 의존성

- **예상 복잡도**: Large (2-3주)
  - FBO 인프라 + Ping-Pong 파이프라인: Medium (1주)
  - Bloom: Medium (1주)
  - Vignette + Chromatic Aberration + Color Grading: Small (3-5일)
  - Screen Shake: Small (1-2일)
- **의존성**: Custom Shader System (다수의 전용 셰이더 필요)
- **영향 범위**: 메인 렌더 루프 변경 (FBO로 렌더링), 리사이즈 핸들링

---

## 5. Sprite Atlas / Texture Packing

### 5.1 무엇이며 왜 필수인가

Sprite Atlas는 여러 개의 개별 스프라이트 이미지를 하나의 큰 텍스처에 묶는 기법이다. 배칭의 핵심 전제 조건이다. 10종류의 텍스처를 사용하는 1,000개 스프라이트는 배칭 없이 1,000 Draw Call, 텍스처별 배칭으로는 10 Draw Call이지만, **하나의 아틀라스에 전부 담으면 1 Draw Call**로 줄어든다.

추가 이점:
- GPU 텍스처 바인딩 전환 최소화
- 텍스처 메모리 낭비 감소 (power-of-2 패딩 제거)
- 텍스처 필터링 시 인접 텍셀 접근이 캐시 친화적

### 5.2 Unity의 구현 방식

- **Sprite Atlas asset**: 에디터에서 스프라이트들을 그룹으로 묶어 아틀라스 생성
- **빌드 타임 패킹**: 빌드 시 최적화된 배치. MaxRects 알고리즘 사용
- **Variant 시스템**: 해상도별 아틀라스 (모바일 1/2, 데스크탑 원본)
- **Late Binding**: 스프라이트가 아틀라스 참조 시 런타임에 실제 아틀라스 할당
- **Tight Packing**: 알파 채널 기준으로 투명 영역을 제거하고 빈틈없이 배치

### 5.3 OpenGL 3.3 구현 방안

#### 빌드타임 vs 런타임 패킹

| 방식 | 장점 | 단점 | 권장 시나리오 |
|------|------|------|---------------|
| **빌드타임** | 최적 배치, 로드 시간 0 | 에셋 변경 시 재빌드 필요 | 최종 빌드, 성능 최우선 |
| **런타임** | 유연성, 동적 에셋 | 로딩 시간 증가, 최적화 한계 | 에디터, 프로토타이핑, 동적 콘텐츠 |

Molga Engine은 에디터가 있으므로 **에디터에서 빌드타임 패킹 + 런타임 로드** 조합이 이상적이다.

#### MaxRects 빈 패킹 알고리즘

MaxRects는 직사각형 빈 패킹 문제에서 가장 널리 사용되는 알고리즘이다. 여러 휴리스틱 중 Best Short Side Fit (BSSF)이 가장 좋은 결과를 보인다.

```cpp
struct AtlasRect {
    int x, y, width, height;
    std::string spriteName;
};

class MaxRectsPacker {
public:
    MaxRectsPacker(int atlasWidth, int atlasHeight, int padding = 1)
        : width(atlasWidth), height(atlasHeight), padding(padding)
    {
        freeRects.push_back({0, 0, atlasWidth, atlasHeight});
    }

    // 스프라이트를 아틀라스에 배치. 실패 시 (-1,-1) 반환
    AtlasRect Insert(int spriteW, int spriteH) {
        int pw = spriteW + padding * 2;
        int ph = spriteH + padding * 2;

        // Best Short Side Fit: 가장 적합한 빈 사각형 찾기
        int bestShortSideFit = INT_MAX;
        int bestLongSideFit = INT_MAX;
        int bestIndex = -1;
        AtlasRect bestRect = {-1, -1, 0, 0};

        for (int i = 0; i < freeRects.size(); i++) {
            auto& fr = freeRects[i];
            if (fr.width >= pw && fr.height >= ph) {
                int leftoverH = abs(fr.width - pw);
                int leftoverV = abs(fr.height - ph);
                int shortSideFit = std::min(leftoverH, leftoverV);
                int longSideFit = std::max(leftoverH, leftoverV);

                if (shortSideFit < bestShortSideFit ||
                    (shortSideFit == bestShortSideFit &&
                     longSideFit < bestLongSideFit))
                {
                    bestRect = {fr.x + padding, fr.y + padding,
                                spriteW, spriteH};
                    bestShortSideFit = shortSideFit;
                    bestLongSideFit = longSideFit;
                    bestIndex = i;
                }
            }
        }

        if (bestIndex == -1) return {-1, -1, 0, 0}; // 공간 부족

        // 사용된 영역을 빈 사각형 목록에서 분할
        SplitFreeRect(freeRects[bestIndex],
                      {bestRect.x - padding, bestRect.y - padding,
                       pw, ph});
        PruneFreeRects();

        return bestRect;
    }

private:
    int width, height, padding;
    std::vector<Rect> freeRects;

    void SplitFreeRect(const Rect& freeRect, const Rect& usedRect);
    void PruneFreeRects(); // 다른 빈 사각형에 완전히 포함된 것 제거
};
```

#### 아틀라스 빌드 파이프라인

```cpp
struct SpriteAtlasEntry {
    std::string name;
    float u0, v0, u1, v1;  // 아틀라스 내 UV 좌표 (정규화)
    int originalWidth, originalHeight;
};

class SpriteAtlas {
public:
    // 빌드타임: 개별 이미지들로부터 아틀라스 생성
    bool Build(const std::vector<std::string>& imagePaths,
               int maxSize = 4096)
    {
        // 1. 모든 이미지 로드 및 크기 수집
        // 2. 큰 것부터 정렬 (면적 기준 내림차순)
        // 3. MaxRectsPacker로 배치
        // 4. 큰 텍스처에 픽셀 복사
        // 5. UV 좌표 계산 및 저장
        // 6. 아틀라스 이미지 + 메타데이터(JSON) 저장
    }

    // 런타임: 아틀라스 로드
    bool Load(const std::string& atlasImagePath,
              const std::string& atlasMetaPath)
    {
        texture = new Texture(atlasImagePath.c_str());
        // JSON에서 각 스프라이트의 UV 정보 로드
        // entries 맵에 저장
    }

    // 스프라이트 이름으로 UV 조회
    const SpriteAtlasEntry* GetEntry(const std::string& name) const;

    Texture* GetTexture() const { return texture; }

private:
    Texture* texture = nullptr;
    std::unordered_map<std::string, SpriteAtlasEntry> entries;
};
```

#### 아틀라스 메타데이터 JSON 형식

```json
{
    "atlas": {
        "width": 2048,
        "height": 2048,
        "padding": 1
    },
    "sprites": {
        "player_idle_0": {
            "x": 0, "y": 0, "w": 64, "h": 64,
            "u0": 0.0, "v0": 0.0,
            "u1": 0.03125, "v1": 0.03125
        },
        "enemy_walk_3": {
            "x": 65, "y": 0, "w": 48, "h": 48,
            "u0": 0.031738, "v0": 0.0,
            "u1": 0.055176, "v1": 0.023438
        }
    }
}
```

#### 패딩(Padding)과 블리딩(Bleeding) 방지

텍스처 필터링(특히 바이리니어)에서 인접 스프라이트의 픽셀이 번지는 문제가 발생할 수 있다. 해결 방법:

1. **패딩**: 스프라이트 간 1-2픽셀 간격
2. **Extrude**: 스프라이트의 가장자리 픽셀을 패딩 영역에 복제
3. **Half-Texel Inset**: UV 좌표를 반 텍셀만큼 안으로 축소

```cpp
// UV 보정: 반 텍셀 인셋
float halfTexelX = 0.5f / atlasWidth;
float halfTexelY = 0.5f / atlasHeight;
entry.u0 += halfTexelX;
entry.v0 += halfTexelY;
entry.u1 -= halfTexelX;
entry.v1 -= halfTexelY;
```

### 5.4 복잡도 및 의존성

- **예상 복잡도**: Medium (1-2주)
  - MaxRects 패커: Small (3-5일)
  - 아틀라스 빌드/로드: Small (3-5일)
  - 에디터 통합: Small (2-3일)
- **의존성**: 없음 (독립 구현 가능). Batch Renderer와 함께 사용 시 최대 효과
- **영향 범위**: TextureManager 확장, SpriteRenderer UV 처리 변경, 에디터 아틀라스 빌드 UI

---

## 6. Camera System 고도화

### 6.1 무엇이며 왜 필수인가

현재 Molga Engine의 Camera2D는 단일 카메라로 position/zoom/rotation만 지원한다. 실제 2D 게임에서는:

- **멀티 카메라**: 미니맵, 분할 화면, UI 카메라 분리
- **Pixel-Perfect 렌더링**: 픽셀아트 게임에서 스프라이트 흔들림(subpixel jitter) 방지
- **카메라 전환**: 부드러운 씬 전환, 컷씬
- **추적 시스템**: 대상을 부드럽게 따라가는 카메라 (Cinemachine 스타일)

### 6.2 Unity의 구현 방식

- **Camera 컴포넌트**: depth 값으로 렌더링 순서 결정. culling mask로 특정 레이어만 렌더링
- **Camera Stacking**: Base Camera 위에 Overlay Camera를 겹침 (URP). UI, 파티클 등 별도 카메라
- **Cinemachine**: Virtual Camera 개념. Brain이 활성 VCam을 선택하여 실제 카메라에 적용. Follow/LookAt 대상, Damping, Dead Zone, Confiner(경계 제한) 등
- **Pixel Perfect Camera**: 게임 해상도 고정, 스냅 기능, 업스케일 필터

### 6.3 OpenGL 3.3 구현 방안

#### 멀티 카메라 아키텍처

```cpp
class Camera2D {
public:
    // ... 기존 멤버 ...

    // 새 멤버
    int depth = 0;              // 렌더링 순서 (낮을수록 먼저)
    uint32_t cullingMask = 0xFFFFFFFF; // 레이어 마스크
    Rect viewport = {0, 0, 1, 1};     // 정규화 뷰포트 (0-1)

    // 뷰포트 설정: 화면의 일부분만 렌더링
    void SetViewport(float x, float y, float w, float h) {
        viewport = {x, y, w, h};
    }

    void ApplyViewport(int screenW, int screenH) {
        glViewport(
            (int)(viewport.x * screenW),
            (int)(viewport.y * screenH),
            (int)(viewport.w * screenW),
            (int)(viewport.h * screenH)
        );
    }

    // 이 카메라가 특정 sorting layer를 렌더링하는지 확인
    bool ShouldRender(uint32_t layerBit) const {
        return (cullingMask & layerBit) != 0;
    }
};
```

#### 카메라 관리자

```cpp
class CameraManager {
public:
    Camera2D* CreateCamera(const std::string& name) {
        cameras.push_back({name, std::make_unique<Camera2D>(/*...*/), {}});
        SortByDepth();
        return cameras.back().camera.get();
    }

    // depth 순서대로 렌더링
    void RenderAll(RenderSystem& renderSystem) {
        for (auto& entry : cameras) {
            if (!entry.active) continue;

            entry.camera->ApplyViewport(screenW, screenH);

            // 이 카메라의 culling mask에 해당하는 오브젝트만 렌더링
            renderSystem.RenderWithCamera(entry.camera.get());
        }
    }

private:
    struct CameraEntry {
        std::string name;
        std::unique_ptr<Camera2D> camera;
        std::unique_ptr<CameraController> controller; // optional
        bool active = true;
    };
    std::vector<CameraEntry> cameras;

    void SortByDepth() {
        std::sort(cameras.begin(), cameras.end(),
                  [](const auto& a, const auto& b) {
                      return a.camera->depth < b.camera->depth;
                  });
    }
};
```

#### Pixel-Perfect Camera

```cpp
class PixelPerfectCamera {
public:
    // referenceResolution: 게임 디자인 기준 해상도
    // pixelsPerUnit: 1 월드 유닛당 픽셀 수
    PixelPerfectCamera(int refWidth, int refHeight, int ppu)
        : refWidth(refWidth), refHeight(refHeight), pixelsPerUnit(ppu) {}

    void UpdateProjection(Camera2D& camera, int screenW, int screenH) {
        // 정수 배율 계산 (정수로 스냅해야 픽셀이 균일함)
        int scaleX = std::max(1, screenW / refWidth);
        int scaleY = std::max(1, screenH / refHeight);
        int scale = std::min(scaleX, scaleY); // 가로/세로 중 작은 배율

        // 실제 렌더링 해상도
        int renderW = screenW / scale;
        int renderH = screenH / scale;

        // 직교 투영: 1 unit = ppu pixels
        float halfW = (float)renderW / (2.0f * pixelsPerUnit);
        float halfH = (float)renderH / (2.0f * pixelsPerUnit);
        camera.SetProjection(-halfW, halfW, -halfH, halfH);
    }

    // 카메라 위치를 픽셀 그리드에 스냅
    void SnapPosition(Camera2D& camera) {
        float snapUnit = 1.0f / pixelsPerUnit;
        float snappedX = roundf(camera.GetX() / snapUnit) * snapUnit;
        float snappedY = roundf(camera.GetY() / snapUnit) * snapUnit;
        camera.SetPosition(snappedX, snappedY);
    }

private:
    int refWidth, refHeight, pixelsPerUnit;
};
```

#### 카메라 추적 시스템 (Cinemachine 스타일)

```cpp
class CameraFollower {
public:
    // 추적 대상
    void SetTarget(float* targetX, float* targetY) {
        this->targetX = targetX;
        this->targetY = targetY;
    }

    // Dead Zone: 이 영역 안에서는 카메라가 움직이지 않음
    void SetDeadZone(float width, float height) {
        deadZoneW = width;
        deadZoneH = height;
    }

    // Damping: 부드러운 추적 (0=즉시, 1=느림)
    void SetDamping(float dampX, float dampY) {
        this->dampX = dampX;
        this->dampY = dampY;
    }

    // 카메라 이동 경계 (맵 밖으로 나가지 않도록)
    void SetBounds(float minX, float minY, float maxX, float maxY) {
        hasBounds = true;
        boundsMin = {minX, minY};
        boundsMax = {maxX, maxY};
    }

    void Update(Camera2D& camera, float dt) {
        if (!targetX || !targetY) return;

        float tx = *targetX;
        float ty = *targetY;
        float cx = camera.GetX();
        float cy = camera.GetY();

        // Dead Zone 적용
        float dx = tx - cx;
        float dy = ty - cy;

        float moveX = 0, moveY = 0;
        if (fabsf(dx) > deadZoneW * 0.5f) {
            moveX = dx - (dx > 0 ? 1 : -1) * deadZoneW * 0.5f;
        }
        if (fabsf(dy) > deadZoneH * 0.5f) {
            moveY = dy - (dy > 0 ? 1 : -1) * deadZoneH * 0.5f;
        }

        // Damping 적용 (지수 감쇠)
        float newX = cx + moveX * (1.0f - expf(-dampX * dt * 10.0f));
        float newY = cy + moveY * (1.0f - expf(-dampY * dt * 10.0f));

        // Bounds 적용
        if (hasBounds) {
            newX = std::clamp(newX, boundsMin.x, boundsMax.x);
            newY = std::clamp(newY, boundsMin.y, boundsMax.y);
        }

        camera.SetPosition(newX, newY);
    }

private:
    float* targetX = nullptr;
    float* targetY = nullptr;
    float deadZoneW = 0, deadZoneH = 0;
    float dampX = 3.0f, dampY = 3.0f;
    bool hasBounds = false;
    Vector2 boundsMin, boundsMax;
};
```

#### 카메라 전환 (Transition)

```cpp
class CameraTransition {
public:
    enum class Type { Cut, LinearBlend, SmoothStep };

    void Start(Camera2D* from, Camera2D* to,
               float duration, Type type = Type::SmoothStep) {
        this->from = from;
        this->to = to;
        this->duration = duration;
        this->type = type;
        timer = 0;
        active = true;
    }

    // 현재 보간된 카메라 상태 반환
    void Update(float dt, Camera2D& output) {
        if (!active) return;
        timer += dt;
        float t = std::clamp(timer / duration, 0.0f, 1.0f);

        if (type == Type::SmoothStep) {
            t = t * t * (3.0f - 2.0f * t); // smoothstep
        }

        // 위치, 줌, 회전 보간
        float x = from->GetX() * (1 - t) + to->GetX() * t;
        float y = from->GetY() * (1 - t) + to->GetY() * t;
        float z = from->GetZoom() * (1 - t) + to->GetZoom() * t;
        float r = from->GetRotation() * (1 - t) + to->GetRotation() * t;

        output.SetPosition(x, y);
        output.SetZoom(z);
        output.SetRotation(r);

        if (timer >= duration) active = false;
    }

private:
    Camera2D *from = nullptr, *to = nullptr;
    float duration = 0, timer = 0;
    Type type;
    bool active = false;
};
```

### 6.4 복잡도 및 의존성

- **예상 복잡도**: Medium (1.5-2주)
  - 멀티 카메라 + 뷰포트: Small (3-5일)
  - Pixel-Perfect: Small (2-3일)
  - Follow/Transition: Medium (1주)
- **의존성**: Sorting Layers (culling mask와 연동)
- **영향 범위**: Camera2D 클래스 확장, 렌더 루프 수정, 에디터 카메라 설정 UI

---

## 7. Custom Shader System

### 7.1 무엇이며 왜 필수인가

현재 Molga Engine은 `default.vert/frag` 단일 셰이더만 사용한다. 라이팅, 포스트 프로세싱, 커스텀 비주얼 이펙트 등을 구현하려면 **다수의 셰이더를 관리하고 런타임에 전환**할 수 있어야 한다. 또한 셰이더에 바인딩된 파라미터(텍스처, 색상, 숫자 등)를 묶어 관리하는 Material 시스템이 필요하다.

### 7.2 Unity의 구현 방식

- **ShaderLab**: Unity의 셰이더 선언 언어. Properties 블록에서 에디터 노출 파라미터를 선언하고, SubShader/Pass 구조로 렌더링 패스 정의
- **ShaderGraph**: 노드 기반 비주얼 셰이더 에디터
- **Material**: 셰이더 + 프로퍼티 값의 인스턴스. 동일 셰이더를 다른 파라미터로 사용 가능
- **Material Property Block**: 인스턴스별 프로퍼티 오버라이드 (배칭 유지하면서)
- **Shader Variants**: 키워드(#define)로 셰이더 변형 생성. multi_compile, shader_feature

### 7.3 OpenGL 3.3 구현 방안

#### 셰이더 매니저

```cpp
class ShaderManager {
public:
    static ShaderManager& Get() {
        static ShaderManager instance;
        return instance;
    }

    // 셰이더 로드 및 캐싱
    Shader* Load(const std::string& name,
                 const std::string& vertPath,
                 const std::string& fragPath) {
        auto it = shaders.find(name);
        if (it != shaders.end()) return it->second.get();

        auto shader = std::make_unique<Shader>(
            vertPath.c_str(), fragPath.c_str());
        Shader* ptr = shader.get();
        shaders[name] = std::move(shader);
        return ptr;
    }

    Shader* Get(const std::string& name) {
        auto it = shaders.find(name);
        return (it != shaders.end()) ? it->second.get() : nullptr;
    }

    // 핫 리로드: 파일 변경 감지 시 재컴파일
    void ReloadAll() {
        for (auto& [name, shader] : shaders) {
            shader->Reload(); // 원본 경로로 재컴파일
        }
    }

    // 특정 셰이더만 리로드
    bool Reload(const std::string& name) {
        auto it = shaders.find(name);
        if (it == shaders.end()) return false;
        return it->second->Reload();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
};
```

#### Shader 클래스 확장 (핫 리로드 지원)

```cpp
class Shader {
public:
    // ... 기존 인터페이스 ...

    // 핫 리로드: 새 프로그램을 컴파일하고, 성공 시에만 교체
    bool Reload() {
        std::string newVert = LoadShaderSource(vertexPath.c_str());
        std::string newFrag = LoadShaderSource(fragmentPath.c_str());

        GLuint vs = CompileShader(newVert.c_str(), GL_VERTEX_SHADER);
        if (!CheckCompileSuccess(vs)) {
            glDeleteShader(vs);
            return false;
        }

        GLuint fs = CompileShader(newFrag.c_str(), GL_FRAGMENT_SHADER);
        if (!CheckCompileSuccess(fs)) {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        GLuint newProgram = glCreateProgram();
        glAttachShader(newProgram, vs);
        glAttachShader(newProgram, fs);
        glLinkProgram(newProgram);

        if (!CheckLinkSuccess(newProgram)) {
            glDeleteProgram(newProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        // 성공: 이전 프로그램 삭제 후 교체
        glDeleteProgram(programID);
        programID = newProgram;
        uniformCache.clear(); // 캐시 무효화

        glDeleteShader(vs);
        glDeleteShader(fs);
        return true;
    }

private:
    std::string vertexPath;   // 원본 경로 보존
    std::string fragmentPath;
};
```

#### Material 시스템

Material은 셰이더 + 프로퍼티 값의 바인딩이다.

```cpp
class Material {
public:
    Material(Shader* shader) : shader(shader) {}

    void SetShader(Shader* s) { shader = s; }
    Shader* GetShader() const { return shader; }

    // 프로퍼티 설정 (타입별)
    void SetFloat(const std::string& name, float value) {
        floatProps[name] = value;
    }
    void SetVec4(const std::string& name,
                 float x, float y, float z, float w) {
        vec4Props[name] = {x, y, z, w};
    }
    void SetTexture(const std::string& name, Texture* tex) {
        textureProps[name] = tex;
    }
    void SetInt(const std::string& name, int value) {
        intProps[name] = value;
    }

    // 모든 프로퍼티를 셰이더에 바인딩
    void Apply() const {
        if (!shader) return;
        shader->Use();

        for (auto& [name, val] : floatProps)
            shader->SetFloat(name.c_str(), val);

        for (auto& [name, val] : intProps)
            shader->SetInt(name.c_str(), val);

        for (auto& [name, val] : vec4Props)
            shader->SetVec4(name.c_str(),
                            val[0], val[1], val[2], val[3]);

        int texSlot = 0;
        for (auto& [name, tex] : textureProps) {
            shader->SetInt(name.c_str(), texSlot);
            tex->Bind(texSlot);
            texSlot++;
        }
    }

    // Material ID -- 배칭 시 동일 머티리얼 판별용
    size_t GetID() const {
        // shader ID + 프로퍼티 해시 조합
        return std::hash<GLuint>{}(shader->GetID());
    }

private:
    Shader* shader = nullptr;
    std::unordered_map<std::string, float> floatProps;
    std::unordered_map<std::string, int> intProps;
    std::unordered_map<std::string, std::array<float, 4>> vec4Props;
    std::unordered_map<std::string, Texture*> textureProps;
};
```

#### 셰이더 프리프로세서 (#define 기반 변형)

```cpp
class ShaderPreprocessor {
public:
    // #define 삽입으로 셰이더 변형 생성
    static std::string Process(const std::string& source,
                               const std::vector<std::string>& defines)
    {
        std::string result;
        // #version 행 뒤에 #define 삽입
        size_t versionEnd = source.find('\n');
        result = source.substr(0, versionEnd + 1);

        for (const auto& def : defines) {
            result += "#define " + def + "\n";
        }

        result += source.substr(versionEnd + 1);
        return result;
    }
};

// 사용 예:
// 노멀맵 있는 스프라이트: {"HAS_NORMAL_MAP", "MAX_LIGHTS 8"}
// 노멀맵 없는 스프라이트: {"MAX_LIGHTS 8"}
```

#### 파일 감시 기반 핫 리로드

```cpp
class ShaderFileWatcher {
public:
    void Watch(const std::string& shaderName,
               const std::string& vertPath,
               const std::string& fragPath) {
        entries[shaderName] = {
            vertPath, fragPath,
            GetFileModTime(vertPath),
            GetFileModTime(fragPath)
        };
    }

    // 주기적으로 호출 (에디터 모드에서만)
    void CheckForChanges() {
        for (auto& [name, entry] : entries) {
            auto vertTime = GetFileModTime(entry.vertPath);
            auto fragTime = GetFileModTime(entry.fragPath);

            if (vertTime != entry.vertModTime ||
                fragTime != entry.fragModTime)
            {
                if (ShaderManager::Get().Reload(name)) {
                    Log::Info("Shader '%s' hot-reloaded", name.c_str());
                } else {
                    Log::Error("Shader '%s' reload failed", name.c_str());
                }
                entry.vertModTime = vertTime;
                entry.fragModTime = fragTime;
            }
        }
    }

private:
    struct WatchEntry {
        std::string vertPath, fragPath;
        std::filesystem::file_time_type vertModTime, fragModTime;
    };
    std::unordered_map<std::string, WatchEntry> entries;
};
```

### 7.4 복잡도 및 의존성

- **예상 복잡도**: Medium (1-1.5주)
  - ShaderManager + Material: Small (3-5일)
  - 핫 리로드: Small (2-3일)
  - 프리프로세서: Small (1-2일)
- **의존성**: 없음 (가장 먼저 구현 권장 -- 다른 모든 시스템이 의존)
- **영향 범위**: Shader 클래스 확장, SpriteRenderer에 Material 연결, 에디터 Material Inspector

---

## 8. Debug / Gizmo Rendering

### 8.1 무엇이며 왜 필수인가

디버그 렌더링은 게임 개발 중 콜라이더 경계, 물리 레이캐스트, 네비게이션 경로, 오브젝트 원점 등을 시각적으로 확인하는 기능이다. 이것 없이는 눈에 보이지 않는 시스템(충돌, AI 경로, 카메라 영역 등)을 디버깅하기 매우 어렵다.

핵심 요구사항:
- **게임 렌더링에 영향 없음**: 별도 패스로 겹쳐 그림
- **즉시 모드 API**: 매 프레임 선언적으로 사용, 상태 보존 불필요
- **최소 성능 영향**: 릴리즈 빌드에서 완전 제거 가능

### 8.2 Unity의 구현 방식

- **Gizmos 클래스**: `OnDrawGizmos()` / `OnDrawGizmosSelected()` 콜백에서 즉시 모드 API 사용
  - `Gizmos.DrawLine()`, `Gizmos.DrawWireSphere()`, `Gizmos.DrawWireCube()`, `Gizmos.DrawRay()`
  - `Gizmos.color` 설정으로 색상 변경
- **Handles 클래스**: 에디터 전용. 이동/회전/스케일 핸들, 사용자 정의 핸들
- **Debug.DrawLine()**: 런타임에서도 사용 가능한 디버그 라인. duration 지정 가능
- **에디터 전용**: 게임 빌드에는 포함되지 않음

### 8.3 OpenGL 3.3 구현 방안

#### 즉시 모드 디버그 드로어

```cpp
class DebugDraw {
public:
    static DebugDraw& Get() {
        static DebugDraw instance;
        return instance;
    }

    void Init() {
        // 라인/기본 도형용 셰이더 (단색, 텍스처 없음)
        debugShader = ShaderManager::Get().Load(
            "debug", "shaders/debug.vert", "shaders/debug.frag");

        // 동적 VBO (매 프레임 리빌드)
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     MAX_VERTICES * sizeof(DebugVertex),
                     nullptr, GL_STREAM_DRAW);

        // position (location 0)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              sizeof(DebugVertex),
                              (void*)offsetof(DebugVertex, x));
        glEnableVertexAttribArray(0);

        // color (location 1)
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                              sizeof(DebugVertex),
                              (void*)offsetof(DebugVertex, r));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    // --- 즉시 모드 API ---

    void DrawLine(float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a = 1.0f) {
        if (lineCount >= MAX_LINES) return;
        lines.push_back({{x1, y1, r, g, b, a},
                          {x2, y2, r, g, b, a}});
        lineCount++;
    }

    void DrawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a = 1.0f) {
        DrawLine(x, y, x+w, y, r, g, b, a);
        DrawLine(x+w, y, x+w, y+h, r, g, b, a);
        DrawLine(x+w, y+h, x, y+h, r, g, b, a);
        DrawLine(x, y+h, x, y, r, g, b, a);
    }

    void DrawCircle(float cx, float cy, float radius,
                    float r, float g, float b, float a = 1.0f,
                    int segments = 32) {
        float step = 2.0f * 3.14159f / segments;
        for (int i = 0; i < segments; i++) {
            float angle1 = i * step;
            float angle2 = (i + 1) * step;
            DrawLine(
                cx + cosf(angle1) * radius,
                cy + sinf(angle1) * radius,
                cx + cosf(angle2) * radius,
                cy + sinf(angle2) * radius,
                r, g, b, a
            );
        }
    }

    void DrawPoint(float x, float y, float size,
                   float r, float g, float b, float a = 1.0f) {
        float hs = size * 0.5f;
        DrawLine(x - hs, y, x + hs, y, r, g, b, a);
        DrawLine(x, y - hs, x, y + hs, r, g, b, a);
    }

    void DrawArrow(float x1, float y1, float x2, float y2,
                   float headSize,
                   float r, float g, float b, float a = 1.0f) {
        DrawLine(x1, y1, x2, y2, r, g, b, a);
        // 화살표 머리
        float dx = x2 - x1, dy = y2 - y1;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.001f) return;
        dx /= len; dy /= len;
        float px = -dy, py = dx; // 수직 벡터
        DrawLine(x2, y2,
                 x2 - dx*headSize + px*headSize*0.5f,
                 y2 - dy*headSize + py*headSize*0.5f,
                 r, g, b, a);
        DrawLine(x2, y2,
                 x2 - dx*headSize - px*headSize*0.5f,
                 y2 - dy*headSize - py*headSize*0.5f,
                 r, g, b, a);
    }

    // AABB (박스 콜라이더 시각화)
    void DrawAABB(const AABB& aabb,
                  float r, float g, float b, float a = 1.0f) {
        DrawRect(aabb.x, aabb.y, aabb.width, aabb.height, r, g, b, a);
    }

    // --- 렌더링 (프레임 끝에 호출) ---

    void Render(Camera2D* camera) {
        if (lines.empty()) return;

        debugShader->Use();

        mat4x4 projView;
        mat4x4 proj, view;
        camera->GetProjectionMatrix(proj);
        camera->GetViewMatrix(view);
        mat4x4_mul(projView, proj, view);
        debugShader->SetMat4("projection", (float*)projView);

        // 정점 데이터 업로드
        std::vector<DebugVertex> vertices;
        vertices.reserve(lines.size() * 2);
        for (const auto& line : lines) {
            vertices.push_back(line.v0);
            vertices.push_back(line.v1);
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices.size() * sizeof(DebugVertex),
                        vertices.data());

        glBindVertexArray(vao);
        glDrawArrays(GL_LINES, 0, (GLsizei)vertices.size());
        glBindVertexArray(0);

        // 매 프레임 클리어
        lines.clear();
        lineCount = 0;
    }

private:
    struct DebugVertex {
        float x, y;
        float r, g, b, a;
    };
    struct DebugLine {
        DebugVertex v0, v1;
    };

    static constexpr int MAX_LINES = 100000;
    static constexpr int MAX_VERTICES = MAX_LINES * 2;

    Shader* debugShader = nullptr;
    GLuint vao = 0, vbo = 0;
    std::vector<DebugLine> lines;
    int lineCount = 0;
};
```

#### 디버그 셰이더 (debug.vert / debug.frag)

```glsl
// debug.vert
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;

out vec4 Color;
uniform mat4 projection;

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    Color = aColor;
}
```

```glsl
// debug.frag
#version 330 core
out vec4 FragColor;
in vec4 Color;

void main() {
    FragColor = Color;
}
```

#### 컴파일 타임 제거 (릴리즈 빌드)

```cpp
#ifdef MOLGA_DEBUG
    #define DEBUG_DRAW_LINE(x1,y1,x2,y2,r,g,b) \
        DebugDraw::Get().DrawLine(x1,y1,x2,y2,r,g,b)
    #define DEBUG_DRAW_RECT(x,y,w,h,r,g,b) \
        DebugDraw::Get().DrawRect(x,y,w,h,r,g,b)
    #define DEBUG_DRAW_CIRCLE(x,y,rad,r,g,b) \
        DebugDraw::Get().DrawCircle(x,y,rad,r,g,b)
    #define DEBUG_RENDER(cam) \
        DebugDraw::Get().Render(cam)
#else
    #define DEBUG_DRAW_LINE(x1,y1,x2,y2,r,g,b) ((void)0)
    #define DEBUG_DRAW_RECT(x,y,w,h,r,g,b) ((void)0)
    #define DEBUG_DRAW_CIRCLE(x,y,rad,r,g,b) ((void)0)
    #define DEBUG_RENDER(cam) ((void)0)
#endif
```

#### 사용 예시 (BoxCollider2D 시각화)

```cpp
// 게임 루프에서
for (auto& obj : scene->GetGameObjects()) {
    auto* collider = obj->GetComponent<BoxCollider2D>();
    if (collider) {
        AABB aabb = collider->GetWorldAABB();
        DEBUG_DRAW_RECT(aabb.x, aabb.y, aabb.width, aabb.height,
                        0.0f, 1.0f, 0.0f); // 초록색
    }
}

// 카메라 영역 시각화
DEBUG_DRAW_RECT(camBounds.x, camBounds.y,
                camBounds.w, camBounds.h,
                1.0f, 1.0f, 0.0f); // 노란색

// 라이트 반경 시각화
DEBUG_DRAW_CIRCLE(light.x, light.y, light.outerRadius,
                  1.0f, 0.8f, 0.0f); // 오렌지

// 프레임 끝에 한 번에 렌더링
DEBUG_RENDER(mainCamera);
```

### 8.4 복잡도 및 의존성

- **예상 복잡도**: Small (3-5일)
- **의존성**: Custom Shader System (debug 셰이더 필요)
- **영향 범위**: 독립적. 기존 렌더링에 영향 없음. 에디터 모드에서만 활성화

---

## 구현 우선순위 및 의존성 맵

### 의존성 그래프

```
[7. Custom Shader System] ─── 기반 시스템 (최우선 구현)
         │
         ├──→ [8. Debug/Gizmo]          (독립적, shader 의존)
         │
         ├──→ [5. Sprite Atlas]          (독립적)
         │        │
         │        └──→ [1. Batch Renderer] ── (atlas + shader 의존)
         │                   │
         │                   └──→ [2. Sorting Layers] ── (batcher와 연동 설계)
         │
         ├──→ [4. Post-Processing] ──── (FBO 인프라, shader 의존)
         │        │
         │        └──→ [3. 2D Lighting] ── (FBO 공유, post-process 인프라 활용)
         │
         └──→ [6. Camera System] ──── (독립적, sorting과 연동)
```

### 권장 구현 순서

| 순서 | 시스템 | 복잡도 | 기간 | 이유 |
|------|--------|--------|------|------|
| **1** | Custom Shader System | Medium | 1-1.5주 | 모든 시스템의 기반. 다중 셰이더/머티리얼 필수 |
| **2** | Debug/Gizmo Rendering | Small | 3-5일 | 이후 모든 시스템 개발 시 디버깅 도구로 활용 |
| **3** | Sprite Atlas | Medium | 1-2주 | Batch Renderer의 전제 조건 |
| **4** | Batch Renderer | Large | 2-3주 | 성능의 핵심. Atlas와 함께 구현 |
| **5** | Sorting Layers | Medium | 1-1.5주 | Batcher의 정렬 로직과 통합 |
| **6** | Camera System | Medium | 1.5-2주 | 독립적이나 Sorting과 연동 |
| **7** | Post-Processing | Large | 2-3주 | FBO 인프라 구축. Lighting의 전제 |
| **8** | 2D Lighting | Very Large | 3-5주 | 가장 복잡. FBO/셰이더 인프라 완비 후 구현 |

**총 예상 기간**: 약 13-22주 (3-5개월)

### 마일스톤 제안

**Milestone 1 -- 렌더링 인프라** (3-4주)
- Custom Shader System + Material
- Debug/Gizmo Rendering
- Sprite Atlas (MaxRects 패커)

**Milestone 2 -- 성능 최적화** (3-4.5주)
- Batch Renderer (10,000+ 스프라이트 목표)
- Sorting Layers & Render Order
- 파티클 인스턴싱

**Milestone 3 -- 카메라 및 비주얼** (3.5-5주)
- Camera System 고도화
- Post-Processing Pipeline (Bloom, Vignette 등)

**Milestone 4 -- 2D 라이팅** (3-5주)
- Point / Global / Spot Light 2D
- 노멀맵 지원
- 2D Shadow Casting

---

### 현재 Renderer와의 호환 전략

현재 `Renderer::DrawSprite()`의 스프라이트당 Draw Call 방식을 유지하면서 `SpriteBatcher`를 병렬로 추가하는 것을 권장한다. 즉:

1. **기존 Renderer**: 단일 스프라이트 즉시 렌더링 용도로 유지 (디버그, 에디터 오버레이 등)
2. **새 SpriteBatcher**: 게임 씬의 메인 렌더링 경로. SpriteRenderer 컴포넌트가 Batcher를 사용
3. **점진적 마이그레이션**: 기존 코드가 Renderer를 사용하는 부분을 단계적으로 Batcher로 전환

이렇게 하면 기존 기능을 깨뜨리지 않으면서 새 시스템을 구축할 수 있다.
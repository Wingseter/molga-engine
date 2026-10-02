# UI/Text Production Backbone 설계

> 작성일: 2026-08-20
>
> 상태: 설계 섹션 승인 완료, 문서 명세 검토 대기
>
> 구현 상태: **NOT STARTED**
>
> 대상: macOS, Molga Engine 마일스톤 A
>
> 기준선: branch `levelup`, commit `94cb608`

## 1. 문서 권위와 성숙도

이 문서는 Molga Engine에서 상용 2D 게임 제작에 사용할 텍스트와 런타임 UI의
생산 기반을 정의한다. 다음 네 설계 섹션은 사용자 승인을 받았다.

1. 패키지 폰트 + HarfBuzz/ICU 기반 결정론적 텍스트 파이프라인
2. 저작 컴포넌트, 직렬화와 실행 상태의 경계
3. 프레임 데이터 흐름과 fail-closed 실패 정책
4. 문자·UI·IME·패키지·성능 완료 게이트

이 문서는 구현 완료 증거가 아니다. 코드, 패키지 또는 macOS 실행 결과가 각 완료
게이트를 실제로 통과하기 전에는 이 기능을 `PRODUCTION READY`로 표시하지 않는다.
또한 이 마일스톤은 Molga Engine 전체가 상용 엔진 수준에 도달했다는 뜻이 아니다.

충돌하는 경우 이 문서가 `docs/plan/subsystems/05_text.md`의 ASCII/코드포인트 기반
폰트 확장안보다 우선한다. 기존 문서의 월드 텍스트 컴포넌트 목표는 유지하지만,
문자 처리와 폰트 자산은 이 문서의 공통 서비스로 대체한다.

## 2. 현재 기준선과 문제

현재 엔진에는 재사용할 수 있는 기반이 있다.

- `TextRenderer2D`, `UILabel`, `UIButton`, `RectTransform`, `UICanvas`
- GUID 기반 에셋 참조와 `SceneSerializer` 컴포넌트 round-trip
- prefab 복제 시 `Component::RemapReferences(...)` 경로
- `FontFace`와 `FontAtlas`의 정적 outline rasterization
- 정렬 가능한 render queue와 SDL_GPU/Metal 렌더러
- Property descriptor, snapshot command와 Undo/Redo 기반 에디터 변경 경로

그러나 현 텍스트 경로는 UTF-8을 Unicode scalar/codepoint로 해석한 뒤 codepoint별
atlas quad로 제출한다. 이는 다음을 올바르게 표현할 수 없다.

- Arabic joining과 Indic 재배치
- ligature, combining mark와 grapheme cluster
- BiDi 문단의 visual order와 caret/hit-test 매핑
- Unicode 줄바꿈과 CJK 금칙 처리
- 하나의 grapheme를 보존하는 폰트 fallback

현 UI는 pointer 중심의 평면 순회이며 intrinsic layout, focus/navigation, clipping,
scrolling, editable text와 SDL IME 소유권 중재가 없다. 따라서 기존 경로에 예외를
추가하는 방식이 아니라, 텍스트 분석부터 UI 입력·렌더까지 하나의 snapshot 계약으로
연결해야 한다.

## 3. 확정 결정

### 3.1 의존성 및 재현성

- 게임이 사용하는 모든 폰트는 프로젝트 에셋 또는 엔진 필수 에셋으로 패키지한다.
- HarfBuzz가 glyph shaping과 cluster mapping을 담당한다.
- ICU4C가 grapheme, line-break, script 속성 및 BiDi를 담당한다.
- HarfBuzz와 ICU는 아래 lock의 submodule/source와 data artifact로 정적 링크한다.
  floating tag, Homebrew runtime, system font와 network FetchContent에 의존하지 않는다.
- 최초 생산 기준은 완전한 pinned ICU data를 포함한다. data filtering은 전체 문자
  fixture의 결과 동등성이 입증된 뒤에만 새 설계 승인을 받아 허용한다.
- 폰트 bytes, HarfBuzz/ICU revision, ICU data와 라이선스/고지 파일의 SHA-256을
  package manifest에 기록한다.

| 항목 | 고정 값 |
|---|---|
| HarfBuzz | `14.3.1`, commit `ab5ecbb83985034a76214ac0b2b833dcd590d774` |
| ICU4C | `78.3`, commit `21d1eb0f306e1141c10931e914dfc038c06121da` |
| outline rasterizer | current `imstb_truetype.h` from ImGui commit `b48d1afbe8ee8b238e2961dc363a949dd7304e23`, file SHA-256 `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528` |
| ICU little-endian data archive | `icu4c-78.3-data-bin-l.zip`, SHA-256 `982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c` |
| packaged ICU data | `icudt78l.dat`, 33,107,232 bytes, SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b` |
| ICU license | SHA-256 `e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af` |

HarfBuzz build는 `BUILD_SHARED_LIBS=OFF`, `HB_HAVE_ICU=ON`이며 CoreText shaper와
모든 불필요한 optional backend를 끈다. 구체적으로 `HB_HAVE_CORETEXT`, Cairo,
FreeType, Graphite2, GLib/GObject, introspection은 `OFF`이고 `HB_BUILD_UTILS`,
`HB_BUILD_SUBSET`, `HB_BUILD_RASTER`, `HB_BUILD_VECTOR`, `HB_BUILD_GPU`와 GPU demo도
`OFF`다. buffer에는 vendored ICU의 `hb_icu_get_unicode_funcs()`를 명시한다.

ICU는 vendored source에서 `icuuc`와 `icui18n` static library만 생산한다. shared
library, tools, samples, tests, extras와 `icuio`는 package/runtime dependency가 아니다.
`U_STATIC_IMPLEMENTATION`을 사용하고 data는 위의 complete archive만 허용한다.
runtime은 manifest와 bytes를 검증한 뒤 aligned/mapped `icudt78l.dat`를
`udata_setCommonData`로 등록하고 file access를 package data로 제한한 다음 `u_init`을
호출한다. mapped bytes는 `u_cleanup`이 끝날 때까지 살아 있어야 하며 이 초기화 전에
ICU/HarfBuzz text object를 만들 수 없다.

configure 결과에는 모든 option, compiler, source revision과 실제 include/library
canonical path를 machine-readable dependency lock으로 남긴다. HarfBuzz/ICU가 repo의
고정 source/build prefix 밖에서 resolve되면 configure가 실패한다.

rasterizer header는 위 bytes를 text-owned third-party snapshot으로 옮겨 고정하며
runtime text core가 ImGui의 private header 경로에 의존하지 않게 한다. 이 file SHA와
license도 text dependency lock/package notice에 포함한다.

macOS CoreText와 system fallback은 이 경로의 대체 구현이 아니다. 에디터와 package가
같은 입력에서 같은 결과를 내야 하므로 host OS의 설치 폰트나 shaping 차이에 기대지
않는다.

### 3.2 목표

- 동일한 UTF-8, 폰트 에셋, locale, logical layout constraint에서
  editor/runtime/package가 동일한 glyph sequence, cluster, logical line과 hit-test
  결과를 생성한다. output backing scale에 따른 raster pixel 차이는 이 동일성 계약에
  포함하지 않는다.
- Latin, Arabic, Hebrew, Indic, Thai, CJK와 combining sequence를 단일 파이프라인으로
  처리한다.
- wrap, ellipsis, alignment, caret, selection, focus, IME와 clip-aware input을
  지원한다.
- layout 또는 text가 변하지 않은 프레임에는 reshape/reflow하지 않는다.
- 누락 폰트, ICU data 또는 라이선스가 조용한 ASCII fallback으로 퇴행하지 않는다.

### 3.3 이번 마일스톤의 비목표

- COLR/CPAL, SVG, bitmap color emoji 렌더링
- non-default variable font axis 렌더링
- arbitrary stencil/alpha mask
- macOS Accessibility API native bridge
- Linux/Windows 실행 qualification
- Intel/x86_64와 Universal 2 qualification
- system font discovery 또는 자동 다운로드
- rich-text markup과 언어별 hyphenation

static TTF/OTF outline face만 importer가 생산 지원 대상으로 승인한다. atlas key에는
향후 확장을 위한 variation/render-mode 구분을 유지할 수 있지만, 이번 단계에서는
default/static instance만 허용한다. ZWJ와 variation selector는 grapheme/selection
경계로 검증하되 color glyph가 표시된다고 주장하지 않는다.

## 4. 계층과 책임

새 계약은 네 계층으로 나눈다.

```text
UTF-8 authoring data
  -> UnicodeTextBuffer / ICU analysis
  -> FontFamilyResolver / HarfBuzz shaping
  -> TextLayoutService / GlyphAtlasCache
  -> UILayoutSnapshot / UI input / clipped RenderQueue
```

권장 소유 위치는 다음과 같다.

| 계층 | 책임 | 권장 위치 |
|---|---|---|
| Unicode/Text | UTF-8 mapping, analysis, shaping, paragraph layout | `src/Text/` |
| Asset | font import, family, license와 package dependency | `src/Assets/` |
| UI | layout, focus, input, scrolling과 text editing | `src/UI/` |
| ECS authoring | 직렬화되는 UI/Text 컴포넌트 | `src/ECS/Components/` |
| Rendering | glyph raster/atlas와 clip-aware command adapter | `src/Rendering/` |
| Platform | SDL text-input ownership과 native event queue | 기존 host/platform 경계 |

`UILabel`과 `TextRenderer2D`는 서로 다른 shaping 구현을 갖지 않는다. 둘 다 같은
`TextShapingService`와 `TextLayoutService` 결과를 사용하고, 최종 좌표 공간과 sorting
정책만 달리한다.

## 5. Unicode와 shaping 계약

### 5.1 UnicodeTextBuffer

`UnicodeTextBuffer`는 원본 UTF-8을 보존하며 다음 매핑을 함께 제공한다.

- UTF-8 byte offset
- Unicode scalar index
- ICU UTF-16 code-unit offset
- extended grapheme cluster index
- 진단용 원본 byte range

잘못된 UTF-8 sequence는 표시용 `U+FFFD`로 치환하되 원본 byte offset과 길이를
잃지 않는다. ICU용 sanitized UTF-16 view의 각 code-unit range도 원본 byte/scalar
range로 역매핑할 수 있어야 한다. 저장 문자열을 묵시적으로 NFC/NFD 정규화하지
않는다. 정규화 기능이 필요하면 이후에 명시적인 import/authoring 옵션으로 추가한다.

### 5.2 ICU 분석

ICU는 paragraph 단위로 다음 결과를 만든다.

- extended grapheme boundaries
- line-break opportunities
- script/script-extension 정보
- paragraph base direction과 BiDi embedding level

component locale의 기본값은 `und`, base direction의 기본값은 `Auto`다. 숫자,
구두점과 common/inherited script는 주변 run 문맥을 사용하며 임의의 LTR로 고정하지
않는다. font 선택 전 `AnalysisItem`은 paragraph boundary, ICU logical BiDi run과
isolate/control boundary, 정확한 embedding level, resolved script, language 또는
style이 바뀌는 곳에서 분리한다. direction parity가 같더라도 embedding level이 다르면
합치지 않는다. fallback 이후 같은 face가 연속되는 범위가 final shaping run이 된다.

### 5.3 FontFamily fallback

fallback 선택 단위는 extended grapheme cluster이며 candidate 순서는 완전히
결정론적이다.

1. 각 family에서 face를 `(stylePenalty, abs(stretch-target), abs(weight-target),
   authoredFaceIndex, fontGuid, faceIndex)`의 lexicographic 순서로 정렬한다.
   `stylePenalty`는 exact match 0, Italic/Oblique 호환 1, 그 외 2다. weight는 정수
   `1..1000`, stretch는 정수 percent `50..200`으로 canonicalize한다.
2. primary family 뒤에 fallback family graph를 authored order의 depth-first로
   순회한다. first visit만 인정하며 visited GUID set으로 유한 candidate list를 만든다.
3. face가 grapheme의 필수 scalar를 표현하는지 preflight한다. join control, ZWJ와
   default-ignorable scalar 자체에는 독립 cmap glyph를 요구하지 않는다. variation
   selector는 explicit Unicode variation mapping이 있으면 그것을 사용하고, 없으면
   default presentation만 허용한다.
4. target grapheme를 포함하는 Section 5.2의 complete `AnalysisItem`을 candidate face로
   probe-shape한다. BOT/EOT와 buffer flag는 target이 아니라 실제 AnalysisItem의
   paragraph 경계를 반영한다. `.notdef` 판정은 output cluster의 source range가 target
   grapheme와 교차하는 항목에만 적용하고 주변 context의 `.notdef`는 무시한다.
   target 출력에 `.notdef`가 있으면 그 face를 거부하고 다음 candidate로 grapheme
   전체를 다시 평가한다.
5. 선택이 바뀌면 해당 grapheme를 포함하는 context run 전체를 다시 shape한다. 이미
   shaped된 개별 glyph만 다른 face로 교체하지 않는다.
6. 모든 candidate가 실패하면 grapheme 전체를 `MissingGlyph`로 만든다.
7. 같은 face, exact embedding level, script, direction, language와 style이 연속되고
   isolate/control/paragraph boundary를 넘지 않는 구간만 최종 shaping run으로
   병합한다.

fallback family graph의 cycle, 존재하지 않는 GUID와 범위 밖 face metadata는 editor
진단이며 package build 오류다. cycle이 있어도 editor preview candidate 생성은 first
visit 규칙으로 종료한다.

### 5.4 HarfBuzz shaping

각 run은 direction, script, language, font face, size와 feature set을 명시하여
HarfBuzz에 전달한다. decoded scalar를 buffer에 추가할 때 원본 UTF-8 byte start를
cluster value로 지정한다. 따라서 invalid sequence를 `U+FFFD`로 표시해도 cluster는
원본 byte range로 돌아와야 하며
`HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS`에 해당하는 안정적 mapping 계약을
사용한다.

shaping 출력은 최소한 다음을 포함한다.

- font asset GUID와 face index
- glyph ID
- advance와 x/y offset
- source cluster byte range와 grapheme range
- BiDi level 및 logical run identity

glyph ID 0 또는 미지원 cluster는 fallback 재선택 후에만 missing glyph로 확정한다.
최종 missing glyph는 system font나 ASCII renderer가 아니라 특수 `MissingGlyph`로
표현한다. 이 항목은 font size에서 유도한 고정 em advance와 procedural monochrome
tofu box를 사용하므로 모든 font asset이 실패해도 진단 결과를 표시할 수 있다.

### 5.5 Paragraph layout

`TextLayoutService`는 shaping 결과와 width/height constraint를 받아 다음을 생성한다.

- fitted logical lines와 baseline
- line별 visual run order
- glyph positions와 logical-to-visual mapping
- grapheme 경계의 caret stops
- logical selection range의 visual rectangles
- wrap/clip/ellipsis 결과와 intrinsic size

명시적 paragraph/line separator는 모든 wrap mode에서 줄을 나눈다. `NoWrap`은 그 외
break를 만들지 않고, `Word`는 extended-grapheme boundary와 일치하는 ICU line-break
opportunity만 사용하며, `Grapheme`은 모든 extended-grapheme boundary를 후보로
허용한다. `Word`에서 하나의 unbreakable span이 폭을 넘으면 중간을 강제로 wrap하지
않고 `Overflow`는 그대로 그리며 `Clip`은 자르고 `Ellipsis`는 마지막으로 보이는
grapheme boundary에서 생략한다.

paragraph shaping은 break 후보의 최초 폭을 계산하는 데만 사용한다. break를 선택한
뒤에는 **모든 최종 line을 line context로 다시 shape**하고 그 결과를 authoritative로
삼는다. post-break advance가 constraint를 넘으면 이전 후보로 돌아가 다시 shape한다.
후보가 없으면 위 overlong policy를 적용한다. shaped glyph array를 break 지점에서
단순히 slice하지 않으며 HarfBuzz unsafe-to-break 경계를 무시하지 않는다. ellipsis도
최종 line의 style/run 문맥에 포함해 함께 shape한다. 이후 ICU BiDi line 결과로 visual
run order를 계산한다.

## 6. 폰트 에셋과 atlas

### 6.1 FontAsset와 FontFamilyAsset

개별 imported font는 다음 metadata를 가진다.

- stable asset GUID, source SHA-256와 face index
- 지원되는 weight, slant와 stretch
- importer가 계산한 static outline/coverage 정보
- 재배포 확인 상태
- copyright, license kind와 notice/license asset GUID

`FontFamilyAsset`은 stable GUID와 다음 저작 데이터를 가진다.

- ordered face entries
- ordered fallback family GUIDs
- schema version

package에 포함되는 각 폰트는 `redistributableConfirmed`가 명시되어야 한다. 오픈소스
폰트는 실제 license/notice file이 있어야 하며, proprietary font는 소유권/재배포
확인 metadata를 갖춰야 한다. 법적 판단을 엔진이 대신하지 않지만, 확인되지 않은
폰트를 release package에 조용히 포함하지 않는다.

### 6.2 GlyphAtlasCache

atlas는 codepoint가 아니라 shaped glyph ID로 조회한다. 논리 key는 다음 필드를
포함한다.

```text
fontGuid, fontRevision, faceIndex, pixelSize,
rasterScaleKey, variationKey(default only), renderMode(monochrome), glyphId
```

현재 `stb_truetype` 기반 rasterizer는 검증된 static outline face의 glyph ID를
rasterize하는 역할만 유지한다. shaping, kerning, fallback 또는 line break를
rasterizer에 맡기지 않는다. HarfBuzz가 logical advance/offset을 소유하고 rasterizer는
bitmap bounds/bearing만 제공한다.

atlas는 page 단위 LRU와 frame pin을 사용한다. 현재 frame 또는 in-flight GPU
command가 참조하는 glyph는 퇴출하지 않는다. 기본 resident budget은 64 MiB이며
할당, eviction, miss와 upload telemetry를 제공한다.

## 7. 저작 컴포넌트와 직렬화

### 7.1 공통 규칙

- scene/prefab에는 제작자가 정한 값만 저장한다.
- layout, focus, hover, composition 같은 계산/상호작용 상태는 저장하지 않는다.
- 새로운 serialized object reference는 raw pointer가 아니라 stable ID를 사용한다.
- 모든 component schema는 명시적 version과 migration test를 가진다.

### 7.2 SceneObjectRef

UI navigation, scroll content와 viewport 참조를 위해 engine-level
`SceneObjectRef`를 둔다. 값은 stable scene object ID이며 prefab clone 시 기존
`Component::RemapReferences(...)` 계약으로 갱신된다.

기존 scripting `ObjectRef`는 source compatibility를 위해 `SceneObjectRef`의 alias
또는 adapter로 유지한다. 같은 의미의 별도 ID 타입을 UI마다 만들지 않는다.

### 7.3 기존 컴포넌트 확장

#### UICanvas

- 이번 단계의 render mode는 screen-space overlay
- scale mode: `ConstantPixelSize`, `ScaleWithViewport`
- reference resolution과 width/height match 정책
- logical UI coordinate를 저장하고 macOS backing scale은 atlas raster scale,
  최종 viewport와 scissor에서만 적용

기존 Canvas scale mode와 reference-size 필드는 동일한 logical 결과를 내도록 schema
migration하며, 기존 scene을 단순히 새 기본값으로 덮어쓰지 않는다.

#### UILabel

- `fontFamilyGuid`, font size, line spacing, color
- locale(`und` 기본), base direction(`Auto/LTR/RTL`)
- wrap(`NoWrap/Word/Grapheme`)
- overflow(`Overflow/Clip/Ellipsis`)와 max lines
- horizontal/vertical alignment와 sorting order

기존 `fontGuid`는 load 시 implicit one-face family view로 해석한다. migration 전
파일을 읽지 못하게 하거나 즉시 강제 rewrite하지 않는다. 새 schema로 저장하는
명시적 migration과 round-trip test를 제공한다.

#### TextRenderer2D

기존 world transform와 sorting contract를 유지하되 `fontFamilyGuid`, locale과
direction을 사용한다. 마일스톤 A의 world text는 explicit newline만 있는
`NoWrap/Overflow`로 제한하고 layout bounds, clip, caret와 hit-test를 제공하지 않는다.
font size는 local logical pixel 단위이고 기존 Transform scale/rotation으로 world에
배치한다. 따라서 새 width/height unit을 암묵적으로 만들지 않는다. glyph shaping,
fallback과 line baseline 계산은 `UILabel`과 공유하며 bounded world text는 후속
`TextBox2D` 설계 대상으로 둔다.

#### UIButton

기존 pointer color/click 데이터는 유지한다. focus/navigation은 sibling
`UISelectable`이 제공하며, 기존 scene의 pointer 동작은 깨뜨리지 않는다.

### 7.4 신규 컴포넌트

#### UILayoutElement

- axis별 min/preferred/flexible size
- `ignoreLayout`

#### UILayoutGroup

- mode: horizontal, vertical, grid
- padding, spacing, child alignment
- child-size control과 expand 정책
- grid cell size, start corner, fill axis
- grid constraint: `Flexible/FixedColumns/FixedRows`와 positive constraint count

#### UIContentSizeFitter

- axis별 `Unconstrained/Min/Preferred`

#### UIMask

- descendant render와 hit-test에 동일하게 적용되는 중첩 가능한 rectangular clip
- arbitrary alpha/stencil masking은 이번 범위가 아님

#### UIScrollView

- `SceneObjectRef` 기반 viewport/content
- horizontal/vertical axis enable
- clamped/elastic movement, inertia, deceleration과 scroll sensitivity
- authored initial normalized position

#### UISelectable

- interactable
- navigation mode: `None/Auto/Explicit`
- explicit up/down/left/right `SceneObjectRef`
- spatial automatic navigation의 결정론적 tie-break

#### UITextInput

- serialized initial text, read-only와 single/multiline
- grapheme 단위 max length
- content/submit policy
- text viewport, rendered label와 placeholder label reference
- font family와 paragraph style

runtime text value, caret, selection, composition와 blink는 저장하지 않는다. edit mode의
initial text 변경은 Property/Snapshot command를 거친다. play/runtime value는 script
API에서 읽고 쓸 수 있으며 `valueChanged`와 `submitted` event를 제공하지만, 영속화는
게임의 save-state 계층이 명시적으로 담당한다.

#### UIAccessibility

- role, name, description과 hidden metadata
- 내부 semantic tree 생성에 사용

macOS Accessibility API bridge가 실제로 구현·검증되기 전에는 native accessibility
지원으로 표시하지 않는다.

### 7.5 Runtime-only snapshot

다음 값은 scene/prefab serialization, prefab override 및 editor dirty state에서
제외한다.

- computed rect, intrinsic size, baseline, clip과 layout revision
- hover, pressed, focus, pointer capture와 IME owner의 full `UIRuntimeTargetIdentity`
- scroll offset/velocity
- runtime text value, caret, selection, composition와 blink
- shape/layout cache와 atlas residency

## 8. 레이아웃, 입력과 렌더 snapshot

### 8.1 레이아웃 계산

모든 authored float는 먼저 finite인지 검사하고 signed zero를 `+0`으로 정규화한 뒤
signed 26.6 fixed-point logical UI unit으로 변환한다. NaN, infinity와 fixed-point
overflow는 component validation 오류다. measure, line fitting, hit-test와 snapshot rect
비교는 이 단위로만 수행한다. physical viewport/scissor 변환에서만 min edge는 floor,
max edge는 ceil한다.

레이아웃은 두 방향으로 계산한다.

1. child-to-parent intrinsic measurement
2. parent-to-child arrangement

axis별 driver 우선순위는 `Canvas root > parent UILayoutGroup > self
UIContentSizeFitter > authored RectTransform`이다. `UILayoutElement`는 driver가 아니라
min/preferred/flexible constraint만 제공한다. parent group과 self fitter가 같은 size
axis를 구동하면 parent가 이기고 fitter를 무시하며 conflict diagnostic을 낸다.

layout 대상 child의 constraint는 `min=max(0,min)`,
`preferred=max(min,preferred)`, `flexible=max(0,flexible)`로 canonicalize한다.
padding, spacing과 grid cell size는 non-negative이고 grid cell의 각 축은 0보다 커야
한다. Horizontal group의 intrinsic main size는 child size 합 + spacing + padding,
cross size는 child 최대값 + padding이며 Vertical은 축을 바꾼다. Grid intrinsic size는
결정된 row/column 수, cell size, spacing과 padding으로 계산한다.
Horizontal/Vertical group의 main axis는 padding과 고정 spacing을 먼저 제외하고 다음
순서로 배분한다.

1. `controlChildSize=false`인 child는 authored size를 min 이상으로 유지한다.
2. 나머지 child는 min에서 시작한다.
3. 남는 공간을 `preferred-min` 비율로 preferred까지 배분한다.
4. 그 뒤 남는 공간은 flexible weight로 배분한다. `forceExpand`는 0 weight를 1로 본다.
5. fixed-point 나눗셈 remainder는 sibling order의 앞 child부터 1 unit씩 배분한다.
6. available space가 min 합보다 작으면 min을 줄이지 않고 arranged bounds를 넘긴다.
   실제 가시성은 `UIMask`와 visual overflow 계약이 처리한다.

flexible 배분 뒤에도 공간이 남으면 main-axis child alignment가 leading offset만
결정하며 authored spacing을 임의로 늘리지 않는다.

cross axis도 min/preferred를 지키며 control/expand가 허용한 경우에만 inner size까지
늘리고, 남는 공간은 authored alignment로 배치한다. Grid는 authored cell size를
사용한다. `FixedColumns/FixedRows`는 positive constraint count를 그대로 쓰고,
`Flexible`은 `(innerSize + spacing) / (cellSize + spacing)`의 fixed-point floor로 최소
1개를 선택한다. start corner와 fill axis, sibling order가 cell 순서를 유일하게
결정한다. `UIContentSizeFitter`는 이 intrinsic min/preferred 결과로 자신의 선택된 축을
구동하되 더 높은 우선순위의 parent group이 그 size axis를 소유하면 무시된다.

각 driven property의 dependency graph에서 strongly connected component를 검출한다.
cycle에 포함된 property는 **마지막 정상값을 사용하지 않고**, 그 축의 모든 dynamic
driver를 무시하여 authored RectTransform 결과로 환원한다. 따라서 cold start,
warm cache와 서로 다른 edit history가 같은 authored input에서 같은 snapshot을 낸다.
last-good geometry는 editor의 비권위 debug overlay에만 표시할 수 있고 render,
hit-test 또는 serialization 입력으로 사용할 수 없다. 계산 결과는 immutable
`UILayoutSnapshot`으로 게시한다.

### 8.2 UI subsystem

현재 `UISystem` facade는 다음 내부 책임으로 분리한다.

- `UILayoutSystem`
- `UIFocusSystem`
- `UIInputRouter`
- `UITextInputSystem`
- render collection adapter

snapshot은 enabled `UICanvas` 아래의 active hierarchy만 정해진 sibling order로
순회한다. inactive/disabled ancestor는 해당 subtree를 layout, focus candidate, input과
render에서 모두 제외한다. draw order는 Canvas sort, hierarchy sibling path,
component sorting order와 stable submission index로 한 번 계산하며 hit-test는 그
순서의 정확한 역순을 사용한다.

frame의 input은 immutable `InteractionSnapshot N` 하나로 hit-test, focus/navigation과
ordered event target을 계산한다. callback이 authored/runtime UI를 바꾸지 않으면
render도 N을 사용한다. callback이 geometry/visibility를 dirty로 만들면 한 번만
`RenderSnapshot N+1`을 만들 수 있지만, 이미 생성된 event를 N+1에서 다시 hit-test하거나
다른 target으로 보내지 않는다. 다음 frame의 interaction은 최신 published snapshot에서
시작한다. 각 snapshot 내부에서는 visibility, parent clip, sorting과 interactable
조건을 서로 다른 방식으로 다시 계산하지 않는다.

event target identity는
`{worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId}`다.
`worldGeneration`은 world/scene 교체마다 증가하고 `componentInstanceId`는 기존
`Component::GetInstanceID()`를 사용한다. callback 직전에 네 값을 모두 re-resolve한다.
remove/add, scene transition 또는 object ID reuse가 일치하지 않으면 건너뛰며, reparent로
identity가 유지된 target은 기존 event를 받되 새 geometry로 retarget하지 않는다.

### 8.3 SDL text input ownership

host는 native SDL event를 한 번 수집하고 text payload를 즉시 deep-copy한다.
`NativeTextEvent`는 monotonic sequence, SDL window ID, timestamp, event kind, copied UTF-8
text와 editing start/length를 보존한다. arbitration은 SDL window별로 수행하며 각
window에는 동시에 하나의 `{kind, ownerIdentity, generation}` token만 존재한다.

- standalone runtime: focused `UITextInput`이 소유
- embedded Game View: Game View가 keyboard focus를 가질 때만 runtime이 소유
- editor text widget: ImGui/editor가 소유

일반 window/pointer/key event는 기존 capture 정책에 따라 관찰할 수 있지만
`SDL_EVENT_TEXT_INPUT`과 editing/composition event는 arbitration을 거친 owner 한 곳만
소비한다. 따라서 native event를 무조건 ImGui에 먼저 보내지 않고, text event는
owner가 editor일 때만 ImGui backend에 전달한다. runtime owner일 때 ImGui는 같은 text
event를 받지 않는다.

owner 전환은 sequence boundary를 가진다. 이미 ingest한 event에는 당시 owner
generation이 찍혀 있어 새 owner로 재전송하지 않으며, 전환 뒤 들어온 native event만
새 generation을 사용한다. old owner에서는 engine composition을 취소하고
`SDL_ClearComposition`과 `SDL_StopTextInput`을 호출한 뒤 new owner에
`SDL_StartTextInput`을 호출한다. start 실패 시 focus는 유지하되 native text entry를
비활성화하고 typed diagnostic을 낸다. clear/stop 실패도 진단하되 generation을
무효화하여 stale event가 적용되지 않게 한다. 이미 commit된 text는 보존한다.

`TextInputArbiter`는 compiled engine/editor/runtime에서 SDL text-input lifecycle API를
호출할 수 있는 유일한 소유자다. ImGui SDL3 backend 초기화 직후 engine-owned
`Platform_SetImeDataFn` bridge를 설치하여 detached viewport를 포함한 ImGui IME
요청도 `{windowId, visible, inputArea, cursorOffset}` request로 arbiter에 보낸다. vendored
backend의 direct callback은 호출 경로에서 제거한다. mock SDL call audit는 editor
request가 다른 window 또는 active runtime owner를 stop/reposition하지 못하며 모든
Start/Stop/Clear/SetTextInputArea 호출이 arbiter owner generation과 일치함을 검사한다.

SDL editing start/length는 SDL3가 정의한 UTF-8 character unit에서
`UnicodeTextBuffer`의 scalar/original-byte range로 변환하고 범위 밖 값은 clamp 후
진단한다. `SDL_SetTextInputArea`에는 caret가 아니라 input area와 그 안의 cursor offset을
전달한다. embedded Game View의 logical rect는 Canvas -> presentation rect -> backing
scale/viewport origin -> SDL window coordinate의 단일 함수로 변환하며 HiDPI, letterbox와
detached viewport fixture로 검증한다.

pointer가 embedded Game View의 실제 presentation 영역을 벗어나는 것은 pointer
capture/hover만 정리하며 keyboard/gamepad focus를 지우지 않는다. 반면 native window
focus loss는 pressed state와 text-input owner를 해제하고 composition을 취소한다.
frame 시작과 callback 이후 focus, capture와 IME owner의
`{worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId}`를 전부 다시
resolve하여 교체·삭제되거나 비활성화된 대상은 안전하게 정리한다. remove/add가 frame
사이에 일어나도 새 component가 이전 focus/capture를 승계하지 않는다.

### 8.4 BiDi caret와 selection

selection은 half-open logical grapheme range `[start, end)`로 저장한다. runtime caret은
`{logicalGraphemeBoundary, Upstream/Downstream affinity}`이며 BiDi 경계에서 동일한
logical boundary가 갖는 두 visual stop을 구분한다. pointer hit-test는 각 visual stop
사이의 half-open interval을 사용하고 정확한 midpoint tie는 line의 visual 진행 방향
쪽 stop으로 고정한다. Left/Right는 visual stop 순서를 이동하지만 insertion,
Backspace/Delete와 max-length 계산은 logical grapheme boundary에서 동작한다.

visual run 경계를 넘는 selection은 여러 rectangle로 표시한다. 여러 grapheme를 합친
ligature 내부 caret은 font의 adjusted GDEF ligature-caret 정보가 있으면 이를 사용하고,
없으면 run direction을 반영해 shaped advance를 grapheme 수로 비례 분할한다. caret가
UTF-16 code-unit 또는 combining sequence 중간에 놓이지 않도록 한다.

### 8.5 Clip-aware rendering

`RenderCommand`는 optional clip/scissor rect를 가진다. clip 변경은 안전한 batch
flush/state transition을 일으킨다. nested mask는 parent와 child rect의 교집합이며,
빈 교집합은 render와 hit-test 양쪽에서 제거한다.

## 9. 프레임 데이터 흐름

한 frame의 UI 순서는 다음으로 고정한다.

1. SDL pointer, keyboard, gamepad와 text/IME event를 native 순서로 수집
2. Canvas coordinate 변환과 dirty UI tree 동기화
3. shaping, intrinsic measure와 arrange로 `InteractionSnapshot N` 확정
4. N으로 hit-test, focus/navigation과 ordered event target list 생성
5. full target identity를 재검증하며 callback dispatch; retarget 금지
6. callback이 UI를 변경했다면 layout을 한 번만 추가 계산해 `RenderSnapshot N+1` 게시
7. N 또는 N+1 중 최종 render snapshot에서 clipped render queue 생성

callback event는 raw component pointer를 보존하지 않는다. N+1 계산 이후 또 구조가
바뀌면 다음 frame으로 넘기고 rate-limited diagnostic을 기록한다. hide, reparent,
remove/add와 scene transition callback test는 event target이 바뀌거나 새 component에
전달되지 않으며 다음 frame snapshot만 갱신됨을 검사한다.

## 10. 캐시와 dirty propagation

shape identity는 hash만 믿지 않고 original byte length와 bytes를 collision-check하며
다음을 모두 포함한다.

- UTF-8 decode-policy version과 paragraph/run byte range
- ordered fallback-graph generation
- 선택된 font GUID, content revision/SHA, face index와 canonical 26.6 font size
- variation key(default), exact BiDi embedding level/direction, script와 language
- canonical component locale와 resolved ICU grapheme/line BreakIterator locale,
  rule/tailoring identity 및 analysis generation
- BOT/EOT 및 기타 HarfBuzz buffer flags, cluster level과 ordered feature ranges
- HarfBuzz/ICU source/data/build contract version

paragraph identity는 final-line shape identity에 canonical width/height, wrap과 overlong
token policy, overflow, ellipsis token/style, max lines, line spacing과 alignment를 더한다.
intrinsic identity는 paragraph result와 visual component revision을 포함한다. UI snapshot은
intrinsic generation, hierarchy/sibling order, active state, RectTransform, 모든 layout
component, Canvas logical scale와 viewport generation을 포함한다. non-finite constraint는
key 생성 전에 거부하고 signed zero 정규화와 26.6 quantization은 Section 8.1 계약을
따른다.

다음 변경만 관련 cache를 무효화한다.

- text/style/locale/direction 변경
- font import 또는 fallback graph 변경
- layout constraint/viewport/Canvas scale 변경
- hierarchy, active state 또는 layout component 변경

generation은 `shape -> paragraph -> intrinsic -> 모든 ancestor layout` 방향으로
전파한다. fallback family의 어느 descendant face가 바뀌어도 이를 참조하는 모든 shape
generation이 바뀐다. 각 key field를 하나씩 변경했을 때 cache miss가 나고, 동일
collision-checked input은 hit가 나는 data-driven test를 둔다. cold start, warm cache와
hot edit 후의 canonical snapshot bytes는 동일해야 한다. text와 font/style은 그대로
두고 component locale만 바꾼 case도 analysis/paragraph cache miss와 새 line-break 결과를
검사한다.

static text에는 warm-up 이후 shaping 호출과 steady-state heap allocation이 없어야 한다.
초기 구현은 correctness를 위해 main-thread deterministic CPU phase로 유지한다.
background shaping은 immutable job/result와 동일성 검증이 별도 승인되기 전까지 범위에
포함하지 않는다.

## 11. 진단과 실패 정책

텍스트/UI 계층은 다음 필드를 가진 typed diagnostic을 만든다.

```text
code, severity, subsystem, message, remediation,
assetGuid, sceneObjectId, componentType, sourceByteRange
```

현재 logger에는 사람이 읽을 수 있는 adapter로 전달하고, 향후 structured console에는
동일 record를 직접 연결한다. 반복 진단은 code와 context key로 rate-limit한다.

package build는 reachable scene/prefab의 `UILabel`, `UITextInput.initialText`와
`TextRenderer2D`, localization table 및 project가 등록한 `requiredTextFixtures`를 전부
실제 shape한다. 그 결과를 family GUID/revision, locale, source SHA와 missing grapheme
목록을 가진 `TextCoverageManifest`로 고정한다. script가 만드는 무한한 dynamic string은
build-time coverage로 가장하지 않고 필요한 범위를 제작자가 fixture로 추가할 수 있다.

실패 동작은 다음 행렬로 고정한다.

| 조건 | Editor import/preview | Development runtime | Package build | Copied packaged runtime |
|---|---|---|---|---|
| HarfBuzz/ICU lock 또는 ICU data 불일치 | ImGui editor는 유지, Game View text subsystem 차단, `TEXT_DEPENDENCY_INVALID` | scene 시작 전 exit 4 | 실패 | manifest 검사 후 scene 시작 전 exit 4 |
| font hot-reload 손상 | 검증된 last-good import artifact 유지; 없으면 tofu, `TEXT_FONT_INVALID` | 해당 grapheme tofu + 오류 | reachable dependency면 실패 | manifest/hash 불일치는 scene 시작 전 exit 4 |
| serialized UTF-8 오류 | `U+FFFD` preview + blocking `TEXT_UTF8_INVALID` | `U+FFFD` + 오류 | 실패 | manifest에 있을 수 없으며 발견 시 exit 4 |
| reachable authored/localized text missing glyph | tofu preview + 정확한 family chain 진단 | tofu + 오류 | `TextCoverageManifest` 생성 실패 | manifest와 asset이 일치한다면 발생 불가; 불일치 시 exit 4 |
| unbounded dynamic string의 invalid UTF-8/missing glyph | 해당 없음 | `U+FFFD`/procedural tofu + rate-limited 진단, 계속 실행 | fixture에 등록된 범위만 검사 | 동일하게 계속 실행 |
| fallback cycle/invalid GUID/license 미확인 | family 비활성화 + blocker | finite first-visit preview/tofu | 실패 | manifest 불일치 시 exit 4 |
| authored SceneObjectRef/layout cycle/axis conflict | 기능 비활성 또는 Section 8.1 authored fallback + blocker | authored fallback + 오류 | scene validation 실패 | validated package에는 없어야 함 |
| runtime mutation으로 생긴 invalid ref/layout conflict | authored fallback + 오류 | 해당 기능만 비활성/ authored fallback, 계속 실행 | 해당 없음 | 동일하게 계속 실행 |
| atlas budget 포화 | frame-pin LRU 후 tofu + `TEXT_ATLAS_EXHAUSTED` | 동일 | stress/performance gate 실패 | 동일; 무제한 증가는 금지 |
| callback 추가 reflow 반복 | N+1까지만 반영, 이후 다음 frame + 진단 | 동일 | stress gate 실패 | 동일 |

GameBuilder는 package 오류에서 `false`와 stable diagnostic code를 반환하고 CLI build는
nonzero로 종료한다. packaged startup의 위 exit 4는 `PackageValidationFailed`이며 code,
failed path/hash와 remediation을 stderr 및 startup/smoke report에 남긴다. sealed package는
last-good artifact를 사용하지 않는다. 필수 폰트 또는 ICU가 없을 때 system font나
ASCII-only renderer로 조용히 돌아가지 않는다.

## 12. 에디터와 패키지 통합

새 component property는 기존 descriptor와 snapshot command 경로를 사용한다.
layout이 계산한 driven field는 Inspector에서 읽기 전용으로 표시하고 source driver를
함께 보여준다. `SceneObjectRef` property는 scene object picker를 제공한다.

font import 결과에는 coverage, face metadata, SHA와 license 상태를 표시한다. family
fallback cycle과 package 누락은 build 전 validation에서 확인할 수 있어야 한다.

기존 flat executable-directory package는 개발 호환 입력으로만 유지하고 release 출력은
다음 canonical bundle을 사용한다.

```text
Game.app/
  Contents/
    Info.plist
    MacOS/<game-executable>
    Resources/                       # RuntimeResourceRoot
      game.json
      asset_catalog.json
      Assets/
      Scenes/
      ShaderBundle/
      Resources/missing_texture.png
      Engine/Text/icudt78l.dat
      Manifests/text_runtime.json
      Licenses/ThirdPartyNotices.md
      Licenses/HarfBuzz.txt
      Licenses/ICU.txt
      Licenses/Fonts/<font-guid>.*
```

`PathService::RuntimeResourceRoot()`는 app bundle에서 `Contents/Resources`를 반환하고,
명시적인 non-bundle developer 실행에서만 executable directory를 반환한다.
release runtime target은 `MACOSX_BUNDLE` 또는 동등한 CMake install/bundle rule로 위
구조를 생산한다. GameBuilder, PackageLayout, `game.json`, scene catalog, shader bundle, asset catalog와
runtime font/ICU loader는 모두 이 단일 resource root를 사용한다. PackageLayout은
executable path와 resource root를 별도 인자로 검증한다. `Info.plist`에는 executable,
bundle identifier, display/version과 minimum OS metadata를 기록하지만 signing,
notarization과 Universal 2 완료를 이 마일스톤의 증거로 주장하지 않는다.

text-backbone가 기존 game bundle에 **추가하는** 항목은 transitive fallback-family
closure의 실제 fonts와 고지, `icudt78l.dat`, static-linked HarfBuzz/ICU의 license,
`TextCoverageManifest` 및 dependency/content SHA manifest다. closure는 first-visit graph
순서와 무관하게 모든 reachable face/license를 포함한다.

검증은 bundle을 checkout 밖의 새 임시 경로에 복사한 뒤 수행한다. 모든 Mach-O를
재귀적으로 찾아 `otool -L`과 `LC_RPATH`를 검사하며 `/opt/homebrew`, checkout/build
path와 허용 목록 밖 dylib가 있으면 실패한다. Homebrew/font 관련 환경 변수를 제거하고
network-disabled harness에서 copied app을 실행해 scene/text fixture를 연다. 사용자
Library font와 네트워크가 없어도 동일해야 한다.

## 13. 검증 설계

### 13.1 고정 fixture

테스트 폰트와 문자열은 repository에 고정하고 SHA와 license를 함께 보관한다. 최소
문자 matrix는 다음과 같다.

- Latin ligature와 kerning
- Arabic joining과 RTL
- Hebrew + number + punctuation 혼합 BiDi
- Devanagari reordering과 conjunct
- Thai grapheme와 line break
- CJK line-break prohibition
- combining marks, variation selector와 ZWJ boundary
- 여러 family를 지나는 fallback과 missing glyph

fixture는 최종 pixel만 비교하지 않는다. glyph IDs, advances, cluster byte ranges,
grapheme boundaries, logical lines와 visual run order도 검증한다.

### 13.2 자동 테스트

- UTF-8 byte/scalar/grapheme round-trip과 invalid sequence
- ICU grapheme/line/BiDi/script analysis
- grapheme-safe fallback과 HarfBuzz cluster mapping
- Arabic/Indic contextual form과 `fi` ligature를 지나는 final-line re-shaping
- wrap, overlong policy, ellipsis, affinity caret, selection과 visual hit-test
- 각 fallback/cache key field의 tie-break와 one-field invalidation
- anchors, group/grid sizing, fixed-point remainder, fitter와 SCC conflict fallback
- 같은 authored data의 cold/warm/hot-edit canonical layout 일치
- nested clip의 render/hit-test 일치
- pointer, keyboard, gamepad와 window별 ordered synthetic SDL IME event
- IME owner 전환, UTF-8-character offset mapping, HiDPI input-area coordinate와 API failure
- ImGui bridge가 다른 window/runtime IME owner를 stop/reposition하지 못하는 call audit
- callback 중 hide/reparent/remove-add/scene-transition의 frozen event identity
- frame 사이 component remove/add가 focus/capture/IME identity를 승계하지 않는 회귀 test
- legacy `fontGuid`, 신규 schema와 prefab reference remap
- 기존 Canvas migration과 unbounded `TextRenderer2D` contract
- Undo/Redo, driven field와 runtime-only non-serialization
- atlas eviction, frame pin, upload와 memory budget
- Section 11의 각 build/startup/continue/exit-4 terminal action
- bundle resource-root migration, dependency/RPATH, hash, license와 tamper validation
- 기존 전체 test suite 회귀 없음

### 13.3 Editor/runtime/package canonical parity

동일 scene, asset catalog, logical viewport와 input trace를 다음 세 mode에서 실행한다.

1. editor Game View
2. standalone development runtime
3. checkout 밖으로 복사한 release `.app`

각 mode는 stable order의 canonical layout JSON을 낸다. JSON에는 dependency-lock SHA,
font GUID/SHA/face, glyph ID, source byte/grapheme range, 26.6 advance/offset/position,
logical line와 visual run order, caret stop/affinity, UI rect/clip, event target와 draw order를
포함한다. GPU handle, pointer, atlas UV/page, timestamp와 physical raster pixel은 제외한다.
runtime event identity는 실행 안전성에만 사용하고 canonical JSON에는
`{stableSceneObjectId, componentTypeName, componentSchemaVersion, eventKind}`로 투영한다.
world generation, runtime type ID와 component instance ID는 mode마다 달라질 수 있으므로
canonical output에서 제외한다.
세 JSON의 SHA-256은 byte-for-byte 같아야 한다. dependency build option이나 data/font
SHA가 다르면 결과 비교 전에 실패한다.

### 13.4 GPU와 실제 macOS 증거

- command-stream test로 glyph order, texture와 clip transition을 정확히 검사한다.
- SDL_GPU/Metal offscreen 결과는 허용 오차가 명시된 golden image로 비교한다.
- 보이는 macOS 창에서 혼합문자, nested clip, caret와 selection을 캡처한다.
- 합성 IME unit test와 별개로 실제 한글·일본어 입력기의 composition/commit/focus
  전환을 기록한다.
- headless 또는 synthetic test만으로 실제 IME 지원을 주장하지 않는다.

### 13.5 성능 게이트

최초 reference machine은 `Mac14,9`, Apple M2 Pro 10-core, 16 GiB,
macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0이다. AC power, Release build,
1920x1080 logical viewport와 backing scale 2.0에서 debugger 없이 실행한다. 120-frame
warm-up 후 600개 sample을 monotonic clock으로 측정하고 nearest-rank p95를 사용한다.
OS, hardware, compiler, commit과 fixture/font/ICU/HarfBuzz SHA를 report에 남긴다.

static workload는 한 Canvas에 1,000개 label, label당 10 grapheme, 3개 font size를
사용하며 Latin/Arabic/Hebrew/Devanagari/Thai/CJK fixture를 round-robin한다. 모든
shape/layout/atlas cache를 먼저 warm-up한다. 측정 구간은 UI event dequeue 직후부터
RenderQueue 완성까지이며 GPU encode/submit/present는 제외한다.

dynamic workload는 정확히 2,000 grapheme인 고정 fixture 600개를 사용한다. 각 fixture는
Latin 400, Arabic 300, Hebrew 300, Devanagari 300, Thai 300, CJK 400 grapheme 비율이며
동일 길이의 한 grapheme를 바꿔 content identity를 매 sample 무효화한다. 측정 구간은
UTF-8 decode, ICU analysis, fallback, HarfBuzz shaping과 final line layout 전체다.

- cached visible glyph 10,000개: UI update + queue build `p95 <= 4 ms`
- 2,000-grapheme mixed-script full analysis + shaping + layout: `p95 <= 8 ms`
- warm-up 이후 static text reshape 0회
- warm-up 이후 Text/UI tagged allocator의 static steady-state heap allocation 0회
- glyph atlas default resident budget 64 MiB 강제 및 무제한 증가 0

atlas budget은 현재 및 in-flight command가 참조하는 모든 monochrome GPU atlas page의
`width * height * bytesPerPixel` 합이며 source font bytes와 일시 upload staging은 별도
telemetry로 기록한다. 최초 qualification 범위는 위 macOS arm64 장비이며 deployment
target 설정만으로 Intel 또는 과거 macOS 실행을 통과했다고 간주하지 않는다.

### 13.6 패키지 게이트

- copied `.app`가 Homebrew, system font와 network 없이 실행
- `Contents/Resources`가 유일한 release `RuntimeResourceRoot`
- 모든 Mach-O의 `otool -L`/`LC_RPATH`에 Homebrew, checkout/build 경로가 없음
- manifest에 HarfBuzz/ICU, ICU data, font와 notice SHA가 있음
- 누락/변조 시 Section 11의 exit 4 startup validation이 fail-closed
- editor/runtime/copied-package canonical JSON SHA가 동일
- visible packaged runtime에서 동일 mixed-script fixture와 IME 경로 확인

## 14. 완료 판정

마일스톤 A는 다음이 모두 참일 때만 완료다.

1. 모든 자동 correctness/serialization/GPU/package test가 통과한다.
2. 기존 전체 test suite가 회귀 없이 통과한다.
3. editor, development runtime와 copied package의 canonical layout SHA가 일치한다.
4. Section 11의 모든 build/startup/continue/exit action이 그대로 검증된다.
5. 실제 macOS visible IME와 mixed-script UI 증거가 남는다.
6. canonical `.app`가 외부 font/Homebrew/network 없이 checkout 밖에서 실행된다.
7. 성능과 atlas memory 게이트를 통과한다.
8. 폰트·ICU·HarfBuzz license/notice와 dependency/content SHA manifest가 검증된다.
9. color/variable font, native accessibility와 비검증 platform을 지원으로 표시하지
   않는다.

이후에만 Action Backbone과 macOS release qualification을 다음 상용 엔진 격차로
재평가한다.

## 15. 구현 경계 순서

상세 구현 작업은 별도 implementation plan에서 파일·test 단위로 나눈다. 설계상
dependency order는 다음과 같다.

1. pinned dependency, fixture font, license와 manifest 기반
2. `UnicodeTextBuffer`, ICU analysis와 diagnostic
3. `FontFamilyAsset`, fallback, HarfBuzz shaping과 glyph-ID atlas
4. paragraph layout, caret/selection와 shared text consumer
5. UI measurement/arrangement, snapshot와 rectangular clip
6. focus/navigation, scroll, `UITextInput`과 SDL IME arbiter
7. editor migration, package validation와 visible macOS qualification

각 단계는 실패 테스트를 먼저 추가하고 해당 단계의 독립 gate를 통과한 뒤 다음
단계로 진행한다. 구현 중 이 문서의 public behavior나 scope를 바꿔야 한다면 먼저
설계 문서를 갱신하고 사용자 승인을 다시 받는다.

## 16. 참조

- HarfBuzz 14.3.1 release: <https://github.com/harfbuzz/harfbuzz/releases/tag/14.3.1>
- HarfBuzz pinned commit: <https://github.com/harfbuzz/harfbuzz/commit/ab5ecbb83985034a76214ac0b2b833dcd590d774>
- ICU 78.3 release: <https://github.com/unicode-org/icu/releases/tag/release-78.3>
- ICU pinned commit: <https://github.com/unicode-org/icu/commit/21d1eb0f306e1141c10931e914dfc038c06121da>
- HarfBuzz shaping concepts: <https://harfbuzz.github.io/shaping-concepts.html>
- HarfBuzz buffer/unsafe-break contract: <https://harfbuzz.github.io/harfbuzz-hb-buffer.html>
- HarfBuzz OpenType layout/caret API: <https://harfbuzz.github.io/harfbuzz-hb-ot-layout.html>
- ICU boundary analysis: <https://unicode-org.github.io/icu/userguide/boundaryanalysis/>
- Unicode Text Segmentation, UAX #29: <https://unicode.org/reports/tr29/>
- Unicode Line Breaking Algorithm, UAX #14: <https://unicode.org/reports/tr14/>
- Unicode Bidirectional Algorithm, UAX #9: <https://unicode.org/reports/tr9/>
- SDL3 text input: <https://wiki.libsdl.org/SDL3/SDL_StartTextInput>
- SDL3 text input area: <https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea>

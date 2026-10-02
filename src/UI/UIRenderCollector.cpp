#include "UI/UIRenderCollector.h"

#include "Rendering/RenderQueue.h"
#include "Rendering/TextureBindingRegistry.h"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>

namespace molga::ui {

namespace {

void FillVertex(molga::Vertex2D& vertex, float x, float y, float u, float v,
                const Color& color) {
    vertex = {x, y, u, v, color.r, color.g, color.b, color.a};
}

// 물리 사각형 하나를 화면 좌표 quad로. ToPhysicalSpriteOutward가 이미 폭/높이
// 가 0이 아님을 보장하므로 여기서 다시 검사하지 않는다.
//
// UV는 변환이 함께 돌려준 값이다. 여기서 0,0->1,1로 못 박으면 뷰포트에 걸친
// 스프라이트가 **잘리는 대신 눌린다**: 사각형만 잘리고 텍스처는 그대로 그
// 좁아진 폭에 들어가기 때문이다. 같은 요소의 글자는 자르지 않는 affine을
// 지나므로 그 순간 글자와 그림이 어긋난다.
void FillQuad(molga::RenderCommand& command,
              const UIPhysicalTransform::SpriteQuad& quad,
              const Color& color) {
    const float left = static_cast<float>(quad.rect.x);
    const float top = static_cast<float>(quad.rect.y);
    const float right = left + static_cast<float>(quad.rect.width);
    const float bottom = top + static_cast<float>(quad.rect.height);
    FillVertex(command.vertices[0], left, top, quad.u0, quad.v0, color);
    FillVertex(command.vertices[1], right, top, quad.u1, quad.v0, color);
    FillVertex(command.vertices[2], right, bottom, quad.u1, quad.v1, color);
    FillVertex(command.vertices[3], left, bottom, quad.u0, quad.v1, color);
}

// UI 명령 하나의 공통 머리. 두 갈래(스프라이트/솔리드)가 각자 적으면
// 언젠가 한쪽만 패스 번호나 클립을 잃고, 그 손실은 그 갈래를 쓰는 화면에서만
// 나타난다.
molga::RenderCommand MakeUiCommand(
    const UIRenderItemSnapshot& item,
    const std::optional<molga::PixelRectU32>& physicalClip) {
    molga::RenderCommand command;
    command.sortKey.cameraPass = kUISnapshotCameraPass;
    command.batchKey.shaderName = "batch";
    command.batchKey.isBatchable = true;
    command.isBatchableSprite = true;
    command.uiDrawOrder = item.order;
    command.scissor = physicalClip;
    return command;
}

std::string ItemFactKey(const UIRenderItemSnapshot& item,
                        const char* reason) {
    return std::string(reason) + ":" +
           std::to_string(item.canonicalSource.sceneObjectId) + ":" +
           item.canonicalSource.componentTypeName;
}

void ReportDrop(molga::text::TextDiagnosticSink& sink,
                const UIRenderItemSnapshot& item, std::string message,
                std::string remediation) {
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = "ui.render";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    diagnostic.sceneObjectId = item.canonicalSource.sceneObjectId;
    diagnostic.componentType = item.canonicalSource.componentTypeName;
    sink.Report(std::move(diagnostic));
}

} // namespace

void UIRenderCollector::OnWorldReleased(std::uint64_t worldGeneration) {
    if (worldGeneration == 0U) return;
    reportedFacts_.erase(
        std::remove_if(reportedFacts_.begin(), reportedFacts_.end(),
                       [worldGeneration](const ReportedFact& fact) {
                           return fact.worldGeneration == worldGeneration;
                       }),
        reportedFacts_.end());
    // 다시 자리가 생겼으면 소진 요약도 다시 낼 수 있어야 한다. 그러지 않으면
    // 죽은 월드가 남긴 한 번의 요약이 프로세스 남은 수명 내내 다음 소진을
    // 침묵시킨다.
    if (reportedFacts_.size() < kMaxRememberedFacts) {
        factBudgetExhaustedReported_ = false;
    }
}

bool UIRenderCollector::NoteFact(std::uint64_t worldGeneration,
                                 const std::string& key,
                                 molga::text::TextDiagnosticSink& sink) const {
    const auto same = [worldGeneration, &key](const ReportedFact& fact) {
        return fact.worldGeneration == worldGeneration && fact.key == key;
    };
    if (std::find_if(reportedFacts_.begin(), reportedFacts_.end(), same) !=
        reportedFacts_.end()) {
        return false;
    }
    if (reportedFacts_.size() >= kMaxRememberedFacts) {
        // 기억할 자리가 없으면 **보고도 멈춘다**. 여기서 "기억하지 않고
        // 보고만 한다"로 두면, 상한을 넘긴 표면이 프레임마다 같은 사실을
        // 전부 다시 내보내 로그가 그 하나로 가득 찬다 — 메모리 상한이 그
        // 순간 로그 상한을 없앤다.
        if (!factBudgetExhaustedReported_) {
            factBudgetExhaustedReported_ = true;
            molga::text::TextDiagnostic diagnostic;
            diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
            diagnostic.severity = molga::text::TextSeverity::Warning;
            diagnostic.subsystem = "ui.render";
            diagnostic.message =
                "this UI surface reached its " +
                std::to_string(kMaxRememberedFacts) +
                " distinct render-drop diagnostic limit; further distinct "
                "drops are suppressed rather than repeated every frame";
            diagnostic.remediation =
                "fix the reported drops; the suppressed ones reappear once "
                "the surface stops producing this many distinct failures";
            sink.Report(std::move(diagnostic));
        }
        return false;
    }
    reportedFacts_.push_back(ReportedFact{worldGeneration, key});
    return true;
}

void UIRenderCollector::Collect(const UISnapshot& snapshot,
                                const UIPhysicalTransform& transform,
                                molga::RenderQueue& queue,
                                TextRenderer& textRenderer,
                                molga::text::TextDiagnosticSink& sink) const {
    // ── 이 수집 하나의 회계 ────────────────────────────────────────────────
    // 누계로 두면 두 계수기가 "이 프레임에서 무엇이 떨어졌는가"가 아니라
    // "이 수집기가 지나온 역사"를 말한다. 진단의 질문은 언제나 앞쪽이다.
    droppedItems_ = 0;
    submittedItems_ = 0;
    const std::uint64_t worldGeneration = snapshot.worldGeneration;
    // 장치 세대 0은 "장치 없음"이다. 그 상태로 만든 물리 사각형은 어느
    // 장치의 것도 아니므로 아무것도 그리지 않는다.
    if (transform.deviceGeneration == 0U) {
        if (NoteFact(worldGeneration, "no-device", sink)) {
            molga::text::TextDiagnostic diagnostic;
            diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
            diagnostic.severity = molga::text::TextSeverity::Error;
            diagnostic.subsystem = "ui.render";
            diagnostic.message =
                "the UI physical transform names no live graphics device "
                "generation, so no snapshot record can be converted";
            diagnostic.remediation =
                "pass GraphicsDevice::Generation() of the live device into "
                "UIPhysicalTransform before collecting";
            sink.Report(std::move(diagnostic));
        }
        droppedItems_ += snapshot.renderItems.size();
        return;
    }

    // ── Step 4b: 래스터 정책은 수집당 정확히 한 번 얻는다 ───────────────────
    // 항목마다 다시 유도하면 같은 화면에서 라벨마다 다른 래스터 높이가 나올
    // 수 있고, 실패했을 때 그 진단이 라벨 수만큼 반복된다. 실패는 텍스트
    // 항목 전부를 atlas 조회 **전에** 떨어뜨린다.
    std::optional<TextRasterPolicy> rasterPolicy;
    bool rasterPolicyResolved = false;

    for (const UIRenderItemSnapshot& item : snapshot.renderItems) {
        const auto physicalRect =
            transform.ToPhysicalSpriteOutward(item.logicalRect);
        if (!physicalRect) {
            ++droppedItems_;
            if (NoteFact(worldGeneration, ItemFactKey(item, "rect"), sink)) {
                ReportDrop(sink, item,
                           "a UI render record's logical rect does not convert "
                           "to a non-empty physical rect in this viewport",
                           "check the authored rect and the physical viewport; "
                           "an empty converted rect removes the record");
            }
            continue;
        }
        // 클립은 이미 조상들과 교집합을 취한 결과다. 변환에 실패하면 이
        // 레코드는 화면에 나올 자리가 없다 — 클립 없이 그리면 잘려야 할
        // 것이 통째로 보인다.
        std::optional<molga::PixelRectU32> physicalClip;
        if (item.logicalClip) {
            physicalClip = transform.ToPhysicalOutward(*item.logicalClip);
            if (!physicalClip) {
                ++droppedItems_;
                if (NoteFact(worldGeneration, ItemFactKey(item, "clip"), sink)) {
                    ReportDrop(sink, item,
                               "a UI render record's already-intersected clip "
                               "converts to an empty physical rect, so the "
                               "record is removed rather than drawn unclipped",
                               "an empty clip intersection removes both the "
                               "render and the hit record by contract");
                }
                continue;
            }
        }

        if (const auto* sprite = std::get_if<UISpriteSnapshot>(&item.payload)) {
            molga::RenderCommand command = MakeUiCommand(item, physicalClip);
            if (!sprite->textureGuid.empty()) {
                // ── Step 5b: 바인딩은 전부 맞거나 그리지 않는다 ─────────────
                // 세 조건이 함께 있어야 한다. 토큰이 있고, 그 토큰이 담은
                // 정체성이 스냅샷이 얼려 둔 바인딩과 같고, 그 바인딩이 지금
                // 살아 있는 장치의 것이어야 한다. 하나라도 어긋나면 이
                // 명령이 가리키는 핸들은 다른 장치의 것이거나 이미 은퇴한
                // 것이다.
                const bool tokenMatches =
                    sprite->resourceLifetime &&
                    sprite->resourceLifetime->Identity() == sprite->binding;
                const bool sameDevice =
                    sprite->binding.deviceGeneration ==
                    transform.deviceGeneration;
                if (!tokenMatches || !sameDevice) {
                    ++droppedItems_;
                    if (NoteFact(worldGeneration, ItemFactKey(item, "binding"), sink)) {
                        ReportDrop(
                            sink, item,
                            "a UI sprite record's runtime texture binding is "
                            "retired, cross-device, or does not match its "
                            "lifetime token, so the command is dropped",
                            "rebuild the snapshot after the device or texture "
                            "binding changed; a stale handle must never reach "
                            "the queue");
                    }
                    continue;
                }
                command.batchKey.texture = sprite->binding.texture;
                command.batchKey.textureSampler = sprite->binding.sampler;
                command.batchKey.textureStableId =
                    sprite->textureContentStableId;
                // 이름과 지분을 짝으로 싣는다. 이 명령이 제출되면
                // RenderSystem2D가 이 지분을 프레임 fence로 넘긴다.
                command.resourceLifetimeIdentity =
                    sprite->binding.lifetimeIdentity;
                command.resourceLifetime = sprite->resourceLifetime;
                // 출처를 함께 싣는다. atlas page 수열과 이 수열은 둘 다
                // 1에서 시작하므로, 출처 없이 값만 실으면 같은 프레임의
                // glyph 명령과 정체성이 충돌한다.
                command.resourceLifetimeDomain =
                    molga::ResourceLifetimeDomain::TextureBinding;
            }
            FillQuad(command, *physicalRect, sprite->tint);
            queue.Submit(command);
            ++submittedItems_;
            continue;
        }

        if (const auto* solid = std::get_if<UISolidRectSnapshot>(&item.payload)) {
            molga::RenderCommand command = MakeUiCommand(item, physicalClip);
            FillQuad(command, *physicalRect, solid->color);
            queue.Submit(command);
            ++submittedItems_;
            continue;
        }

        const auto* text = std::get_if<UITextSnapshot>(&item.payload);
        if (!text || !text->layout) {
            ++droppedItems_;
            continue;
        }

        if (!rasterPolicyResolved) {
            rasterPolicyResolved = true;
            rasterPolicy = transform.RasterPolicy(sink);
        }
        if (!rasterPolicy) {
            // 정책이 없으면 atlas를 건드리기 전에 떨어뜨린다. 조회 자체가
            // page를 만들 수 있으므로, 그릴 수 없는 화면에서도 비용이 없어야
            // 한다.
            ++droppedItems_;
            continue;
        }

        // Step 5c: affine이 없으면 glyph를 하나도 내보내지 않는다. 원점만
        // 옮긴 항등 변환으로 대신하면 backing scale이 1이 아닌 표면에서
        // 글자가 뷰포트 배율 없이 그려진다.
        const auto affine = transform.LayoutToOutputAffine(text->origin);
        if (!affine) {
            ++droppedItems_;
            if (NoteFact(worldGeneration, ItemFactKey(item, "affine"), sink)) {
                ReportDrop(sink, item,
                           "a UI text record has no layout-to-output affine "
                           "for this viewport, so no glyph is submitted",
                           "an origin-only translation is never a substitute; "
                           "fix the viewport or the stored logical origin");
            }
            continue;
        }

        // 예약된 구간과 이 배치가 실제로 낼 위치 기록 수가 어긋나면, 다음
        // 항목의 정렬 키는 이미 잘못된 자리에서 시작한 뒤다. 그리지 않는다.
        const std::uint64_t span =
            molga::text::TextRenderCommandSpan(*text->layout);
        if (span != item.reservedCommandSpan) {
            ++droppedItems_;
            if (NoteFact(worldGeneration, ItemFactKey(item, "span"), sink)) {
                ReportDrop(sink, item,
                           "a UI text record's reserved command span (" +
                               std::to_string(item.reservedCommandSpan) +
                               ") does not match its layout's positioned "
                               "record count (" + std::to_string(span) + ")",
                           "the reservation and the collection must count the "
                           "same records; a mismatch misplaces every later "
                           "draw order key");
            }
            continue;
        }

        TextCollectContext context;
        context.layoutToOutput = *affine;
        context.color = text->color;
        context.cameraPass = kUISnapshotCameraPass;
        context.rasterPolicy = *rasterPolicy;
        context.uiDrawOrder = item.order;
        // 이미 예약된 번호를 그대로 쓴다. 수집기 지역의 두 번째 기준점을
        // 만들지 않는다 — 그 순간 hit-test가 보는 순서와 화면의 순서가
        // 서로 다른 두 규칙을 따르게 된다.
        context.stableSubmissionBase = item.order.stableSubmissionIndex;
        context.scissor = physicalClip;
        textRenderer.CollectLayout(queue, *text->layout, context, sink);
        ++submittedItems_;
    }
}

} // namespace molga::ui

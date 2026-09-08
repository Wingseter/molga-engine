#include "Rendering/TextureBindingRegistry.h"

#include "Rendering/GraphicsDevice.h"

#include <algorithm>

namespace molga {

TextureBindingRegistry& TextureBindingRegistry::Get() {
    // 의도적으로 파괴하지 않는다.
    //
    // 이 등록부는 정적 저장 수명을 가진 다른 시설이 *죽는 동안* 손을 뻗는
    // 자리다: TextureManager의 텍스처 캐시가 종료 시점에 자기 Texture들을
    // 파괴하고, 그 ~Texture가 은퇴를 알리러 여기로 온다. 평범한 Meyers
    // 싱글턴이면 나중에 만들어진 이쪽이 먼저 죽으므로, 그때 도착한 ~Texture는
    // 이미 파괴된 vector를 건드린다 — 프로세스 종료 때만 나오는
    // "pointer being freed was not allocated"가 정확히 그 경로다.
    //
    // 종료 시점에 남는 것은 값 몇 개뿐이고, 진짜 GPU 반납은 그 전에
    // DestroyRetiredBindings/SweepRetiredBindings가 이미 끝낸다.
    static TextureBindingRegistry* registry = new TextureBindingRegistry();
    return *registry;
}

void TextureBindingRegistry::ReleaseRecord(Record& record) {
    // 핸들을 실제로 반납하고 나서야 기록을 놓는다. 반납 없이 버리면 그 핸들을
    // 아는 마지막 지식이 사라진다 — 그것이 곧 누수다.
    if (GraphicsDevice* device = GraphicsDevice::Current()) {
        TextureHandle texture = record.identity.texture;
        SamplerHandle sampler = record.identity.sampler;
        device->DestroySampler(sampler);
        device->DestroyTexture(texture);
    }
    ++releasedBindings_;
}

std::size_t TextureBindingRegistry::SweepRetiredBindings() {
    std::size_t released = 0;
    std::vector<Record> remaining;
    remaining.reserve(records_.size());
    for (auto& record : records_) {
        // 만료되지 않은 토큰은 진짜 외부 소유자다(스냅샷, 렌더 명령, 텍스처
        // 매니저의 지분). 그 핸들은 건드리지 않는다.
        if (!record.token.expired()) {
            remaining.push_back(std::move(record));
            continue;
        }
        ReleaseRecord(record);
        ++released;
    }
    records_ = std::move(remaining);
    return released;
}

std::shared_ptr<const TextureBindingLifetime> TextureBindingRegistry::Publish(
    const ui::TextureRuntimeBindingIdentity& identity) {
    // 은퇴한 기록을 먼저 반납한다. 그러지 않으면 같은 텍스처를 계속 다시
    // 올리는 세션에서 기록이 상한 없이 쌓인다.
    SweepRetiredBindings();
    auto token = std::make_shared<TextureBindingLifetime>();
    token->identity_ = identity;
    Record record;
    record.identity = identity;
    record.token = token;
    records_.push_back(std::move(record));
    return token;
}

std::size_t TextureBindingRegistry::LiveRetainedBindingCount(
    std::uint64_t deviceGeneration) const {
    std::size_t live = 0;
    for (const auto& record : records_) {
        if (record.identity.deviceGeneration != deviceGeneration) continue;
        if (!record.token.expired()) ++live;
    }
    return live;
}

bool TextureBindingRegistry::DestroyRetiredBindings(
    std::uint64_t deviceGeneration, std::string& errorOut) {
    const std::size_t live = LiveRetainedBindingCount(deviceGeneration);
    if (live != 0) {
        // 만료되지 않은 토큰은 진짜 외부 소유자다. 여기서 파괴하면 그 소유자가
        // 다음 프레임에 죽은 핸들을 제출한다.
        errorOut = "texture binding teardown blocked: " + std::to_string(live) +
                   " external binding lifetime(s) still retained";
        return false;
    }
    std::vector<Record> remaining;
    remaining.reserve(records_.size());
    for (auto& record : records_) {
        if (record.identity.deviceGeneration != deviceGeneration) {
            remaining.push_back(std::move(record));
            continue;
        }
        ReleaseRecord(record);
    }
    records_ = std::move(remaining);
    errorOut.clear();
    return true;
}

std::size_t TextureBindingRegistry::RecordCount() const noexcept {
    return records_.size();
}

std::uint64_t TextureBindingRegistry::ReleasedBindingCount() const noexcept {
    return releasedBindings_;
}

} // namespace molga

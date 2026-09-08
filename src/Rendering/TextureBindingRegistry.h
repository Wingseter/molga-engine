#pragma once

#include "UI/UILayoutSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace molga {

// ── Step 3c: 게시된 바인딩 하나의 수명 토큰 ─────────────────────────────────
// 값만 담는다. 이 토큰의 지분을 든 스냅샷이나 렌더 명령이 하나라도 살아 있는
// 동안에는 그 핸들을 파괴할 수 없다.
class TextureBindingLifetime {
public:
    const ui::TextureRuntimeBindingIdentity& Identity() const noexcept {
        return identity_;
    }

private:
    friend class TextureBindingRegistry;
    ui::TextureRuntimeBindingIdentity identity_;
};

// 약한 기록만 남기는 바인딩 등록부.
//
// ── 소유 규칙 (Task 11.1 마감) ──────────────────────────────────────────────
// 게시된 바인딩의 GPU 핸들은 **이 등록부만** 파괴한다. Texture가 자기 포인터를
// 놓거나 바인딩을 교체하는 것은 그 바인딩을 *은퇴*시키는 것일 뿐이다. 규칙이
// 하나여야 하는 이유는 단순하다: Texture가 직접 파괴하면, 아직 살아 있는
// 스냅샷이 붙든 토큰을 등록부가 거절해도 핸들은 이미 사라진 뒤다. 거절이
// 아무것도 지키지 못한다.
//
// 기록은 나중에 파괴할 핸들과 그 정체성을 값으로 들고 있지만, 토큰 자체는
// weak_ptr로만 붙든다. 그래야 "GPU를 비우고 캐시/최신 프레임/텍스처 매니저의
// 강한 소유자를 전부 놓은 뒤에도 만료되지 않은 토큰"이 곧 진짜 외부 소유자가
// 된다 — shared_ptr::use_count()의 임계값을 추측하는 것과 다르다. use_count는
// 등록부 자신의 지분까지 세므로 임계값이 구현 세부에 달라붙고, 그 임계값은
// 소유자가 하나 늘어난 날 조용히 틀린다.
//
// 만료된 토큰의 기록은 버리는 것이 아니라 **놓아 준다**. 그냥 버리면 아직
// 파괴되지 않은 핸들을 아는 마지막 지식이 사라져, 등록부가 책임진 핸들이
// 영원히 반납되지 않는다.
class TextureBindingRegistry {
public:
    static TextureBindingRegistry& Get();

    // 정체성 하나를 게시하고 그 수명 토큰을 돌려준다. 같은 정체성을 두 번
    // 게시하면 서로 다른 토큰이 나온다 — 정체성은 값이고 토큰은 소유권이라,
    // 두 번째 게시가 첫 번째의 소유자를 대신 늘리면 첫 게시자가 놓은 뒤에도
    // 핸들이 살아남는다.
    //
    // 게시 전에 은퇴한 기록을 한 번 쓸어 낸다(SweepRetiredBindings와 같은 일).
    std::shared_ptr<const TextureBindingLifetime> Publish(
        const ui::TextureRuntimeBindingIdentity&);

    // 외부 소유자가 하나도 남지 않은 기록의 핸들을 실제로 반납하고 그 기록을
    // 놓는다. 돌려주는 값은 이번에 놓은 기록 수다.
    //
    // 이것이 Texture의 옛 직접 파괴를 대신하는 자리다: 살아 있는 스냅샷이
    // 토큰을 들고 있으면 그 기록은 만료되지 않았으므로 손대지 않는다.
    std::size_t SweepRetiredBindings();

    // 그 장치 세대에 묶인 기록 중 아직 만료되지 않은 토큰의 수.
    std::size_t LiveRetainedBindingCount(std::uint64_t deviceGeneration) const;

    // 그 장치 세대의 은퇴한 기록을 실제로 파괴한다. 살아 있는 외부 토큰이
    // 하나라도 있으면 아무것도 파괴하지 않고 거짓을 돌려준다.
    bool DestroyRetiredBindings(std::uint64_t deviceGeneration,
                                std::string& errorOut);

    // 등록부에 남은 기록 수. 테스트가 누수를 관찰하는 자리다.
    std::size_t RecordCount() const noexcept;
    // 등록부가 지금까지 놓아 준(= 핸들 반납까지 끝낸) 기록의 누적 수.
    //
    // 관찰자가 없으면 은퇴 경로는 시험할 수 없다: LiveRetainedBindingCount는
    // 만료된 기록을 정의상 건너뛰므로, 기록을 조용히 버리는 구현과 제대로
    // 반납하는 구현이 그 계수기에서 똑같이 보인다.
    std::uint64_t ReleasedBindingCount() const noexcept;

private:
    struct Record {
        ui::TextureRuntimeBindingIdentity identity;
        std::weak_ptr<const TextureBindingLifetime> token;
    };
    void ReleaseRecord(Record&);

    std::vector<Record> records_;
    std::uint64_t releasedBindings_ = 0;
};

} // namespace molga

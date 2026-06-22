#include "Rendering/RenderPassState.h"
#include "doctest.h"

using molga::RenderPassState;

TEST_CASE("begin then end is a valid empty pass") {
    RenderPassState s;
    CHECK(s.phase() == RenderPassState::Phase::Idle);
    CHECK(s.TryBegin());
    CHECK(s.CanDraw());
    CHECK(s.TryEnd());
    CHECK(s.phase() == RenderPassState::Phase::Idle);
    CHECK(s.violations() == 0);
}

TEST_CASE("nested begin is rejected and counted") {
    RenderPassState s;
    REQUIRE(s.TryBegin());
    CHECK_FALSE(s.TryBegin());     // nested begin → violation, state unchanged
    CHECK(s.CanDraw());            // still inside the first pass
    CHECK(s.violations() == 1);
}

TEST_CASE("draw is not allowed outside a pass") {
    RenderPassState s;
    CHECK_FALSE(s.CanDraw());
}

TEST_CASE("end without begin is rejected and counted") {
    RenderPassState s;
    CHECK_FALSE(s.TryEnd());
    CHECK(s.violations() == 1);
    CHECK(s.phase() == RenderPassState::Phase::Idle);
}

TEST_CASE("one pass can host 100+ draws") {
    RenderPassState s;
    REQUIRE(s.TryBegin());
    for (int i = 0; i < 128; ++i) {
        CHECK(s.CanDraw());        // 100개 이상 스프라이트가 한 패스 안에서 그려질 수 있어야 한다
    }
    REQUIRE(s.TryEnd());
    CHECK(s.violations() == 0);
}

#include "Rendering/Renderer.h"

TEST_CASE("Renderer counts draw calls and shader switches between resets") {
    Renderer r;
    molga::RenderStats before = r.Stats();
    CHECK(before.drawCalls == 0);
    CHECK(before.shaderSwitches == 0);
    r.ResetStats();
    CHECK(r.Stats().drawCalls == 0);
    CHECK(r.Stats().shaderSwitches == 0);
}

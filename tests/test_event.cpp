#include "Core/Event.h"
#include "Core/EventBus.h"
#include "doctest.h"
#include <vector>

// ── Test event types ─────────────────────────────────────────────────────────

struct TestEvent : EventBase {
    int value = 0;
};

struct OtherEvent : EventBase {
    float data = 0.0f;
};

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("Event: subscribe and publish") {
    bool called = false;
    int received = 0;

    EventBus::Subscribe<TestEvent>([&](TestEvent& e) {
        called = true;
        received = e.value;
    });

    TestEvent event;
    event.value = 42;
    EventBus::Publish(event);

    CHECK(called);
    CHECK(received == 42);

    EventBus::Clear();
}

TEST_CASE("Event: unsubscribe") {
    bool called = false;

    SubscriptionID id = EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        called = true;
    });

    EventBus::Unsubscribe(id);

    TestEvent event;
    EventBus::Publish(event);

    CHECK(!called);

    EventBus::Clear();
}

TEST_CASE("Event: priority order") {
    std::vector<int> order;

    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        order.push_back(0);
    }, 0);

    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        order.push_back(10);
    }, 10);

    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        order.push_back(5);
    }, 5);

    TestEvent event;
    EventBus::Publish(event);

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 10);
    CHECK(order[1] == 5);
    CHECK(order[2] == 0);

    EventBus::Clear();
}

TEST_CASE("Event: handled cancellation") {
    bool handlerA_called = false;
    bool handlerB_called = false;

    EventBus::Subscribe<TestEvent>([&](TestEvent& e) {
        handlerA_called = true;
        e.handled = true;
    }, 10);

    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        handlerB_called = true;
    }, 0);

    TestEvent event;
    EventBus::Publish(event);

    CHECK(handlerA_called);
    CHECK(!handlerB_called);

    EventBus::Clear();
}

TEST_CASE("Event: queue deferred") {
    bool called = false;
    int received = 0;

    EventBus::Subscribe<TestEvent>([&](TestEvent& e) {
        called = true;
        received = e.value;
    });

    TestEvent event;
    event.value = 99;
    EventBus::QueueEvent(event);

    // Not yet processed
    CHECK(!called);

    EventBus::ProcessQueue();

    CHECK(called);
    CHECK(received == 99);

    EventBus::Clear();
}

TEST_CASE("Event: scoped subscription") {
    int callCount = 0;

    {
        ScopedSubscription scoped(
            EventBus::Subscribe<TestEvent>([&](TestEvent&) {
                callCount++;
            })
        );

        TestEvent event;
        EventBus::Publish(event);
        CHECK(callCount == 1);
    }
    // ScopedSubscription destroyed — handler unsubscribed

    TestEvent event;
    EventBus::Publish(event);
    CHECK(callCount == 1);  // not called again

    EventBus::Clear();
}

TEST_CASE("Event: unsubscribe during publish") {
    bool handlerA_called = false;
    bool handlerB_called = false;
    SubscriptionID idB = 0;

    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        handlerA_called = true;
        EventBus::Unsubscribe(idB);  // remove B during publish
    }, 10);

    idB = EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        handlerB_called = true;
    }, 0);

    TestEvent event;
    EventBus::Publish(event);

    CHECK(handlerA_called);
    CHECK(!handlerB_called);  // B was removed by A during publish

    // Publish again — B should be permanently gone
    handlerA_called = false;
    handlerB_called = false;
    TestEvent event2;
    EventBus::Publish(event2);

    CHECK(handlerA_called);
    CHECK(!handlerB_called);

    EventBus::Clear();
}

TEST_CASE("Event: a queued subscription destroyed during publish is never installed") {
    int transientCalls = 0;
    EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        ScopedSubscription transient(EventBus::Subscribe<TestEvent>(
            [&](TestEvent&) { ++transientCalls; }));
    });

    TestEvent first;
    EventBus::Publish(first);
    TestEvent second;
    EventBus::Publish(second);
    CHECK(transientCalls == 0);

    EventBus::Clear();
}

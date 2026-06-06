#include "Core/Event.h"
#include "Core/EventBus.h"
#include <cassert>
#include <cstdio>
#include <vector>

// ── Test event types ─────────────────────────────────────────────────────────

struct TestEvent : EventBase {
    int value = 0;
};

struct OtherEvent : EventBase {
    float data = 0.0f;
};

// ── Tests ────────────────────────────────────────────────────────────────────

static void test_subscribe_publish() {
    bool called = false;
    int received = 0;

    EventBus::Subscribe<TestEvent>([&](TestEvent& e) {
        called = true;
        received = e.value;
    });

    TestEvent event;
    event.value = 42;
    EventBus::Publish(event);

    assert(called);
    assert(received == 42);

    EventBus::Clear();
}

static void test_unsubscribe() {
    bool called = false;

    SubscriptionID id = EventBus::Subscribe<TestEvent>([&](TestEvent&) {
        called = true;
    });

    EventBus::Unsubscribe(id);

    TestEvent event;
    EventBus::Publish(event);

    assert(!called);

    EventBus::Clear();
}

static void test_priority_order() {
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

    assert(order.size() == 3);
    assert(order[0] == 10);
    assert(order[1] == 5);
    assert(order[2] == 0);

    EventBus::Clear();
}

static void test_handled_cancellation() {
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

    assert(handlerA_called);
    assert(!handlerB_called);

    EventBus::Clear();
}

static void test_queue_deferred() {
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
    assert(!called);

    EventBus::ProcessQueue();

    assert(called);
    assert(received == 99);

    EventBus::Clear();
}

static void test_scoped_subscription() {
    int callCount = 0;

    {
        ScopedSubscription scoped(
            EventBus::Subscribe<TestEvent>([&](TestEvent&) {
                callCount++;
            })
        );

        TestEvent event;
        EventBus::Publish(event);
        assert(callCount == 1);
    }
    // ScopedSubscription destroyed — handler unsubscribed

    TestEvent event;
    EventBus::Publish(event);
    assert(callCount == 1);  // not called again

    EventBus::Clear();
}

static void test_unsubscribe_during_publish() {
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

    assert(handlerA_called);
    assert(!handlerB_called);  // B was removed by A during publish

    // Publish again — B should be permanently gone
    handlerA_called = false;
    handlerB_called = false;
    TestEvent event2;
    EventBus::Publish(event2);

    assert(handlerA_called);
    assert(!handlerB_called);

    EventBus::Clear();
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    test_subscribe_publish();
    test_unsubscribe();
    test_priority_order();
    test_handled_cancellation();
    test_queue_deferred();
    test_scoped_subscription();
    test_unsubscribe_during_publish();

    printf("test_event: all 7 tests passed\n");
    return 0;
}

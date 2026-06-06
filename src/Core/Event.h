#pragma once

#include <cstddef>
#include <cstdint>

// Compile-time event type ID — mirrors ComponentTypeID pattern from ECS/Component.h
class EventTypeID {
    static inline size_t nextID = 0;
public:
    template<typename T>
    static size_t Get() {
        static size_t id = nextID++;
        return id;
    }
};

// Opaque handle for unsubscribe. 0 = invalid.
using SubscriptionID = uint64_t;

// Base struct for all events. Handlers set handled=true to stop propagation.
struct EventBase {
    bool handled = false;
};

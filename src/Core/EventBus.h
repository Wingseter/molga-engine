#pragma once

#include "Event.h"

#include <functional>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <exception>

class EventBus {
    // --- Internal types ---

    struct HandlerEntry {
        SubscriptionID id;
        int priority;
        std::function<void(void*)> erasedCallback;
    };

    struct HandlerList {
        std::vector<HandlerEntry> handlers;         // sorted descending by priority
        bool publishing = false;                     // re-entrancy guard
        std::vector<HandlerEntry> pendingAdds;       // deferred adds during publish
        std::vector<SubscriptionID> pendingRemoves;  // deferred removes during publish
    };

    struct SubscriptionRecord {
        size_t eventTypeID;
    };

    // --- Static storage ---
    static inline std::unordered_map<size_t, HandlerList> handlers_;
    static inline std::unordered_map<SubscriptionID, SubscriptionRecord> subscriptionMap_;
    static inline std::vector<std::function<void()>> deferredQueue_;
    static inline SubscriptionID nextSubID_ = 1;

public:
    // Subscribe to EventT with optional priority (higher = called first)
    template<typename EventT>
    static SubscriptionID Subscribe(std::function<void(EventT&)> callback,
                                     int priority = 0);

    // Non-template unsubscribe by ID only
    static void Unsubscribe(SubscriptionID id);

    // Publish event immediately. Handlers called in priority order.
    // Stops if event.handled becomes true.
    template<typename EventT>
    static void Publish(EventT& event);

    // Queue event for deferred processing (copies the event)
    template<typename EventT>
    static void QueueEvent(EventT event);

    // Process all queued events. Called at frame end.
    static void ProcessQueue();

    // Remove all handlers and queued events.
    static void Clear();
};

// RAII auto-unsubscribe handle
class ScopedSubscription {
public:
    ScopedSubscription() = default;
    explicit ScopedSubscription(SubscriptionID id) : id_(id) {}

    ~ScopedSubscription() {
        if (id_ != 0) EventBus::Unsubscribe(id_);
    }

    ScopedSubscription(ScopedSubscription&& other) noexcept : id_(other.id_) {
        other.id_ = 0;
    }

    ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
        if (this != &other) {
            if (id_ != 0) EventBus::Unsubscribe(id_);
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;

    SubscriptionID GetID() const { return id_; }
    void Release() { id_ = 0; }

private:
    SubscriptionID id_ = 0;
};

// --- Template implementations ---

template<typename EventT>
SubscriptionID EventBus::Subscribe(std::function<void(EventT&)> callback,
                                    int priority) {
    size_t typeID = EventTypeID::Get<EventT>();
    SubscriptionID subID = nextSubID_++;

    HandlerEntry entry;
    entry.id = subID;
    entry.priority = priority;
    entry.erasedCallback = [cb = std::move(callback)](void* raw) {
        cb(*static_cast<EventT*>(raw));
    };

    auto& list = handlers_[typeID];

    if (list.publishing) {
        list.pendingAdds.push_back(std::move(entry));
    } else {
        auto it = std::upper_bound(
            list.handlers.begin(), list.handlers.end(), entry,
            [](const HandlerEntry& a, const HandlerEntry& b) {
                return a.priority > b.priority;
            }
        );
        list.handlers.insert(it, std::move(entry));
    }

    subscriptionMap_[subID] = { typeID };
    return subID;
}

template<typename EventT>
void EventBus::Publish(EventT& event) {
    size_t typeID = EventTypeID::Get<EventT>();
    auto it = handlers_.find(typeID);
    if (it == handlers_.end()) return;

    HandlerList& list = it->second;
    list.publishing = true;

    std::exception_ptr callbackError;
    try {
        for (size_t i = 0; i < list.handlers.size(); ++i) {
            if (event.handled) break;

            // Skip handlers marked for removal during this Publish
            bool removed = false;
            for (auto remID : list.pendingRemoves) {
                if (list.handlers[i].id == remID) { removed = true; break; }
            }
            if (removed) continue;

            list.handlers[i].erasedCallback(static_cast<void*>(&event));
        }
    } catch (...) {
        callbackError = std::current_exception();
    }

    // Always restore the bus to a usable state, even when user code throws.
    list.publishing = false;

    // Retain removed IDs until queued additions are processed. A subscription
    // may be created and destroyed inside the same Publish; installing that
    // pending addition would leave an untracked callback behind.
    std::vector<SubscriptionID> removedDuringPublish;
    removedDuringPublish.swap(list.pendingRemoves);
    for (auto removeID : removedDuringPublish) {
        list.handlers.erase(
            std::remove_if(list.handlers.begin(), list.handlers.end(),
                [removeID](const HandlerEntry& e) { return e.id == removeID; }),
            list.handlers.end()
        );
    }

    // Apply pending additions (sorted insertion)
    for (auto& addEntry : list.pendingAdds) {
        if (std::find(removedDuringPublish.begin(), removedDuringPublish.end(),
                      addEntry.id) != removedDuringPublish.end()) {
            continue;
        }
        auto ins = std::upper_bound(
            list.handlers.begin(), list.handlers.end(), addEntry,
            [](const HandlerEntry& a, const HandlerEntry& b) {
                return a.priority > b.priority;
            }
        );
        list.handlers.insert(ins, std::move(addEntry));
    }
    list.pendingAdds.clear();

    if (callbackError) std::rethrow_exception(callbackError);
}

template<typename EventT>
void EventBus::QueueEvent(EventT event) {
    deferredQueue_.push_back([ev = std::move(event)]() mutable {
        EventBus::Publish(ev);
    });
}

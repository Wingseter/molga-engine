#include "EventBus.h"

#include <algorithm>

void EventBus::Unsubscribe(SubscriptionID id) {
    if (id == 0) return;

    auto recIt = subscriptionMap_.find(id);
    if (recIt == subscriptionMap_.end()) return;

    size_t typeID = recIt->second.eventTypeID;
    subscriptionMap_.erase(recIt);

    auto listIt = handlers_.find(typeID);
    if (listIt == handlers_.end()) return;

    HandlerList& list = listIt->second;

    if (list.publishing) {
        list.pendingRemoves.push_back(id);
    } else {
        list.handlers.erase(
            std::remove_if(list.handlers.begin(), list.handlers.end(),
                [id](const HandlerEntry& e) { return e.id == id; }),
            list.handlers.end()
        );
    }
}

void EventBus::ProcessQueue() {
    // Swap to local to allow QueueEvent during processing
    std::vector<std::function<void()>> batch;
    batch.swap(deferredQueue_);

    for (auto& fn : batch) {
        fn();
    }
}

void EventBus::Clear() {
    handlers_.clear();
    subscriptionMap_.clear();
    deferredQueue_.clear();
    // Do NOT reset nextSubID_ — stale ScopedSubscription safety
}

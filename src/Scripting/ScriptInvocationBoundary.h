#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class Script;
class World;

// A value identity for one Script instance. Component addresses can be reused
// after user code removes/replaces a Script, so deferred and planned calls must
// resolve this handle immediately before invocation.
struct ScriptHandle {
    unsigned int objectId = 0;
    std::size_t typeId = 0;
    std::uint64_t instanceId = 0;

    // Component instance IDs reserve zero; serialized GameObject IDs do not
    // need a sentinel and may legitimately be zero.
    explicit operator bool() const { return instanceId != 0; }

    friend bool operator==(const ScriptHandle& lhs, const ScriptHandle& rhs) {
        return lhs.objectId == rhs.objectId &&
               lhs.typeId == rhs.typeId &&
               lhs.instanceId == rhs.instanceId;
    }
    friend bool operator!=(const ScriptHandle& lhs, const ScriptHandle& rhs) {
        return !(lhs == rhs);
    }
};

enum class ScriptPhase {
    Awake,
    OnEnable,
    Start,
    Update,
    FixedUpdate,
    LateUpdate,
    OnDisable,
    CollisionEnter,
    CollisionStay,
    CollisionExit,
    TriggerEnter,
    TriggerStay,
    TriggerExit,
    Invoke,
    Coroutine,
};

const char* ScriptPhaseName(ScriptPhase phase);

// Runtime-only diagnostic state. It is deliberately not part of Script
// serialization, scene data, or prefab data.
struct ScriptFaultInfo {
    ScriptHandle handle;
    ScriptPhase phase = ScriptPhase::Update;
    std::string objectName;
    std::string scriptType;
    std::string exceptionMessage;
    std::string secondaryExceptionMessage;
    bool unknownException = false;
};

class ScriptInvocationBoundary {
public:
    using Callback = std::function<void(Script&)>;

    static ScriptHandle MakeHandle(const Script& script);

    // Returns nullptr when the object/component identity no longer exists or
    // when the current runtime state is not eligible for the requested call.
    static Script* Resolve(World& world, const ScriptHandle& handle,
                           bool allowInactive = false,
                           bool allowDisabled = false,
                           bool allowFaulted = false);

    // Returns true only when the exact instance was invoked and completed.
    // Script exceptions are always consumed here; a false result means the
    // instance was stale/ineligible or became faulted.
    static bool Invoke(World& world, const ScriptHandle& handle,
                       ScriptPhase phase, const Callback& callback,
                       bool allowInactive = false,
                       bool allowDisabled = false);

    // Used for teardown after a component has intentionally been detached from
    // its GameObject lookup table. The caller must keep the instance alive for
    // the duration of this synchronous call.
    static bool InvokeDetached(Script& script, ScriptPhase phase,
                               const Callback& callback);

private:
    static void IsolateFault(World& world, const ScriptHandle& handle,
                             const std::string& objectName,
                             const std::string& scriptType,
                             ScriptPhase phase, std::string message,
                             bool unknown, bool wasEnabled);
    static void RecordSecondaryFault(World& world, const ScriptHandle& handle,
                                     const ScriptFaultInfo& original,
                                     std::string message);
};

#include "ScriptInvocationBoundary.h"

#include "Script.h"
#include "Core/Scheduler.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "Common/Log.h"

#include <exception>
#include <sstream>

namespace {

struct InvocationMetadata {
    ScriptHandle handle;
    std::string objectName;
    std::string scriptType;
};

InvocationMetadata CaptureMetadata(const Script& script) {
    InvocationMetadata metadata;
    metadata.handle = ScriptInvocationBoundary::MakeHandle(script);
    if (const GameObject* object = script.GetGameObject()) {
        metadata.objectName = object->GetName();
    }
    metadata.scriptType = script.GetScriptName();
    return metadata;
}

std::string DescribeFault(const ScriptFaultInfo& fault) {
    std::ostringstream stream;
    stream << "Object '" << fault.objectName << "' (ID " << fault.handle.objectId
           << "), Script '" << fault.scriptType << "' (type ID "
           << fault.handle.typeId << ", instance ID " << fault.handle.instanceId
           << "), phase " << ScriptPhaseName(fault.phase) << ": "
           << fault.exceptionMessage;
    return stream.str();
}

ScriptFaultInfo MakeFault(const InvocationMetadata& metadata, ScriptPhase phase,
                          std::string message, bool unknown) {
    ScriptFaultInfo fault;
    fault.handle = metadata.handle;
    fault.phase = phase;
    fault.objectName = metadata.objectName;
    fault.scriptType = metadata.scriptType;
    fault.exceptionMessage = std::move(message);
    fault.unknownException = unknown;
    return fault;
}

void CancelOwnedWork(World* world, const ScriptHandle& handle) {
    if (world && world->GetScheduler()) {
        world->GetScheduler()->CancelInvoke(handle);
        world->GetScheduler()->StopCoroutines(handle);
    }
}

} // namespace

const char* ScriptPhaseName(ScriptPhase phase) {
    switch (phase) {
        case ScriptPhase::Awake: return "Awake";
        case ScriptPhase::OnEnable: return "OnEnable";
        case ScriptPhase::Start: return "Start";
        case ScriptPhase::Update: return "Update";
        case ScriptPhase::FixedUpdate: return "FixedUpdate";
        case ScriptPhase::LateUpdate: return "LateUpdate";
        case ScriptPhase::OnDisable: return "OnDisable";
        case ScriptPhase::CollisionEnter: return "OnCollisionEnter";
        case ScriptPhase::CollisionStay: return "OnCollisionStay";
        case ScriptPhase::CollisionExit: return "OnCollisionExit";
        case ScriptPhase::TriggerEnter: return "OnTriggerEnter";
        case ScriptPhase::TriggerStay: return "OnTriggerStay";
        case ScriptPhase::TriggerExit: return "OnTriggerExit";
        case ScriptPhase::Invoke: return "Invoke";
        case ScriptPhase::Coroutine: return "Coroutine";
    }
    return "Unknown";
}

ScriptHandle ScriptInvocationBoundary::MakeHandle(const Script& script) {
    ScriptHandle handle;
    if (const GameObject* object = script.GetGameObject()) {
        handle.objectId = object->GetID();
    }
    handle.typeId = script.GetRuntimeTypeID();
    handle.instanceId = script.GetInstanceID();
    return handle;
}

Script* ScriptInvocationBoundary::Resolve(World& world, const ScriptHandle& handle,
                                          bool allowInactive,
                                          bool allowDisabled,
                                          bool allowFaulted) {
    if (!handle) return nullptr;
    GameObject* object = world.FindById(handle.objectId);
    if (!object || (!allowInactive && !object->IsActive())) return nullptr;

    for (Component* component : object->GetComponents()) {
        if (!component || component->GetRuntimeTypeID() != handle.typeId ||
            component->GetInstanceID() != handle.instanceId) {
            continue;
        }
        Script* script = dynamic_cast<Script*>(component);
        if (!script || (!allowDisabled && !script->IsEnabled()) ||
            (!allowFaulted && script->IsFaulted())) {
            return nullptr;
        }
        return script;
    }
    return nullptr;
}

void ScriptInvocationBoundary::RecordSecondaryFault(
    World& world, const ScriptHandle& handle, const ScriptFaultInfo& original,
    std::string message) {
    if (Script* live = Resolve(world, handle, true, true, true)) {
        if (live->faultInfo_) {
            live->faultInfo_->secondaryExceptionMessage = message;
        }
    }
    Log::Error("Script", DescribeFault(original) +
        "; OnDisable while isolating the Script also failed: " + message);
}

void ScriptInvocationBoundary::IsolateFault(
    World& world, const ScriptHandle& handle, const std::string& objectName,
    const std::string& scriptType, ScriptPhase phase, std::string message,
    bool unknown, bool wasEnabled) {
    InvocationMetadata metadata{handle, objectName, scriptType};
    ScriptFaultInfo fault = MakeFault(metadata, phase, std::move(message), unknown);
    CancelOwnedWork(&world, handle);

    Script* live = Resolve(world, handle, true, true, true);
    if (live && live->faultInfo_) {
        // A nested path already isolated this exact instance. Do not emit the
        // same primary fault again.
        return;
    }

    const GameObject* owner = live ? live->GetGameObject() : nullptr;
    const bool callDisable = live && owner && owner->IsActive() && wasEnabled &&
                             live->enabled && phase != ScriptPhase::OnDisable;
    if (live) {
        live->faultInfo_ = fault;
        // Do not call Script::SetEnabled here: fault isolation owns the one
        // safe OnDisable attempt and must not recurse through normal toggling.
        live->enabled = false;
    }

    Log::Error("Script", DescribeFault(fault));

    if (!callDisable || !live) return;
    try {
        // Never inspect `live` after entering user code. OnDisable may remove
        // and destroy its own component.
        live->OnDisable();
    } catch (const std::exception& error) {
        RecordSecondaryFault(world, handle, fault, error.what());
    } catch (...) {
        RecordSecondaryFault(world, handle, fault, "unknown C++ exception");
    }
}

bool ScriptInvocationBoundary::Invoke(World& world, const ScriptHandle& handle,
                                      ScriptPhase phase, const Callback& callback,
                                      bool allowInactive, bool allowDisabled) {
    Script* script = Resolve(world, handle, allowInactive, allowDisabled, false);
    if (!script) return false;

    const InvocationMetadata metadata = CaptureMetadata(*script);
    const bool wasEnabled = script->IsEnabled();
    try {
        callback(*script);
        return true;
    } catch (const std::exception& error) {
        IsolateFault(world, metadata.handle, metadata.objectName,
                     metadata.scriptType, phase, error.what(), false,
                     wasEnabled);
    } catch (...) {
        IsolateFault(world, metadata.handle, metadata.objectName,
                     metadata.scriptType, phase, "unknown C++ exception", true,
                     wasEnabled);
    }
    return false;
}

bool ScriptInvocationBoundary::InvokeDetached(Script& script, ScriptPhase phase,
                                              const Callback& callback) {
    const InvocationMetadata metadata = CaptureMetadata(script);
    try {
        callback(script);
        return true;
    } catch (const std::exception& error) {
        ScriptFaultInfo fault = MakeFault(metadata, phase, error.what(), false);
        CancelOwnedWork(script.GetWorld(), metadata.handle);
        Log::Error("Script", DescribeFault(fault));
    } catch (...) {
        ScriptFaultInfo fault = MakeFault(
            metadata, phase, "unknown C++ exception", true);
        CancelOwnedWork(script.GetWorld(), metadata.handle);
        Log::Error("Script", DescribeFault(fault));
    }
    return false;
}

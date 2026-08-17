#include "Core/SaveSystem.h"

#include "Common/Log.h"
#include "Core/PersistentStorage.h"

#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path SlotPath(const std::string& slotName) {
    return PersistentStorage::Root() / "Saves" / (slotName + ".json");
}
} // namespace

bool SaveSystem::SaveSlot(const std::string& slotName,
                          const nlohmann::json& data) {
    if (!PersistentStorage::IsConfigured() || !IsValidSlotName(slotName)) {
        Log::Error("SaveSystem", "Invalid storage configuration or slot name.");
        return false;
    }

    try {
        const nlohmann::json envelope = {
            {"schemaVersion", kSchemaVersion},
            {"data", data}
        };
        const std::string serialized = envelope.dump(2);
        std::string error;
        if (!PersistentStorage::AtomicWriteText(
                SlotPath(slotName), serialized, &error)) {
            Log::Error("SaveSystem", error);
            return false;
        }
    } catch (const std::exception& e) {
        Log::Error("SaveSystem", "Could not serialize slot '" + slotName +
                                 "': " + e.what());
        return false;
    }
    return true;
}

bool SaveSystem::LoadSlot(const std::string& slotName,
                          nlohmann::json& dataOut) {
    if (!PersistentStorage::IsConfigured() || !IsValidSlotName(slotName)) {
        return false;
    }

    try {
        std::ifstream input(SlotPath(slotName));
        if (!input) return false;
        nlohmann::json envelope;
        input >> envelope;
        if (!envelope.is_object() ||
            !envelope.contains("schemaVersion") ||
            !envelope["schemaVersion"].is_number_integer() ||
            envelope["schemaVersion"].get<int>() != kSchemaVersion ||
            !envelope.contains("data")) {
            Log::Error("SaveSystem", "Unsupported or invalid save-slot envelope.");
            return false;
        }
        dataOut = envelope["data"];
        return true;
    } catch (const std::exception& e) {
        Log::Error("SaveSystem", "Could not load slot '" + slotName + "': " + e.what());
        return false;
    }
}

bool SaveSystem::DeleteSlot(const std::string& slotName) {
    if (!PersistentStorage::IsConfigured() || !IsValidSlotName(slotName)) {
        return false;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(SlotPath(slotName), error);
    return !error && removed;
}

bool SaveSystem::SlotExists(const std::string& slotName) {
    if (!PersistentStorage::IsConfigured() || !IsValidSlotName(slotName)) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(SlotPath(slotName), error) && !error;
}

bool SaveSystem::IsValidSlotName(const std::string& slotName) {
    if (slotName.empty() || slotName.size() > 64) return false;
    for (unsigned char c : slotName) {
        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (!alpha && !digit && c != '_' && c != '-') return false;
    }
    return true;
}

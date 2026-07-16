#pragma once

#include <nlohmann/json.hpp>
#include <string>

class SaveSystem {
public:
    static constexpr int kSchemaVersion = 1;

    static bool SaveSlot(const std::string& slotName,
                         const nlohmann::json& data);
    static bool LoadSlot(const std::string& slotName,
                         nlohmann::json& dataOut);
    static bool DeleteSlot(const std::string& slotName);
    static bool SlotExists(const std::string& slotName);

    static bool IsValidSlotName(const std::string& slotName);
};

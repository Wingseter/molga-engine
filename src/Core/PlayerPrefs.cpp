#include "Core/PlayerPrefs.h"

#include "Common/Log.h"
#include "Core/PersistentStorage.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
nlohmann::json g_values = nlohmann::json::object();
std::filesystem::path g_loadedPath;
bool g_loaded = false;
bool g_dirty = false;
bool g_corrupt = false;

std::filesystem::path PrefsPath() {
    return PersistentStorage::Root() / "prefs.json";
}

bool ValidKey(const std::string& key) {
    return !key.empty() && key.find('\0') == std::string::npos;
}

void EnsureLoaded() {
    const std::filesystem::path path = PrefsPath();
    if (g_loaded && path == g_loadedPath) return;

    g_values = nlohmann::json::object();
    g_loadedPath = path;
    g_loaded = true;
    g_dirty = false;
    g_corrupt = false;

    if (!PersistentStorage::IsConfigured() || !std::filesystem::exists(path)) {
        return;
    }

    try {
        std::ifstream input(path);
        nlohmann::json parsed;
        input >> parsed;
        if (!parsed.is_object()) {
            throw nlohmann::json::type_error::create(
                302, "prefs root must be an object", &parsed);
        }
        g_values = std::move(parsed);
    } catch (const std::exception& e) {
        g_corrupt = true;
        Log::Error("PlayerPrefs", "Could not read prefs.json: " + std::string(e.what()));
    }
}

const nlohmann::json* FindValue(const std::string& key) {
    EnsureLoaded();
    if (!ValidKey(key) || !g_values.contains(key)) return nullptr;
    return &g_values.at(key);
}
} // namespace

bool PlayerPrefs::GetBool(const std::string& key, bool defaultValue) {
    const auto* value = FindValue(key);
    return value && value->is_boolean() ? value->get<bool>() : defaultValue;
}

int PlayerPrefs::GetInt(const std::string& key, int defaultValue) {
    const auto* value = FindValue(key);
    if (!value || !value->is_number_integer() || value->is_number_unsigned()) {
        if (!value || !value->is_number_unsigned()) return defaultValue;
    }
    try {
        const auto number = value->get<long long>();
        if (number < std::numeric_limits<int>::min() ||
            number > std::numeric_limits<int>::max()) return defaultValue;
        return static_cast<int>(number);
    } catch (const std::exception&) {
        return defaultValue;
    }
}

double PlayerPrefs::GetDouble(const std::string& key, double defaultValue) {
    const auto* value = FindValue(key);
    return value && value->is_number_float() ? value->get<double>() : defaultValue;
}

std::string PlayerPrefs::GetString(const std::string& key,
                                   const std::string& defaultValue) {
    const auto* value = FindValue(key);
    return value && value->is_string() ? value->get<std::string>() : defaultValue;
}

bool PlayerPrefs::Get(const std::string& key, bool defaultValue) {
    return GetBool(key, defaultValue);
}
int PlayerPrefs::Get(const std::string& key, int defaultValue) {
    return GetInt(key, defaultValue);
}
double PlayerPrefs::Get(const std::string& key, double defaultValue) {
    return GetDouble(key, defaultValue);
}
std::string PlayerPrefs::Get(const std::string& key,
                             const std::string& defaultValue) {
    return GetString(key, defaultValue);
}

void PlayerPrefs::SetBool(const std::string& key, bool value) { Set(key, value); }
void PlayerPrefs::SetInt(const std::string& key, int value) { Set(key, value); }
void PlayerPrefs::SetDouble(const std::string& key, double value) { Set(key, value); }
void PlayerPrefs::SetString(const std::string& key, const std::string& value) { Set(key, value); }

void PlayerPrefs::Set(const std::string& key, bool value) {
    EnsureLoaded();
    if (!ValidKey(key)) return;
    g_values[key] = value;
    g_dirty = true;
}
void PlayerPrefs::Set(const std::string& key, int value) {
    EnsureLoaded();
    if (!ValidKey(key)) return;
    g_values[key] = value;
    g_dirty = true;
}
void PlayerPrefs::Set(const std::string& key, double value) {
    EnsureLoaded();
    if (!ValidKey(key)) return;
    g_values[key] = value;
    g_dirty = true;
}
void PlayerPrefs::Set(const std::string& key, const std::string& value) {
    EnsureLoaded();
    if (!ValidKey(key)) return;
    g_values[key] = value;
    g_dirty = true;
}
void PlayerPrefs::Set(const std::string& key, const char* value) {
    Set(key, std::string(value ? value : ""));
}

bool PlayerPrefs::HasKey(const std::string& key) {
    return FindValue(key) != nullptr;
}

void PlayerPrefs::DeleteKey(const std::string& key) {
    EnsureLoaded();
    if (!ValidKey(key)) return;
    if (g_values.erase(key) > 0) g_dirty = true;
}

void PlayerPrefs::DeleteAll() {
    EnsureLoaded();
    g_values = nlohmann::json::object();
    g_dirty = true;
    // This is an explicit recovery action, so replacing a corrupt file is OK.
    g_corrupt = false;
}

bool PlayerPrefs::Save() {
    EnsureLoaded();
    if (!g_dirty) return !g_corrupt;
    if (!PersistentStorage::IsConfigured()) {
        Log::Error("PlayerPrefs", "Persistent storage has not been configured.");
        return false;
    }
    if (g_corrupt) {
        Log::Error("PlayerPrefs",
                   "Refusing to overwrite corrupt prefs.json without DeleteAll().");
        return false;
    }

    try {
        const std::string serialized = g_values.dump(2);
        std::string error;
        if (!PersistentStorage::AtomicWriteText(PrefsPath(), serialized, &error)) {
            Log::Error("PlayerPrefs", error);
            return false;
        }
    } catch (const std::exception& e) {
        // Keep the cache dirty so callers may repair the invalid value and
        // retry. Serialization happens before AtomicWriteText, therefore an
        // existing prefs file is left untouched.
        Log::Error("PlayerPrefs", "Could not serialize prefs.json: " +
                                  std::string(e.what()));
        return false;
    }
    g_dirty = false;
    return true;
}

bool PlayerPrefs::IsDirty() {
    EnsureLoaded();
    return g_dirty;
}

bool PlayerPrefs::Shutdown() {
    return Save();
}

void PlayerPrefs::ResetCacheForTesting() {
    g_values = nlohmann::json::object();
    g_loadedPath.clear();
    g_loaded = false;
    g_dirty = false;
    g_corrupt = false;
}

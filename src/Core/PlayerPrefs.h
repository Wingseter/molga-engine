#pragma once

#include <string>

// Typed, process-local preference cache backed by <storage-root>/prefs.json.
// Set/Delete mark the cache dirty; Save performs an atomic replacement.
class PlayerPrefs {
public:
    static bool GetBool(const std::string& key, bool defaultValue = false);
    static int GetInt(const std::string& key, int defaultValue = 0);
    static double GetDouble(const std::string& key, double defaultValue = 0.0);
    static std::string GetString(const std::string& key,
                                 const std::string& defaultValue = {});

    static bool Get(const std::string& key, bool defaultValue);
    static int Get(const std::string& key, int defaultValue);
    static double Get(const std::string& key, double defaultValue);
    static std::string Get(const std::string& key,
                           const std::string& defaultValue);

    static void SetBool(const std::string& key, bool value);
    static void SetInt(const std::string& key, int value);
    static void SetDouble(const std::string& key, double value);
    static void SetString(const std::string& key, const std::string& value);

    static void Set(const std::string& key, bool value);
    static void Set(const std::string& key, int value);
    static void Set(const std::string& key, double value);
    static void Set(const std::string& key, const std::string& value);
    static void Set(const std::string& key, const char* value);

    static bool HasKey(const std::string& key);
    static void DeleteKey(const std::string& key);
    static void DeleteAll();
    static bool Save();
    static bool IsDirty();

    // Flushes dirty preferences on a normal engine shutdown.
    static bool Shutdown();

    // Drops only the in-memory cache; useful after changing the test root.
    static void ResetCacheForTesting();
};

#ifndef MOLGA_AUDIO_H
#define MOLGA_AUDIO_H

#include <string>
#include <unordered_map>
#include <memory>

struct ma_engine;
struct ma_sound;

// Custom deleters for miniaudio types (uninit then free)
struct MaEngineDeleter {
    void operator()(ma_engine* e);
};

struct MaSoundDeleter {
    void operator()(ma_sound* s);
};

class Audio {
public:
    static bool Init();
    static void Shutdown();

    // Sound effects
    static bool LoadSound(const std::string& name, const std::string& filepath);
    static void PlaySound(const std::string& name, float volume = 1.0f);
    static void StopSound(const std::string& name);

    // Music (streaming)
    static bool LoadMusic(const std::string& filepath);
    static void PlayMusic(bool loop = true);
    static void StopMusic();
    static void PauseMusic();
    static void ResumeMusic();
    static void SetMusicVolume(float volume);
    static bool IsMusicPlaying();

    // Master volume
    static void SetMasterVolume(float volume);
    static float GetMasterVolume();

private:
    static std::unique_ptr<ma_engine, MaEngineDeleter> engine;
    static std::unique_ptr<ma_sound, MaSoundDeleter> musicSound;
    static std::unordered_map<std::string, std::unique_ptr<ma_sound, MaSoundDeleter>> sounds;
    static float masterVolume;
    static bool initialized;
};

#endif

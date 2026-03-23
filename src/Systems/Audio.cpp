#define MINIAUDIO_IMPLEMENTATION
#include "../../external/miniaudio/miniaudio.h"
#include "Audio.h"
#include <iostream>

// Custom deleter implementations
void MaEngineDeleter::operator()(ma_engine* e) {
    if (e) {
        ma_engine_uninit(e);
        delete e;
    }
}

void MaSoundDeleter::operator()(ma_sound* s) {
    if (s) {
        ma_sound_uninit(s);
        delete s;
    }
}

// Static member definitions
std::unique_ptr<ma_engine, MaEngineDeleter> Audio::engine;
std::unique_ptr<ma_sound, MaSoundDeleter> Audio::musicSound;
std::unordered_map<std::string, std::unique_ptr<ma_sound, MaSoundDeleter>> Audio::sounds;
float Audio::masterVolume = 1.0f;
bool Audio::initialized = false;

bool Audio::Init() {
    if (initialized) return true;

    // init-then-wrap: only attach deleter after successful init
    auto raw = new ma_engine();
    if (ma_engine_init(nullptr, raw) != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        delete raw; // uninit not needed since init failed
        return false;
    }
    engine.reset(raw);

    initialized = true;
    return true;
}

void Audio::Shutdown() {
    if (!initialized) return;

    // Deleters handle uninit + delete automatically
    sounds.clear();
    musicSound.reset();
    engine.reset();

    initialized = false;
}

bool Audio::LoadSound(const std::string& name, const std::string& filepath) {
    if (!initialized) return false;

    // Remove existing sound with same name (deleter handles uninit+delete)
    sounds.erase(name);

    // init-then-wrap pattern
    auto raw = new ma_sound();
    if (ma_sound_init_from_file(engine.get(), filepath.c_str(), 0, nullptr, nullptr, raw) != MA_SUCCESS) {
        std::cerr << "Failed to load sound: " << filepath << std::endl;
        delete raw;
        return false;
    }

    sounds[name] = std::unique_ptr<ma_sound, MaSoundDeleter>(raw);
    return true;
}

void Audio::PlaySound(const std::string& name, float volume) {
    if (!initialized) return;

    auto it = sounds.find(name);
    if (it == sounds.end()) return;

    ma_sound_set_volume(it->second.get(), volume * masterVolume);
    ma_sound_seek_to_pcm_frame(it->second.get(), 0);
    ma_sound_start(it->second.get());
}

void Audio::StopSound(const std::string& name) {
    if (!initialized) return;

    auto it = sounds.find(name);
    if (it != sounds.end()) {
        ma_sound_stop(it->second.get());
    }
}

bool Audio::LoadMusic(const std::string& filepath) {
    if (!initialized) return false;

    // Reset existing music (deleter handles uninit+delete)
    musicSound.reset();

    // init-then-wrap pattern
    auto raw = new ma_sound();
    ma_uint32 flags = MA_SOUND_FLAG_STREAM;  // Stream for music
    if (ma_sound_init_from_file(engine.get(), filepath.c_str(), flags, nullptr, nullptr, raw) != MA_SUCCESS) {
        std::cerr << "Failed to load music: " << filepath << std::endl;
        delete raw;
        return false;
    }

    musicSound.reset(raw);
    return true;
}

void Audio::PlayMusic(bool loop) {
    if (!initialized || !musicSound) return;

    ma_sound_set_looping(musicSound.get(), loop);
    ma_sound_start(musicSound.get());
}

void Audio::StopMusic() {
    if (!initialized || !musicSound) return;

    ma_sound_stop(musicSound.get());
    ma_sound_seek_to_pcm_frame(musicSound.get(), 0);
}

void Audio::PauseMusic() {
    if (!initialized || !musicSound) return;
    ma_sound_stop(musicSound.get());
}

void Audio::ResumeMusic() {
    if (!initialized || !musicSound) return;
    ma_sound_start(musicSound.get());
}

void Audio::SetMusicVolume(float volume) {
    if (!initialized || !musicSound) return;
    ma_sound_set_volume(musicSound.get(), volume * masterVolume);
}

bool Audio::IsMusicPlaying() {
    if (!initialized || !musicSound) return false;
    return ma_sound_is_playing(musicSound.get());
}

void Audio::SetMasterVolume(float volume) {
    masterVolume = volume;
    if (initialized && engine) {
        ma_engine_set_volume(engine.get(), volume);
    }
}

float Audio::GetMasterVolume() {
    return masterVolume;
}

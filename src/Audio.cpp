#define MINIAUDIO_IMPLEMENTATION
#include "../external/miniaudio/miniaudio.h"
#include "Audio.h"
#include <iostream>

ma_engine* Audio::engine = nullptr;
ma_sound* Audio::musicSound = nullptr;
std::unordered_map<std::string, ma_sound*> Audio::sounds;
float Audio::masterVolume = 1.0f;
bool Audio::initialized = false;

bool Audio::Init() {
    if (initialized) return true;

    engine = new ma_engine();
    if (ma_engine_init(nullptr, engine) != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        delete engine;
        engine = nullptr;
        return false;
    }

    initialized = true;
    return true;
}

void Audio::Shutdown() {
    if (!initialized) return;

    // Stop and free all sounds
    for (auto& pair : sounds) {
        ma_sound_uninit(pair.second);
        delete pair.second;
    }
    sounds.clear();

    // Stop and free music
    if (musicSound) {
        ma_sound_uninit(musicSound);
        delete musicSound;
        musicSound = nullptr;
    }

    // Shutdown engine
    if (engine) {
        ma_engine_uninit(engine);
        delete engine;
        engine = nullptr;
    }

    initialized = false;
}

bool Audio::LoadSound(const std::string& name, const std::string& filepath) {
    if (!initialized) return false;

    // Remove existing sound with same name
    auto it = sounds.find(name);
    if (it != sounds.end()) {
        ma_sound_uninit(it->second);
        delete it->second;
        sounds.erase(it);
    }

    ma_sound* sound = new ma_sound();
    if (ma_sound_init_from_file(engine, filepath.c_str(), 0, nullptr, nullptr, sound) != MA_SUCCESS) {
        std::cerr << "Failed to load sound: " << filepath << std::endl;
        delete sound;
        return false;
    }

    sounds[name] = sound;
    return true;
}

void Audio::PlaySound(const std::string& name, float volume) {
    if (!initialized) return;

    auto it = sounds.find(name);
    if (it == sounds.end()) return;

    ma_sound_set_volume(it->second, volume * masterVolume);
    ma_sound_seek_to_pcm_frame(it->second, 0);
    ma_sound_start(it->second);
}

void Audio::StopSound(const std::string& name) {
    if (!initialized) return;

    auto it = sounds.find(name);
    if (it != sounds.end()) {
        ma_sound_stop(it->second);
    }
}

bool Audio::LoadMusic(const std::string& filepath) {
    if (!initialized) return false;

    // Stop and free existing music
    if (musicSound) {
        ma_sound_uninit(musicSound);
        delete musicSound;
        musicSound = nullptr;
    }

    musicSound = new ma_sound();
    ma_uint32 flags = MA_SOUND_FLAG_STREAM;  // Stream for music
    if (ma_sound_init_from_file(engine, filepath.c_str(), flags, nullptr, nullptr, musicSound) != MA_SUCCESS) {
        std::cerr << "Failed to load music: " << filepath << std::endl;
        delete musicSound;
        musicSound = nullptr;
        return false;
    }

    return true;
}

void Audio::PlayMusic(bool loop) {
    if (!initialized || !musicSound) return;

    ma_sound_set_looping(musicSound, loop);
    ma_sound_start(musicSound);
}

void Audio::StopMusic() {
    if (!initialized || !musicSound) return;

    ma_sound_stop(musicSound);
    ma_sound_seek_to_pcm_frame(musicSound, 0);
}

void Audio::PauseMusic() {
    if (!initialized || !musicSound) return;
    ma_sound_stop(musicSound);
}

void Audio::ResumeMusic() {
    if (!initialized || !musicSound) return;
    ma_sound_start(musicSound);
}

void Audio::SetMusicVolume(float volume) {
    if (!initialized || !musicSound) return;
    ma_sound_set_volume(musicSound, volume * masterVolume);
}

bool Audio::IsMusicPlaying() {
    if (!initialized || !musicSound) return false;
    return ma_sound_is_playing(musicSound);
}

void Audio::SetMasterVolume(float volume) {
    masterVolume = volume;
    if (initialized && engine) {
        ma_engine_set_volume(engine, volume);
    }
}

float Audio::GetMasterVolume() {
    return masterVolume;
}

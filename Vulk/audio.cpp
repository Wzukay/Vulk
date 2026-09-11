#include "audio.h"
#include "asset_manager.h"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

AudioEngine g_AudioEngine;

// Self-cleaning callback for fire-and-forget 3D sounds
void SoundEndCallback(void* pUserData, ma_sound* pSound) {
    AudioEngine* engine = static_cast<AudioEngine*>(pUserData);
    engine->MarkSoundForDeletion(pSound);
}   

void AudioEngine::MarkSoundForDeletion(void* sound) {
    std::lock_guard<std::mutex> lock(m_audioMutex);
    m_garbageSounds.push_back(sound);
}

void AudioEngine::Tick() {
    std::lock_guard<std::mutex> lock(m_audioMutex);
    for (void* p : m_garbageSounds) {
        ma_sound* sound = static_cast<ma_sound*>(p);
        ma_sound_uninit(sound);
        delete sound;
    }
    m_garbageSounds.clear();
}

void AudioEngine::Init() {
    m_engine = new ma_engine();
    ma_result result = ma_engine_init(NULL, static_cast<ma_engine*>(m_engine));

    if (result != MA_SUCCESS) {
        std::cerr << "[Audio Engine] Failed to initialize Miniaudio!\n";
        delete static_cast<ma_engine*>(m_engine);
        m_engine = nullptr;
    }
    else {
        std::cout << "[Audio Engine] Miniaudio initialized successfully.\n";
    }
}

void AudioEngine::Cleanup() {
    if (m_engine) {
        // 1. Process any pending garbage normally
        Tick();

        // 2. Shut down the engine (This triggers the callbacks for all currently playing sounds)
        ma_engine_uninit(static_cast<ma_engine*>(m_engine));
        delete static_cast<ma_engine*>(m_engine);
        m_engine = nullptr;

        // 3. Delete the raw C++ memory for the sounds that were killed during shutdown
        // (Do NOT call ma_sound_uninit here, the engine is already dead!)
        for (void* p : m_garbageSounds) {
            delete static_cast<ma_sound*>(p);
        }
        m_garbageSounds.clear();
    }
}

void AudioEngine::UpdateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_engine) return;
    ma_engine* engine = static_cast<ma_engine*>(m_engine);

    // Vulkan uses right-handed Y-up or Y-down depending on projection, 
    // Miniaudio defaults to right-handed. Pass your raw camera vectors.
    ma_engine_listener_set_position(engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(engine, 0, up.x, up.y, up.z);
}

void AudioEngine::Play2D(const std::string& nickname, float volume) {
    if (!m_engine) return;

    AssetRecord* record = g_AssetManager.GetAssetRecord(nickname);
    if (!record) {
        std::cerr << "[Audio Engine] Warning: Could not find sound alias '" << nickname << "'\n";
        return;
    }

    // Fire and forget global 2D sound
    ma_engine_play_sound(static_cast<ma_engine*>(m_engine), record->path.c_str(), NULL);
}

void AudioEngine::Play3D(const std::string& nickname, const glm::vec3& position, float volume, float minDistance) {
    if (!m_engine) return;

    AssetRecord* record = g_AssetManager.GetAssetRecord(nickname);
    if (!record) return;

    ma_engine* engine = static_cast<ma_engine*>(m_engine);
    ma_sound* sound = new ma_sound();

    ma_result result = ma_sound_init_from_file(engine, record->path.c_str(), 0, NULL, NULL, sound);

    if (result == MA_SUCCESS) {
        ma_sound_set_volume(sound, volume);
        ma_sound_set_position(sound, position.x, position.y, position.z);
        ma_sound_set_spatialization_enabled(sound, MA_TRUE);
        ma_sound_set_min_distance(sound, minDistance);
        ma_sound_set_max_distance(sound, minDistance * 10.0f);

        // FIX: Pass 'this' as the user data so the callback knows which engine to route to
        ma_sound_set_end_callback(sound, SoundEndCallback, this);

        ma_sound_start(sound);
    }
    else {
        delete sound;
    }
}
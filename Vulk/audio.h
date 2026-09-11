#pragma once

#include <string>
#include <glm/glm.hpp>
#include <vector>
#include <mutex>

class AudioEngine {
public:
    void Init();
    void Cleanup();
    void Tick(); // NEW: Called every frame to clear memory

    void UpdateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void Play2D(const std::string& nickname, float volume = 1.0f);
    void Play3D(const std::string& nickname, const glm::vec3& position, float volume = 1.0f, float minDistance = 5.0f);

    void MarkSoundForDeletion(void* sound); // NEW: Callback router

private:
    void* m_engine = nullptr;
    std::vector<void*> m_garbageSounds; // NEW: The garbage queue
    std::mutex m_audioMutex;            // NEW: Thread safety lock
};

extern AudioEngine g_AudioEngine;
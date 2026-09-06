#pragma once

#include "scene_interface.h"
#include "scene.h"
#include "chunk.h"
#include "player_system.h"
#include "day_night.h"
#include <random>

class GameplayScene : public IScene {
private:
    Scene m_scene;
    Chunk m_chunk;
    DayNightManager m_dayNight;
    Entity m_playerEntity = INVALID_ENTITY;
    bool m_isPaused = false;

public:
    using IScene::IScene;

    void OnEnter() override;
    void OnExit() override;
    void Update(float deltaTime) override;
    void Render() override;
};
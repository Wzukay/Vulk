#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <iostream>
#include <algorithm>
#include <string>
#include <cassert>
#include <limits>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "mesh_types.h"

// Forward declarations for templated view interactions
class Registry;
template <typename... Components> class MultiView;

// Identity Types
using Entity = uint32_t;
const Entity INVALID_ENTITY = 0xFFFFFFFF;
const size_t INVALID_INDEX = std::numeric_limits<size_t>::max();

// Pure Data Components
struct TransformComponent {
    // Spatial properties relative to parent node (or absolute world space if independent)
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler Angles (Pitch, Yaw, Roll)
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    bool isDirty = true;

    // Hierarchy Anchor Linkages
    Entity parent = INVALID_ENTITY;

    // OVERLOAD 1: For standalone/independent world entries (Fixes C2660 error!)
    void UpdateMatrix();

    // OVERLOAD 2: For parent-child hierarchical scene graph tracking
    void UpdateMatrix(Registry& registry);
};

struct RenderComponent {
    std::string meshName;
    uint32_t albedoTextureId = 0;
    uint32_t normalTextureId = 0;
    MeshType type = MeshType::Static;
    bool isVisible = true;
    bool isInstanced = false; // NEW FLAG
};

struct PhysicsComponent {
    glm::vec3 velocity = glm::vec3(0.0f);
    float gravity = -9.81f * 3.0f;
    float jumpForce = 35.0f;
    bool isGrounded = false;
};

struct PlayerComponent {
    float movementSpeed = 40.0f;
    float playerHeight = 6.0f;
    float maxSlopeAngle = 45.0f;
    float minSlopeDot = 0.7071f;
};

// Component Storage Interfaces
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void RemoveEntityData(Entity entity) = 0;
};

template <typename T>
class ComponentPool : public IComponentPool {
private:
    std::vector<T> m_components;
    std::vector<Entity> m_denseToEntity;
    std::vector<size_t> m_entityToDense;

public:
    void Add(Entity entity, const T& component) {
        if (entity >= m_entityToDense.size()) {
            m_entityToDense.resize(entity + 1, INVALID_INDEX);
        }
        assert(m_entityToDense[entity] == INVALID_INDEX && "Component already attached to this entity!");

        m_entityToDense[entity] = m_components.size();
        m_denseToEntity.push_back(entity);
        m_components.push_back(component);
    }

    void RemoveEntityData(Entity entity) override {
        if (entity >= m_entityToDense.size() || m_entityToDense[entity] == INVALID_INDEX) return;

        size_t indexToRemove = m_entityToDense[entity];
        size_t lastIndex = m_components.size() - 1;

        if (indexToRemove != lastIndex) {
            m_components[indexToRemove] = std::move(m_components[lastIndex]);
            Entity lastEntity = m_denseToEntity[lastIndex];
            m_denseToEntity[indexToRemove] = lastEntity;
            m_entityToDense[lastEntity] = indexToRemove;
        }

        m_components.pop_back();
        m_denseToEntity.pop_back();
        m_entityToDense[entity] = INVALID_INDEX;
    }

    T& Get(Entity entity) {
        assert(entity < m_entityToDense.size() && m_entityToDense[entity] != INVALID_INDEX && "Attempted to fetch missing component!");
        return m_components[m_entityToDense[entity]];
    }

    bool Has(Entity entity) const {
        if (entity >= m_entityToDense.size()) return false;
        return m_entityToDense[entity] != INVALID_INDEX;
    }

    const std::vector<Entity>& GetActiveEntities() const { return m_denseToEntity; }
};

class Registry {
private:
    Entity m_nextEntity = 0;
    std::vector<Entity> m_freeEntities;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;

    template <typename T>
    ComponentPool<T>* GetPool() {
        auto typeIdx = std::type_index(typeid(T));
        if (m_pools.find(typeIdx) == m_pools.end()) {
            m_pools[typeIdx] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>*>(m_pools[typeIdx].get());
    }

public:
    Entity CreateEntity() {
        if (!m_freeEntities.empty()) {
            Entity recycled = m_freeEntities.back();
            m_freeEntities.pop_back();
            return recycled;
        }
        return m_nextEntity++;
    }

    void DestroyEntity(Entity entity) {
        for (auto& pair : m_pools) {
            pair.second->RemoveEntityData(entity);
        }
        m_freeEntities.push_back(entity);
    }

    template <typename T> void AddComponent(Entity entity, const T& component = T()) { GetPool<T>()->Add(entity, component); }
    template <typename T> void RemoveComponent(Entity entity) { GetPool<T>()->RemoveEntityData(entity); }
    template <typename T> T& GetComponent(Entity entity) { return GetPool<T>()->Get(entity); }
    template <typename T> bool HasComponent(Entity entity) { return GetPool<T>()->Has(entity); }
    template <typename T> const std::vector<Entity>& View() { return GetPool<T>()->GetActiveEntities(); }

    // Variadic template filter query declaration
    template <typename... Components> MultiView<Components...> QueryGroup();

    void Clear() {
        m_pools.clear();
        m_freeEntities.clear();
        m_nextEntity = 0;
    }
};

template <typename... Components>
class MultiView {
private:
    std::vector<Entity> m_filteredEntities;

public:
    MultiView(Registry& registry) {
        std::array<const std::vector<Entity>*, sizeof...(Components)> views = {
            &registry.View<Components>()...
        };

        auto smallestViewIt = std::min_element(views.begin(), views.end(),
            [](const std::vector<Entity>* a, const std::vector<Entity>* b) {
                return a->size() < b->size();
            });

        const std::vector<Entity>* smallestPool = *smallestViewIt;

        for (Entity entity : *smallestPool) {
            bool matchesAll = (registry.HasComponent<Components>(entity) && ...);
            if (matchesAll) {
                m_filteredEntities.push_back(entity);
            }
        }
    }

    const std::vector<Entity>& GetEntities() const { return m_filteredEntities; }

    auto begin() { return m_filteredEntities.begin(); }
    auto end() { return m_filteredEntities.end(); }
    auto begin() const { return m_filteredEntities.begin(); }
    auto end() const { return m_filteredEntities.end(); }
};

template <typename... Components>
inline MultiView<Components...> Registry::QueryGroup() {
    return MultiView<Components...>(*this);
}

// IMPLEMENTATION OVERLOAD 1: Standalone version (Fixes C2660 immediately!)
inline void TransformComponent::UpdateMatrix() {
    if (!isDirty) return;

    glm::mat4 identity = glm::mat4(1.0f);
    glm::mat4 translationMap = glm::translate(identity, position);
    glm::mat4 rotationMap = glm::rotate(identity, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    rotationMap = glm::rotate(rotationMap, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    rotationMap = glm::rotate(rotationMap, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 scaleMap = glm::scale(identity, scale);

    modelMatrix = translationMap * rotationMap * scaleMap;
    isDirty = false;
}

// IMPLEMENTATION OVERLOAD 2: Hierarchical version 
inline void TransformComponent::UpdateMatrix(Registry& registry) {
    if (!isDirty && parent == INVALID_ENTITY) return;

    glm::mat4 identity = glm::mat4(1.0f);
    glm::mat4 translationMap = glm::translate(identity, position);
    glm::mat4 rotationMap = glm::rotate(identity, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    rotationMap = glm::rotate(rotationMap, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    rotationMap = glm::rotate(rotationMap, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 scaleMap = glm::scale(identity, scale);
    glm::mat4 localMatrix = translationMap * rotationMap * scaleMap;

    if (parent != INVALID_ENTITY) {
        auto& parentTransform = registry.GetComponent<TransformComponent>(parent);
        parentTransform.UpdateMatrix(registry);
        modelMatrix = parentTransform.modelMatrix * localMatrix;
    }
    else {
        modelMatrix = localMatrix;
    }
    isDirty = false;
}
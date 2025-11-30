#include "Entity.h"
#include "VanK/Core/core.h"

namespace VanK
{
    Entity::Entity(entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene)
    {
    }
}

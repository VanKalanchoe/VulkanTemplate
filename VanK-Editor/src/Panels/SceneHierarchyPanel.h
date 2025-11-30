#pragma once
#include <imgui.h>

#include "VanK/Core/core.h"
#include "VanK/Scene/Scene.h"
#include "VanK/Scene/Entity.h"

namespace VanK
{
    class SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& scene);

        void SetContext(const Ref<Scene>& scene);

        void OnImGuiRender();

        Entity GetSelectedEntity() const { return m_SelectionContext; };
        void SetSelectedEntity(Entity entity);

        ImVec2 left;
        bool leftFocused;
        bool leftHovered;
        bool leftPropFocused;
        bool leftPropHovered;
    private:
        template<typename T>
        void DisplayAddComponentEntry(const std::string& entryName);
        
        void DrawEntityNode(Entity entity);
        void DrawComponents(Entity entity);
    private:
        Ref<Scene> m_Context;
        Entity m_SelectionContext;
    };
}

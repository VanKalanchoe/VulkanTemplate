#pragma once
#include "VanK/Core/Layer.h"
#include "VanK/Renderer/OrthographicCameraController.h"

namespace VanK
{
    class EditorLayer : public VanK::Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() override;
        
        virtual void OnEvent(VanK::Event& event) override;

        virtual void OnUpdate(float ts) override;
        virtual void OnRender() override;

    private:
        bool IsButtonHovered() const;
        
        bool OnMouseButtonPressed(VanK::MouseButtonPressedEvent& event);
    private:
        OrthographicCameraController m_CameraController;
    };
}

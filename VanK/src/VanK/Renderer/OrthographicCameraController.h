#pragma once

#include "OrthographicCamera.h"
#include "VanK/Core/Timestep.h"
#include "VanK/Events/WindowEvents.h"
#include "VanK/Events/InputEvents.h"

namespace VanK
{
    class OrthographicCameraController
    {
    public:
        OrthographicCameraController(float aspectRatio, bool rotation = false);

        void OnUpdate(Timestep ts);
        void OnEvent(Event& e);

        void OnResize(float width, float height);

        OrthographicCamera& GetCamera() { return m_camera; }
        const OrthographicCamera& GetCamera() const { return m_camera; }
        float m_zoomLevel = 1.0f;
    private:
        bool OnMouseScrolled(MouseScrolledEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
    private:
        float m_aspectRatio;
        
        OrthographicCamera m_camera;

        bool m_rotation;
        
        glm::vec3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
        float m_CameraRotation = 0.0f;
        float m_CameraTranslationSpeed = 5.0f, m_CameraRotationSpeed = 1.0f;
        
    }; 
}

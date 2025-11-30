#include "OrthographicCameraController.h"

#include "VanK/Core/Application.h"
#include "VanK/Core/core.h"
#include "VanK/Core/Input.h"
#include "VanK/Debug/Instrumentor.h"

namespace VanK
{
    OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation) :
        m_aspectRatio(aspectRatio),
        m_camera(-m_aspectRatio * m_zoomLevel, m_aspectRatio * m_zoomLevel, -m_zoomLevel, m_zoomLevel),
        m_rotation(rotation)
    {
    }

    void OrthographicCameraController::OnUpdate(Timestep ts)
    {
        VK_PROFILE_FUNCTION();
        
        /*if (!m_BlockEvents) // will change in the future because im not moving camera but the player
        {*/
            if (Input::IsKeyPressed(SDL_SCANCODE_A))
            {
                m_cameraPosition.x -= m_CameraTranslationSpeed * ts;
            }
            else if (Input::IsKeyPressed(SDL_SCANCODE_D))
            {
                m_cameraPosition.x += m_CameraTranslationSpeed * ts;
            }
            if (Input::IsKeyPressed(SDL_SCANCODE_W))
            {
                m_cameraPosition.y += m_CameraTranslationSpeed * ts;
            }
            else if (Input::IsKeyPressed(SDL_SCANCODE_S))
            {
                m_cameraPosition.y -= m_CameraTranslationSpeed * ts;
            }

            if (m_rotation)
            {
                if (Input::IsKeyPressed(SDL_SCANCODE_Q))
                {
                    m_CameraRotation += m_CameraRotationSpeed;
                }
                if (Input::IsKeyPressed(SDL_SCANCODE_E))
                {
                    m_CameraRotation -= m_CameraRotationSpeed;
                }
                m_camera.SetRotation(m_CameraRotation);
            }

            m_camera.SetPosition(m_cameraPosition);

            m_CameraTranslationSpeed = m_zoomLevel;
        /*}*/
    }

    void OrthographicCameraController::OnEvent(Event& e)
    {
        VK_PROFILE_FUNCTION();
        
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>(VanK_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
        dispatcher.Dispatch<WindowResizeEvent>(VanK_BIND_EVENT_FN(OrthographicCameraController::OnWindowResize));
    }

    void OrthographicCameraController::OnResize(float width, float height)
    {
        m_aspectRatio = width / height;
        m_camera.SetProjection(-m_aspectRatio * m_zoomLevel, m_aspectRatio * m_zoomLevel, -m_zoomLevel, m_zoomLevel);
    }

    bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
    {
        VK_PROFILE_FUNCTION();
        
        m_zoomLevel -= e.GetYOffset() * 0.25f;
        m_zoomLevel = std::max(m_zoomLevel, 0.25f);
        m_camera.SetProjection(-m_aspectRatio * m_zoomLevel, m_aspectRatio * m_zoomLevel, -m_zoomLevel, m_zoomLevel);

        return false;
    }

    bool OrthographicCameraController::OnWindowResize(WindowResizeEvent& e)
    {
        VK_PROFILE_FUNCTION();
        
        OnResize(e.GetWidth(), e.GetHeight());
        return false;
    }
}

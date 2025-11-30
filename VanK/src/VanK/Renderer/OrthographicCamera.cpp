#include "OrthographicCamera.h"

#include "VanK/Debug/Instrumentor.h"

namespace VanK
{
    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top) :
        m_ProjectionMatrix(glm::ortho(left, right, bottom, top, m_NearPlane, m_FarPlane)), m_ViewMatrix(1.0f)
    {
        VK_PROFILE_FUNCTION();
        
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

    void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)
    {
        VK_PROFILE_FUNCTION();
        
        m_ProjectionMatrix = glm::ortho(left, right, bottom, top, m_NearPlane, m_FarPlane);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

    void OrthographicCamera::RecalculateViewMatrix()
    {
        VK_PROFILE_FUNCTION();
        
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));

        m_ViewMatrix = glm::inverse(transform);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }
}

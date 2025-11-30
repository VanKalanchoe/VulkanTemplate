#include "SceneCamera.h"

#include "VanK/Core/Log.h"

namespace VanK
{
  
    SceneCamera::SceneCamera()
    {
        RecalculateProjection();
    }

    void VanK::SceneCamera::SetOrthographic(float size, float nearClip, float farClip)
    {
        m_ProjectionType = ProjectionType::Orthographic;
        m_OrthographicSize = size;
        m_OrthographicNear = nearClip;
        m_OrthographicFar = farClip;

        RecalculateProjection();
    }

    void SceneCamera::SetPerspective(float verticalFOV, float nearClip, float farClip)
    {
        m_ProjectionType = ProjectionType::Perspective;
        m_PerspectiveFOV = verticalFOV;
        m_PerspectiveNear = nearClip;
        m_PerspectiveFar = farClip;
        RecalculateProjection();
    }

    void SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
    {
        /*VK_CORE_ASSERT(width < 0 && height < 0, "SceneCamera::SetViewportSize");*/
        m_AspectRatio = (float)width / (float)height;
        
        RecalculateProjection();
    }

    void SceneCamera::RecalculateProjection()
    {
        if (m_ProjectionType == ProjectionType::Perspective)
        {
            m_Projection = glm::perspective(m_PerspectiveFOV, m_AspectRatio, m_PerspectiveNear, m_PerspectiveFar);
            /*m_Projection[1][1] *= -1; // Flip Y for Vulkan//added after vulkan myself*/
        } else 
        {
            float orthoLeft = -m_OrthographicSize * m_AspectRatio * 0.5f;
            float orthoRight = m_OrthographicSize * m_AspectRatio * 0.5f;
            float orthoBottom = -m_OrthographicSize  * 0.5f;
            float orthoTop = m_OrthographicSize  * 0.5f;
        
            m_Projection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, m_OrthographicNear, m_OrthographicFar);
            /*m_Projection[1][1] *= -1; // Flip Y for Vulkan//added after vulkan myself*/
        }
    }
}



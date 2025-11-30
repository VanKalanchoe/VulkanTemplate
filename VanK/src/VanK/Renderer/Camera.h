#pragma once

#include <glm/glm.hpp>

namespace VanK
{
    class Camera
    {
    public:
        Camera() = default;
        Camera(const glm::mat4& projection) : m_Projection(projection) {}

        virtual ~Camera() = default;

        const glm::mat4& GetProjection() const { return m_Projection; }

        glm::mat4 GetProjectionForImGuizmo() const {
            glm::mat4 p = GetProjection();
            p[1][1] *= -1.0f;
            return p;
        }

    protected:
        glm::mat4 m_Projection = glm::mat4(1.0f);
    };
}
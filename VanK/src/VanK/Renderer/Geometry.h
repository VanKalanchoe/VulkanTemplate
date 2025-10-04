#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/constants.hpp"
#include "RenderCommand.h"

namespace VanK
{
    namespace shaderio
    {
        using namespace glm;
        #include "shaderIO.h"
    }
    
    class Geometry
    {
    private:
        inline static uint32_t CurrentVertexOffset = 0;
        inline static uint32_t CurrentIndexOffset  = 0;
        inline static uint32_t CurrentStorageOffset = 0;
    public:
        static void AppendGeometry(const std::string& name, const std::vector<shaderio::InstancedVertexData>& vertices, std::vector<uint32_t> indices);
        static void AppendGeometryData(VanKCommandBuffer cmd, const std::string& name, const std::vector<shaderio::InstancedStorageData>& data);
    };

    namespace GeometryData
    {
        // Cube
        inline static std::vector<shaderio::InstancedVertexData> cubeVertices =
        {
            // Front face (Z+)
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},

            // Back face (Z-)
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},

            // Left face (X-)
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},

            // Right face (X+)
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},

            // Top face (Y+)
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},

            // Bottom face (Y-)
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
        };

        // Skybox (4 verts per face, placeholder attributes)
        inline static std::vector<shaderio::InstancedVertexData> skyboxVertices =
        {
            // Front face (Z = -1)
            {{-1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},

            // Back face (Z = 1)
            {{ 1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},

            // Left face (X = -1)
            {{-1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},

            // Right face (X = 1)
            {{ 1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},

            // Top face (Y = 1)
            {{-1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f,  1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},

            // Bottom face (Y = -1)
            {{-1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f, -1.0f,  1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{ 1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
            {{-1.0f, -1.0f, -1.0f}, {0,0,0}, {0,0}, {0,0,0}, {0,0,0}},
        };

        inline static std::vector<uint32_t> cubeIndices = {
            0, 1, 2,  2, 3, 0,        // Front
            4, 5, 6,  6, 7, 4,        // Back
            8, 9,10, 10,11, 8,        // Left
           12,13,14, 14,15,12,        // Right
           16,17,18, 18,19,16,        // Top
           20,21,22, 22,23,20         // Bottom
        };

        inline void GenerateSphere(float radius, uint32_t latSegments, uint32_t longSegments,
                                   std::vector<shaderio::InstancedVertexData>& vertices, std::vector<uint32_t>& indices)
        {
            vertices.clear();
            indices.clear();

            for (uint32_t y = 0; y <= latSegments; ++y)
            {
                float v = (float)y / (float)latSegments;
                float phi = v * glm::pi<float>(); // latitude angle 0 -> pi

                for (uint32_t x = 0; x <= longSegments; ++x)
                {
                    float u = (float)x / (float)longSegments;
                    float theta = u * glm::two_pi<float>(); // longitude angle 0 -> 2pi

                    glm::vec3 pos;
                    pos.x = radius * sin(phi) * cos(theta);
                    pos.y = radius * cos(phi);
                    pos.z = radius * sin(phi) * sin(theta);

                    glm::vec3 normal = glm::normalize(pos);
                    glm::vec2 texCoord = { u, 1.0f - v }; // flip V for Vulkan

                    vertices.push_back({ pos, normal, texCoord });
                }
            }

            for (uint32_t y = 0; y < latSegments; ++y)
            {
                for (uint32_t x = 0; x < longSegments; ++x)
                {
                    uint32_t i0 = y * (longSegments + 1) + x;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + (longSegments + 1);
                    uint32_t i3 = i2 + 1;

                    // First triangle (CCW)
                    indices.push_back(i0);
                    indices.push_back(i2);
                    indices.push_back(i1);

                    // Second triangle (CCW)
                    indices.push_back(i1);
                    indices.push_back(i2);
                    indices.push_back(i3);
                }
            }

            for (size_t i = 0; i < indices.size(); i += 3)
            {
                uint32_t i0 = indices[i + 0];
                uint32_t i1 = indices[i + 1];
                uint32_t i2 = indices[i + 2];

                glm::vec3& pos0 = vertices[i0].position;
                glm::vec3& pos1 = vertices[i1].position;
                glm::vec3& pos2 = vertices[i2].position;

                glm::vec2& uv0 = vertices[i0].texcoords;
                glm::vec2& uv1 = vertices[i1].texcoords;
                glm::vec2& uv2 = vertices[i2].texcoords;

                glm::vec3 edge1 = pos1 - pos0;
                glm::vec3 edge2 = pos2 - pos0;
                glm::vec2 deltaUV1 = uv1 - uv0;
                glm::vec2 deltaUV2 = uv2 - uv0;

                float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

                glm::vec3 tangent;
                tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
                tangent = glm::normalize(tangent);

                // Accumulate tangent per vertex (for averaging shared vertices)
                vertices[i0].tangent += tangent;
                vertices[i1].tangent += tangent;
                vertices[i2].tangent += tangent;
            }

            // Normalize all tangents
            for (auto& v : vertices)
            {
                v.tangent = glm::normalize(v.tangent);
                v.bitangent = glm::normalize(glm::cross(v.normals, v.tangent)); // optional
            }

        }
    }
}

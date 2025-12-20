#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/constants.hpp"

namespace VanK
{
    namespace shaderio
    {
        using namespace glm;
        #include "shaderIO.h"
    }
    
    struct MeshHandle
    {
        uint32_t pipelineType;
        uint32_t meshHandle;
        MeshHandle() = default;
        MeshHandle(shaderio::PipelineType pipeline, uint32_t handle) : pipelineType(static_cast<uint32_t>(pipeline)), meshHandle(handle) {} 
        
        bool operator==(const MeshHandle& other) const
        {
            return pipelineType == other.pipelineType && meshHandle == other.meshHandle;
        }
    };
    
    class RegistryMesh
    {
    public:
        static void DebugPrintPipelineInstances(shaderio::PipelineType pipelineType)
        {
            auto& meshes = meshInfos[pipelineType];

            std::cout << "Pipeline " << pipelineType << " MeshInfo:\n";
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const auto& info = meshes[i];
                std::cout << std::dec << "Mesh " << i
                          << " | instanceCount = " << info.instanceCount
                          << " | firstInstance = " << info.firstInstance
                          << " | firstIndex = " << info.firstIndex
                          << " | vertexOffset = " << info.vertexOffset
                          << "\n";
            }
        }
        static MeshHandle registerMesh(shaderio::PipelineType pipelineType, const std::vector<shaderio::InstancedVertexData>& vertices, const std::vector<uint32_t>& indices);
        template<typename InstanceType>
        static void registerInstance(shaderio::PipelineType pipelineType, MeshHandle meshHandle, InstanceType& instance);
        
    public:
        static bool hasDraws() { return hasAnyDraws; }
        static std::vector<shaderio::InstancedVertexData>& getVertices() { return globalVertices; }
        static std::vector<uint32_t>& getIndices() { return globalIndices; }
        static std::vector<shaderio::MeshInfo>& getMeshInfo(shaderio::PipelineType pipelineType) { return meshInfos[pipelineType]; }
        template<typename InstanceType>
        static std::vector<InstanceType> getInstances(shaderio::PipelineType pipelineType)
        {
            std::vector<InstanceType> allInstances;

            auto& meshes = meshInfos[pipelineType];

            for (uint32_t meshID = 0; meshID < meshes.size(); ++meshID)
            {
                MeshHandle handle{ pipelineType, meshID };

                auto it = meshInstances<InstanceType>.find(handle);
                if (it != meshInstances<InstanceType>.end())
                {
                    auto& instances = it->second;
                    allInstances.insert(allInstances.end(), instances.begin(), instances.end());
                }
            }

            return allInstances;
        }
    private:
        inline static bool hasAnyDraws = false;
        inline static std::unordered_map<uint32_t, std::vector<shaderio::MeshInfo>> meshInfos;
        struct MeshHandleHash 
        {
            size_t operator()(const MeshHandle& h) const 
            {
                return std::hash<uint32_t>()(h.pipelineType) ^ (std::hash<uint32_t>()(h.meshHandle) << 1);
            }
        };
        template<typename InstanceType>
        inline static std::unordered_map<MeshHandle, std::vector<InstanceType>, MeshHandleHash> meshInstances;
        inline static std::vector<shaderio::InstancedVertexData> globalVertices;
        inline static std::vector<uint32_t> globalIndices;
    };

    template <typename InstanceType>
    void RegistryMesh::registerInstance(shaderio::PipelineType pipelineType, MeshHandle meshHandle, InstanceType& instance)
    {
        meshInstances<InstanceType>[meshHandle].push_back(instance);
        
        uint32_t meshID = meshHandle.meshHandle;
        auto& meshes = meshInfos[pipelineType];
        meshes[meshID].instanceCount = static_cast<uint32_t>(meshInstances<InstanceType>[meshHandle].size());
        
        if (meshID == 0)
            meshes[0].firstInstance = 0;
        else
            meshes[meshID].firstInstance = meshes[meshID - 1].firstInstance + meshes[meshID - 1].instanceCount;
        
        uint32_t cumulative = meshes[meshID].firstInstance + meshes[meshID].instanceCount;
        for (size_t i = meshID + 1; i < meshes.size(); ++i)
        {
            meshes[i].firstInstance = cumulative;
            cumulative += meshes[i].instanceCount;
        }
        
        hasAnyDraws = true;
    }

    namespace GeometryData
    {
        // Line mesh
        inline static std::vector<shaderio::InstancedVertexData> lineVertices =
        {
            {{0.0f, 0.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}},
        };

        inline static std::vector<uint32_t> lineIndices =
        {
            0, 1
        };
        
        // Quad
        inline static std::vector<shaderio::InstancedVertexData> quadVertices = 
        {
            {{-0.5f, -0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f, -0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{ 0.5f,  0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
            {{-0.5f,  0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}},
        };
        
        inline static std::vector<uint32_t> quadIndices = 
        {
            0, 1, 2, 
            2, 3, 0,
        };
        
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

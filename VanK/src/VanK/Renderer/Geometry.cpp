/*
#include "Geometry.h"

#include "Renderer.h"
#include "VanK/Core/Log.h"

namespace VanK
{std::unordered_map<std::string, std::vector<VanK::shaderio::InstancedPBRData>> VanK::Geometry::s_PBRMeshData{};
    std::vector<shaderio::MeshInfo> Geometry::s_MeshCache{};
    std::vector<CpuMeshInfo> Geometry::s_MeshInfos{};
    std::vector<shaderio::InstancedVertexData> Geometry::s_Vertices{};
    std::vector<uint32_t> Geometry::s_Indices{};
    uint32_t Geometry::s_TotalInstances = 0;
    std::vector<shaderio::InstancedPBRData> Geometry::s_PBRData{};
    std::vector<shaderio::InstancedQuadData> Geometry::s_QuadData{};
    std::vector<shaderio::InstancedCircleData> Geometry::s_CircleData{};
    std::vector<shaderio::InstancedTextData> Geometry::s_TextData{};
    std::vector<shaderio::InstancedLineData> Geometry::s_LineData{};
    
    void Geometry::UpdateGpuMeshCache()
    {
        if (s_MeshCache.size() != s_MeshInfos.size())
            s_MeshCache.resize(s_MeshInfos.size());

        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            s_MeshCache[i] = s_MeshInfos[i].gpu;
        }
    }
    
    void Geometry::AppendGeometry(
    const std::string& name,
    const std::vector<shaderio::InstancedVertexData>& vertices,
    const std::vector<uint32_t>& indices,
    shaderio::PipelineType pipelineType)
    {
        shaderio::MeshInfo gpuInfo{};
        gpuInfo.indexCount = static_cast<uint32_t>(indices.size());
        gpuInfo.instanceCount = 0;
        gpuInfo.firstInstance = 0;
        gpuInfo.pipelineType = pipelineType;

        // Each mesh gets its own vertexOffset and firstIndex
        gpuInfo.vertexOffset = static_cast<uint32_t>(s_Vertices.size());
        gpuInfo.firstIndex  = static_cast<uint32_t>(s_Indices.size());

        // Append vertices for this mesh
        s_Vertices.insert(s_Vertices.end(), vertices.begin(), vertices.end());

        // Append indices with proper offset
        s_Indices.reserve(s_Indices.size() + indices.size());
        if (pipelineType == shaderio::PipelineType_Line) {
            s_Indices.insert(s_Indices.end(), indices.begin(), indices.end());
        } else {
            for (uint32_t i : indices)
                s_Indices.push_back(i + gpuInfo.vertexOffset);
        }

        // Add to CPU mesh list
        s_MeshInfos.push_back(CpuMeshInfo{name, gpuInfo, pipelineType});
    }



    void Geometry::SetPBRFrameInstances(
     const std::string& name,
     const std::vector<shaderio::InstancedPBRData>& instances)
    {
        bool updated = false;
        for (auto& mesh : s_MeshInfos)
        {
            if (mesh.name == name && mesh.pipelineType == shaderio::PipelineType_PBR)
            {
                uint32_t& firstInstance = mesh.gpu.firstInstance;
                uint32_t oldCount = mesh.gpu.instanceCount;

                if (oldCount == 0)
                {
                    firstInstance = static_cast<uint32_t>(s_PBRData.size());
                    s_PBRData.insert(s_PBRData.end(), instances.begin(), instances.end());
                }
                else
                {
                    if (s_PBRData.size() < firstInstance + instances.size())
                        s_PBRData.resize(firstInstance + instances.size());

                    std::copy(instances.begin(), instances.end(), s_PBRData.begin() + firstInstance);
                }

                mesh.gpu.instanceCount = static_cast<uint32_t>(instances.size());
                s_TotalInstances += instances.size() - oldCount;
                updated = true;
                // continue looping to update other meshes with the same name if needed
            }
        }

        if (!updated)
        {
            // fallback for meshes not yet appended
            s_PBRMeshData[name] = instances;
        }
    }




    
    void Geometry::SetQuadFrameInstances
    (
        const std::string& name,
        const std::vector<shaderio::InstancedQuadData>& instances
    )
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                uint32_t& firstInstance = s_MeshInfos[i].gpu.firstInstance;
                uint32_t oldCount = s_MeshInfos[i].gpu.instanceCount;

                // If this is the first time setting instances for this mesh
                if (oldCount == 0)
                {
                    // Allocate new instance region at the end of s_StorageData
                    firstInstance = s_QuadData.size();

                    // Grow storage
                    s_QuadData.resize(firstInstance + instances.size());

                    // New global instance count
                 
                    s_TotalInstances += instances.size(); 
                }
                else
                {
                    // Normal behavior: replace existing region
                    if (s_QuadData.size() < firstInstance + instances.size())
                        s_QuadData.resize(firstInstance + instances.size());

                    int32_t sizeDiff = instances.size() - oldCount;
                    s_TotalInstances += sizeDiff;
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_QuadData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
               
                return;
            }
        }
    }
    
    void Geometry::SetCircleFrameInstances
    (
        const std::string& name,
        const std::vector<shaderio::InstancedCircleData>& instances
    )
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                uint32_t& firstInstance = s_MeshInfos[i].gpu.firstInstance;
                uint32_t oldCount = s_MeshInfos[i].gpu.instanceCount;

                // If this is the first time setting instances for this mesh
                if (oldCount == 0)
                {
                    // Allocate new instance region at the end of s_CircleData
                    firstInstance = s_CircleData.size();

                    // Grow storage
                    s_CircleData.resize(firstInstance + instances.size());

                    // New global instance count
                    
                    s_TotalInstances += instances.size(); 
                }
                else
                {
                    // Normal behavior: replace existing region
                    if (s_CircleData.size() < firstInstance + instances.size())
                        s_CircleData.resize(firstInstance + instances.size());
                    
                    int32_t sizeDiff = instances.size() - oldCount;
                    s_TotalInstances += sizeDiff;
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_CircleData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
                
                return;
            }
        }
    }
    
    void Geometry::SetTextFrameInstances
    (
        const std::string& name,
        const std::vector<shaderio::InstancedTextData>& instances
    )
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                uint32_t& firstInstance = s_MeshInfos[i].gpu.firstInstance;
                uint32_t oldCount = s_MeshInfos[i].gpu.instanceCount;

                // If this is the first time setting instances for this mesh
                if (oldCount == 0)
                {
                    // Allocate new instance region at the end of s_CircleData
                    firstInstance = s_TextData.size();

                    // Grow storage
                    s_TextData.resize(firstInstance + instances.size());

                    // New global instance count
                    
                    s_TotalInstances += instances.size(); 
                }
                else
                {
                    // Normal behavior: replace existing region
                    if (s_TextData.size() < firstInstance + instances.size())
                        s_TextData.resize(firstInstance + instances.size());
                    
                    int32_t sizeDiff = instances.size() - oldCount;
                    s_TotalInstances += sizeDiff;
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_TextData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
                
                return;
            }
        }
    }
    
    void Geometry::SetLineFrameInstances
    (
        const std::string& name,
        const std::vector<shaderio::InstancedLineData>& instances
    )
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                uint32_t& firstInstance = s_MeshInfos[i].gpu.firstInstance;
                uint32_t oldCount = s_MeshInfos[i].gpu.instanceCount;

                // If this is the first time setting instances for this mesh
                if (oldCount == 0)
                {
                    // Allocate new instance region at the end of s_CircleData
                    firstInstance = s_LineData.size();

                    // Grow storage
                    s_LineData.resize(firstInstance + instances.size());

                    // New global instance count
                    
                    s_TotalInstances += instances.size(); 
                }
                else
                {
                    // Normal behavior: replace existing region
                    if (s_LineData.size() < firstInstance + instances.size())
                        s_LineData.resize(firstInstance + instances.size());
                    
                    int32_t sizeDiff = instances.size() - oldCount;
                    s_TotalInstances += sizeDiff;
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_LineData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
                
                return;
            }
        }
    }
    
    void Geometry::BeginFrame()
    {
        s_PBRData.clear();
        s_QuadData.clear();
        s_CircleData.clear();
        s_TextData.clear();
        s_LineData.clear();

        s_TotalInstances = 0;

        for (auto& mesh : s_MeshInfos)
            mesh.gpu.instanceCount = 0;
    }
}
*/

#include "Geometry.h"

#include "Renderer.h"
#include "VanK/Core/Log.h"

namespace VanK
{
    std::vector<CpuMeshInfo> Geometry::s_MeshInfos{};
    std::vector<shaderio::InstancedVertexData> Geometry::s_Vertices{};
    std::vector<uint32_t> Geometry::s_Indices{};
    uint32_t Geometry::s_TotalInstances = 0;
    std::vector<shaderio::InstancedStorageData> Geometry::s_StorageData{};
    std::vector<shaderio::InstancedCircleData> Geometry::s_CircleData{};
    std::vector<shaderio::InstancedTextData> Geometry::s_TextData{};
    
    void Geometry::AppendGeometry
    (
        const std::string& name,
        const std::vector<shaderio::InstancedVertexData>& vertices,
        const std::vector<uint32_t>& indices,
        CpuMeshInfo::PipelineType pipelineType
    )
    {
        // 1. Fill the GPU Info
        shaderio::MeshInfo gpuInfo;
        gpuInfo.indexCount = static_cast<uint32_t>(indices.size());
        gpuInfo.instanceCount = 0;
        gpuInfo.firstIndex = s_Indices.size();
        gpuInfo.vertexOffset = s_Vertices.size();
        gpuInfo.firstInstance = (pipelineType == CpuMeshInfo::PipelineType::Quad) ? s_StorageData.size() : (pipelineType == CpuMeshInfo::PipelineType::Circle) ? s_CircleData.size() : s_TextData.size();
        
        // 2. Append the Data
        s_Vertices.insert(s_Vertices.end(), vertices.begin(), vertices.end());
        s_Indices.insert(s_Indices.end(), indices.begin(), indices.end());

        // 3. Find insertion point by pipeline type (using std::find_if)
        auto insertIt = std::ranges::find_if(s_MeshInfos,
                                             [&](const CpuMeshInfo& info) {
                                                 return info.pipelineType > pipelineType;
                                             });

        // 4. Insert the new mesh in sorted order
        insertIt = s_MeshInfos.insert(insertIt, CpuMeshInfo{name, gpuInfo, pipelineType});

        // 5. Shift firstInstance of subsequent meshes
        for (auto shiftIt = insertIt + 1; shiftIt != s_MeshInfos.end(); ++shiftIt)
        {
            shiftIt->gpu.firstInstance += gpuInfo.instanceCount;
        }
    }
    
    void Geometry::SetFrameInstances
    (
        const std::string& name,
        const std::vector<shaderio::InstancedStorageData>& instances
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
                    firstInstance = s_StorageData.size();

                    // Grow storage
                    s_StorageData.resize(firstInstance + instances.size());

                    // New global instance count
                 
                    s_TotalInstances += instances.size(); 
                }
                else
                {
                    // Normal behavior: replace existing region
                    if (s_StorageData.size() < firstInstance + instances.size())
                        s_StorageData.resize(firstInstance + instances.size());

                    int32_t sizeDiff = instances.size() - oldCount;
                    s_TotalInstances += sizeDiff;
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_StorageData.begin() + firstInstance);

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
}

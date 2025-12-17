#include "Geometry.h"

#include "Renderer.h"
#include "VanK/Core/Log.h"

namespace VanK
{
    std::vector<shaderio::MeshInfo> Geometry::s_MeshCache{};
    std::vector<CpuMeshInfo> Geometry::s_MeshInfos{};
    std::vector<shaderio::InstancedVertexData> Geometry::s_Vertices{};
    std::vector<uint32_t> Geometry::s_Indices{};
    uint32_t Geometry::s_TotalInstances = 0;
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
    
    void Geometry::AppendGeometry
    (
        const std::string& name,
        const std::vector<shaderio::InstancedVertexData>& vertices,
        const std::vector<uint32_t>& indices,
        shaderio::PipelineType pipelineType
    )
    {
        // 1. Fill the GPU Info
        shaderio::MeshInfo gpuInfo;
        gpuInfo.indexCount = static_cast<uint32_t>(indices.size());
        gpuInfo.instanceCount = 0;
        gpuInfo.firstIndex = s_Indices.size();
        gpuInfo.vertexOffset = s_Vertices.size();
        gpuInfo.firstInstance = (pipelineType == shaderio::PipelineType_Quad) ? s_QuadData.size() : 
                                (pipelineType == shaderio::PipelineType_Circle) ? s_CircleData.size() : 
                                (pipelineType == shaderio::PipelineType_Text) ? s_TextData.size() : s_LineData.size();
        gpuInfo.pipelineType = pipelineType;
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
        s_QuadData.clear();
        s_CircleData.clear();
        s_TextData.clear();
        s_LineData.clear();

        s_TotalInstances = 0;

        for (auto& mesh : s_MeshInfos)
            mesh.gpu.instanceCount = 0;
    }
}

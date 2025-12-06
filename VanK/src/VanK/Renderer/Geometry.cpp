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
    void Geometry::AppendGeometry
    (
        const std::string& name,
        const std::vector<shaderio::InstancedVertexData>& vertices,
        const std::vector<uint32_t>& indices)
    {
        // 1. Fill the GPU Info
        shaderio::MeshInfo gpuInfo;
        gpuInfo.indexCount = static_cast<uint32_t>(indices.size());
        gpuInfo.instanceCount = 0;
        gpuInfo.firstIndex = s_Indices.size();
        gpuInfo.vertexOffset = s_Vertices.size();
        gpuInfo.firstInstance = s_TotalInstances;
        
        // 2. Append the Data
        s_Vertices.insert(s_Vertices.end(), vertices.begin(), vertices.end());
        s_Indices.insert(s_Indices.end(), indices.begin(), indices.end());
        
        // 3. Update Global State
        s_MeshInfos.emplace_back(name, gpuInfo);
    }
    
    void Geometry::AppendGeometryData(const std::string& name, const std::vector<shaderio::InstancedStorageData>& data)
    {
        // 1. Append the new instance data
        // This assumes AppendGeometryData is called only once per mesh.
        s_StorageData.insert(s_StorageData.end(), data.begin(), data.end());

        uint32_t newInstanceCount = static_cast<uint32_t>(data.size());

        // 2. Find the mesh and update its count
        for (size_t i = 0; i < s_MeshInfos.size(); ++i) 
        {
            if (s_MeshInfos[i].name == name) 
            {
                // The old instance count is 0 (set in AppendGeometry).
                uint32_t oldInstanceCount = s_MeshInfos[i].gpu.instanceCount; // Should be 0

                // The difference to add to the total and subsequent offsets.
                // This is simply newInstanceCount, as oldInstanceCount is 0.
                uint32_t instanceCountDifference = newInstanceCount - oldInstanceCount; 
            
                s_MeshInfos[i].gpu.instanceCount = newInstanceCount;

                // ⭐ 3. Back-Patch the firstInstance offset for all subsequent meshes
                if (instanceCountDifference > 0)
                {
                    // Iterate through all meshes that were appended *after* the current mesh
                    for (size_t j = i + 1; j < s_MeshInfos.size(); ++j)
                    {
                        // Add the delta instance count (which is the new count)
                        s_MeshInfos[j].gpu.firstInstance += instanceCountDifference;
                    }
                
                    // Update s_TotalInstances by the difference (which is the new count)
                    s_TotalInstances += instanceCountDifference;
                }
                return;
            }
        }
    
        VK_CORE_ERROR("Geometry::AppendGeometryData: Could not find mesh with name '%s'", name.c_str());
    }
}

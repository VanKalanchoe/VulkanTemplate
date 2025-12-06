#include "Geometry.h"

#include "Renderer.h"

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
        s_TotalInstances++;
    }
    
    void Geometry::AppendGeometryData(const std::string& name, const std::vector<shaderio::InstancedStorageData>& data)
    {
        // 1. Append the new instance data to the global storage vector
        // This is correct: all instances are stored sequentially.
        s_StorageData.insert(s_StorageData.end(), data.begin(), data.end());

        // 2. Find the corresponding mesh and SET its instance count
        bool found = false;
        for (auto& cpuMesh : s_MeshInfos) 
        {
            if (cpuMesh.name == name) 
            {
                // ⭐ CRITICAL CHANGE: Set the instanceCount to the new total size
                // Since you set the initial count to 0 in AppendGeometry, we just set the size.
                cpuMesh.gpu.instanceCount = static_cast<uint32_t>(data.size());
                found = true;
                break; 
            }
        }
    
        if (!found) 
        {
            // Log error if mesh not found
        }

        // 3. Update the total instance count globally
        // We must recalculate the total count based on all meshes' final instance counts.
        s_TotalInstances = 0;
        for (const auto& mesh : s_MeshInfos)
            s_TotalInstances += mesh.gpu.instanceCount;
    }
}

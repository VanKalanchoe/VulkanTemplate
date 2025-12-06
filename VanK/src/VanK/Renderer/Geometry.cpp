#include "Geometry.h"

#include "Renderer.h"

namespace VanK
{
    std::vector<CpuMeshInfo> Geometry::s_MeshInfos{};
    std::vector<shaderio::InstancedVertexData> Geometry::s_Vertices{};
    std::vector<uint32_t> Geometry::s_Indices{};
    uint32_t Geometry::s_TotalInstances = 0;
 
    void Geometry::AppendGeometry
    (
        const std::string& name,
        const std::vector<shaderio::InstancedVertexData>& vertices,
        const std::vector<uint32_t>& indices)
    {
        // 1. Fill the GPU Info
        shaderio::MeshInfo gpuInfo;
        gpuInfo.indexCount = static_cast<uint32_t>(indices.size());
        gpuInfo.instanceCount = 1;
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
    
    void Geometry::AppendGeometryData(VanKCommandBuffer cmd, const std::string& name, const std::vector<shaderio::InstancedStorageData>& data)
    {
       
    }
}

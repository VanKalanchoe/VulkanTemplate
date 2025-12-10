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
    bool Geometry::s_VerticesChanged = false;
    bool Geometry::s_IndicesChanged = false;
    bool Geometry::s_StorageChanged = false;
    bool Geometry::s_MeshesChanged = false;
    bool Geometry::s_CirclesChanged = false;
    
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
        gpuInfo.firstInstance = s_TotalInstances;
        
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

        // 6. Set changed flags
        s_VerticesChanged = true;
        s_IndicesChanged = true;
        s_MeshesChanged = true;
    }

    
    void Geometry::RemoveGeometry(const std::string& name)
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                const shaderio::MeshInfo& infoToRemove = s_MeshInfos[i].gpu;
                
                // NOTE: Instance data MUST be removed first.
                if (infoToRemove.instanceCount > 0)
                {
                    RemoveGeometryData(name);
                }

                // --- 1. Calculate sizes and offsets to remove ---
                
                // Indices: This count is stored directly in the mesh info and is correct.
                uint32_t indexCountToRemove = infoToRemove.indexCount;
                uint32_t firstIndex = infoToRemove.firstIndex;

                // Vertices: Must calculate the count by finding the offset of the next mesh.
                uint32_t currentVertexOffset = infoToRemove.vertexOffset;
                uint32_t nextVertexOffset = static_cast<uint32_t>(s_Vertices.size()); 
                
                // Check if there is a mesh after this one
                if (i + 1 < s_MeshInfos.size()) {
                    nextVertexOffset = s_MeshInfos[i+1].gpu.vertexOffset;
                }
                
                uint32_t vertexCountToRemove = nextVertexOffset - currentVertexOffset;

                // 2. Remove data from the global buffers
                s_Vertices.erase(
                    s_Vertices.begin() + currentVertexOffset,
                    s_Vertices.begin() + currentVertexOffset + vertexCountToRemove
                );
                
                s_Indices.erase(
                    s_Indices.begin() + firstIndex,
                    s_Indices.begin() + firstIndex + indexCountToRemove
                );

                // 3. Remove the mesh info structure
                // We do this BEFORE the back-patch loop, so the indices for j are correct.
                s_MeshInfos.erase(s_MeshInfos.begin() + i);
                
                // 4. Back-Patch subsequent meshes
                // The loop starts at index i, which is the index of the mesh *after* the one we just removed.
                for (size_t j = i; j < s_MeshInfos.size(); ++j)
                {
                    // Adjust vertex offsets
                    s_MeshInfos[j].gpu.vertexOffset -= vertexCountToRemove;
                    
                    // Adjust index offsets
                    s_MeshInfos[j].gpu.firstIndex -= indexCountToRemove;

                    // NOTE: firstInstance does not need to be adjusted here because it was already 
                    // handled by the required preceding call to RemoveGeometryData.
                }
                
                // 5. Set Dirty Flags
                s_VerticesChanged = true;
                s_IndicesChanged = true;
                s_MeshesChanged = true;

                /*VK_CORE_INFO("Geometry::RemoveGeometry: Removed mesh '%s' (V: %u, I: %u).", name.c_str(), vertexCountToRemove, indexCountToRemove);*/
                return;
            }
        }
        
        /*VK_CORE_WARN("Geometry::RemoveGeometry: Mesh '%s' not found.", name.c_str());*/
    }
    
    void Geometry::AppendGeometryData(const std::string& name, const std::vector<shaderio::InstancedStorageData>& data)
    {
        if (data.empty())
            return;

        // 1. Append the new instance data to global storage
        uint32_t newInstanceCount = static_cast<uint32_t>(data.size());
        s_StorageData.insert(s_StorageData.end(), data.begin(), data.end());

        // 2. Find the mesh and update instance count and offsets
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                // Add to existing count, do NOT overwrite
                uint32_t oldInstanceCount = s_MeshInfos[i].gpu.instanceCount;
                s_MeshInfos[i].gpu.instanceCount += newInstanceCount;

                // Update s_TotalInstances
                s_TotalInstances += newInstanceCount;

                // Back-patch firstInstance of subsequent meshes
                for (size_t j = i + 1; j < s_MeshInfos.size(); ++j)
                {
                    s_MeshInfos[j].gpu.firstInstance += newInstanceCount;
                }

                s_StorageChanged = true;
                return;
            }
        }

        /*VK_CORE_ERROR("Geometry::AppendGeometryData: Could not find mesh '%s'", name.c_str());*/
    }

    void Geometry::RemoveGeometryData(const std::string& name)
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                uint32_t instanceCountToRemove = s_MeshInfos[i].gpu.instanceCount;
            
                if (instanceCountToRemove == 0)
                    return; // Nothing to remove

                // 1. Calculate removal indices for s_StorageData
                uint32_t firstInstanceIndex = s_MeshInfos[i].gpu.firstInstance;
            
                // 2. Remove data from the global instance storage vector
                s_StorageData.erase(
                    s_StorageData.begin() + firstInstanceIndex,
                    s_StorageData.begin() + firstInstanceIndex + instanceCountToRemove
                );

                // 3. Update the mesh's instance count to 0
                s_MeshInfos[i].gpu.instanceCount = 0;
            
                // 4. Update the total instance count
                s_TotalInstances -= instanceCountToRemove;

                // 5. Back-Patch subsequent meshes: Decrement their firstInstance offset
                for (size_t j = i + 1; j < s_MeshInfos.size(); ++j)
                {
                    s_MeshInfos[j].gpu.firstInstance -= instanceCountToRemove;
                }
                
                // 4. Set Changed Flag
                s_StorageChanged = true;

                /*VK_CORE_INFO("Geometry::RemoveGeometryData: Removed %u instances for mesh '%s'.", instanceCountToRemove, name.c_str());*/
                return;
            }
        }
        /*VK_CORE_WARN("Geometry::RemoveGeometryData: Mesh '%s' not found.", name.c_str());*/
    }

    void Geometry::RemoveSingleData(const std::string& name, uint32_t instanceLocalIndex)
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                // 0. Validate the local index
                uint32_t currentCount = s_MeshInfos[i].gpu.instanceCount;
                if (instanceLocalIndex >= currentCount) {
                    /*VK_CORE_WARN("Geometry::RemoveSingleInstance: Index %u out of range for mesh '%s' (Count: %u).", instanceLocalIndex, name.c_str(), currentCount);*/
                    return;
                }

                // 1. Calculate the Global Index (the actual index in s_StorageData)
                uint32_t firstInstanceIndex = s_MeshInfos[i].gpu.firstInstance;
                uint32_t globalIndexToRemove = firstInstanceIndex + instanceLocalIndex;
                
                // 2. Remove the single element from the global instance storage vector
                s_StorageData.erase(s_StorageData.begin() + globalIndexToRemove);

                // 3. Update the mesh's instance count (decrement by 1)
                s_MeshInfos[i].gpu.instanceCount--;
            
                // 4. Update the total instance count (decrement by 1)
                s_TotalInstances--;

                // 5. Back-Patch the firstInstance offset for all subsequent meshes
                // All meshes after the current one must shift their starting point back by 1.
                for (size_t j = i + 1; j < s_MeshInfos.size(); ++j)
                {
                    s_MeshInfos[j].gpu.firstInstance -= 1;
                }
                
                // 6. Set Changed Flag
                s_StorageChanged = true;
                
                // 6. Optional: Update the firstInstance for the current mesh
                // If the removal shifted the elements of this mesh, the firstInstance stays the same.
                // However, the *remaining* instances in this mesh have been shifted, which is handled
                // by the s_StorageData.erase call. No change to firstInstance is needed for the current mesh 'i'.

                /*VK_CORE_INFO("Geometry::RemoveSingleInstance: Removed single instance %u from mesh '%s'.", instanceLocalIndex, name.c_str());*/
                return;
            }
        }
        /*VK_CORE_WARN("Geometry::RemoveSingleInstance: Mesh '%s' not found.", name.c_str());*/
    }
    void Geometry::ClearInstances(const std::string& name)
    {
        for (size_t i = 0; i < s_MeshInfos.size(); ++i)
        {
            if (s_MeshInfos[i].name == name)
            {
                // Reset instance count only
                s_MeshInfos[i].gpu.instanceCount = 0;
                return;
            }
        }
    }
    
    void Geometry::SetFrameInstances(const std::string& name,
                                 const std::vector<shaderio::InstancedStorageData>& instances)
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
                    firstInstance = s_TotalInstances;

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

                    // Adjust global count (if size changed)
                    s_TotalInstances += (instances.size() - oldCount);
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_StorageData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
                s_StorageChanged = true;
                return;
            }
        }
    }
    
    void Geometry::SetCircleFrameInstances(const std::string& name,
                                 const std::vector<shaderio::InstancedCircleData>& instances)
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
                    firstInstance = s_TotalInstances;

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
                    
                    // Adjust global count (if size changed)
                    s_TotalInstances += (instances.size() - oldCount);
                }

                // Copy data
                std::copy(instances.begin(), instances.end(),
                          s_CircleData.begin() + firstInstance);

                // Update mesh count
                s_MeshInfos[i].gpu.instanceCount = (uint32_t)instances.size();
                s_CirclesChanged = true;
                return;
            }
        }
    }
}

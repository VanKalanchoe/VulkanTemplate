#include "RegistryMesh.h"

namespace VanK
{
    MeshHandle RegistryMesh::registerMesh(shaderio::PipelineType pipelineType, const std::vector<shaderio::InstancedVertexData>& vertices, const std::vector<uint32_t>& indices)
    {
        uint32_t vertexOffset = static_cast<uint32_t>(globalVertices.size());
        uint32_t indexOffset = static_cast<uint32_t>(globalIndices.size());
        
        globalVertices.insert(globalVertices.end(), vertices.begin(), vertices.end());
        std::vector<uint32_t> offsetIndices = indices;
        for (auto& idx : offsetIndices)
            idx += vertexOffset;

        globalIndices.insert(globalIndices.end(), offsetIndices.begin(), offsetIndices.end());
        
        shaderio::MeshInfo meshInfo;
        meshInfo.indexCount = static_cast<uint32_t>(indices.size());
        meshInfo.instanceCount = 0;
        meshInfo.firstIndex = indexOffset;
        meshInfo.vertexOffset = 0;
        meshInfo.firstInstance = 0;
        meshInfo.pipelineType = pipelineType;
        
        auto& meshVector = meshInfos[pipelineType];
        meshVector.push_back(meshInfo);
        
        MeshHandle handle;
        handle.pipelineType = pipelineType;
        handle.meshHandle = static_cast<uint32_t>(meshVector.size() - 1);
        
        return handle;
    }
}

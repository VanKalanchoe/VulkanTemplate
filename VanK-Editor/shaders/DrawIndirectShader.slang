#include "shaderIO.h"

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

[[vk::binding(0, 1)]]
ConstantBuffer<SceneInfo, ScalarDataLayout> ubo;

[shader("compute")]
[numthreads(64,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint meshIndex = GlobalInvocationID.x;

    // Only process if meshIndex is valid
    if (meshIndex >= ubo.numMeshes)
        return;

    // Pointer to indirect draw commands buffer
    DrawIndexedIndirectCommand* indirect = (DrawIndexedIndirectCommand*)ubo.indirectAddress;

    // Read mesh info from a structured buffer or uniform (e.g., firstIndex, indexCount, vertexOffset, instanceCount, firstInstance)
    MeshInfo* meshInfo = (MeshInfo*)ubo.meshInfoAddress;
    MeshInfo meshInfoData = meshInfo[meshIndex];

    indirect[meshIndex].indexCount    = meshInfoData.indexCount;
    indirect[meshIndex].instanceCount = meshInfoData.instanceCount;
    indirect[meshIndex].firstIndex    = meshInfoData.firstIndex;
    indirect[meshIndex].vertexOffset  = meshInfoData.vertexOffset;
    indirect[meshIndex].firstInstance = meshInfoData.firstInstance;

    if (meshIndex == 0)
    {
        uint* count = (uint*)ubo.countAddress;
        count[0] = ubo.numMeshes; // total number of draw commands
    }
}
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
    if (meshIndex >= ubo.numMeshes) return;

    MeshInfo* meshInfo = (MeshInfo*)ubo.meshInfoAddress;
    MeshInfo meshInfoData = meshInfo[meshIndex];

   /*  if (meshInfoData.instanceCount == 0)
        return; */

    uint32_t pipelineType = meshInfoData.pipelineType;

    uint32_t* count = (uint32_t*)ubo.countAddresses[pipelineType];
    uint drawIndex;
    InterlockedAdd(count[0], 1, drawIndex);

    DrawIndexedIndirectCommand* indirect = (DrawIndexedIndirectCommand*)ubo.indirectAddresses[pipelineType];

    indirect[drawIndex].indexCount    = meshInfoData.indexCount;
    indirect[drawIndex].instanceCount = meshInfoData.instanceCount;
    indirect[drawIndex].firstIndex    = meshInfoData.firstIndex;
    indirect[drawIndex].vertexOffset  = meshInfoData.vertexOffset;
    indirect[drawIndex].firstInstance = meshInfoData.firstInstance;

}
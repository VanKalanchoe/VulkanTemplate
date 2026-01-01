#include "shaderIO.h"

struct VanKDrawMeshTasksIndirectCommand 
{
    uint    groupCountX;
    uint    groupCountY;
    uint    groupCountZ;
};

struct meshTasksSubmitPushConstant
{
    uint64_t meshTasksIndirectBufferAddress;
    uint64_t localMeshTasksIndirectBufferAddress;
};

[shader("compute")]
[numthreads(64,1,1)]
void compMain
(
    uint3 GlobalInvocationID : SV_DispatchThreadID, 
    uniform meshTasksSubmitPushConstant pushConstant
)
{
    uint idx = GlobalInvocationID.x;

    VanKDrawMeshTasksIndirectCommand* localPtr = (VanKDrawMeshTasksIndirectCommand*)pushConstant.localMeshTasksIndirectBufferAddress;
    VanKDrawMeshTasksIndirectCommand* indirectPtr = (VanKDrawMeshTasksIndirectCommand*)pushConstant.meshTasksIndirectBufferAddress;

    indirectPtr[idx].groupCountX = localPtr[idx].groupCountX;
    indirectPtr[idx].groupCountY = localPtr[idx].groupCountY;
    indirectPtr[idx].groupCountZ = localPtr[idx].groupCountZ;
}

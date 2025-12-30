#include "shaderIO.h"

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

struct VanKDrawMeshTasksIndirectCommand 
{
    uint32_t    groupCountX;
    uint32_t    groupCountY;
    uint32_t    groupCountZ;
};

struct meshTasksSubmitPushConstant
{
    uint64_t meshTasksIndirectBufferAddress;
    uint64_t localMeshTasksIndirectBufferAddress;
};

[shader("compute")]
[numthreads(64,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID, uniform meshTasksSubmitPushConstant pushConstant)
{
    uint idx = GlobalInvocationID.x;

    // Only 1 task in buffer
    if (idx >= 2) return;

    VanKDrawMeshTasksIndirectCommand* localPtr = (VanKDrawMeshTasksIndirectCommand*)pushConstant.localMeshTasksIndirectBufferAddress;
    VanKDrawMeshTasksIndirectCommand* indirectPtr = (VanKDrawMeshTasksIndirectCommand*)pushConstant.meshTasksIndirectBufferAddress;

    indirectPtr[idx] = localPtr[idx]; // safe copy
}

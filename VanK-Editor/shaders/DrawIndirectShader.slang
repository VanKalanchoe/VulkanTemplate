#include "shaderIO.h"

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

[[vk::binding(0, 1)]]
ConstantBuffer<UniformBuffer, ScalarDataLayout> ubo;

[shader("compute")]
[numthreads(1,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint meshIndex = GlobalInvocationID.x;

    // Only process if meshIndex is valid
    if (meshIndex >= ubo.numMeshes)
        return;

    // Pointer to indirect draw commands buffer
    DrawIndexedIndirectCommand* indirect = (DrawIndexedIndirectCommand*)ubo.indirectAddress;

    // Read mesh info from a structured buffer or uniform (e.g., firstIndex, indexCount, vertexOffset, instanceCount, firstInstance)
    MeshInfo mesh = ubo.meshes[meshIndex];

    indirect[meshIndex].indexCount    = mesh.indexCount;
    indirect[meshIndex].instanceCount = mesh.instanceCount;
    indirect[meshIndex].firstIndex    = mesh.firstIndex;
    indirect[meshIndex].vertexOffset  = mesh.vertexOffset;
    indirect[meshIndex].firstInstance = mesh.firstInstance;

    // Optionally write draw count (for DrawIndexedIndirectCount)
    if (meshIndex == 0)
    {
        uint* count = (uint*)ubo.countAddress;
        count[0] = ubo.numMeshes; // total number of draw commands
    }
}
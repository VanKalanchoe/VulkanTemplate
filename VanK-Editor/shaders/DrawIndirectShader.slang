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
    uint index = GlobalInvocationID.x;
    // only one thread (index 0) fills the indirect command
    if (index == 0)
    {
        DrawIndexedIndirectCommand* indirect = (DrawIndexedIndirectCommand*)ubo.indirectBuffer;

        indirect[0].indexCount    = ubo.numindic;   // total number of indices
        indirect[0].instanceCount = 1;              // number of instances
        indirect[0].firstIndex    = 0;              // start of index buffer
        indirect[0].vertexOffset  = 0;              // offset added to vertex indices
        indirect[0].firstInstance = 0;              // first instance ID

        // optionally, write draw count
        uint* count = (uint*)ubo.countBuffer;
        count[0] = 1; // one draw command
    }
}
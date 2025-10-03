struct InstancedVertexData
{
    float3 inPosition;
    float4 inColor;
    float2 inTexCoord;
};

struct UniformBuffer 
{
    float4x4 view;
    float4x4 proj;
    uint64_t buffer;
};
[[vk::binding(0, 1)]]
ConstantBuffer<UniformBuffer, ScalarDataLayout> ubo;

[shader("compute")]
[numthreads(256,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint index = GlobalInvocationID.x;
    if(index > 1245) return;
    
    InstancedVertexData* vertex = (InstancedVertexData*)ubo.buffer;
    vertex[index].inColor = float4(0.0f, 1.0f, 0.0f, 1.0f);
}
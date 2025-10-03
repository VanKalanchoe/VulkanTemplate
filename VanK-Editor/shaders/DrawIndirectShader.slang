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
    uint32_t num;
};
[[vk::binding(0, 1)]]
ConstantBuffer<UniformBuffer, ScalarDataLayout> ubo;

[shader("compute")]
[numthreads(256,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint index = GlobalInvocationID.x;
    if(index > ubo.num) return;
    
    InstancedVertexData* vertex = (InstancedVertexData*)ubo.buffer;
    vertex[index].inColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
}
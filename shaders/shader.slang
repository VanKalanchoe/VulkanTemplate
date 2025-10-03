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

// Specialization constant - can be set at pipeline creation time
// Note: This is equivalent to GLSL's layout(constant_id = X)
[[vk::constant_id(0)]]
const bool useTexture = false; // Controls whether texture sampling is enabled

struct VSOutput
{
    float4 pos : SV_Position;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;
     float4x4 model = float4x4(
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    );
 
    InstancedVertexData* vertex = (InstancedVertexData*)ubo.buffer;
    InstancedVertexData vertexData = vertex[vertexID];

    output.pos = mul(ubo.proj, mul(ubo.view, mul(model, float4(vertexData.inPosition, 1.0))));
    output.fragColor = vertexData.inColor;
    output.fragTexCoord = vertexData.inTexCoord.xy;
    return output;
}
[[vk::binding(0, 0)]]
Sampler2D texture[];

[shader("pixel")]
float4 fragmentMain(VSOutput vertIn) : SV_TARGET0 {
    
   return texture[0].Sample(vertIn.fragTexCoord);
}
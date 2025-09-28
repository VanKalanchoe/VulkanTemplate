struct VSInput 
{
    [[vk::location(0)]]
    float3 inPosition : POSITION;
    [[vk::location(1)]]
    float3 inColor : COLOR;
    [[vk::location(2)]]
    float2 inTexCoord : TEXCOORD0;
};

struct UniformBuffer 
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 1)]]
ConstantBuffer<UniformBuffer> ubo;

// Specialization constant - can be set at pipeline creation time
// Note: This is equivalent to GLSL's layout(constant_id = X)
[[vk::constant_id(0)]]
const bool useTexture = false; // Controls whether texture sampling is enabled

struct VSOutput
{
    float4 pos : SV_Position;
    float3 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}
[[vk::binding(0, 0)]]
Sampler2D texture[];

[shader("pixel")]
float4 fragmentMain(VSOutput vertIn) : SV_TARGET0 {
    
   return texture[0].Sample(vertIn.fragTexCoord);
}
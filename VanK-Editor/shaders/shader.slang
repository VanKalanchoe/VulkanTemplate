struct VSInput 
{
    [[vk::location(0)]]
    float4 inPosition : POSITION;
    [[vk::location(1)]]
    float4 inColor : COLOR;
    [[vk::location(2)]]
    float4 inTexCoord : TEXCOORD0;
};

struct UniformBuffer 
{
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
     float4x4 model = float4x4(
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    );
    output.pos = mul(ubo.proj, mul(ubo.view, mul(model, input.inPosition)));
    output.fragColor = input.inColor.xyz;
    output.fragTexCoord = input.inTexCoord.xy;
    return output;
}
[[vk::binding(0, 0)]]
Sampler2D texture[];

[shader("pixel")]
float4 fragmentMain(VSOutput vertIn) : SV_TARGET0 {
    
   return texture[0].Sample(vertIn.fragTexCoord);
}
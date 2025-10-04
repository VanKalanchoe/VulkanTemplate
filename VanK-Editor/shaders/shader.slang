#include "shaderIO.h"

//------------------------------------------------------------------------------
// Input Structures
//------------------------------------------------------------------------------

// Define the data that will be interpolated and passed from vertex to fragment shader
// Note: SV_Position is a special semantic in Slang/HLSL for the clip-space position
struct VertexOutput
{
    float4 pos : SV_Position;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

// Define the final output of the fragment shader
// Note: SV_TARGET specifies this is the color output to the render target
struct PixelOutput
{
    float4 Color : SV_TARGET0; // Final color output (RGBA)
};

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

[[vk::binding(LBindTextures, LSetTextures)]]
Sampler2D texture[];

[[vk::binding(LBindSceneInfo, LSetScene)]]
ConstantBuffer<UniformBuffer, ScalarDataLayout> ubo;

// Specialization constant - can be set at pipeline creation time
// Note: This is equivalent to GLSL's layout(constant_id = X)
[[vk::constant_id(0)]]
const bool useTexture = false; // Controls whether texture sampling is enabled

//------------------------------------------------------------------------------
// Vertex Shader
//------------------------------------------------------------------------------

// Note: [shader("vertex")] is Slang's way of marking the vertex shader entry point
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    VertexOutput output;

     float4x4 model = float4x4(
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    );
 
    InstancedVertexData* vertex = (InstancedVertexData*)ubo.vertbuffer;
    InstancedVertexData vertexData = vertex[vertexID];

    output.pos = mul(ubo.proj, mul(ubo.view, mul(model, float4(vertexData.inPosition, 1.0))));
    output.fragColor = vertexData.inColor;
    output.fragTexCoord = vertexData.inTexCoord.xy;

    return output;
}

//------------------------------------------------------------------------------
// Fragment/Pixel Shader
//------------------------------------------------------------------------------

// Note: [shader("pixel")] is Slang's way of marking the fragment shader entry point
[shader("pixel")]
PixelOutput fragmentMain(VertexOutput input) : SV_TARGET0 
{
    PixelOutput output;

    output.Color = input.fragColor * texture[0].Sample(input.fragTexCoord);

   return output;
}
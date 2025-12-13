#include "shaderIO.h"

//------------------------------------------------------------------------------
// Input Structures
//------------------------------------------------------------------------------

// Define the data that will be interpolated and passed from vertex to fragment shader
// Note: SV_Position is a special semantic in Slang/HLSL for the clip-space position
struct VertexOutput
{
    float4 pos : SV_Position;
    uint32_t entityID : TEXCOORD0;  // Add entity ID to pass to fragment shader
    float4 color : COLOR0;
};

// Define the final output of the fragment shader
// Note: SV_TARGET specifies this is the color output to the render target
struct PixelOutput
{
    float4 Color : SV_TARGET0; // Final color output (RGBA)
    int EntityID : SV_TARGET1;
};

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

[[vk::binding(LBindTextures, LSetTextures)]]
Sampler2D texture[];

[[vk::binding(LBindSceneInfo, LSetScene)]]
ConstantBuffer<SceneInfo, ScalarDataLayout> ubo;

// Specialization constant - can be set at pipeline creation time
// Note: This is equivalent to GLSL's layout(constant_id = X)
[[vk::constant_id(0)]]
const bool useTexture = false; // Controls whether texture sampling is enabled

//------------------------------------------------------------------------------
// Vertex Shader
//------------------------------------------------------------------------------

// Note: [shader("vertex")] is Slang's way of marking the vertex shader entry point
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, uint drawIndex : SV_DrawIndex) 
{
    VertexOutput output;

    MeshInfo* meshInfo = (MeshInfo*)ubo.meshInfoAddress;
    MeshInfo meshInfoData = meshInfo[drawIndex];

    InstancedVertexData* vertex = (InstancedVertexData*)ubo.vertexAddress;
    InstancedVertexData vertexData = vertex[vertexID + meshInfoData.vertexOffset];

    InstancedTextData* text = (InstancedTextData*)ubo.textAddress;
    InstancedTextData textData = text[instanceID + meshInfoData.firstInstance];

    // Pass entity ID to fragment shader
    // Option 1: Use instanceID directly (if instanceID == entity ID)
    output.entityID = textData.EntityID;

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

    // Output entity ID to second render target (R32_SINT format)
    output.EntityID = input.entityID;

   return output;
}
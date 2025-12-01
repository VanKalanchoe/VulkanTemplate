#include "shaderIO.h"

//------------------------------------------------------------------------------
// Input Structures
//------------------------------------------------------------------------------

// Define the data that will be interpolated and passed from vertex to fragment shader
// Note: SV_Position is a special semantic in Slang/HLSL for the clip-space position
struct VertexOutput
{
    float4 pos : SV_Position;
    float2 fragTexCoord : TEXCOORD1;
    uint32_t textureIndex : TEXCOORD2;
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
VertexOutput vertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) 
{
    VertexOutput output;

    InstancedVertexData* vertex = (InstancedVertexData*)ubo.vertexAddress;
    InstancedVertexData vertexData = vertex[vertexID];

    InstancedStorageData* storage = (InstancedStorageData*)ubo.storageAddress;
    InstancedStorageData storageData = storage[instanceID];

    output.pos = mul(ubo.proj, mul(ubo.view, mul(storageData.Model, float4(vertexData.position, 1.0))));
    output.fragTexCoord = vertexData.texcoords.xy;
    output.textureIndex = storageData.albedoMap;

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

    output.Color = texture[input.textureIndex].Sample(input.fragTexCoord);

   return output;
}
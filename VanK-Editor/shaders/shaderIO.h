
#ifndef HOST_DEVICE_H
#define HOST_DEVICE_H

#ifdef __SLANG__
typealias vec2 = float2;
typealias vec3 = float3;
typealias vec4 = float4;
typealias mat4 = column_major float4x4;
typealias mat3 = column_major float3x3;
#define STATIC_CONST static const
#else
#define STATIC_CONST const
#endif

// Layout constants
// Set 0
STATIC_CONST int LSetTextures  = 0;
STATIC_CONST int LBindTextures = 0;
// Set 1
STATIC_CONST int LSetScene      = 1;
STATIC_CONST int LBindSceneInfo = 0;

struct UniformBuffer 
{
    mat4 view;
    mat4 proj;
    uint64_t vertbuffer;
    uint64_t indebuffer;
    uint64_t indirectBuffer;
    uint64_t countBuffer;
    uint32_t numvert;
    uint32_t numindic;
};

struct InstancedVertexData
{
    vec3 inPosition;
    vec4 inColor;
    vec2 inTexCoord;
};

struct Vertex
{
    vec3 pos;
    vec4 color;
    vec2 texCoord;
};

struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

#endif  // HOST_DEVICE_H

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

enum PipelineType 
{
    PipelineType_Quad   = 0,
    PipelineType_Circle = 1,
    PipelineType_Text   = 2,
    PipelineType_Line   = 3,
    PipelineType_Count  = 4
};

struct MeshInfo
{
    uint32_t indexCount;    // number of indices for this mesh
    uint32_t instanceCount; // how many instances of this mesh you want
    uint32_t firstIndex;    // starting index in the global index buffer
    uint32_t vertexOffset;  // vertex base offset
    uint32_t firstInstance; // optional, for indirect draw
    uint32_t pipelineType;
};

struct SceneInfo 
{
    mat4 view;
    mat4 proj;
    uint64_t indirectAddresses[4];
    uint64_t countAddresses[4];
    uint64_t vertexAddress;
    uint64_t storageAddress;
    uint64_t meshInfoAddress;
    uint64_t circleAddress;
    uint64_t textAddress;
    uint64_t lineAddress;
    uint32_t numMeshes;
};

struct InstancedIndexData
{
  int indices;
};

struct InstancedVertexData
{
    vec3 position;
    vec3 normals;
    vec2 texcoords;
    vec3 tangent;
    vec3 bitangent;
};

struct InstancedLineData
{
    vec3 P0;
    vec3 P1;
    vec4 Color;
    
    // Editor-only
    int EntityID;
};

struct InstancedTextData
{
    mat4 Transform;
    vec2 QuadMin;
    vec2 QuadMax;
    vec2 TexMin;
    vec2 TexMax;
    vec4 Color;
    uint32_t TextureIndex;

    // TODO: bg color for outline/bg

    // Editor-only
    int EntityID;
};

struct InstancedCircleData
{
    mat4 WorldPosition;
    vec4 Color;
    float Thickness;
    float Fade;

    // Editor-only
    int EntityID;
};

struct InstancedStorageData
{
    mat4 Model;
    vec4 color;
    uint32_t textureIndex;

    // Editor-only
    int EntityID;
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
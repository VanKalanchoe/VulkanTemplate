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

struct InstanceLUT
{
    uint32_t materialID;
    uint32_t indexBufferOffset;
    uint32_t firstVertex;
};

struct Material
{
    uint32_t albedoTexture; // pbr metallic roughness workflow
    uint32_t diffuseTexture; // pbr spec gloss workflow
    uint32_t normalTexture;
    uint32_t metallicRoughnessTexture; // pbr metallic roughness workflow
    
    uint32_t specularTexture; // pbr spec gloss workflow
    uint32_t emissiveTexture;
    uint32_t ambientOcclusionTexture;

    vec4 baseColorFactor; // pbr metallic roughness workflow
    vec4 diffuseFactor; // pbr spec gloss workflow
    float metallicFactor; // pbr metallic roughness workflow
    float roughnessFactor; // pbr metallic roughness workflow roughness = 1 - glossines
    
    vec4 specularGlossFactor; // pbr spec gloss workflow
    bool isSpecularGlossWorkflow; // pbr spec gloss workflow needed to convert to metallic roughness workflow function in shader
    float specularFactor; // khr extension does not work with spec gloss workflow
    vec3 specularColorFactor; // khr extension does not work with spec gloss workflow
    vec3 emissiveFactor;
    float emissiveStrength;
    float thicknessFactor;
    float attenuationDistance;
    vec3 attenuationColor;
    float ior;
    float ambientOcclusionFactor;

    uint32_t transparent;
    uint32_t doubleSided;

    uint32_t alphaMode;    // 0: OPAQUE, 1: MASK, 2: BLEND
    float    alphaCutoff;  // The 0.5 value from your JSON
    float transmissionFactor; // 0 = opaque, 1 = full transparent
};

// AABB structure for acceleration structure
struct Aabb
{
    vec3 minimum;
    vec3 maximum;
};

struct Vertex
{
    vec3 position;
    vec2 texcoords;
    vec3 normals;
    vec4 tangents;
};

struct MeshletPrimitive
{
    // Meshlet data (raster)
    uint32_t meshletOffset;
    uint32_t meshletCount;
    
    // Material
    uint32_t materialIndex;
    uint32_t padding1;
    
    // Ray tracing / classic geometry
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t maxVertex;

    // {3} center, {1} radius
    vec4 boundingSphere;
};

// Brick voxel data (8x8x8)
struct BrickVoxelData 
{// could be color, density, material ID, etc.
    uint8_t r[512];
    uint8_t g[512];
    uint8_t b[512];
};

#endif  // HOST_DEVICE_H
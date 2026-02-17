#include "shaderIO.h"
#include "nvshaders/random.h.slang"
#include "pbr.h.slang"
import common;

#define MISS_DEPTH 1000

[[vk::binding(0,2)]]
RaytracingAccelerationStructure topLevelAS;

[[vk::binding(1,2)]]
RWTexture2D<float4> outImage;

[[vk::binding(0, 0)]]
Sampler2D texture[];


struct PushConstant 
{
    SceneData* sceneData;
    Vertex* vertexBuffer;
    uint32_t* indexBuffer;
    MeshletVerticesData* meshletVerticesBuffer;
    MeshletTrianglesData* meshletTrianglesBuffer;
    MeshletData* meshletBuffer;
    MeshletPrimitive* meshletPrimitive;
    MeshDrawData* meshDrawBuffer;
    Material* materialBuffer;
    InstanceLUT* instanceLutBuffer;
    LightData* lightingBuffer;
    uint32_t skyBoxIndex;
};
[[vk::push_constant]]
ConstantBuffer<PushConstant> pushConstant;

// ------------------------------------------------------------
// Ray payload structure
// ------------------------------------------------------------
struct HitPayload
{
    float3 color;   // Accumulated color along the ray path
    float  weight;  // Weight/importance of this ray (for importance sampling)
    int    depth;   // Current recursion depth (for limiting bounces)
};

// ------------------------------------------------------------
// Ray Generation Shader
// ------------------------------------------------------------
[shader("raygeneration")]
void raygenerationMain()
{
    // Get pixel coordinates
    float2 launchID = (float2)DispatchRaysIndex().xy;
    float2 launchSize = (float2)DispatchRaysDimensions().xy;

    // Convert to NDC
    float2 ndc;
    ndc.x = (launchID.x + 0.5) / launchSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - ((launchID.y + 0.5) / launchSize.y * 2.0);

    // Camera
    SceneData scene = pushConstant.sceneData[0];
    float4 viewPos = mul(scene.projInverse, float4(ndc, 1.0, 1.0));
    float3 rayDir = normalize(viewPos.xyz / viewPos.w);
    float3 rayOrigin = mul(scene.viewInverse, float4(0,0,0,1)).xyz;
    rayDir = mul(scene.viewInverse, float4(rayDir,0)).xyz;

    // Prepare ray
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = INFINITE;

    // Payload
    HitPayload payload;
    payload.color = float3(0,0,0);
    payload.depth = 0;

    // Trace ray
    TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, ray, payload);

    // Write final color
    outImage[int2(launchID)] = float4(payload.color, 1.0);
}

// Closest hit shader - called when ray hits geometry
[shader("closesthit")]
void closesthitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // ... [Keep all your ID fetching and Vertex logic exactly as is] ...
    uint instanceID = InstanceIndex();
    uint triID      = PrimitiveIndex();
    InstanceLUT lut = pushConstant.instanceLutBuffer[NonUniformResourceIndex(instanceID)];
    uint i0 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 0)] + lut.firstVertex;
    uint i1 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 1)] + lut.firstVertex;
    uint i2 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 2)] + lut.firstVertex;
    Vertex v0 = pushConstant.vertexBuffer[i0]; Vertex v1 = pushConstant.vertexBuffer[i1]; Vertex v2 = pushConstant.vertexBuffer[i2];
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = bary.x * v0.texcoords + bary.y * v1.texcoords + bary.z * v2.texcoords;
    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN   = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
    float3 worldPos = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(localPos, 1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)pushConstant.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localN));
    if (dot(worldNormal, WorldRayDirection()) > 0) worldNormal = -worldNormal;

    Material mat = pushConstant.materialBuffer[NonUniformResourceIndex(lut.materialID)];

    // --- 1. SAMPLE TEXTURES ---
    float4 albedo = mat.diffuseFactor;
    if (mat.albedoTexture != 0) albedo *= texture[mat.albedoTexture].SampleLevel(uv, 0.0);
    
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if(mat.metallicRoughnessTexture != 0) {
       float4 mrT = texture[mat.metallicRoughnessTexture].SampleLevel(uv, 0.0);
       metallic *= mrT.b; roughness *= mrT.g;
    }

    float3 emissive = mat.emissiveFactor * mat.emissiveStrength;
    if (mat.emissiveTexture != 0) 
        emissive *= texture[mat.emissiveTexture].SampleLevel(uv, 0.0).rgb;

    // C. Default Opaque (Walls, Floors, etc.)
    payload.color = albedo.rgb + emissive;
}


// ------------------------------------------------------------
// Miss Shader
// ------------------------------------------------------------
[shader("miss")]
void missMain(inout HitPayload payload)
{
    // Background color
    payload.color = float3(0.0, 0.0, 0.0); // Black
    payload.depth = MISS_DEPTH;
}

// ------------------------------------------------------------
// Any-Hit Shader (optional for transparency, etc.)
// ------------------------------------------------------------
[shader("anyhit")]
void anyhitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Empty for now
}

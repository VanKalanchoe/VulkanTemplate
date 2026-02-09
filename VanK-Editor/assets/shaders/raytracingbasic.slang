#include "shaderIO.h"
#include "pbr.h.slang"
import common;

#define MISS_DEPTH 1000

[[vk::binding(0,2)]]
RaytracingAccelerationStructure topLevelAS;

[[vk::binding(1,2)]]
RWTexture2D<float4> outImage;

[[vk::binding(0, 0)]]
SamplerCube cubes[];

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

// Ray payload structure - carries data through the ray tracing pipeline
struct HitPayload
{
    float3 color;   // Accumulated color along the ray path
    float  weight;  // Weight/importance of this ray (for importance sampling)
    int    depth;   // Current recursion depth (for limiting bounces)
};

//-----------------------------------------------------------------------
// SHADOW TESTING
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection)
{
  RayDesc shadowRay;
  shadowRay.Origin    = worldPos + worldNormal * 0.001;  // Offset to avoid self-intersection
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;
  shadowRay.TMax      = INFINITE;

  // Create a simple payload for shadow ray
  HitPayload shadowPayload;
  shadowPayload.depth = 0;

  // Trace the shadow ray with optimized flags
  TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0,
           shadowRay, shadowPayload);

  // If the shadow ray hit something, the light is occluded
  return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

[shader("raygeneration")]
void raygenerationMain()
{
	// Get the current pixel coordinates and image dimensions
    float2 launchID = (float2)DispatchRaysIndex().xy;     // Current pixel (x,y)
    float2 launchSize = (float2)DispatchRaysDimensions().xy; // Image size (width,height)

    // Retrieve scene information from push constants
    SceneData sceneInfo = pushConstant.sceneData[0];

    // Set ray tracing flags (0 = no special flags)
    const uint rayFlags = 0;
    
    // Convert pixel coordinates to normalized device coordinates (NDC)
    // Range: [-1,1] for both x and y
    const float2 clipCoords = float2(launchID.x / launchSize.x * 2.0 - 1.0, 1.0 - (launchID.y / launchSize.y) * 2.0 + 1.0);
    
    // Transform from NDC to view space using inverse projection matrix
    const float4 viewCoords = mul(sceneInfo.projInverse, float4(clipCoords, 1.0, 1.0));

    // Create the primary ray
    RayDesc ray;
    // Transform camera origin (0,0,0) from view space to world space
    ray.Origin = mul(sceneInfo.viewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    // Transform ray direction from view space to world space
    ray.Direction = mul(sceneInfo.viewInverse, float4(normalize(viewCoords.xyz), 0.0)).xyz;
    ray.TMin = 0.001;    // Minimum distance to avoid self-intersection
    ray.TMax = INFINITE; // Maximum distance (infinite for primary rays)

    // Initialize ray payload with default values
    HitPayload payload;
    payload.color = float3(0, 0, 0);  // Start with black
    payload.weight = 1;               // Full weight for primary rays
    payload.depth = 0;                // Start at depth 0

    // Cast the ray into the scene using the acceleration structure
    // Parameters: AS, flags, instance mask, sbt offset, sbt stride, miss offset, ray, payload
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
    
    // Get the final color from the ray tracing result
    float3 color = payload.color;

    float exposure = 1.0;
color *= exposure;
    color = ACESFilm(color);
/*     color = Uncharted2Tonemap(color);
    color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx)); */

    // simple gamma correction
    color = pow(color, 1.0 / 2.2);
    
    // Write the result to the output image
    outImage[int2(launchID)] = float4(color, 1.0);
}
float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
float3 Uncharted2Tonemap(float3 color)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}
// Closest hit shader - called when ray hits geometry
[shader("closesthit")]
void closesthitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // IDs from the TLAS and BLAS
    uint instanceID = InstanceIndex();  // TLAS instance
    uint triID      = PrimitiveIndex(); // Triangle in BLAS / meshlet

    // 1. Get instance LUT
    InstanceLUT lut = pushConstant.instanceLutBuffer[NonUniformResourceIndex(instanceID)];

    // 2. Get meshlet primitive info for this instance
    MeshletPrimitive prim = pushConstant.meshletPrimitive[NonUniformResourceIndex(instanceID)];

    // 3. Map triangle index to global vertex indices
    uint i0 = pushConstant.indexBuffer[prim.indexOffset + triID * 3 + 0] + lut.firstVertex;
    uint i1 = pushConstant.indexBuffer[prim.indexOffset + triID * 3 + 1] + lut.firstVertex;
    uint i2 = pushConstant.indexBuffer[prim.indexOffset + triID * 3 + 2] + lut.firstVertex;

    // 4. Fetch vertices
    Vertex v0 = pushConstant.vertexBuffer[i0];
    Vertex v1 = pushConstant.vertexBuffer[i1];
    Vertex v2 = pushConstant.vertexBuffer[i2];

    // 5. Interpolate attributes using barycentrics
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, 
                         attr.barycentrics.x, 
                         attr.barycentrics.y);

    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN   = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
    float2 uv       = bary.x * v0.texcoords + bary.y * v1.texcoords + bary.z * v2.texcoords;

    // 6. Transform to world space (like ObjectToWorld in tutorial)
    float4 worldPos4 = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(localPos, 1.0));
    float3 worldPos  = worldPos4.xyz;

    float3 worldNormal = normalize(mul((float3x3)pushConstant.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localN));

    // 7. Get material
    uint materialID = lut.materialID;
    Material mat = pushConstant.materialBuffer[NonUniformResourceIndex(materialID)];

	// Simple PBR shading
    float3 albedo = mat.diffuseFactor.xyz;
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    float3 emissive = mat.emissiveFactor * mat.emissiveStrength;

    float3 lightColor = float3(1.0, 1.0, 1.0);
    float3 lightDir = float3(0.0, 1.0, 1.0);
 
	float3 N = normalize(worldNormal);
    float3 V = -WorldRayDirection();
    float3 L = normalize(lightDir - worldPos);

    // Test for shadows
    float shadowFactor = testShadow(worldPos, N, lightDir - worldPos);

    // Calculate PBR lighting with shadow factor
	float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
    color *= lightColor /* * 4.0f *//* * sceneInfo.punctualLights[0].intensity */ * shadowFactor;

    // ADD emissive AFTER lighting
    color += emissive;

    // 8. Now you can do PBR shading, reflections, normal mapping, etc.
    payload.color = color; // simple test
}

// Miss shader - called when ray doesn't hit any geometry
[shader("miss")]
void missMain(inout HitPayload payload)
{
    // Set background color when ray misses all geometry
    // In a full implementation, this could sample from:
    // - Skybox textures
    // - Procedural sky gradients
    // - Environment maps

    float3 dir = normalize(WorldRayDirection());
    dir.z = -dir.z;
    float3 envColor = cubes[pushConstant.skyBoxIndex].SampleLevel(dir, 0.0).rgb;
    payload.color += envColor * payload.weight; // Gray background

    payload.depth = MISS_DEPTH;
}
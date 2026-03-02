import common;

#include "shaderIO.h"
#include "pbr.h.slang"

[[vk::binding(0,2)]]
RaytracingAccelerationStructure topLevelAS;

[[vk::binding(1,2)]]
RWTexture2D<float4> outImage;

[[vk::binding(0, 0)]]
Sampler2D texture[];

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
};
[[vk::push_constant]]
ConstantBuffer<PushConstant> pushConst;

struct HitPayload
{
    float3 color;
    float  weight;
    int    depth;
};

#define MISS_DEPTH 1000

[shader("raygeneration")]
void raygenerationMain()
{
	// Get the current pixel coordinates and image dimensions
    float2 launchID = (float2)DispatchRaysIndex().xy;     // Current pixel (x,y)
    float2 launchSize = (float2)DispatchRaysDimensions().xy; // Image size (width,height)

    // Retrieve scene information from push constants
    SceneData sceneInfo = pushConst.sceneData[0];

    // Set ray tracing flags (0 = no special flags)
    //RAY_FLAG_CULL_BACK_FACING_TRIANGLES
    const uint rayFlags = 0;
    
    // Convert pixel coordinates to normalized device coordinates (NDC)
    // Range: [-1,1] for both x and y
    const float2 clipCoords = float2(launchID.x / launchSize.x * 2.0 - 1.0, 1.0 - (launchID.y / launchSize.y) * 2.0 + 1.0);
    float2 ndc;
ndc.x = (launchID.x + 0.5) / launchSize.x * 2.0 - 1.0;
ndc.y = 1.0 - ((launchID.y + 0.5) / launchSize.y * 2.0);
    // Transform from NDC to view space using inverse projection matrix
    const float4 viewCoords = mul(sceneInfo.projInverse, float4(ndc, 1.0, 1.0));
float3 dir = normalize(viewCoords.xyz / viewCoords.w); // perspective divide if needed
    // Create the primary ray
    RayDesc ray;
    // Transform camera origin (0,0,0) from view space to world space
    ray.Origin = mul(sceneInfo.viewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    // Transform ray direction from view space to world space
    ray.Direction = mul(sceneInfo.viewInverse, float4(dir, 0.0)).xyz;
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

    float exposure = 1.0; // start here, tweak in UI later

    // HDR tonemapping
    /* color = color / (color + float3(1.0)); */
    /* color = Uncharted2Tonemap(color * exposure);
	color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx)); */
    /* color = PBRNeutralToneMapping(color); */
    /* color = ACESFilm(color); */

    // gamma correct already because of srgb swapchain format
    /* color = pow(color, float3(1.0/2.2));  */
    
    // Write the result to the output image
    outImage[int2(launchID)] = float4(color, 1.0);
}

// Closest hit shader - called when ray hits geometry
[shader("closesthit")]
void closesthitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
   payload.color = float3(1, 0, 0); // Pink
}

[shader("miss")]
void missMain(inout HitPayload payload)
{
    SceneData* scene = pushConst.sceneData;

    // Get ray direction in world space
    float3 rayDir = WorldRayDirection();
    rayDir = normalize(rayDir);
    rayDir.z = -rayDir.z;
  
    // Sample skybox
    float3 envColor = float3(0,0,0);
    if(scene->useSky && scene->skyBoxIndex != 0)
    {
        envColor = cubes[NonUniformResourceIndex(scene->skyBoxIndex)].SampleLevel(rayDir, 0.0).rgb;
    }
    else 
    {
        // Use simple solid background color
        envColor = float3(0,0,0); // sceneInfo.backgroundColor
    }

    payload.color = envColor;

    payload.depth = MISS_DEPTH;
}

[shader("intersection")]
void intersectionMain()
{
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDir    = ObjectRayDirection();

    // AABB intersection
    Aabb aabb;
    aabb.minimum = float3(-0.5, -0.5, -0.5);
    aabb.maximum = float3(0.5, 0.5, 0.5);

    float tHit = hitAabb(aabb, rayOrigin, rayDir);

    if (tHit > 0)
    {
        // Create hit attributes
        BuiltInTriangleIntersectionAttributes attr;
        attr.barycentrics = float2(0.0, 0.0);  // For a plane, we can use (0,0) as barycentrics

        ReportHit(tHit, 0, attr);
        // hier die 0 bedeutet was closeshit2 für verschiende geoemtrys machen kann
        // z.b cube die id 0 sphere wäre 1 dann in closeshit kann ich 
        // uint kind = HitKind(); und dann je nachdem was machen cool
    }
}

[shader("closesthit")]
void closesthitMain2(inout HitPayload payload)
{
    payload.color = float3(1, 0, 1); // Pink
}

// Ray-AABB intersection
float hitAabb(Aabb aabb, float3 rayOrigin, float3 rayDirection)
{
  float3 invDir = 1.0 / rayDirection;
  float3 tbot   = invDir * (aabb.minimum - rayOrigin);
  float3 ttop   = invDir * (aabb.maximum - rayOrigin);
  float3 tmin   = min(ttop, tbot);
  float3 tmax   = max(ttop, tbot);
  float  t0     = max(tmin.x, max(tmin.y, tmin.z));
  float  t1     = min(tmax.x, min(tmax.y, tmax.z));
  return t1 > max(t0, 0.0) ? t0 : -1.0;
}
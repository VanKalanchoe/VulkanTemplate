import common;

#include "shaderIO.h"
#include "pbr.h.slang"
#include "nvshaders/random.h.slang"

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
    Material* materialBuffer;
    InstanceLUT* instanceLutBuffer;
    LightData* lightingBuffer;
    BrickVoxelData* voxelBuffer;
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
    SceneData* scene = pushConst.sceneData;

    uint32_t frame = scene->frameIndex;

    // Initialize high-quality random number generator
    // Uses xxhash32 with pixel coordinates and frame number for good distribution
    uint seed = xxhash32(uint3(uint2(launchID.xy), frame));

    // ANTI-ALIASING: Subpixel jittering for temporal supersampling
    // First frame uses pixel center, subsequent frames use random subpixel positions
    const float2 subpixelJitter = frame == 0 ? float2(0.5, 0.5) : float2(rand(seed), rand(seed));
    const float2 pixelCenter = launchID + subpixelJitter; // Jittered pixel position
    float2 clipCoords = pixelCenter / launchSize * 2.0 - 1.0;  // Convert to NDC [-1,1]
    clipCoords.y *= -1.0; 
    const float4 viewCoords = mul(scene->projInverse, float4(clipCoords.x, clipCoords.y, 1.0, 1.0));  // Transform to view space

    // Generate primary camera ray
    RayDesc ray;
    ray.Origin = mul(scene->viewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space
    ray.Direction = mul(scene->viewInverse, float4(normalize(viewCoords.xyz), 0.0)).xyz; // Ray direction in world space
    ray.TMin = 0.001;    // Minimum distance to avoid self-intersection
    ray.TMax = INFINITE; // Maximum distance (infinite for primary rays)

    // Initialize ray payload with default values
    HitPayload payload;
    payload.color = float3(0, 0, 0);  // Start with black
    payload.weight = 1;               // Full weight for primary rays
    payload.depth = 0;                // Start at depth 0

    // Cast the ray into the scene using the acceleration structure
    // Parameters: AS, flags, instance mask, sbt offset, sbt stride, miss offset, ray, payload
    TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, ray, payload);
    
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


struct VoxelHitAttributes
{
    int3 voxel;
    float tHit;
};

[shader("intersection")]
void intersectionMain()
{
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDir    = normalize(ObjectRayDirection());

    const int3 brickSize = int3(8,8,8);
    Aabb aabb;
    aabb.minimum = float3(0,0,0);
    aabb.maximum = float3(brickSize);

    // --- Step 1: Ray-AABB intersection ---
    float3 invDir = 1.0 / rayDir;
    float3 t1 = (aabb.minimum - rayOrigin) * invDir;
    float3 t2 = (aabb.maximum - rayOrigin) * invDir;
    float3 tMin = min(t1, t2);
    float3 tMax = max(t1, t2);
    float tEnter = max(max(tMin.x, tMin.y), tMin.z);
    float tExit  = min(min(tMax.x, tMax.y), tMax.z);

    if(tExit < max(tEnter, 0.0)) return; // ray misses brick

    // --- Step 2: Starting voxel ---
    float3 startPos = rayOrigin + rayDir * max(tEnter, 0.0);
    int3 voxel = clamp(int3(floor(startPos)), int3(0), brickSize - 1);

    // --- Step 3: DDA setup using scalars only ---
    int3 step = int3(sign(rayDir));

    float tMaxX = (rayDir.x != 0.0) ? ((voxel.x + (step.x > 0 ? 1 : 0) - rayOrigin.x) / rayDir.x) : INFINITE;
    float tMaxY = (rayDir.y != 0.0) ? ((voxel.y + (step.y > 0 ? 1 : 0) - rayOrigin.y) / rayDir.y) : INFINITE;
    float tMaxZ = (rayDir.z != 0.0) ? ((voxel.z + (step.z > 0 ? 1 : 0) - rayOrigin.z) / rayDir.z) : INFINITE;

    float tDeltaX = (rayDir.x != 0.0) ? (1.0 / abs(rayDir.x)) : INFINITE;
    float tDeltaY = (rayDir.y != 0.0) ? (1.0 / abs(rayDir.y)) : INFINITE;
    float tDeltaZ = (rayDir.z != 0.0) ? (1.0 / abs(rayDir.z)) : INFINITE;

    uint instance = InstanceIndex();
    BrickVoxelData* voxelBuffer = pushConst.voxelBuffer;

    // --- Step 4: Traverse voxels ---
    const int maxSteps = 32;
    float tCur = max(tEnter, 0.0);

    for(int i=0; i<maxSteps; i++)
    {
        // --- Check voxel bounds ---
        if(any(voxel < 0) || any(voxel >= brickSize)) break;

        int index = voxel.x + voxel.y * 8 + voxel.z * 64;
        if(voxelBuffer[instance].r[index] != 0 || voxelBuffer[instance].g[index] != 0 || voxelBuffer[instance].b[index] != 0)
        {
            // Hit non-empty voxel
            VoxelHitAttributes attr;
            attr.voxel = voxel;
            attr.tHit  = tCur;
            ReportHit(tCur, 0, attr);
            return;
        }

        // --- Advance to next voxel ---
        if(tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            voxel.x += step.x;
            tCur = tMaxX;
            tMaxX += tDeltaX;
        }
        else if(tMaxY < tMaxZ)
        {
            voxel.y += step.y;
            tCur = tMaxY;
            tMaxY += tDeltaY;
        }
        else
        {
            voxel.z += step.z;
            tCur = tMaxZ;
            tMaxZ += tDeltaZ;
        }
    }
}

[shader("closesthit")]
void closesthitMain2(inout HitPayload payload, in VoxelHitAttributes attr)
{
    uint instance = InstanceIndex();
    BrickVoxelData* voxelBuffer = pushConst.voxelBuffer;

    int3 v = attr.voxel;
    int index = v.x + v.y * 8 + v.z * 64;

    float3 colorBase = float3(voxelBuffer[instance].r[index], voxelBuffer[instance].g[index], voxelBuffer[instance].b[index]) / 255.0;

    // 1. Calculate Hit Position in Object Space
    float3 hitPos = ObjectRayOrigin() + ObjectRayDirection() * attr.tHit;
    
    // 2. Get local position within the specific voxel [0, 1]
    float3 localPos = hitPos - float3(v);

    // 3. Determine which face was hit (the Normal)
    // We find which component is closest to 0 or 1
    float3 f = abs(localPos - 0.5);
    float2 uv;
    
    if (f.x > f.y && f.x > f.z) {
        // --- X faces (Left / Right) ---
        if (localPos.x > 0.5) {
            // Right face (+X): Looking at it, Z increases to the left
            uv = float2(1.0 - localPos.z, localPos.y);
        } else {
            // Left face (-X): Looking at it, Z increases to the right
            uv = float2(localPos.z, localPos.y);
        }
    } 
    else if (f.y > f.z) {
        // --- Y faces (Top / Bottom) ---
        if (localPos.y > 0.5) {
            // Top face (+Y): 
            // We want U to be X, and V to increase as we go "into" the screen (-Z)
            uv = float2(localPos.x, 1.0 - localPos.z);
        } else {
            // Bottom face (-Y): 
            // When looking up, X is still X, but V increases as we go "away" (+Z)
            uv = float2(localPos.x, localPos.z);
        }
    }
    else {
        // --- Z faces (Front / Back) ---
        if (localPos.z > 0.5) {
            // Front face (+Z): Looking at it, X increases to the right
            uv = float2(localPos.x, localPos.y);
        } else {
            // Back face (-Z): Looking at it, X increases to the left
            uv = float2(1.0 - localPos.x, localPos.y);
        }
    }

    // 4. Sample the texture using the corrected UVs
    float4 texColor = texture[NonUniformResourceIndex(8)].SampleLevel(uv, 0.0);

    payload.color = colorBase * texColor.rgb;
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
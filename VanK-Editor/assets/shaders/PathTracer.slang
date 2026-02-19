import common;

#include "pbr.h.slang"
#include "nvshaders/constants.h.slang"
#include "nvshaders/random.h.slang"
#include "nvshaders/ray_utils.h.slang"
#include "nvshaders/sky_functions.h.slang"

#include "shaderIO.h"

#define MISS_DEPTH 1000

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

// Build tangent and bitangent for a given normal
void CreateOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    t = abs(n.x) > 0.1 ? normalize(cross(float3(0,1,0), n)) : normalize(cross(float3(1,0,0), n));
    b = cross(n, t);
}

// Sample a direction on hemisphere around normal (cosine-weighted)
float3 SampleHemisphere(float3 normal, inout uint seed)
{
    // Generate 2 random numbers in [0,1)
    float xi1 = rand(seed);
    float xi2 = rand(seed);

    // Spherical coordinates (cosine-weighted)
    float phi = 2.0f * 3.14159265f * xi1;
    float cosTheta = sqrt(1.0 - xi2);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Orthonormal basis
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    // Convert to world space
    return normalize(sinTheta * cos(phi) * tangent +
                     sinTheta * sin(phi) * bitangent +
                     cosTheta * normal);
}

//-----------------------------------------------------------------------
// SHADOW TESTING (hard shadow version)
//-----------------------------------------------------------------------
bool traceShadow(RayDesc ray)
{
    RayDesc shadowRay;
    shadowRay.Origin    = ray.Origin;  // offset to avoid self-intersection
    shadowRay.Direction = ray.Direction;
    shadowRay.TMin      = ray.TMin;
    shadowRay.TMax      = ray.TMax; // can use maxDistance here for efficiency

    HitPayload shadowPayload;
    shadowPayload.depth = 0;

    TraceRay(topLevelAS,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0xff, 0, 0, 0,
             shadowRay, shadowPayload);

    // True = light blocked, False = light visible
    return shadowPayload.depth != MISS_DEPTH;
}

//-----------------------------------------------------------------------
// LIGHT PROCESSING - Handles sky override, distance attenuation, and spot light cone effects
//-----------------------------------------------------------------------
// Processes light properties based on type and applies appropriate attenuation
Lights processLight(/* GltfSceneInfo* sceneInfo, */ float3 worldPos)
{
  // Start with the first punctual light in the scene (simplified for tutorial)
  Lights light = pushConst.lightingBuffer->lights[0];

/*   // Sky override: Replace punctual light with sun parameters from sky system
  // This allows using procedural sky lighting instead of manual light setup
  if(pushConst.useSky == 1)
  {
    light.direction = sceneInfo.skySimpleParam.sunDirection;  // Sun direction from sky
    light.color     = sceneInfo.skySimpleParam.sunColor;      // Sun color from sky
    light.intensity = sceneInfo.skySimpleParam.sunIntensity;  // Sun intensity from sky
    light.type      = LightType::eDirectional;            // Sun is always directional
  } */

    if(light.type == LightType::eDirectional)
    {
        light.direction = -light.direction;
    }

  // Point light: Calculate direction from light to surface and apply distance attenuation
  if(light.type == LightType::ePoint)
  {
    // Direction from surface point to light position
    light.direction = light.position - worldPos;
    float d         = length(light.direction);
    // Apply inverse square law: intensity decreases with distance squared
    light.intensity /= (d * d);
  }
  // Spot light: Calculate direction, apply distance attenuation and cone falloff
  else if(light.type == LightType::eSpot)
  {
    // Direction from surface point to light position
    float3 lightDir = light.position - worldPos;
    float  d        = length(lightDir);
    // Apply distance attenuation (inverse square law)
    light.intensity /= (d * d);

    // Calculate cone attenuation based on angle from spot light direction
    // theta: cosine of angle between light direction and spot direction
    float theta = dot(normalize(lightDir), normalize(light.direction));
    // Smooth falloff from inner cone (1.0) to outer cone (0.0)
    float spotIntensity = clamp((theta - cos(light.coneAngle)) / (1.0 - cos(light.coneAngle)), 0.0, 1.0);
    light.intensity *= spotIntensity;
    light.direction = lightDir;
  }

  return light;
}


// ------------------------------------------------------------
// Ray payload structure
// ------------------------------------------------------------
struct HitPayload
{
    float3 color;   // Accumulated color along the ray path
    float  weight;  // Weight/importance of this ray (for importance sampling)
    int    depth;   // Current recursion depth (for limiting bounces)
    uint seed; 
    float3 throughput;
};

// ------------------------------------------------------------
// Ray Generation Shader
// ------------------------------------------------------------
[shader("raygeneration")]
void raygenerationMain()
{
    // Get current pixel coordinates from thread ray dispatch ID
    int2 launchID = int2(DispatchRaysIndex().xy);
    float2 launchSize = float2(DispatchRaysDimensions().xy); // Query texture dimensions

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
    ray.TMin = 0.001; // Minimum distance to avoid numerical issues
    ray.TMax = INFINITE; // Maximum distance (infinite for primary rays)

    // Initialize payload
    HitPayload payload;
    payload.color = float3(0,0,0); // Accumulated radiance (final color)
    payload.depth = 0;
    payload.seed = seed;
    payload.throughput = float3(1,1,1); // Path throughput (energy transmission)

    // Trace the primary ray through the scene
    TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, ray, payload);

    // FIREFLY CLAMPING - Remove bright noise artifacts
    // Calculate luminance using standard RGB-to-luminance conversion
    float lum = dot(payload.color, float3(0.212671, 0.715160, 0.072169));
    const float fireflyClampThreshold = 10.0;
    if(lum > fireflyClampThreshold)
    {
        // Clamp overly bright pixels while preserving color ratios
        payload.color *= fireflyClampThreshold / lum;
    }

    // TEMPORAL ACCUMULATION - Progressive refinement over multiple frames
    if(frame == 0)
    {
        // First frame: Initialize with current sample
        outImage[launchID] = float4(payload.color, 1.0);
    }
    else
    {
        // Subsequent frames: Blend with accumulated result
        // Uses exponential moving average for stable convergence
        float a = 1.0F / float(frame + 1); // Blend factor
        float3 prevColor = outImage[launchID].rgb; // Previous accumulated result
        outImage[launchID] = float4(lerp(prevColor, payload.color, a), 1.0); // Blend and store
    }
}

// Closest hit shader - called when ray hits geometry
[shader("closesthit")]
void closesthitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // -------------------------------
    // Fetch triangle, vertex, normal, UV
    // -------------------------------
    uint instanceID = InstanceIndex();
    uint triID = PrimitiveIndex();
    InstanceLUT lut = pushConst.instanceLutBuffer[NonUniformResourceIndex(instanceID)];

    uint i0 = pushConst.indexBuffer[lut.indexBufferOffset + (triID * 3 + 0)] + lut.firstVertex;
    uint i1 = pushConst.indexBuffer[lut.indexBufferOffset + (triID * 3 + 1)] + lut.firstVertex;
    uint i2 = pushConst.indexBuffer[lut.indexBufferOffset + (triID * 3 + 2)] + lut.firstVertex;

    Vertex v0 = pushConst.vertexBuffer[i0];
    Vertex v1 = pushConst.vertexBuffer[i1];
    Vertex v2 = pushConst.vertexBuffer[i2];

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = bary.x * v0.texcoords + bary.y * v1.texcoords + bary.z * v2.texcoords;
    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
    float3 worldPos = mul(pushConst.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(localPos,1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)pushConst.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localN));
    if(dot(worldNormal, WorldRayDirection()) > 0) worldNormal = -worldNormal;

    Material mat = pushConst.materialBuffer[NonUniformResourceIndex(lut.materialID)];

    // -------------------------------
    // Sample textures
    // -------------------------------
    float4 albedo = mat.diffuseFactor;
    if(mat.albedoTexture != 0) 
    {
        albedo *= texture[mat.albedoTexture].SampleLevel(uv,0.0); // Base color (albedo)
    }

    float3 emissive = mat.emissiveFactor * mat.emissiveStrength;
    if(mat.emissiveTexture != 0)
    {
        emissive *= texture[mat.emissiveTexture].SampleLevel(uv,0.0).rgb;
    }

    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if(mat.metallicRoughnessTexture != 0) 
    {
       float4 mrT = texture[mat.metallicRoughnessTexture].SampleLevel(uv, 0.0);
       metallic *= mrT.b; // Metallic factor (0=dielectric, 1=metal)
       roughness *= max(0.0001f, mrT.g); // Surface roughness (clamped to avoid divisions by zero)
    }

    // Add emissive contribution scaled by throughput ?????? idk
    payload.color += payload.throughput * emissive;

    // Process light source (handles different light types and sky override)
    Lights light = processLight(/* sceneInfo, */ /* payload.hit.pos */worldPos);

    // Set up lighting vectors for BSDF evaluation
    float  pdf = 0.0F;                        // Probability density function (for importance sampling)
    float3 V   = -WorldRayDirection();              // View direction (towards camera)
    float3 L   = normalize(light.direction);  // Light direction

    float3 p0 = mul(pushConst.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v0.position,1)).xyz;
    float3 p1 = mul(pushConst.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v1.position,1)).xyz;
    float3 p2 = mul(pushConst.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v2.position,1)).xyz;

    float3 geonrm = normalize(cross(p1 - p0, p2 - p0));
 
    // Ensure geometric normal faces against ray
    if(dot(geonrm, WorldRayDirection()) > 0) geonrm = -geonrm;
    
    // Initialize PBR material structure for BSDF evaluation
    PbrBaseMaterial pbrMat = initPbrBaseMaterial(albedo.rgb, metallic, roughness, worldNormal, geonrm);
    float3          contrib = float3(0, 0, 0);  // Direct lighting contribution

    // -------------------------------
    // Recursive Monte Carlo bounce with NEE jitter
    // -------------------------------
    if(payload.depth < 5) // max depth
    {
        payload.depth += 1;

        // NEXT EVENT ESTIMATION - Direct lighting evaluation
        // Check if light direction is above the surface (no self-illumination)
        bool nextEventValid = (dot(L, worldNormal) > 0.0f);
        if(nextEventValid)
        {
            // Evaluate BSDF for direct lighting
            BsdfEvaluateData evalData;
            evalData.k1 = -WorldRayDirection();                                    // Incoming direction (from camera)
            evalData.k2 = normalize(/* sceneInfo.skySimpleParam.sunDirection */L);  // Outgoing direction (to light)
            evalData.xi = float3(rand(payload.seed), rand(payload.seed), rand(payload.seed));        // Random numbers for sampling

            // Evaluate PBR BSDF (both diffuse and specular components)
            bsdfEvaluateSimple(evalData, pbrMat);

            // Accumulate direct lighting contribution
            // NOTE: Light sampling strategy and MIS
            // This implementation uses a simplified approach:
            // - NEE (Next Event Estimation) for direct lighting with weight = 1.0
            // - BSDF sampling for indirect lighting only
            // 
            // For DELTA light sources (directional lights/sun), w=1.0 is mathematically correct
            // because BSDF sampling has zero probability of hitting a delta light, so only NEE contributes.
            //
            // For AREA LIGHTS or ENVIRONMENT MAPS, proper MIS would combine both light and BSDF sampling.
            // The evalData.pdf output (currently unused) would be needed for this calculation.
            const float3 w = powerHeuristic(1.0f, evalData.pdf);  // MIS weight (correct for delta lights, simplified for area lights)
            contrib += w * evalData.bsdf_diffuse;  // Diffuse reflection
            contrib += w * evalData.bsdf_glossy;   // Specular reflection
            contrib *= payload.throughput;                 // Weight by path throughput
        }

          // SHADOW TESTING - Add direct lighting only if not occluded
        if(nextEventValid)
        {
            // Create shadow ray from surface point towards light
            RayDesc shadowRay;
            shadowRay.Origin    = offsetRay(worldPos, worldNormal);  // Start from offset surface point
            shadowRay.Direction = L;                                          // Direction towards light
            shadowRay.TMin      = 0.01;                                       // Avoid self-intersection
            shadowRay.TMax = light.type == LightType::eDirectional ? INFINITE : length(light.direction);  // Trace to infinity (or light distance)

           // Test if light is visible from this point
            bool inShadow = traceShadow(shadowRay);
            if(!inShadow)
            {
                // Light is visible - add direct lighting contribution
                payload.color += contrib * light.color * light.intensity;
            }
            // If in shadow, skip direct lighting (contrib is discarded)
        }

        // RUSSIAN ROULETTE - Probabilistic path termination for efficiency
        // Terminate paths with low contribution probability to reduce computation
        float rrPcont = min(max(payload.throughput.x, max(payload.throughput.y, payload.throughput.z)) + 0.001F, 0.95F);
        if(rand(payload.seed) >= rrPcont)
            return;                // Terminate paths with low throughput (won't contribute much)
        payload.throughput /= rrPcont;  // Boost energy of surviving paths to maintain unbiased estimation
        
        // BSDF SAMPLING - Generate next ray direction for indirect lighting
        {
            BsdfSampleData sampleData;
            sampleData.k1 = -WorldRayDirection();                              // Incoming direction
            sampleData.xi = float3(rand(payload.seed), rand(payload.seed), rand(payload.seed));  // Random numbers for sampling

            // Sample BSDF to get next ray direction and evaluate BSDF/PDF ratio
            bsdfSampleSimple(sampleData, pbrMat);

            // Check if ray was absorbed (path termination)
            if(sampleData.event_type == BSDF_EVENT_ABSORB)
            {
                return;  // Terminate path - no more bounces
            }

            // Update path throughput with BSDF/PDF ratio (Monte Carlo estimator)
            payload.throughput *= sampleData.bsdf_over_pdf;

            // Set up next ray with slight offset to avoid self-intersection
            RayDesc newRay;
            newRay.Origin    = offsetRay(worldPos, worldNormal);
            newRay.Direction = sampleData.k2;
            newRay.TMin      = 0.001;
            newRay.TMax      = INFINITE;

            TraceRay(topLevelAS, 0, 0xff, 0,0,0, newRay, payload);
        }
    }
}

// ------------------------------------------------------------
// Miss Shader
// ------------------------------------------------------------
[shader("miss")]
void missMain(inout HitPayload payload)
{
    SceneData* scene = pushConst.sceneData;

    // Get ray direction in world space
    float3 rayDir = WorldRayDirection();
    rayDir = normalize(rayDir);

    // Sample skybox
    float3 envColor = float3(0,0,0);
    if(scene->useSky && scene->skyBoxIndex != 0)
    {
        envColor = cubes[scene->skyBoxIndex].SampleLevel(rayDir, 0.0).rgb;
    }
    else 
    {
        // Use simple solid background color
        envColor = float3(0,0,0); // sceneInfo.backgroundColor
    }

    // Add environment contribution weighted by path throughput and terminate
    payload.color +=  envColor * payload.throughput;

    // Set depth to miss
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

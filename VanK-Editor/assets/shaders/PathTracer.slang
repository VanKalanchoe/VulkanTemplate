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
    uint32_t skyBoxIndex;
    uint32_t frameIndex;
};
[[vk::push_constant]]
ConstantBuffer<PushConstant> pushConstant;

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
bool testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, float maxDistance)
{
    RayDesc shadowRay;
    shadowRay.Origin    = worldPos + worldNormal * 0.001;  // offset to avoid self-intersection
    shadowRay.Direction = normalize(lightDirection);
    shadowRay.TMin      = 0.001;
    shadowRay.TMax      = maxDistance; // can use maxDistance here for efficiency

    HitPayload shadowPayload;
    shadowPayload.depth = 0;

    TraceRay(topLevelAS,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0xff, 0, 0, 0,
             shadowRay, shadowPayload);

    // True = light blocked, False = light visible
    return shadowPayload.depth != MISS_DEPTH;
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

    SceneData scene = pushConstant.sceneData[0];
    uint32_t frame = pushConstant.frameIndex;

    // Initialize high-quality random number generator
    // Uses xxhash32 with pixel coordinates and frame number for good distribution
    uint seed = xxhash32(uint3(uint2(launchID.xy), frame));

    // ANTI-ALIASING: Subpixel jittering for temporal supersampling
    // First frame uses pixel center, subsequent frames use random subpixel positions
    const float2 subpixelJitter = frame == 0 ? float2(0.5, 0.5) : float2(rand(seed), rand(seed));
    const float2 pixelCenter = launchID + subpixelJitter; // Jittered pixel position
    const float2 clipCoords = pixelCenter / launchSize * 2.0 - 1.0;  // Convert to NDC [-1,1]
    const float4 viewCoords = mul(scene.projInverse, float4(clipCoords.x, 1.0 - clipCoords.y, 1.0, 1.0));  // Transform to view space

    // Generate primary camera ray
    RayDesc ray;
    ray.Origin = mul(scene.viewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space
    ray.Direction = mul(scene.viewInverse, float4(normalize(viewCoords.xyz), 0.0)).xyz; // Ray direction in world space
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
    InstanceLUT lut = pushConstant.instanceLutBuffer[NonUniformResourceIndex(instanceID)];

    uint i0 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 0)] + lut.firstVertex;
    uint i1 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 1)] + lut.firstVertex;
    uint i2 = pushConstant.indexBuffer[lut.indexBufferOffset + (triID * 3 + 2)] + lut.firstVertex;

    Vertex v0 = pushConstant.vertexBuffer[i0];
    Vertex v1 = pushConstant.vertexBuffer[i1];
    Vertex v2 = pushConstant.vertexBuffer[i2];

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = bary.x * v0.texcoords + bary.y * v1.texcoords + bary.z * v2.texcoords;
    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
    float3 worldPos = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(localPos,1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)pushConstant.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localN));
    if(dot(worldNormal, WorldRayDirection())>0) worldNormal = -worldNormal;

    Material mat = pushConstant.materialBuffer[NonUniformResourceIndex(lut.materialID)];

    // -------------------------------
    // Sample textures
    // -------------------------------
    float4 albedo = mat.diffuseFactor;
    if(mat.albedoTexture != 0) 
    {
        albedo *= texture[mat.albedoTexture].SampleLevel(uv,0.0);
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
       metallic *= mrT.b; roughness *= mrT.g;
    }

    // -------------------------------
    // Add emissive contribution scaled by throughput
    // -------------------------------
    payload.color += payload.throughput * emissive;

    float3 p0 = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v0.position,1)).xyz;
    float3 p1 = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v1.position,1)).xyz;
    float3 p2 = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(v2.position,1)).xyz;

    float3 geonrm = normalize(cross(p1 - p0, p2 - p0));

    // Ensure geometric normal faces against ray
    if(dot(geonrm, WorldRayDirection()) > 0) geonrm = -geonrm;
    
    PbrBaseMaterial pbrMat = initPbrBaseMaterial(albedo.rgb, metallic, roughness, worldNormal, geonrm);

    // -------------------------------
    // Recursive Monte Carlo bounce with NEE jitter
    // -------------------------------
    if(payload.depth < 5) // max depth
    {
        payload.depth += 1;

        // -------------------------------
        // Next Event Estimation (direct lighting)
        // -------------------------------
        uint numLights = 1;
        for(uint li = 0; li < numLights; li++)
        {
            Lights light = pushConstant.lightingBuffer->lights[li];

            // Sample a point on the light (area light or sphere light)
            // Add small random jitter per frame for anti-aliasing
            float3 lightPos = light.lightPosition;

            float3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            float3 L = normalize(toLight);

            // Cosine term for Lambertian diffuse
            float cosTheta = max(dot(L, worldNormal), 0.0);

            // Shadow ray
            RayDesc shadowRay;
            shadowRay.Origin = worldPos + worldNormal * 0.001;
            shadowRay.Direction = L;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = dist - 0.001;

            bool visible = !testShadow(worldPos, worldNormal, L, dist);

            // Add direct lighting contribution
            if(visible)
            {
                float3 albedo = mat.diffuseFactor.rgb;
                if(mat.albedoTexture != 0) 
                    albedo *= texture[mat.albedoTexture].SampleLevel(float2(uv),0.0).rgb;

                payload.color += payload.throughput * albedo * light.lightColor * cosTheta / (dist*dist);
            }
        }

          // -------------------------------
    // Russian Roulette termination
    // -------------------------------
    float3 throughputMax = payload.throughput;
    float p = max(throughputMax.r, max(throughputMax.g, throughputMax.b));
    if(payload.depth >= 3) // start RR after a few bounces
    {
        if(rand(payload.seed) > p)
        {
            // terminate path
            return;
        }
        payload.throughput /= p; // compensate
    }

        // -------------------------------
        // Indirect lighting (diffuse bounce)
        // -------------------------------
        float3 newDir = SampleHemisphere(worldNormal, payload.seed);

        RayDesc newRay;
        newRay.Origin = worldPos + worldNormal * 0.001;
        newRay.Direction = newDir;
        newRay.TMin = 0.001;
        newRay.TMax = INFINITE;

        HitPayload newPayload;
        newPayload.color = float3(0,0,0);
        float cosThetaBounce = max(dot(newDir, worldNormal), 0.0);
        float3 albedo = mat.diffuseFactor.rgb;
        if(mat.albedoTexture != 0) albedo *= texture[mat.albedoTexture].SampleLevel(float2(uv),0.0).rgb;
        newPayload.throughput = payload.throughput * albedo * cosThetaBounce / 3.14159265;
        newPayload.depth = payload.depth;
        newPayload.seed = payload.seed;

        TraceRay(topLevelAS, 0, 0xff, 0,0,0, newRay, newPayload);

        // Accumulate result
        payload.color += newPayload.color;
    }
}

// ------------------------------------------------------------
// Miss Shader
// ------------------------------------------------------------
[shader("miss")]
void missMain(inout HitPayload payload)
{
    // Get ray direction in world space
    float3 rayDir = WorldRayDirection();
    rayDir = normalize(rayDir);

    // Sample skybox
    float3 envColor = float3(0,0,0);
    if(pushConstant.skyBoxIndex != 0)
    {
        envColor = cubes[pushConstant.skyBoxIndex].SampleLevel(rayDir, 0.0).rgb;
    }
    else 
    {
        // Use simple solid background color
        envColor = float3(1, 0, 1); // sceneInfo.backgroundColor
    }

    // Add environment contribution weighted by path throughput and terminate
    payload.color += payload.throughput * envColor;

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

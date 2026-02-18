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
    // Pixel coordinates
    int2 pixel = int2(DispatchRaysIndex().xy);
    float2 launchSize = float2(DispatchRaysDimensions().xy);

    // Initialize RNG seed per pixel & frame
    uint seed = xxhash32(uint3(pixel.xy, pushConstant.frameIndex));

    // Subpixel jitter
    float2 subpixelJitter = pushConstant.frameIndex == 0 ? float2(0.5, 0.5) : float2(rand(seed), rand(seed));

    // Compute NDC with jitter
    float2 ndc;
    ndc.x = (pixel.x + subpixelJitter.x) / launchSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - ((pixel.y + subpixelJitter.y) / launchSize.y * 2.0);

    // Camera setup
    SceneData scene = pushConstant.sceneData[0];
    float4 viewPos = mul(scene.projInverse, float4(ndc, 1.0, 1.0));
    float3 rayDir = normalize(viewPos.xyz / viewPos.w);
    float3 rayOrigin = mul(scene.viewInverse, float4(0,0,0,1)).xyz;
    rayDir = mul(scene.viewInverse, float4(rayDir,0)).xyz;

    // Prepare primary ray
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = INFINITE;

    // Initialize payload
    HitPayload payload;
    payload.color = float3(0,0,0);
    payload.depth = 0;
    payload.seed = seed;
    payload.throughput = float3(1,1,1);

    // Trace the ray
    TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, ray, payload);

     // FIREFLY CLAMPING
    float lum = dot(payload.color, float3(0.212671, 0.715160, 0.072169)); // standard luminance
    const float fireflyClampThreshold = 10.0;
    if(lum > fireflyClampThreshold)
    {
        payload.color *= fireflyClampThreshold / lum; // scale down extreme values
    }

    // Progressive accumulation
    float3 prevColor = outImage[pixel].rgb;
    float frame = float(pushConstant.frameIndex);
    float3 accumColor = (prevColor * frame + payload.color) / (frame + 1.0);

    // Write accumulated color
    outImage[pixel] = float4(accumColor, 1.0);
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

    // Return the environment color multiplied by the throughput
    payload.color = payload.throughput * envColor;

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

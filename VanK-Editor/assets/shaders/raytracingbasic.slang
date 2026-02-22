#include "shaderIO.h"
#include "pbr.h.slang"
#include "nvshaders/random.h.slang"
import common;

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

// ----------------------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
// ----------------------------------------------------------------------------
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}   
// ----------------------------------------------------------------------------
float3 PBRNeutralToneMapping( float3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return lerp(color, newPeak * float3(1, 1, 1), g);
}
// ----------------------------------------------------------------------------
float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
// ----------------------------------------------------------------------------
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
// ----------------------------------------------------------------------------
float3 ToEnvSpace(float3 v)
{
    // Vulkan world → Blender cubemap space
    return float3(v.x, v.y, -v.z);
}

float3 tosrgb(float3 c)
{
	return pow(c.xyz, float3(1.0 / 2.2));
}

float4 tosrgb(float4 c)
{
	return float4(pow(c.xyz, float3(1.0 / 2.2)), c.w);
}

float3 fromsrgb(float3 c)
{
	return pow(c.xyz, float3(2.2));
}

float4 fromsrgb(float4 c)
{
	return float4(pow(c.xyz, float3(2.2)), c.w);
}

static const float PI = 3.14159265359;

//-----------------------------------------------------------------------
// SHADOW TESTING
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, float maxDistance)
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
  return shadowPayload.depth != MISS_DEPTH ? 0.2 : 1.0;
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
    color = PBRNeutralToneMapping(color);
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
    //not always needed the flip only dds i think
/*     uv.y = 1.0 - uv.y; */
    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN   = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
    float3 worldPos = mul(pushConstant.meshDrawBuffer->meshDraw[instanceID].modelMatrix, float4(localPos, 1.0)).xyz;
    float3 Ng = normalize(mul((float3x3)pushConstant.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localN));
    float3 V = -WorldRayDirection();
    bool isInside = dot(Ng, V) < 0.0;

    if (isInside) {
        Ng = -Ng;
    }

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

    float ambientOcclusion = mat.ambientOcclusionFactor;
    if(mat.ambientOcclusionTexture != 0) ambientOcclusion *= texture[mat.ambientOcclusionTexture].SampleLevel(uv, 0.0).r;

    // --- 2. TBN & NORMAL MAPPING ---
    float4 localT4 = bary.x * v0.tangents + bary.y * v1.tangents + bary.z * v2.tangents;
    float3 worldTangent = normalize(mul((float3x3)pushConstant.meshDrawBuffer->meshDraw[instanceID].normalMatrix, localT4.xyz));
    if (isInside) {
    worldTangent = -worldTangent;
}
    float3 worldBitangent = normalize(cross(Ng, worldTangent)) * localT4.w;
    float3x3 TBN = float3x3(worldTangent, worldBitangent, Ng);

    float3 N;
    if (mat.normalTexture != 0) {
        float3 mapN = texture[mat.normalTexture].SampleLevel(uv, 0.0).rgb * 2.0 - 1.0;
        N = normalize(mul(mapN, TBN));
    } else {
        N = Ng;
    }

    if (dot(N, V) < 0.0) N = -N;

    // --- 3. PBR CALCULATIONS ---
    float3 R = reflect(-V, N);
    float3 F0 = lerp(float3(0.04), albedo.rgb, metallic);

    
    // Only override F0 if specular extension exists
    // But only if material is mostly non-metallic
    if(any(mat.specularColorFactor != float3(1.0, 1.0, 1.0)) && metallic < 0.5)
    {
        F0 = mat.specularColorFactor; // use the specular color
    }

    // Roughness from glossiness (if SpecGloss)
    /* float roughnessFinal = roughness;
    if(mat.specularColorFactor.a > 0.0 && metallic < 0.5)
        roughnessFinal = 1.0 - mat.specularColorFactor.a; */

    // --- LIGHTING LOOP (Lo) ---
    float3 Lo = float3(0.0);
    for(int i = 0; i < 0; ++i) // Set to 1 or higher if you have lights in buffer
    {
        float3 L = normalize(pushConstant.lightingBuffer->lights[i].position - worldPos);
        float3 H = normalize(V + L);
        float distance = length(pushConstant.lightingBuffer->lights[i].position - worldPos);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = pushConstant.lightingBuffer->lights[i].color * attenuation;

        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);      
        float3 F_schlick = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 specularBRDF = (NDF * G * F_schlick) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
        float3 kS_light = F_schlick;
        float3 kD_light = (float3(1.0) - kS_light) * (1.0 - metallic);

        Lo += (kD_light * albedo.rgb / PI + specularBRDF) * radiance * max(dot(N, L), 0.0) * testShadow(worldPos, Ng, L, distance);
    }

    // --- IBL (Ambient) ---
    float3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);
    
    float3 irradiance = cubes[pushConstant.sceneData->irradianceTexture].SampleLevel(ToEnvSpace(N), 0.0).rgb;
    float3 prefilteredColor = cubes[pushConstant.sceneData->prefilteredTexture].SampleLevel(ToEnvSpace(R), roughness * log2(512)).rgb;
    float2 brdf = texture[pushConstant.sceneData->brdflutTexture].SampleLevel(float2(max(dot(N, V), 0.0), roughness), 0.0).rg;
    
    float3 diffuseIBL = irradiance * albedo.rgb;
    float3 specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);
    float3 ambient = (kD_ibl * diffuseIBL + specularIBL) * ambientOcclusion;

    float3 finalSurfaceColor = ambient + Lo + emissive;

    // --- 4. TRANSPARENCY LOGIC ---

    // A. Physical Refraction (Windows)
    if(mat.transmissionFactor > 0.0 && payload.depth < 5) // Increased depth
    {
        float3 I = WorldRayDirection();
        float3 Nf = Ng; // Already flipped in your code above

        // 1. Fresnel
        float cosTheta = clamp(dot(-I, Nf), 0.0, 1.0);
        // Use the actual IOR of the material if possible
        float F0_glass = abs((1.0 - mat.ior) / (1.0 + mat.ior));
        F0_glass *= F0_glass; 
        float F_glass = F0_glass + (1.0 - F0_glass) * pow(1.0 - cosTheta, 5.0);

        // 2. Transmission Ray (Through the glass)
        HitPayload transPayload = {float3(0), 1.0, payload.depth + 1};
        // Use a smaller offset to avoid skipping thin objects behind the glass
        RayDesc transRay;
        transRay.Origin = worldPos + I * 0.0001; 
        transRay.Direction = I;
        transRay.TMin = 0.0001;
        transRay.TMax = 10000.0;
        TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, transRay, transPayload);

        // 3. Reflection Ray (Mirror look)
        HitPayload reflPayload = {float3(0), 1.0, payload.depth + 1};
        RayDesc reflRay;
        reflRay.Origin = worldPos + Nf * 0.0001;
        reflRay.Direction = reflect(I, Nf);
        reflRay.TMin = 0.0001;
        reflRay.TMax = 10000.0;
        TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, reflRay, reflPayload);

        // 4. Combine
        // We mix the background (transmission) with the mirror (reflection)
        // AND we can still add a bit of the local emissive/Lo if desired
        float3 transmissionResult = transPayload.color * albedo.rgb;
        payload.color = lerp(transmissionResult, reflPayload.color, F_glass) + (emissive);
        return;
    }

    // B. Hardcoded Blending (Pictures/Posters)
    // Since everything is RED (Opaque), we force a through-ray if alpha is low
    if (albedo.a < 0.98 && payload.depth < 5) 
    {
        RayDesc thruRay = { worldPos + WorldRayDirection() * 0.001, 0.001, WorldRayDirection(), 10000.0 };
        HitPayload thruPayload = { float3(0), 1.0, payload.depth + 1 };
        TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, thruRay, thruPayload);

        // MIRROR KILL: If it's a poster, dampen the specular/IBL by the alpha.
        // This stops it from looking like a mirror window on a wall.
        float3 posterSurface = (kD_ibl * diffuseIBL) + (specularIBL * albedo.a) + Lo + emissive;
        
        payload.color = lerp(thruPayload.color, posterSurface, albedo.a) + emissive;
        return;
    }

    // C. Default Opaque (Walls, Floors, etc.)
    payload.color = finalSurfaceColor;
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

    float3 dir = ToEnvSpace(normalize(WorldRayDirection()));
  
    float3 envColor = cubes[pushConstant.skyBoxIndex].SampleLevel(dir, 0.0).rgb;
  
    payload.color = envColor/*  * payload.weight */;

    payload.depth = MISS_DEPTH;
}

//-----------------------------------------------------------------------
// ANYHIT - With Transparency Support
//-----------------------------------------------------------------------
[shader("anyhit")]
void anyhitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
/* if(payload.depth > 1) return; */

  // Find where the ray hit
  float3 pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

  // Get material properties to apply color tinting
  uint                  instanceID = InstanceIndex();
  uint triID      = PrimitiveIndex();
  InstanceLUT lut = pushConstant.instanceLutBuffer[NonUniformResourceIndex(instanceID)];

   // 2. Get meshlet primitive info for this instance
    uint indexOffset = lut.indexBufferOffset;
    uint vertexOffset = lut.firstVertex;

    // 3. Map triangle index to global vertex indices
    uint i0 = pushConstant.indexBuffer[indexOffset + (triID * 3 + 0)] + vertexOffset;
    uint i1 = pushConstant.indexBuffer[indexOffset + (triID * 3 + 1)] + vertexOffset;
    uint i2 = pushConstant.indexBuffer[indexOffset + (triID * 3 + 2)] + vertexOffset;

    // 4. Fetch vertices
    Vertex v0 = pushConstant.vertexBuffer[i0];
    Vertex v1 = pushConstant.vertexBuffer[i1];
    Vertex v2 = pushConstant.vertexBuffer[i2];

    // 5. Interpolate attributes using barycentrics
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv       = bary.x * v0.texcoords + bary.y * v1.texcoords + bary.z * v2.texcoords;

    float3 localPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    float3 localN   = normalize(bary.x * v0.normals + bary.y * v1.normals + bary.z * v2.normals);
  uint materialID = lut.materialID;
  Material mat = pushConstant.materialBuffer[NonUniformResourceIndex(materialID)];

    // 1. OPAQUE: Do nothing, hardware handles the hit.
    if (mat.alphaMode == 0) return;

    // 2. BLEND: Usually we return and let ClosestHit handle the alpha-lerp.
    if (mat.alphaMode == 2 || mat.transmissionFactor > 0.0) return;

    // 3. MASK (Cutout): Check the alpha against the specific cutoff.
    if (mat.alphaMode == 1) 
    {
        float4 albedo = mat.diffuseFactor;
        if (mat.albedoTexture != 0) 
            albedo *= texture[mat.albedoTexture].SampleLevel(uv, 0.0);

        // Use the actual cutoff from the JSON (usually 0.5)
        if (albedo.a < mat.alphaCutoff) 
        {
            IgnoreHit(); // The ray passes through as if nothing was there.
        }
    }
}

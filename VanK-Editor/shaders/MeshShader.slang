#include "shaderIO.h"
import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float4 worldPos : TEXCOORD0;
    float2 texcoords : TEXCOORD1;
    float3 normals : TEXCOORD2;
    float4 tangents : TEXCOORD3;
    uint32_t materialIndex : TEXCOORD4;
};

struct CulledData
{
    Atomic<uint32_t> frustumCulled;
    Atomic<uint32_t> backfaceCulled;
    Atomic<uint32_t> totalCulled;
}

struct PushConstant 
{
    SceneData* sceneData;
    CulledData* culledData;
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

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;
const static uint MAX_VERTICES = 64;
const static uint MAX_PRIMITIVES = 96;


// Per https://developer.nvidia.com/blog/using-mesh-shaders-for-professional-graphics/
// Task shader output should ideally be below 236/108. We can hit the 108 if we turn uint32_t to uint8_t and store offsets in the payload.
// 4 + 64 = 68
struct MeshletPayload 
{
    uint drawID;
    uint primitiveID;
    uint32_t groupMeshletOffset;

    uint8_t meshletIndices[TASK_SHADER_DISPATCH_X];
};

groupshared MeshletPayload sharedPayload;
groupshared Atomic<uint32_t> visibleMeshletCount;

[shader("task")]
[numthreads(TASK_SHADER_DISPATCH_X, 1, 1)]
void taskMain
(
    uint drawID : SV_DrawIndex, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform PushConstant pushConstant
)
{
    uint drawIndex = drawID;
    uint primitiveID = (uint)pushConstant.meshDrawBuffer->meshDraw[drawIndex].primitiveID;
    uint32_t groupMeshletOffset = (gid.x * TASK_SHADER_DISPATCH_X);
    uint32_t totalMeshlets = pushConstant.meshletPrimitive[primitiveID].meshletCount; // currently Drawn meshletCount

    // thread 0 populates shared information for mesh shader
    if (gtid.x == 0) 
    {
        visibleMeshletCount.store(0);
        sharedPayload.groupMeshletOffset = groupMeshletOffset;
    }

    GroupMemoryBarrierWithGroupSync();

    // group thread count is only 64, will never exceed uint8
    uint8_t threadMeshletOffset = (uint8_t)gtid.x;
    
    uint groupMeshletCount = min(TASK_SHADER_DISPATCH_X, totalMeshlets - groupMeshletOffset);

    if (threadMeshletOffset < groupMeshletCount) 
    {
        uint32_t meshletIndex = groupMeshletOffset + threadMeshletOffset + pushConstant.meshletPrimitive[primitiveID].meshletOffset; // offset to the previous one in the global buffer
        Meshlet meshlet = pushConstant.meshletBuffer->meshlets[meshletIndex];

        // Transform to world space
        float3 worldCenter = mul(pushConstant.meshDrawBuffer->meshDraw[drawIndex].modelMatrix, float4(meshlet.meshletBoundingSphere.xyz, 1.0)).xyz;
        float4 worldBounds = float4(worldCenter, meshlet.meshletBoundingSphere.w);

        bool visible = true;

        // Frustum Cull
        if(pushConstant.sceneData->FrustumCullEnabled)
        {
            bool frustumVisible = true;
            for (uint i = 0; i < 6; i++) 
            {
                float dist = dot(pushConstant.sceneData.frozenFrustum.planes[i].xyz, worldBounds.xyz) + pushConstant.sceneData.frozenFrustum.planes[i].w;
                frustumVisible &= (dist >= -worldBounds.w);
            }
            visible &= frustumVisible;
            if (!frustumVisible) 
            {
                pushConstant.culledData.frustumCulled.add(1);
            }
        }
        
        // todo: send back to show how many culled from imgui
        if (visible) 
        {
            uint index = visibleMeshletCount.add(1);
            sharedPayload.meshletIndices[index] = threadMeshletOffset;
            sharedPayload.drawID = drawID;
            sharedPayload.primitiveID = primitiveID;
        } 
        else 
        {
            pushConstant.culledData->totalCulled.add(1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (gtid.x == 0) 
    {
        uint count = visibleMeshletCount.load();
        DispatchMesh(count, 1, 1, sharedPayload);
    }
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void meshMain
(
    in payload MeshletPayload sharedPayload,
    out indices uint3 triangles[MAX_PRIMITIVES], out vertices VertexOutput vertices[MAX_VERTICES],
    uint3 groupId : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform PushConstant pushConstant
)
{
    uint drawID = sharedPayload.drawID;
    uint primitiveID = sharedPayload.primitiveID;
    uint baseOffset = pushConstant.meshletPrimitive[primitiveID].meshletOffset;
    uint meshletIdx = baseOffset + sharedPayload.groupMeshletOffset + sharedPayload.meshletIndices[groupId.x];
    Meshlet meshlet = pushConstant.meshletBuffer->meshlets[meshletIdx];
    float4x4 viewProj = pushConstant.sceneData->viewProj;
    // to do make is a function for debug only
/*     uint hash = meshletIdx * 747796405u + 2891336453u;
    float3 hashcolor = float3
    (
        (hash & 0xFF) / 255.0,
        ((hash >> 8) & 0xFF) / 255.0,
        ((hash >> 16) & 0xFF) / 255.0
    );  * by color*/

    SetMeshOutputCounts(meshlet.meshletVerticesCount, meshlet.meshletTriangleCount);
    
    uint32_t instanceIndex = gtid.x;
    for (uint i = instanceIndex; i < meshlet.meshletVerticesCount; i += MESH_SHADER_DISPATCH_X) 
    {
        uint localVertexIndex = pushConstant.meshletVerticesBuffer->meshletVertices[meshlet.meshletVerticesOffset + i].vertexIndex;
        Vertex v = pushConstant.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        
        float4 worldPos = mul(pushConstant.meshDrawBuffer->meshDraw[drawID].modelMatrix, float4(v.position, 1.0));
        float4 clipPos = mul(viewProj, worldPos);
        
        vertices[i].position = clipPos;
        vertices[i].worldPos = worldPos;
        vertices[i].texcoords = v.texcoords;
        vertices[i].normals = mul(pushConstant.meshDrawBuffer->meshDraw[drawID].normalMatrix, v.normals); // might need rotation of model ?
        vertices[i].tangents = v.tangents;
        vertices[i].materialIndex = pushConstant.meshletPrimitive[primitiveID].materialIndex;
    }

    for (uint i = instanceIndex; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) 
    {
        uint triOffset = meshlet.meshletTriangleOffset + i * 3;

        uint idx0 = pushConstant.meshletTrianglesBuffer->meshletTriangles[triOffset + 0].triangleIndex;
        uint idx1 = pushConstant.meshletTrianglesBuffer->meshletTriangles[triOffset + 1].triangleIndex;
        uint idx2 = pushConstant.meshletTrianglesBuffer->meshletTriangles[triOffset + 2].triangleIndex;

        triangles[i] = uint3(idx0, idx1, idx2);
    }
}

[[vk::binding(0, 0)]]
Sampler2D texture[];

[[vk::binding(0, 0)]]
SamplerCube cubes[];

// TASK04: Acceleration structure binding
[[vk::binding(0,2)]]
RaytracingAccelerationStructure accelerationStructure;

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
// ----------------------------------------------------------------------------
// Easy trick to get tangent-normals to world-space to keep PBR code simplified.
// Don't worry if you don't get what's going on; you generally want to do normal 
// mapping the usual way for performance anyways; I do plan make a note of this 
// technique somewhere later in the normal mapping tutorial.
float3 getNormalFromMap(uint32_t normalTextureIndex, float2 texCoord, float4 worldPos, float3 normals)
{
    // If no normal map (-1), just return the vertex normal in world space
    if (normalTextureIndex == 0)
    {
        return normalize(normals);
    }

    float3 tangentNormal = texture[normalTextureIndex].Sample(texCoord).rgb * 2.0 - 1.0;

    float3 Q1  = ddx(worldPos.rgb);
    float3 Q2  = ddy(worldPos.rgb);
    float2 st1 = ddx(texCoord); // ?
    float2 st2 = ddy(texCoord); // ?

    float3 N   = normalize(normals);
    float3 T  = normalize(Q1 * st2.y - Q2 * st1.y);
    float3 B  = -normalize(cross(N, T));
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(tangentNormal, TBN));
}
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
// ----------------------------------------------------------------------------

static const float3 lightDir = float3(-6.0, 6.0, 0.0);

// Small epsilon to avoid self-intersection
static const float EPSILON = 0.01;

float2 intersection_uv(uint instanceID, uint primIndex, float2 barycentrics, InstanceLUT* instanceLUTBuffer, Vertex* vertexBuffer, uint32_t* indexBuffer) {
    uint indexOffset = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].indexBufferOffset;
    uint vertexOffset = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].firstVertex;

    uint i0 = indexBuffer[indexOffset + (primIndex * 3 + 0)] + vertexOffset;
    uint i1 = indexBuffer[indexOffset + (primIndex * 3 + 1)] + vertexOffset;
    uint i2 = indexBuffer[indexOffset + (primIndex * 3 + 2)] + vertexOffset;

    float2 uv0 = vertexBuffer[i0].texcoords;
    float2 uv1 = vertexBuffer[i1].texcoords;
    float2 uv2 = vertexBuffer[i2].texcoords;

    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    float w1 = barycentrics.x;
    float w2 = barycentrics.y;

    return w0 * uv0 + w1 * uv1 + w2 * uv2;
}

void apply_reflection(float3 P, float3 N, inout float4 baseColor, InstanceLUT* instanceLUTBuffer, Vertex* vertexBuffer, uint32_t* indexBuffer, Material* materialBuffer, float4 camPos) {
    // Build the reflections ray
    float3 V = normalize(camPos.xyz - P);
    float3 R = reflect(-V, N);

    RayDesc reflectionRayDesc;
    reflectionRayDesc.Origin = P;
    reflectionRayDesc.Direction = R;
    reflectionRayDesc.TMin = EPSILON;
    reflectionRayDesc.TMax = 1e4;

    // Initialize a ray query for reflections
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

    rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, reflectionRayDesc);

    while (rq.Proceed())
    {
        uint instanceID = rq.CandidateRayInstanceCustomIndex();
        uint primIndex = rq.CandidatePrimitiveIndex();

        float2 uv = intersection_uv(instanceID, primIndex, rq.CandidateTriangleBarycentrics(), instanceLUTBuffer, vertexBuffer, indexBuffer);

        uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
        // Get the material from the material buffer
        Material mat = materialBuffer[NonUniformResourceIndex(materialID)];
        
        // Sample the albedo texture to check alpha - USE mat.albedoTexture, NOT materialID!
        float4 intersectionColor = mat.diffuseFactor;
        if (mat.albedoTexture != 0)
        {
            intersectionColor *= texture[NonUniformResourceIndex(mat.albedoTexture)].SampleLevel(uv, 0);
        }

        if (intersectionColor.a < 0.5) {
            // If the triangle is transparent, we continue to trace
            // to find the next opaque triangle.
        } else {
            // If we hit an opaque triangle, we stop tracing.
            rq.CommitNonOpaqueTriangleHit();
        }
    }

    bool hit = (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    if (hit)
    {
        uint instanceID = rq.CommittedRayInstanceCustomIndex();
        uint primIndex = rq.CommittedPrimitiveIndex();

        float2 uv = intersection_uv(instanceID, primIndex, rq.CandidateTriangleBarycentrics(), instanceLUTBuffer, vertexBuffer, indexBuffer);

        uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
        // Get the material from the material buffer
        Material mat = materialBuffer[NonUniformResourceIndex(materialID)];
        
        // Sample the albedo texture to check alpha - USE mat.albedoTexture, NOT materialID!
        float4 intersectionColor = mat.diffuseFactor;
        if (mat.albedoTexture != 0)
        {
            intersectionColor *= texture[NonUniformResourceIndex(mat.albedoTexture)].SampleLevel(uv, 0);
        }

        baseColor.rgb = lerp(baseColor.rgb, intersectionColor.rgb, 1);
    }
    else 
    {
        float3 R_env = ToEnvSpace(reflect(-normalize(camPos.xyz - P), N));
        float3 skyColor = cubes[5].Sample(R_env).rgb;

        baseColor.rgb = lerp(baseColor.rgb, skyColor, 1);
    }
}

// TASK05: Implement ray query shadows
bool in_shadow(float3 P, InstanceLUT* instanceLUTBuffer, Vertex* vertexBuffer, uint32_t* indexBuffer, Material* materialBuffer)
{
   // Build the shadow ray from the world position toward the light
    RayDesc shadowRayDesc;
    shadowRayDesc.Origin = P;
    shadowRayDesc.Direction = normalize(lightDir);
    shadowRayDesc.TMin = EPSILON;
    shadowRayDesc.TMax = 1e4;

    // Initialize a ray query for shadows
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

    sq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, shadowRayDesc);

    while (sq.Proceed())
    {
        uint instanceID = sq.CandidateRayInstanceCustomIndex();
        uint primIndex = sq.CandidatePrimitiveIndex();

        float2 uv = intersection_uv(instanceID, primIndex, sq.CandidateTriangleBarycentrics(), instanceLUTBuffer, vertexBuffer, indexBuffer);
        
        // Get material ID from instance LUT
        uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
        
        // Get the material from the material buffer
        Material mat = materialBuffer[NonUniformResourceIndex(materialID)];
        
        // Sample the albedo texture to check alpha - USE mat.albedoTexture, NOT materialID!
        float4 intersectionColor = mat.diffuseFactor;
        if (mat.albedoTexture != 0)
        {
            intersectionColor *= texture[NonUniformResourceIndex(mat.albedoTexture)].SampleLevel(uv, 0);
        }

        if (intersectionColor.a < 0.5) {
            // If the triangle is transparent, we continue to trace
            // to find the next opaque triangle.
        } else {
            // If we hit an opaque triangle, we stop tracing.
            sq.CommitNonOpaqueTriangleHit();
        }
    }

    // If the shadow ray intersects an opaque triangle, we consider the pixel in shadow
    bool hit = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    return hit;
}

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput, uniform PushConstant pushConstant)
{
    Material mats = pushConstant.materialBuffer[vertexInput.materialIndex];

    float3 emissive = mats.emissiveFactor; // constant color
    if (mats.emissiveTexture != 0)
    {
        emissive *= /* fromsrgb */(texture[mats.emissiveTexture].Sample(vertexInput.texcoords)).rgb;
    }

    // Base color + texture
    float4 albedo = mats.diffuseFactor;
    if (mats.albedoTexture != 0) // when converting i did al ltextures srgb idk if thats good
        albedo *= /* fromsrgb */(texture[mats.albedoTexture].Sample(vertexInput.texcoords)); // could be wrong srgb or not

    float3 P = vertexInput.worldPos.xyz;
    float3 NR = vertexInput.normals.xyz;
    
    // Alpha test
    if(albedo.a < 0.5)
        discard;

    if(mats.metallicFactor > 0)
        apply_reflection(P, NR, albedo, pushConstant.instanceLutBuffer, pushConstant.vertexBuffer, pushConstant.indexBuffer, pushConstant.materialBuffer, pushConstant.sceneData.cameraWorldPos);

    bool inShadow = in_shadow(P, pushConstant.instanceLutBuffer, pushConstant.vertexBuffer, pushConstant.indexBuffer, pushConstant.materialBuffer);
    
    // Darken if in shadow
    if(inShadow)
    {
        albedo.rgb *= 0.2;
    }

    float metallic = mats.metallicFactor;
    float roughness = mats.roughnessFactor;
    float4 mrT;
    if(mats.metallicRoughnessTexture != 0)
    {
       mrT = texture[mats.metallicRoughnessTexture].Sample(vertexInput.texcoords);
       metallic  *= mrT.b;
       roughness *= mrT.g;
    }

    float ambientOcclusion = mats.ambientOcclusionFactor;
    if(mats.ambientOcclusionFactor != 0)
    {
        float4 aoT = texture[mats.ambientOcclusionTexture].Sample(vertexInput.texcoords);
        ambientOcclusion *= aoT.r;
    }

  /*float3 N = normalize(vertexInput.normals); */
    float3 N = getNormalFromMap(mats.normalTexture, vertexInput.texcoords, vertexInput.worldPos, vertexInput.normals);
    float3 V = normalize(pushConstant.sceneData->cameraWorldPos.xyz - vertexInput.worldPos.xyz);
    float3 R = reflect(-V, N);

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0 
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)  
    float3 F0 = float3(0.04); // todo read above
    F0 = lerp(F0, albedo.rgb, metallic);

    // reflectance equation
    float3 Lo = float3(0.0);
    for(int i = 0; i < 0; ++i)
    {
        // calculate per-light radiance
        float3 L = normalize(pushConstant.lightingBuffer->lights[i].lightPosition - vertexInput.worldPos.xyz);
        float3 H = normalize(V + L);
        float distance = length(pushConstant.lightingBuffer->lights[i].lightPosition - vertexInput.worldPos.xyz);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = pushConstant.lightingBuffer->lights[i].lightColor * attenuation;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);      
        float3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator    = NDF * G * F; 
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
        float3 specular = numerator / denominator;

        // kS is equal to Fresnel
        float3 kS = F;
        // for energy conservation, the diffuse and specular light can't
        // be above 1.0 (unless the surface emits light); to preserve this
        // relationship the diffuse component (kD) should equal 1.0 - kS.
        float3 kD = float3(1.0) - kS;
        // multiply kD by the inverse metalness such that only non-metals 
        // have diffuse lighting, or a linear blend if partly metal (pure metals
        // have no diffuse light).
        kD *= 1.0 - metallic;

        // scale light by NdotL
        float NdotL = max(dot(N, L), 0.0);        

        // add to outgoing radiance Lo
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
    }

    // ambient lighting (we now use IBL as the ambient term)
    float3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    /* N.z = -N.z; */
    float3 N_env = ToEnvSpace(N);
    float3 irradiance = cubes[pushConstant.sceneData->irradianceTexture].Sample(N_env).rgb;
    float3 diffuse = irradiance * albedo.rgb;

    // sample both the pre-filter map and the BRDF lut and combine them together as per the Split-Sum approximation to get the IBL specular part.
    const float MAX_REFLECTION_LOD = log2(512); // log2(prefilterresolution)
    float3 R_env = ToEnvSpace(R);
    float3 prefilteredColor = cubes[pushConstant.sceneData->prefilteredTexture].SampleLevel(R_env, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = texture[pushConstant.sceneData->brdflutTexture].Sample(float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    float iblIntensity = 1.0;
    float3 ambient = (kD * diffuse + specular) * ambientOcclusion * iblIntensity;

    float3 color = ambient + Lo + emissive;

    float exposure = 1.0; // start here, tweak in UI later

    // HDR tonemapping
    color = color / (color + float3(1.0));
    /* color = Uncharted2Tonemap(color * exposure);
	color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx)); */
   /*  color = PBRNeutralToneMapping(color); */
    /* color = ACESFilm(color); */

    // gamma correct already because of srgb swapchain format
    /* color = pow(color, float3(1.0/2.2));  */
    
    //normalize(vertexInput.normals) * 0.5 + 0.5

    return float4(albedo.rgb, 1.0);
}
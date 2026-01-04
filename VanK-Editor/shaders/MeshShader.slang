import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 texcoords : TEXCOORD1;
    uint32_t materialIndex : TEXCOORD2;
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
    VertexData* vertexBuffer;
    MeshletVerticesData* meshletVerticesBuffer;
    MeshletTrianglesData* meshletTrianglesBuffer;
    MeshletData* meshletBuffer;
    MeshletPrimitiveData* meshletPrimitive;
    MeshDrawData* meshDrawBuffer;
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
    uint32_t totalMeshlets = pushConstant.meshletPrimitive->primitives[primitiveID].meshletCount; // currently Drawn meshletCount

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
        uint32_t meshletIndex = groupMeshletOffset + threadMeshletOffset + pushConstant.meshletPrimitive->primitives[primitiveID].meshletOffset; // offset to the previous one in the global buffer
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
    uint baseOffset = pushConstant.meshletPrimitive->primitives[primitiveID].meshletOffset;
    uint meshletIdx = baseOffset + sharedPayload.groupMeshletOffset + sharedPayload.meshletIndices[groupId.x];
    Meshlet meshlet = pushConstant.meshletBuffer->meshlets[meshletIdx];
    float4x4 viewProj = pushConstant.sceneData->viewProj;
    // to do make is a function for debug only
    uint hash = meshletIdx * 747796405u + 2891336453u;
    float3 color = float3
    (
        (hash & 0xFF) / 255.0,
        ((hash >> 8) & 0xFF) / 255.0,
        ((hash >> 16) & 0xFF) / 255.0
    );

    SetMeshOutputCounts(meshlet.meshletVerticesCount, meshlet.meshletTriangleCount);
    
    uint32_t instanceIndex = gtid.x;
    for (uint i = instanceIndex; i < meshlet.meshletVerticesCount; i += MESH_SHADER_DISPATCH_X) 
    {
        uint localVertexIndex = pushConstant.meshletVerticesBuffer->meshletVertices[meshlet.meshletVerticesOffset + i].vertexIndex;
        Vertex v = pushConstant.vertexBuffer->vertices[meshlet.vertexOffset + localVertexIndex];
        
        float4 worldPos = mul(pushConstant.meshDrawBuffer->meshDraw[drawID].modelMatrix, float4(v.position, 1.0));
        float4 clipPos = mul(viewProj, worldPos);

        vertices[i].position = clipPos;
        vertices[i].color = float4(color, 1.0);
        vertices[i].texcoords = v.texcoords;
        vertices[i].materialIndex = pushConstant.meshletPrimitive->primitives[primitiveID].materialIndex;
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

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput)
{
  float4 textureColor = texture[vertexInput.materialIndex].Sample(vertexInput.texcoords);
  float4 hashColor = float4(vertexInput.color.xyz, 1.0f);
  return textureColor /* * hashColor */; // make debug only key
}
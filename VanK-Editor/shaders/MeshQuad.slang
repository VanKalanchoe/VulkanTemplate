import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 texcoords : TEXCOORD1;
    uint32_t materialIndex : TEXCOORD2;
};

struct PushConstant 
{
    uint64_t numOfQuads;
    QuadData* quadData;
    SceneData* sceneData;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct QuadPayLoad
{
    uint quadID;
};

[shader("task")]
[numthreads(TASK_SHADER_DISPATCH_X, 1, 1)]
void taskMain(
    uint3 gid : SV_GroupID, uint3 groupThreadID: SV_GroupThreadID,
    uniform PushConstant pushConstant
)
{
    // globalThreadID = thread index in group + offset from group
    uint globalThreadID = gid.x;

    // safety check
    if (globalThreadID >= pushConstant.numOfQuads) return;

    // create payload for this quad
    QuadPayLoad payload;
    payload.quadID = globalThreadID;

    // dispatch exactly 1 mesh per quad
    DispatchMesh(1, 1, 1, payload);
}

static const float4 positions[4] = 
{
    float4(-0.5, -0.5, 0.0, 1.0),  // bottom-left
    float4( 0.5, -0.5, 0.0, 1.0),  // bottom-right
    float4( 0.5,  0.5, 0.0, 1.0),  // top-right
    float4(-0.5,  0.5, 0.0, 1.0)   // top-left
};

static const float2 uv[4] = 
{
    float2(0.0, 0.0), // bottom-left
    float2(1.0, 0.0), // bottom-right
    float2(1.0, 1.0), // top-right
    float2(0.0, 1.0)  // top-left
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain
(
    in payload QuadPayLoad sharedPayload,
    out indices uint3 triangles[2], out vertices VertexOutput vertices[4],
    uniform PushConstant pushConstant
)
{
    uint quadID = sharedPayload.quadID;
    float4x4 mvp = mul(pushConstant.sceneData->viewProj, pushConstant.quadData->quads[quadID].modelMatrix);

	SetMeshOutputCounts(4, 2); // 4 vertices, 2 triangles

	for (uint i = 0; i < 4; i++) 
    {
		vertices[i].position = mul(mvp, positions[i]);
		vertices[i].color = pushConstant.quadData->quads[quadID].color;
        vertices[i].texcoords = uv[i];
        vertices[i].materialIndex = pushConstant.quadData->quads[quadID].materialIndex;
	}

	triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}

[[vk::binding(0, 0)]]
Sampler2D texture[];

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput)
{
    float4 textureColor = texture[vertexInput.materialIndex].Sample(vertexInput.texcoords);
    return textureColor * vertexInput.color;
}
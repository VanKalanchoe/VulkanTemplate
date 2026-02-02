import common;

struct VertexOutput
{
    float4 position : SV_Position;
    int entityID : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct PushConstant 
{
    uint64_t numOfElements;
    LineData* lineData;
    SceneData* sceneData;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct QuadPayLoad
{
    uint lineID;
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
    if (globalThreadID >= pushConstant.numOfElements) return;

    // create payload for this quad
    QuadPayLoad payload;
    payload.lineID = globalThreadID;

    // dispatch exactly 1 mesh per quad
    DispatchMesh(1, 1, 1, payload);
}

[shader("mesh")]
[outputtopology("line")]
[numthreads(1, 1, 1)]
void meshMain
(
    in payload QuadPayLoad sharedPayload,
    out indices uint2 lines[1], out vertices VertexOutput vertices[2],
    uniform PushConstant pushConstant
)
{
    uint lineID = sharedPayload.lineID;

	SetMeshOutputCounts(2, 1); // 2 vertices, 1 line

    // Vertex 0
    vertices[0].position = mul(pushConstant.sceneData->viewProj, float4(pushConstant.lineData->lines[lineID].P0, 1.0));
    vertices[0].color = pushConstant.lineData->lines[lineID].Color;
    vertices[0].entityID = lineID;

    // Vertex 1
    vertices[1].position = mul(pushConstant.sceneData->viewProj, float4(pushConstant.lineData->lines[lineID].P1, 1.0));
    vertices[1].color = pushConstant.lineData->lines[lineID].Color;
    vertices[1].entityID = lineID;

    // Line uses first 2 vertices
    lines[0] = uint2(0, 1); // third index ignored
}

struct PixelOutput
{
    float4 color : SV_TARGET0; // Final color output (RGBA)
    int entityID : SV_TARGET1;
};

[shader("pixel")]
PixelOutput fragmentMain(VertexOutput vertexInput)
{
    PixelOutput output;

    output.color = vertexInput.color;

    // Output entity ID to second render target (R32_SINT format)
    output.entityID = vertexInput.entityID;

   return output;
}
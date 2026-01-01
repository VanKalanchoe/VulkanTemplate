import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float thickness : TEXCOORD1;
    float3 localPosition : TEXCOORD2;
    float fade : TEXCOORD3;
};

struct PushConstant 
{
    uint64_t numOfElements;
    CircleData* circleData;
    SceneData* sceneData;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct QuadPayLoad
{
    uint circleID;
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
    payload.circleID = globalThreadID;

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
    uint circleID = sharedPayload.circleID;
    float4x4 mvp = mul(pushConstant.sceneData->viewProj, pushConstant.circleData->circles[circleID].worldPosition);

	SetMeshOutputCounts(4, 2); // 4 vertices, 2 triangles

	for (uint i = 0; i < 4; i++) 
    {
		vertices[i].position = mul(mvp, positions[i]);
		vertices[i].color = pushConstant.circleData->circles[circleID].color;
        vertices[i].thickness = pushConstant.circleData->circles[circleID].thickness;
        vertices[i].fade = pushConstant.circleData->circles[circleID].fade;
        float2 lp = positions[i].xy * 2.0;
        vertices[i].localPosition = float3(lp, 0.0);;
	}

	triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}

[[vk::binding(0, 0)]]
Sampler2D texture[];

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput)
{
    // Calculate distance and fill circle with white
    float distance = 1.0 - length(vertexInput.localPosition);
    float circle = smoothstep(0.0, vertexInput.fade, distance);
    circle *= smoothstep(vertexInput.thickness + vertexInput.fade, vertexInput.thickness, distance);

    if(circle == 0.0)
        discard;

    // Set output color
    float4 color = vertexInput.color;
    color.a *= circle; 

    return color;
}
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
    column_major float4x4 modelMatrix;
    SceneData* sceneData;
};

const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct DummyPayLoad
{
    uint dummyData;
};

[shader("task")]
[numthreads(1, 1, 1)]
void taskMain()
{
    DummyPayLoad localPayload;
    DispatchMesh(1, 1, 1, localPayload);
}

static const float4 positions[3] = 
{
    float4( 0.0, -0.5, 0.0, 1.0),  // bottom
    float4( 0.5,  0.5, 0.0, 1.0),  // top-right
    float4(-0.5,  0.5, 0.0, 1.0)   // top-left
};

static const float4 colors[3] =
{
    float4(1, 0, 0, 1),
    float4(0, 1, 0, 1),
    float4(0, 0, 1, 1)
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain
(
    out indices uint3 triangles[1], out vertices VertexOutput vertices[3],
    uint3 DispatchThreadID : SV_DispatchThreadID, uint3 groupId : SV_GroupID,
    uniform PushConstant pushConstant
)
{
    uint meshletID = groupId.x;

    float4x4 mvp = mul(pushConstant.sceneData.viewProj, pushConstant.modelMatrix);

	SetMeshOutputCounts(3, 1);

	for (uint i = 0; i < 3; i++) 
    {
		vertices[i].position = mul(mvp, positions[i]);
		vertices[i].color = colors[i];
	}

	triangles[0] = uint3(0, 1, 2);
}

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput)
{
  return vertexInput.color;
}
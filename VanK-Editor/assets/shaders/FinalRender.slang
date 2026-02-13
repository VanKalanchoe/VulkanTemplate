import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoords : TEXCOORD1;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct QuadPayLoad
{
};

[shader("task")]
[numthreads(TASK_SHADER_DISPATCH_X, 1, 1)]
void taskMain(
    uint3 gid : SV_GroupID, uint3 groupThreadID: SV_GroupThreadID
)
{
    // create payload for this quad
    QuadPayLoad payload;

    // dispatch exactly 1 mesh per quad
    DispatchMesh(1, 1, 1, payload);
}

static const float4 positions[4] = 
{
    float4(-1, -1, 0.0, 1.0),  // bottom-left
    float4( 1, -1, 0.0, 1.0),  // bottom-right
    float4( 1,  1, 0.0, 1.0),  // top-right
    float4(-1,  1, 0.0, 1.0)   // top-left
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
    out indices uint3 triangles[2], out vertices VertexOutput vertices[4]
)
{
	SetMeshOutputCounts(4, 2); // 4 vertices, 2 triangles

	for (uint i = 0; i < 4; i++) 
    {
		vertices[i].position = positions[i];
        vertices[i].texcoords = uv[i];
	}

	triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}

struct PixelOutput
{
    float4 color : SV_TARGET0;
};

[[vk::binding(2, 2)]]
Sampler2D sceneImage;

[[vk::binding(3, 2)]]
Sampler2D rayTraceImage;

[shader("pixel")]
PixelOutput fragmentMain(VertexOutput vertexInput)
{
     PixelOutput output;

    // Flip Y for Vulkan if needed
    float2 uv = float2(vertexInput.texcoords.x, 1.0 - vertexInput.texcoords.y);

    float4 rayColor   = rayTraceImage.Sample(uv); // 3D PBR scene = background
    float4 sceneColor = sceneImage.Sample(uv);   // 2D overlay = on top

    // Alpha-over: scene on top of raytrace
    float3 blendedRGB = rayColor.rgb * (1.0 - sceneColor.a) + sceneColor.rgb * sceneColor.a;

    output.color = float4(blendedRGB, 1.0); // fully opaque final
    return output;
}
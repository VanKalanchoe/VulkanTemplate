import common;

#include "nvshaders/tonemap_functions.h.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoords : TEXCOORD1;
};

struct DummyPayLoad
{
    uint dummyData;
};

groupshared DummyPayLoad dummyPayLoad;

[shader("task")]
[numthreads(1, 1, 1)]
void taskMain()
{
    DispatchMesh(1, 1, 1, dummyPayLoad);
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
void meshMain(out indices uint3 triangles[2], out vertices VertexOutput vertices[4])
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

    float4 color;

    float2 uv = float2(vertexInput.texcoords.x, 1.0 - vertexInput.texcoords.y);

    float4 rayColor   = rayTraceImage.Sample(uv);
    float4 sceneColor = sceneImage.Sample(uv);

    float3 blendedRGB = rayColor.rgb * (1.0 - sceneColor.a) + sceneColor.rgb * sceneColor.a;

    color = float4(tonemapACES(blendedRGB.rgb), 1.0);

    output.color = color;
    return output;
}
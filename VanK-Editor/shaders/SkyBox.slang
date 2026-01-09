import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 WorldPos : TEXCOORD0;
};

struct PushConstant 
{
    uint32_t materialIndex;
    SceneData* sceneData;
};

static const float4 positions[8] = 
{
    float4(-1, -1, -1, 1), // 0
    float4( 1, -1, -1, 1), // 1
    float4( 1,  1, -1, 1), // 2
    float4(-1,  1, -1, 1), // 3
    float4(-1, -1,  1, 1), // 4
    float4( 1, -1,  1, 1), // 5
    float4( 1,  1,  1, 1), // 6
    float4(-1,  1,  1, 1)  // 7
};


[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void meshMain(
    out indices uint3 triangles[36],
    out vertices VertexOutput vertices[8],
    uniform PushConstant pushConstant
)
{
    SetMeshOutputCounts(8, 12);

    // copy vertices and world positions
    for (uint i = 0; i < 8; i++)
    {
        vertices[i].WorldPos = positions[i].xyz;

        // Remove translation from view (like a skybox)
        float3x3 rot3x3 = (float3x3)pushConstant.sceneData->view;
        float4x4 rotView = float4x4(
            float4(rot3x3[0], 0),
            float4(rot3x3[1], 0),
            float4(rot3x3[2], 0),
            float4(0,0,0,1)
        );

        float4 clipPos = mul(pushConstant.sceneData->proj, mul(rotView, float4(positions[i].xyz, 1.0)));

        // skybox trick: make depth = 1.0 (farthest)
        vertices[i].position = float4(clipPos.xy, 0.0, clipPos.w);
    }

    static const uint3 cubeTriangles[12] = {
        /* uint3(0,2,1), uint3(0,3,2),  */// car view
    /* uint3(4,5,6), uint3(4,6,7), */ // stairs
   /*  uint3(4,1,5), uint3(4,0,1), */ // floor
    /* uint3(3,6,2), uint3(3,7,6), */ // roof
   /*  uint3(0,5,1), uint3(0,4,5),  */// different floor
    /* uint3(3,6,7), uint3(3,2,6) */ // different roof
      // -Z (back)
    uint3(0, 1, 2),
    uint3(0, 2, 3),

    // +Z (front)
    uint3(4, 6, 5),
    uint3(4, 7, 6),

    // -X (left)
    uint3(4, 5, 1),
    uint3(4, 1, 0),

    // +X (right)
    uint3(3, 2, 6),
    uint3(3, 6, 7),

    // +Y (top)
    uint3(3, 7, 4),
    uint3(3, 4, 0),

    // -Y (bottom)
    uint3(1, 5, 6),
    uint3(1, 6, 2),
    };

    for (uint i = 0; i < 12; i++)
        triangles[i] = cubeTriangles[i];
}

[[vk::binding(0, 0)]]
SamplerCube texture[];

[shader("pixel")]
float4 fragmentMain(VertexOutput vertexInput, uniform PushConstant pushConstant)
{
    float3 dir = vertexInput.WorldPos;
    dir.z = -dir.z;

    float3 envColor = texture[pushConstant.materialIndex].SampleLevel(dir, 0.0).rgb;

    // HDR tonemap and gamma correct
    envColor = envColor / (envColor + float3(1.0));
  /*   envColor = pow(envColor, float3(1.0/2.2));  */

    return float4(envColor, 1.0); // pink
}
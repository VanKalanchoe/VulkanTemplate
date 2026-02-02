import common;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    uint32_t materialIndex : TEXCOORD1;
    int entityID : TEXCOORD2;
    float4 color : TEXCOORD3;
};

struct PushConstant 
{
    uint64_t numOfElements;
    TextData* textData;
    SceneData* sceneData;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;
const static int32_t MESH_SHADER_DISPATCH_X = 32;

struct QuadPayLoad
{
    uint textID;
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
    payload.textID = globalThreadID;

    // dispatch exactly 1 mesh per quad
    DispatchMesh(1, 1, 1, payload);
}

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
    uint textID = sharedPayload.textID;
    

	SetMeshOutputCounts(4, 2); // 4 vertices, 2 triangles

    
	for (uint i = 0; i < 4; i++) 
    {
        // Determine which corner of the quad this vertex corresponds to
        uint quadVertex = i;
        float2 pos;
        float2 uv;

        // Correct winding order and flip X if needed
        if (quadVertex == 0) { pos = float2(pushConstant.textData->texts[textID].quadMax.x, pushConstant.textData->texts[textID].quadMin.y); uv = float2(pushConstant.textData->texts[textID].texMax.x, pushConstant.textData->texts[textID].texMin.y); } // bottom-right
        else if (quadVertex == 1) { pos = pushConstant.textData->texts[textID].quadMax; uv = pushConstant.textData->texts[textID].texMax; }                                                // top-right
        else if (quadVertex == 2) { pos = float2(pushConstant.textData->texts[textID].quadMin.x, pushConstant.textData->texts[textID].quadMax.y); uv = float2(pushConstant.textData->texts[textID].texMin.x, pushConstant.textData->texts[textID].texMax.y); } // top-left
        else { pos = pushConstant.textData->texts[textID].quadMin; uv = pushConstant.textData->texts[textID].texMin; }                                                                    // bottom-left

        float4 worldPos = mul(pushConstant.textData->texts[textID].Transform, float4(pos, 0.0, 1.0));
		vertices[i].position = mul(pushConstant.sceneData->viewProj, worldPos);
        vertices[i].texCoord = uv;
        vertices[i].color = pushConstant.textData->texts[textID].color;
        vertices[i].materialIndex =  pushConstant.textData->texts[textID].materialIndex;
        vertices[i].entityID = pushConstant.textData->texts[textID].entityID;
	}

	triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}

[[vk::binding(0, 0)]]
Sampler2D texture[];

struct PixelOutput
{
    float4 color : SV_TARGET0; // Final color output (RGBA)
    int entityID : SV_TARGET1;
};

float screenPxRange(Sampler2D atlas, float2 uv) 
{
    uint w, h;
    atlas.GetDimensions(w, h);
	const float pxRange = 2.0; // set to distance field's pixel range
    float2 unitRange = float2(pxRange)/float2(w, h);
    float2 screenTexSize = float2(1.0)/fwidth(uv);
    return max(0.5*dot(unitRange, screenTexSize), 1.0);
}

[shader("pixel")]
PixelOutput fragmentMain(VertexOutput vertexInput)
{
    PixelOutput output;

   float4 texColor = vertexInput.color * texture[vertexInput.materialIndex].Sample(vertexInput.texCoord);

   float3 msd = texture[vertexInput.materialIndex].Sample(vertexInput.texCoord).rgb;
   float sd = median3(msd.r, msd.g, msd.b);

   float screenPxDistance = screenPxRange(texture[vertexInput.materialIndex], vertexInput.texCoord) * (sd - 0.5);
   float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
   if(opacity == 0.0)
        discard;

   float4 bgColor = float4(0.0);
   output.color = lerp(bgColor, vertexInput.color, opacity);
   if(output.color.a == 0.0)
        discard;

    // Output entity ID to second render target (R32_SINT format)
    output.entityID = vertexInput.entityID;

   return output;
}
// Slang
import common;
static const float4 positions[3] = {
  float4( 0.0, -1.0, 0.0, 1.0),
  float4(-1.0, 1.0, 0.0, 1.0),
  float4( 1.0, 1.0, 0.0, 1.0)
};

struct VertexOutput
{
  float4 position : SV_Position;
  float4 color : TEXCOORD0;
};

struct DummyPayLoad
{
  uint dummyData;
};

// Hardcoded push constant values for now
static const float4x4 hardcodedModelMatrix = float4x4(
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1
);

static const float zoom = 0.1; // smaller -> zoomed out
static const float4x4 hardcodedViewProj = float4x4(
    zoom, 0,    0, 0,
    0,    zoom, 0, 0,
    0,    0,    1, 0,
    0,    0,    0, 1
);

[shader("task")]
[numthreads(1, 1, 1)]
void taskMain()
{
  DummyPayLoad localPayload;
  DispatchMesh(3, 1, 1, localPayload);
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain(out vertices VertexOutput vertices[3], out indices uint3 triangles[1],
  uint3 DispatchThreadID : SV_DispatchThreadID, uint3 groupId : SV_GroupID)
{
  float4x4 mvp = mul(hardcodedViewProj, hardcodedModelMatrix);
  uint meshletID = groupId.x;
  // Hash-based per-meshlet coloring for visualization
  uint hash = meshletID * 747796405u + 2891336453u;
  float3 color = float3(
    (hash & 0xFF) / 255.0,
    ((hash >> 8) & 0xFF) / 255.0,
    ((hash >> 16) & 0xFF) / 255.0
  );

  float4 offset = float4(float(DispatchThreadID.x) * 1.5, 0.0, 0.0, 0.0);
  SetMeshOutputCounts(3, 1);
  for (uint i = 0; i < 3; i++) {
    vertices[i].position = mul(mvp, positions[i] + offset);
    vertices[i].color = float4(color, 1.0f);
  }

  triangles[0] = uint3(0, 1, 2);
}

[shader("pixel")]
float4 fragmentMain(VertexOutput input)
{
  return input.color;
}
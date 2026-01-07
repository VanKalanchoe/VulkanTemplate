module common;

public struct Vertex
{
    public float3 position;
    public float2 texcoords;
    public float3 normals;
    public float4 tangents;
}

public struct Meshlet
{
    public float4 meshletBoundingSphere;

    public float3 coneApex;
    public float coneCutoff;

    public float3 coneAxis;
    public uint32_t vertexOffset;

    public uint32_t meshletVerticesOffset;
    public uint32_t meshletTriangleOffset;
    public uint32_t meshletVerticesCount;
    public uint32_t meshletTriangleCount;
};

public struct MeshletPrimitive
{
    public uint32_t meshletOffset;
    public uint32_t meshletCount;
    public uint32_t materialIndex;
    public uint32_t bHasTransparent;
    public float4 boundingSphere;
};

public struct MeshDraw
{
    public column_major float4x4 modelMatrix;
    public uint64_t  primitiveID;
};

public struct MeshletVertexIndex 
{
    public uint32_t vertexIndex;
}

public struct MeshletTriangleIndex 
{
    public uint8_t triangleIndex;
}

public struct VertexData 
{
    public Vertex[] vertices;
};

public struct MeshletPrimitiveData 
{
    public MeshletPrimitive[] primitives;
}

public struct MeshDrawData 
{
    public MeshDraw[] meshDraw;
}

public struct MeshletData 
{
    public Meshlet[] meshlets;
};

public struct MeshletVerticesData 
{
    public MeshletVertexIndex[] meshletVertices;
};

public struct MeshletTrianglesData 
{
    public MeshletTriangleIndex[] meshletTriangles;
};

public struct Material
{
    public uint32_t albedoTexture;
    public uint32_t normalTexture;
    public uint32_t metallicRoughnessTexture;

    public uint32_t specularTexture;
    public uint32_t emissiveTexture;
    public uint32_t ambientOcclusionTexture;

    public float4 diffuseFactor;
    public float metallicFactor;
    public float roughnessFactor;

    public float4 specularFactor;
    public float3 emissiveFactor;
    public float ambientOcclusionFactor;

    public bool transparent;
    public float transmissionFactor;
};

public struct MaterialData
{
    public Material[] materials;
}

public struct Quads
{
    public column_major float4x4 modelMatrix;
    public float4 color;
    public uint32_t materialIndex;
    public int entityID;
};

public struct QuadData
{
    public Quads[] quads;
}

public struct Circles
{
    public column_major float4x4 worldPosition;
    public float4 color;
    public float thickness;
    public float fade;
    public int entityID;
};

public struct CircleData
{
    public Circles[] circles;
}

public struct Texts
{
    public column_major float4x4 Transform;
    public float2 quadMin;
    public float2 quadMax;
    public float2 texMin;
    public float2 texMax;
    public float4 color;
    public uint32_t materialIndex;

    // TODO: bg color for outline/bg

    // Editor-only
    public int entityID;
};

public struct TextData
{
    public Texts[] texts;
}

public struct Lines
{
    public float3 P0;
    public float3 P1;
    public float4 Color;

    // Editor-only
    public int EntityID;
};

public struct LineData
{
    public Lines[] lines;
}

public struct Frustum
{
    public float4 planes[6];
};

public struct SceneData
{
    public column_major float4x4 view;
    public column_major float4x4 proj;
    public column_major float4x4 viewProj;

    public column_major float4x4 frozenView;
    public column_major float4x4 frozenProj;
    public column_major float4x4 frozenViewProj;

    public float4 cameraWorldPos;
    public float4 frozenCameraWorldPos;

    public Frustum frustum;
    public Frustum frozenFrustum;

    public float2 renderTargetSize;
    float2 texelSize;

    public float deltaTime;

    public bool FrustumCullEnabled;
};


public struct Lights
{
    public float3 lightPosition;
    public float3 lightColor;
};

public struct LightData
{
    public Lights[] lights;
}
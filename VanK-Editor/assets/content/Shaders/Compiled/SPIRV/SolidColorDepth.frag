Texture2D<float4> Texture : register(t0, space2);
SamplerState Sampler : register(s0, space2);

//play button video cherno min 5 he fixed that white texture bug something its interpolated even tho it hsouldnt
cbuffer UBO : register(b0, space3)
{
    float NearPlane;
    float FarPlane;
};

struct Input
{
    float2 TexCoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float4 Position : SV_Position;
    int entityID : TEXCOORD2;
};

struct Output
{
    float4 Color : SV_Target0;
    int Color2 : SV_Target1;
    float Depth : SV_Depth;
};

float LinearizeDepth(float depth, float near, float far)
{
    float z = depth * 2.0 - 1.0;
    return ((2.0 * near * far) / (far + near - z * (far - near))) / far;
}

Output main(Input input)
{
    Output result;
    result.Color = input.Color * Texture.Sample(Sampler, input.TexCoord);
    result.Color2 = input.entityID;
    result.Depth = LinearizeDepth(input.Position.z, NearPlane, FarPlane);
    return result;
}
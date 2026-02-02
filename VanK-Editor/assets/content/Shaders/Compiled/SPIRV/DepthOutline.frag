Texture2D ColorTexture : register(t0, space2);
SamplerState ColorSampler : register(s0, space2);

Texture2D DepthTexture : register(t1, space2);
SamplerState DepthSampler : register(s1, space2);

// Gets the difference between a depth value and adjacent depth pixels
float GetDifference(float depth, float2 TexCoord, float distance)
{
    float w, h;
    DepthTexture.GetDimensions(w, h);
    
    return
        abs(DepthTexture.Sample(DepthSampler, TexCoord + float2(1.0 / w, 0) * distance).r - depth) +
        abs(DepthTexture.Sample(DepthSampler, TexCoord + float2(-1.0 / w, 0) * distance).r - depth) +
        abs(DepthTexture.Sample(DepthSampler, TexCoord + float2(0, 1.0 / h) * distance).r - depth) +
        abs(DepthTexture.Sample(DepthSampler, TexCoord + float2(0, -1.0 / h) * distance).r - depth);
}

// Function to detect edges based on alpha channel (transparency)
float GetAlphaDifference(float alpha, float2 TexCoord, float distance)
{
    float w, h;
    ColorTexture.GetDimensions(w, h);
    
    return
        abs(ColorTexture.Sample(ColorSampler, TexCoord + float2(1.0 / w, 0) * distance).a - alpha) +
        abs(ColorTexture.Sample(ColorSampler, TexCoord + float2(-1.0 / w, 0) * distance).a - alpha) +
        abs(ColorTexture.Sample(ColorSampler, TexCoord + float2(0, 1.0 / h) * distance).a - alpha) +
        abs(ColorTexture.Sample(ColorSampler, TexCoord + float2(0, -1.0 / h) * distance).a - alpha);
}

float4 main(float2 TexCoord : TEXCOORD0) : SV_Target0
{
    // Get the color, alpha, and depth value
    float4 color = ColorTexture.Sample(ColorSampler, TexCoord);
    float depth = DepthTexture.Sample(DepthSampler, TexCoord).r;
    float alpha = color.a;

    // Get depth-based edges
    float edge = GetDifference(depth, TexCoord, 1.0f);
    float edge2 = GetDifference(depth, TexCoord, 2.0f);

    // Get alpha-based edges (for transparent texture boundaries)
    float alphaEdge = GetAlphaDifference(alpha, TexCoord, 1.0f);
    float alphaEdge2 = GetAlphaDifference(alpha, TexCoord, 2.0f);

    // Combine both depth and alpha-based edges
    float combinedEdge = max(edge, alphaEdge);
    float combinedEdge2 = max(edge2, alphaEdge2);

    // Edge thresholding (fine-tune this value to control the sensitivity)
    combinedEdge = step(0.01, combinedEdge);
    combinedEdge2 = step(0.01, combinedEdge2);

    // Ensure outlines are only drawn where the texture is visible (i.e., where alpha > 0)
    combinedEdge *= step(0.01, alpha);
    combinedEdge2 *= step(0.01, alpha);

    // Turn inner edges black and outer edges white
    float3 resultColor = lerp(color.rgb, 0.0f, combinedEdge2); // Inner black edge
    resultColor = lerp(resultColor, 1.0f, combinedEdge);       // Outer white edge

    // Preserve the original alpha change this back then outline is back
    //return float4(resultColor, color.a);
    return float4(color.rgb, color.a);
}

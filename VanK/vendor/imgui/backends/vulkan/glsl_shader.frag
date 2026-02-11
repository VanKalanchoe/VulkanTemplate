#version 450 core
layout(location = 0) out vec4 fColor;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct {
    vec4 Color;
    vec2 UV;
} In;

void main()
{
    vec4 texColor = texture(sTexture, In.UV.st);

    // Convert ImGui vertex color from sRGB → linear
    vec3 linearVertex = pow(In.Color.rgb, vec3(2.2));

    fColor = vec4(linearVertex, In.Color.a) * texColor;
}

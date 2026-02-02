#version 450

// Uniform block for MultiplyColor
layout(set = 3, binding = 0) uniform UniformColor {
    vec4 MultiplyColor;
};

// Uniform block for MultiplyColor
layout(set = 3, binding = 1) uniform UniformTilingFactor {
    float TilingFactor;
};

// Sampler and Texture bindings
layout(set = 2, binding = 0) uniform sampler2D Texture; // Texture binding
layout(set = 2, binding = 1) uniform sampler Sampler;    // Sampler binding

// Input variable (texture coordinates)
layout(location = 0) in vec2 TexCoord;  // location(0) for texture coordinates

// Output variable (color output)
layout(location = 0) out vec4 FragColor; // location(0) for the fragment output

void main()
{
    // Sample the texture at the given coordinates
    vec4 textureColor = texture(Texture, TexCoord);
    
    // Multiply the texture color by the uniform color
    FragColor = MultiplyColor * textureColor * TilingFactor;
}
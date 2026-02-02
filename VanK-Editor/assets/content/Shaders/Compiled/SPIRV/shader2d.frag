#version 450

// Declare the texture and sampler as combined image sampler
layout(set = 2, binding = 0) uniform sampler2D Texture;

// Declare the input variables
layout(location = 0) in vec2 inTexCoord;  // TEXCOORD0
layout(location = 1) in vec4 inColor;     // TEXCOORD1

// Output color
layout(location = 0) out vec4 fragColor;

void main() {
    // Sample the texture and multiply it with the input color
    fragColor = inColor * texture(Texture, inTexCoord);
}
#version 450

// Define the uniform block for the transformation matrix
layout(set = 1, binding = 0) uniform UniformBlock {
    mat4 MatrixTransform;
};

// Define the input structure
layout(location = 0) in vec4 inPosition;  // TEXCOORD0 in HLSL
layout(location = 1) in vec2 inTexCoord;  // TEXCOORD1 in HLSL
layout(location = 2) in vec4 inColor;     // TEXCOORD2 in HLSL

// Define the output structure
layout(location = 0) out vec2 outTexCoord;  // TEXCOORD0 in output
layout(location = 1) out vec4 outColor;     // TEXCOORD1 in output

void main() {
    // Pass through the texture coordinates and color
    outTexCoord = inTexCoord;
    outColor = inColor;

    // Transform the position using the matrix
    gl_Position = MatrixTransform * inPosition;
}
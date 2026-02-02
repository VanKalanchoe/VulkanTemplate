#version 450 core

layout(set = 1, binding = 0) uniform transform
{
    mat4 MatrixTransform;
};

layout(set = 1, binding = 1) uniform camera
{
    mat4 ViewProjection;
};

// Input variables
layout(location = 0) in vec4 Position;    // TEXCOORD0
layout(location = 1) in vec2 TexCoord;    // TEXCOORD1

// Output variables
layout(location = 0) out vec2 FragTexCoord;   // TEXCOORD0
layout(location = 1) out vec4 FragPosition;    // SV_Position

// Main function
void main()
{
    // Pass TexCoord to the output
    FragTexCoord = TexCoord;
    
    // Apply the transformation matrix to the position
    FragPosition = MatrixTransform * Position;
    
    // Set the output position for the fragment shader
    gl_Position = ViewProjection * FragPosition;
}
#include "Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

#include <SDL3/SDL_log.h>

#include "VanK/Core/Application.h"
#include "VanK/Core/Log.h"
#include "VanK/Core/Timer.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "MSDFData.h"
#include "RegistryMesh.h"
#include "VanK/Asset/AssetManager.h"

namespace VanK
{
    static std::vector<std::unique_ptr<filewatch::FileWatch<std::string>>> s_ShaderWatcher;
    static std::atomic<bool> s_IsPipelineReloadFinished = false;
    bool IsShaderReloadFinished = false;
    std::string changedFile;
    Timer ReloadTimer;
    
    struct Renderer3DData
    {
        MeshHandle vikingHandle;
        MeshHandle cubeHandle;
        MeshHandle quadHandle;
        MeshHandle circleHandle;
        MeshHandle textHandle;
        MeshHandle lineHandle;
        
        shaderio::SceneInfo SceneData;
        
        Ref<Texture2D> FontAtlasTexture;
    };
    static Renderer3DData s_Data;
    
    const std::string MODEL_PATH = "../build/VanK/models/viking_room.glb";
    
    void Renderer::loadModel()
    {
        // Use tinygltf to load the model instead of tinyobjloader
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);

        if (!warn.empty())
        {
            std::cout << "glTF warning: " << warn << std::endl;
        }

        if (!err.empty())
        {
            std::cout << "glTF error: " << err << std::endl;
        }

        if (!ret)
        {
            throw std::runtime_error("Failed to load glTF model");
        }

        vertices.clear();
        indices.clear();

        // Process all meshes in the model
        for (const auto& mesh : model.meshes)
        {
            for (const auto& primitive : mesh.primitives)
            {
                // Get indices
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

                // Get vertex positions
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
                const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

                // Get texture coordinates if available
                bool hasTexCoords = primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
                const tinygltf::Accessor* texCoordAccessor = nullptr;
                const tinygltf::BufferView* texCoordBufferView = nullptr;
                const tinygltf::Buffer* texCoordBuffer = nullptr;

                if (hasTexCoords)
                {
                    texCoordAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
                    texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
                }

                uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

                for (size_t i = 0; i < posAccessor.count; i++)
                {
                    shaderio::InstancedVertexData vertex{};

                    const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor
                        .byteOffset + i * 12]);
                    vertex.position = {pos[0], pos[1], pos[2]};

                    if (hasTexCoords)
                    {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->
                            byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        vertex.texcoords = {texCoord[0], texCoord[1]};
                    }
                    else
                    {
                        vertex.texcoords = {0.0f, 0.0f};
                    }

                    /*vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};*/

                    vertices.push_back(vertex);
                }

                const unsigned char* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
                size_t indexCount = indexAccessor.count;
                size_t indexStride = 0;

                // Determine index stride based on component type
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    indexStride = sizeof(uint16_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    indexStride = sizeof(uint32_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                {
                    indexStride = sizeof(uint8_t);
                }
                else
                {
                    throw std::runtime_error("Unsupported index component type");
                }

                indices.reserve(indices.size() + indexCount);

                for (size_t i = 0; i < indexCount; i++)
                {
                    uint32_t index = 0;

                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        index = *reinterpret_cast<const uint16_t*>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        index = *reinterpret_cast<const uint32_t*>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        index = *reinterpret_cast<const uint8_t*>(indexData + i * indexStride);
                    }

                    indices.push_back(baseVertex + index);
                }
            }
        }
    }
    
    void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
    {
        glm::mat4 View = glm::inverse(transform);
        glm::mat4 Proj = camera.GetProjection();
        
        s_Data.SceneData.view = View;
        s_Data.SceneData.proj = Proj;
    }
    
    void Renderer::BeginScene(const EditorCamera& camera)
    {
        glm::mat4 View = camera.GetViewMatrix();
        glm::mat4 Proj = camera.GetProjection();
        
        s_Data.SceneData.view = View;
        s_Data.SceneData.proj = Proj;
    }

    void Renderer::EndScene()
    {
        shaderio::InstancedPBRData inst1;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0,1,0)); // rotate around Y axis
        inst1.Model = model;
        inst1.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst1.textureIndex = vikingRoom->GetTextureIndex();
        inst1.EntityID = -1;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.vikingHandle, inst1);
        
        /*
        shaderio::InstancedPBRData inst2;
        inst2.Model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
        inst2.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst2.textureIndex = whiteTexture->GetTextureIndex();
        inst2.EntityID = -1;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.cubeHandle, inst2);
        
        shaderio::InstancedPBRData inst3;
        inst3.Model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 1.0f));
        inst3.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst3.textureIndex = vikingRoom->GetTextureIndex();
        inst3.EntityID = -1;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.vikingHandle, inst3);*/
    }

    void Renderer::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        shaderio::InstancedQuadData instance;
        instance.Model = transform;
        instance.color = color;
        instance.textureIndex = whiteTexture->GetTextureIndex();
        instance.EntityID = entityID;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_Quad, s_Data.quadHandle, instance);
    }

    void Renderer::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, int entityID)
    {
        shaderio::InstancedQuadData instance;
        instance.Model = transform;
        instance.color = tintColor;
        instance.textureIndex = texture->GetTextureIndex();
        instance.EntityID = entityID;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_Quad, s_Data.quadHandle, instance);
    }

    void Renderer::DrawSprite(const glm::mat4& transform, SpriteRendererComponent& src, int entityID)
    {
        if (src.Texture)
        {
            Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(src.Texture);
            DrawQuad(transform, texture, src.TilingFactor, src.Color, entityID);
        }
        else
        {
            DrawQuad(transform, src.Color, entityID);
        }
    }

    void Renderer::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness /*= 1.0f*/, float fade /*= 0.005f*/, int entityID /*= -1*/)
    {
        shaderio::InstancedCircleData instance;
        instance.WorldPosition = transform;
        instance.Color = color;
        instance.Thickness = thickness;
        instance.Fade = fade;
        instance.EntityID = entityID;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_Circle, s_Data.circleHandle, instance);
    }

    void Renderer::DrawString(const std::string& string, Ref<Font> font, const glm::mat4& transform, const TextParams& textParams, int entityID)
    {
        const auto& fontGeometry = font->GetMSDFData()->FontGeometry;
        const auto& metrics = fontGeometry.getMetrics();
        Ref<Texture2D> fontAtlas = font->GetAtlasTexture();
        
        s_Data.FontAtlasTexture = fontAtlas;
        
        double x = 0.0;
        double fsScale = 1.0 / (metrics.ascenderY - metrics.descenderY);
        double y = 0.0;
        
        const float spaceGlyphAdvance = fontGeometry.getGlyph(' ')->getAdvance();
        
        for (size_t i = 0; i < string.size(); i++)
        {
            char character = string[i];
            if (character == '\r')
                continue;
            
            if (character == '\n')
            {
                x = 0;
                y -= fsScale * metrics.lineHeight * textParams.LineSpacing;
                continue;
            }
            
            if (character == ' ')
            {
                float advance = spaceGlyphAdvance;
                if (i < string.size() - 1)
                {
                    char nextCharacter = string[i + 1];
                    double dAdvance;
                    fontGeometry.getAdvance(dAdvance, character, nextCharacter);
                    advance = (float)dAdvance;
                }
                
                x += fsScale * advance + textParams.Kerning;
                continue;
            }
            
            if (character == '\t')
            {
                // NOTE(Yan): is this right?
                x += 4.0f * (fsScale * spaceGlyphAdvance + textParams.Kerning);
                continue;
            }
            
            auto glyph = fontGeometry.getGlyph(character);
            if (!glyph)
                glyph = fontGeometry.getGlyph('?');
            if (!glyph)
                return;
            
            double al, ab, ar, at;
            glyph->getQuadAtlasBounds(al, ab, ar, at);
            glm::vec2 texCoordMin((float)al, (float)ab);
            glm::vec2 texCoordMax((float)ar, (float)at);

            double pl, pb, pr, pt;
            glyph->getQuadPlaneBounds(pl, pb, pr, pt);
            glm::vec2 quadMin((float)pl, (float)pb);
            glm::vec2 quadMax((float)pr, (float)pt);

            quadMin *= fsScale, quadMax *= fsScale;
            quadMin += glm::vec2(x, y);
            quadMax += glm::vec2(x, y);

            float texelWidth = 1.0f / fontAtlas->GetWidth();
            float texelHeight = 1.0f / fontAtlas->GetHeight();
            texCoordMin *= glm::vec2(texelWidth, texelHeight);
            texCoordMax *= glm::vec2(texelWidth, texelHeight);
            
            // render here
            shaderio::InstancedTextData instance;
            instance.QuadMin = quadMin;
            instance.QuadMax = quadMax;
            instance.Transform = transform;                // store transform per draw
            instance.TexMin = texCoordMin;
            instance.TexMax = texCoordMax;
            instance.Color = textParams.Color;
            instance.TextureIndex = fontAtlas->GetTextureIndex();
            instance.EntityID = entityID;

            RegistryMesh::registerInstance(shaderio::PipelineType_Text, s_Data.textHandle, instance);
            
            if (i < string.size() - 1)
            {
                double advance = glyph->getAdvance();
                char nextCharacter = string[i + 1];
                fontGeometry.getAdvance(advance, character, nextCharacter);
                
                x += fsScale * advance + textParams.Kerning;
            }
        }
    }

    void Renderer::DrawString(const std::string& string, const glm::mat4& transform, const TextComponent& component, int entityID)
    {
        DrawString(string, component.FontAsset, transform, { component.Color, component.Kerning, component.LineSpacing }, entityID);
    }

    void Renderer::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID)
    {
        shaderio::InstancedLineData instance;
        instance.P0 = p0;
        instance.P1 = p1;
        instance.Color = color;
        instance.EntityID = entityID;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_Line, s_Data.lineHandle, instance);
    }

    void Renderer::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID)
    {
        glm::vec3 p0 = glm::vec3(position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p1 = glm::vec3(position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p2 = glm::vec3(position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z);
        glm::vec3 p3 = glm::vec3(position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z);

        DrawLine(p0, p1, color, entityID);
        DrawLine(p1, p2, color, entityID);
        DrawLine(p2, p3, color, entityID);
        DrawLine(p3, p0, color, entityID);
    }

    void Renderer::DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        glm::vec3 p0 = transform * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f);
        glm::vec3 p1 = transform * glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f);
        glm::vec3 p2 = transform * glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f);
        glm::vec3 p3 = transform * glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f);

        DrawLine(p0, p1, color, entityID);
        DrawLine(p1, p2, color, entityID);
        DrawLine(p2, p3, color, entityID);
        DrawLine(p3, p0, color, entityID);
    }

    void Renderer::Init(Window& window)
    {
        RendererAPI::Config config;
        config.window = window.getWindowHandle();
        m_window = window.getWindowHandle();
        RenderCommand::SetConfig(config);
        RenderCommand::Init();

        // Shader creation
        auto DrawIndirectShader = GetShaderLibrary().Load("DrawIndirectShader", "DrawIndirectShader.slang");
        auto PBRShader = GetShaderLibrary().Load("PBRShader", "PBRShader.slang");
        auto QuadShader = GetShaderLibrary().Load("QuadShader", "QuadShader.slang");
        auto CircleShader = GetShaderLibrary().Load("CircleShader", "CircleShader.slang");
        auto TextShader = GetShaderLibrary().Load("TextShader", "TextShader.slang");
        auto LineShader = GetShaderLibrary().Load("LineShader", "LineShader.slang");
        auto MeshShader = GetShaderLibrary().Load("MeshShader", "MeshShader.slang");
        
        // Pipeline Creation
        uint32_t useTexture = true;
        std::vector<VanKSpecializationMapEntries> mapEntries
        {
            {.constantID = 0, .offset = 0, .size = sizeof(uint32_t)}
        };
        //dont like needed to be like this because if reloadpipeline then it craashes because data in struct is not copied fk this
        VanKSpecializationInfo specInfo;
        specInfo.Data.resize(sizeof(uint32_t));
        std::memcpy(specInfo.Data.data(), &useTexture, sizeof(uint32_t));
        specInfo.MapEntries = mapEntries;

        VanKPipelineShaderStageCreateInfo ShaderStageCreateInfo
        {
            .specializationInfo = specInfo
        };

        /*BufferLayout DebugLayout
        {
            {ShaderDataType::Float3, "Position"},
            {ShaderDataType::Float3, "Color"},
            {ShaderDataType::Float2, "TexCoord"},
        };*/
        
        VanKPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo
        {
            .VanKBufferLayout = {}
        };

        VanKPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo
        {
            .VanKPrimitive = VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST // make it shorter maybe ??
        };

        VanKPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo
        {
            .VanKPolygon = VanK_POLYGON_MODE_FILL,
            .VanKFrontFace = VanK_FRONT_FACE_COUNTER_CLOCKWISE,
        };

        const std::vector<VanKPipelineColorBlendAttachmentState> ColorBlendAttachmentStates =
        {
            {
                .blendEnable = true,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT | VanK_COLOR_COMPONENT_G_BIT | VanK_COLOR_COMPONENT_B_BIT | VanK_COLOR_COMPONENT_A_BIT,
            },
            {
                .blendEnable = false,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_ONE,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ZERO,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT,
            }, 
        };

        VanKPipelineColorBlendStateCreateInfo ColorBlendStateCreateInfo
        {
            .logicOp = false,
            .VanKLogicOp = VanK_LOGIC_OP_COPY,
            .VanKColorBlendAttachmentState = ColorBlendAttachmentStates
        };

        VanKPipelineMultisampleStateCreateInfo MultisampleStateCreateInfo
        {
            .sampleCount = VanK_SAMPLE_COUNT_64_BIT,
            .sampleShadingEnable = true,
            .minSampleShading = 0.2f
        };

        VanKPipelineDepthStencilStateCreateInfo DepthStencilStateCreateInfo
        {
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .VanKdepthCompareOp = VanK_COMPARE_OP_LESS_OR_EQUAL
        };

        VanKPipelineRenderingCreateInfo RenderingCreateInfo
        {
            .VanKColorAttachmentFormats = { VanK_Format_B8G8R8A8Srgb, VanK_FORMAT_R32_SINT }
        };
        
        VanKPipelineLayoutCreateInfo PipelineLayoutCreateInfo
        {
            .PushConstants = { PushConstantRange{0, sizeof(uint32_t)} }
        };

        VanKGraphicsPipelineSpecification GraphicsPipelineSpecification
        {
            .PipelineType = VanK_Graphics,
            .ShaderStageCreateInfo = ShaderStageCreateInfo,
            .VertexInputStateCreateInfo = VertexInputStateCreateInfo,
            .InputAssemblyStateCreateInfo = InputAssemblyStateCreateInfo,
            .RasterizationStateCreateInfo = RasterizationStateCreateInfo,
            .ColorBlendStateCreateInfo = ColorBlendStateCreateInfo,
            .MultisampleStateCreateInfo = MultisampleStateCreateInfo,
            .DepthStateInfo = DepthStencilStateCreateInfo,
            .RenderingCreateInfo = RenderingCreateInfo,
            .PipelineLayoutInfo = PipelineLayoutCreateInfo,
        };
        
        m_GraphicsPBRPipelineSpecification = GraphicsPipelineSpecification;
        m_GraphicsPBRPipelineSpecification.ShaderStageCreateInfo.VanKShader = PBRShader;
        m_GraphicsPBRPipeline = RenderCommand::createGraphicsPipeline(m_GraphicsPBRPipelineSpecification);
        RegisterPipelineForShaderWatcher("PBRShader", "PBRShader.slang", &m_GraphicsPBRPipelineSpecification, nullptr, &m_GraphicsPBRPipeline, VanKGraphics);

        m_GraphicsQuadPipelineSpecification = GraphicsPipelineSpecification;
        m_GraphicsQuadPipelineSpecification.ShaderStageCreateInfo.VanKShader = QuadShader;
        m_GraphicsQuadPipeline = RenderCommand::createGraphicsPipeline(m_GraphicsQuadPipelineSpecification);
        RegisterPipelineForShaderWatcher("QuadShader", "QuadShader.slang", &m_GraphicsQuadPipelineSpecification, nullptr, &m_GraphicsQuadPipeline, VanKGraphics);
        
        m_GraphicsCirclePipelineSpecification = GraphicsPipelineSpecification;
        m_GraphicsCirclePipelineSpecification.ShaderStageCreateInfo.VanKShader = CircleShader;
        m_GraphicsCirclePipeline = RenderCommand::createGraphicsPipeline(m_GraphicsCirclePipelineSpecification);
        RegisterPipelineForShaderWatcher("CircleShader", "CircleShader.slang", &m_GraphicsCirclePipelineSpecification, nullptr, &m_GraphicsCirclePipeline, VanKGraphics);
        
        m_GraphicsTextPipelineSpecification = GraphicsPipelineSpecification;
        m_GraphicsTextPipelineSpecification.ShaderStageCreateInfo.VanKShader = TextShader;
        m_GraphicsTextPipeline = RenderCommand::createGraphicsPipeline(m_GraphicsTextPipelineSpecification);
        RegisterPipelineForShaderWatcher("TextShader", "TextShader.slang", &m_GraphicsTextPipelineSpecification, nullptr, &m_GraphicsTextPipeline, VanKGraphics);
        
        m_GraphicsLinePipelineSpecification = GraphicsPipelineSpecification;
        m_GraphicsLinePipelineSpecification.ShaderStageCreateInfo.VanKShader = LineShader;
        m_GraphicsLinePipelineSpecification.InputAssemblyStateCreateInfo.VanKPrimitive = VanK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        m_GraphicsLinePipeline = RenderCommand::createGraphicsPipeline(m_GraphicsLinePipelineSpecification);
        RegisterPipelineForShaderWatcher("LineShader", "LineShader.slang", &m_GraphicsLinePipelineSpecification, nullptr, &m_GraphicsLinePipeline, VanKGraphics);
        
        m_MeshPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshShader;
        m_MeshPipeline = RenderCommand::createGraphicsPipeline(m_MeshPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshShader", "MeshShader.slang", &m_MeshPipelineSpecification, nullptr, &m_MeshPipeline, VanKGraphics);
        
        // Compute Pipelines creations
        VanKComputePipelineCreateInfo ComputePipelineCreateInfo
        {
            .VanKShader = DrawIndirectShader
        };
        
        VanKComputePipelineSpecification computePipelineSpecification
        {
            .ComputePipelineCreateInfo = ComputePipelineCreateInfo
        };
        
        m_ComputeDrawIndirectPipelineSpecification = computePipelineSpecification;
        
        m_ComputeDrawIndirectPipeline = RenderCommand::createComputeShaderPipeline(m_ComputeDrawIndirectPipelineSpecification);
        RegisterPipelineForShaderWatcher("DrawIndirectShader", "DrawIndirectShader.slang", nullptr, &m_ComputeDrawIndirectPipelineSpecification, &m_ComputeDrawIndirectPipeline, VanKCompute);
        
        WatchShaderFiles(); // has to be last after pipeline creation
        
        pipelines =
        {
            m_GraphicsPBRPipeline,
            m_GraphicsQuadPipeline,
            m_GraphicsCirclePipeline,
            m_GraphicsTextPipeline,
            m_GraphicsLinePipeline
        };

        uniformScene.reset(UniformBuffer::Create(sizeof(s_Data.SceneData)));

        whiteTexture = TextureImporter::LoadTexture2D("");
        vikingRoom = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room.ktx2");
        ChernoLogo = TextureImporter::LoadTexture2D("../build/VanK/textures/ChernoLogo.ktx2");
        
        loadModel();
        
        s_Data.vikingHandle = RegistryMesh::registerMesh(shaderio::PipelineType_PBR, vertices, indices);
        s_Data.cubeHandle = RegistryMesh::registerMesh(shaderio::PipelineType_PBR, GeometryData::cubeVertices, GeometryData::cubeIndices);
        s_Data.quadHandle = RegistryMesh::registerMesh(shaderio::PipelineType_Quad, GeometryData::quadVertices, GeometryData::quadIndices);
        s_Data.circleHandle = RegistryMesh::registerMesh(shaderio::PipelineType_Circle, GeometryData::quadVertices, GeometryData::quadIndices);
        s_Data.textHandle = RegistryMesh::registerMesh(shaderio::PipelineType_Text, GeometryData::quadVertices, GeometryData::quadIndices);
        s_Data.lineHandle = RegistryMesh::registerMesh(shaderio::PipelineType_Line, GeometryData::lineVertices, GeometryData::lineIndices);
        
        /*RegistryMesh::DebugPrintPipelineInstances(shaderio::PipelineType_PBR);
        RegistryMesh::DebugPrintPipelineInstances(shaderio::PipelineType_Quad);*/
        
        // 4            4        156         152                   152
        //draw calls, meshes, instances, actualy instances, draws saved by instancing
        //pipeline statatistics imputassemblyvertices/primitives vertexshaderinvocation clippinginvocation clipping primitives fragmentshaderinvocations computershaderinvocatinon
    }
    
    // this is needed because of shaderlibrary holding raii modules and they die last because renderer has it
    //maybe move to vulkanrenderapi backend ?
    void Renderer::Shutdown() 
    {
        RenderCommand::waitForGraphicsQueueIdle();

        RenderCommand::DestroyAllPipelines();

        GetShaderLibrary().ShutdownAll();

        uniformScene.reset();

        m_TransferRingBuffer.reset();
        
        for (auto& indirectBuffer : m_IndirectBuffers)
        {
            indirectBuffer.reset();
        }
        
        for (auto& countBuffer : m_CountBuffers)
        {
            countBuffer.reset();
        }

        m_InstancedVertexBuffer.reset();
        
        m_InstancedIndexBuffer.reset();
        
        m_InstancedPBRBuffer.reset();

        m_InstancedQuadBuffer.reset();
        
        m_InstancedCircleBuffer.reset();
        
        m_InstancedTextBuffer.reset();
        
        m_InstancedLineBuffer.reset();
        
        m_MeshInfoBuffer.reset();
    }
    
    void Renderer::CheckPendingVSyncChange()
    {
        if (!s_VSyncChangeRequested) return;
        
        RenderCommand::RebuildSwapchain(vSync);
        
        s_VSyncChangeRequested = false;    // reset
    };
    static bool s_FlushedThisFrame = false;

    void Renderer::BeginSubmit()
    {
        s_FlushedThisFrame = false;
        
        if (isEditor)
            RenderCommand::BeginFrame(VanK_Render_ImGui);
        else
            RenderCommand::BeginFrame(VanK_Render_Swapchain);
        
        cmd = RenderCommand::BeginCommandBuffer();
        if (!cmd)
            SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        
        RegistryMesh::clearInstances();
    }

    void Renderer::EndSubmit()
    {
        RegistryMesh::rebuildAllInstances();
        
        Flush();
        
        RenderCommand::SubmitRendering(cmd);
        
        RenderCommand::EndCommandBuffer(cmd);
        
        RenderCommand::EndFrame();
        
        CheckPendingVSyncChange();
    }
    
    void Renderer::Flush()
    {
        VK_CORE_ASSERT(!s_FlushedThisFrame, "Flush called more than once per frame");
        s_FlushedThisFrame = true;
        
        /*BeginSubmit();*/
        
        if (s_IsPipelineReloadFinished.exchange(false))
        {
            IsShaderReloadFinished = false;
            if (s_ShaderWatcher.empty())
                WatchShaderFiles();

            EndSubmit();
            ReloadPipelines();
            BeginSubmit();
            return;
        }
        
        DrawMeshShader();
     
        /*DrawFrame();*/
        
        /*EndSubmit();*/
    }
    
    void Renderer::DrawMeshShader()
    {
        ScopeTimer timer("Renderer::DrawMeshShader");
        
        std::vector<VanKColorTargetInfo> colorAttachments;
        colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
        colorAttachments.emplace_back(VanK_FORMAT_R32_SINT, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.i = {-1}});

        VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_CLEAR, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
        RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);
            
        VanKViewport viewPort = { 0, 0, m_ViewportSize.width, m_ViewportSize.height, 0, 1 };
        RenderCommand::SetViewport(cmd, 1, viewPort);

        VankRect rect = { 0, 0, m_ViewportSize.width, m_ViewportSize.height };
        RenderCommand::SetScissor(cmd, 1, rect);
            
        RenderCommand::SetCullMode(cmd, VanK_CULL_MODE_NONE);
        
        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshPipeline);

        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);
        
        RenderCommand::DrawMeshTasks(cmd, 1, 1, 1);
            
        RenderCommand::EndRendering(cmd);
    }
    
    void Renderer::DrawFrame()
    {
        ScopeTimer timer("Renderer::DrawFrame");
        
        if (!RegistryMesh::hasDraws())
        {
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_LOAD, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
            colorAttachments.emplace_back(VanK_FORMAT_R32_SINT, VanK_LOADOP_LOAD, VanK_STOREOP_STORE, VanK_FColor{.i = {-1}});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_LOAD, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);
            
            VanKViewport viewPort = { 0, 0, m_ViewportSize.width, m_ViewportSize.height, 0, 1 };
            RenderCommand::SetViewport(cmd, 1, viewPort);

            VankRect rect = { 0, 0, m_ViewportSize.width, m_ViewportSize.height };
            RenderCommand::SetScissor(cmd, 1, rect);
            
            RenderCommand::SetCullMode(cmd, VanK_CULL_MODE_NONE);
            
            RenderCommand::EndRendering(cmd);
            return;
        }
        
        auto& globalVertices = RegistryMesh::getVertices();
        uint64_t vertexSize = sizeof(shaderio::InstancedVertexData) * std::max(1ull, globalVertices.size());
        if (!m_InstancedVertexBuffer || m_InstancedVertexBuffer->GetSize() < vertexSize)
            m_InstancedVertexBuffer.reset(VertexBuffer::Create(vertexSize));
        
        auto& globalIndices = RegistryMesh::getIndices();
        uint64_t indexSize = sizeof(uint32_t) * std::max(1ull, globalIndices.size());
        if (!m_InstancedIndexBuffer || m_InstancedIndexBuffer->GetSize() < indexSize)
            m_InstancedIndexBuffer.reset(IndexBuffer::Create(indexSize));
        
        auto pbrInstances = RegistryMesh::getInstances<shaderio::InstancedPBRData>(shaderio::PipelineType_PBR);
        uint64_t pbrSize = sizeof(shaderio::InstancedPBRData) * std::max(1ull, pbrInstances.size());
        if (!m_InstancedPBRBuffer || m_InstancedPBRBuffer->GetSize() < pbrSize)
            m_InstancedPBRBuffer.reset(StorageBuffer::Create(pbrSize));
        
        auto quadInstances = RegistryMesh::getInstances<shaderio::InstancedQuadData>(shaderio::PipelineType_Quad);
        uint64_t quadSize = sizeof(shaderio::InstancedQuadData) * std::max(1ull, quadInstances.size());
        if (!m_InstancedQuadBuffer || m_InstancedQuadBuffer->GetSize() < quadSize)
            m_InstancedQuadBuffer.reset(StorageBuffer::Create(quadSize));
        
        auto circleInstances = RegistryMesh::getInstances<shaderio::InstancedCircleData>(shaderio::PipelineType_Circle);
        uint64_t circleSize = sizeof(shaderio::InstancedCircleData) * std::max(1ull, circleInstances.size());
        if (!m_InstancedCircleBuffer || m_InstancedCircleBuffer->GetSize() < circleSize)
            m_InstancedCircleBuffer.reset(StorageBuffer::Create(circleSize));
        
        auto textInstances = RegistryMesh::getInstances<shaderio::InstancedTextData>(shaderio::PipelineType_Text);
        uint64_t textSize = sizeof(shaderio::InstancedTextData) * std::max(1ull, textInstances.size());
        if (!m_InstancedTextBuffer || m_InstancedTextBuffer->GetSize() < textSize)
            m_InstancedTextBuffer.reset(StorageBuffer::Create(textSize));
        
        auto lineInstances = RegistryMesh::getInstances<shaderio::InstancedLineData>(shaderio::PipelineType_Line);
        uint64_t lineSize = sizeof(shaderio::InstancedLineData) * std::max(1ull, lineInstances.size());
        if (!m_InstancedLineBuffer || m_InstancedLineBuffer->GetSize() < lineSize)
            m_InstancedLineBuffer.reset(StorageBuffer::Create(lineSize));
        
        std::vector<shaderio::MeshInfo> allMeshInfos;
        size_t countBuffersSize = sizeof(uint32_t) * (size_t)shaderio::PipelineType_Count;
        for (uint32_t p = 0; p < static_cast<uint32_t>(shaderio::PipelineType_Count); ++p)
        {
            auto& meshes = RegistryMesh::getMeshInfo(static_cast<shaderio::PipelineType>(p));
            allMeshInfos.insert(allMeshInfos.end(), meshes.begin(), meshes.end());
            
            if (!m_CountBuffers[p] || m_CountBuffers[p]->GetSize() < sizeof(uint32_t))
                m_CountBuffers[p].reset(IndirectBuffer::Create(sizeof(uint32_t)));
        }
        uint64_t meshSize = sizeof(shaderio::MeshInfo) * std::max(1ull, allMeshInfos.size());
        if (!m_MeshInfoBuffer || m_MeshInfoBuffer->GetSize() < meshSize)
            m_MeshInfoBuffer.reset(StorageBuffer::Create(meshSize));
        
        uint64_t transferSize = vertexSize + indexSize + pbrSize + quadSize + circleSize + textSize + lineSize + countBuffersSize + meshSize;
        if (!m_TransferRingBuffer || m_TransferRingBuffer->GetSize() < transferSize)
            m_TransferRingBuffer.reset(TransferBuffer::Create(transferSize, VanKTransferBufferUsageUpload));
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedVertexBuffer, globalVertices, shaderio::InstancedVertexData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedIndexBuffer, globalIndices, uint32_t, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedPBRBuffer, pbrInstances, shaderio::InstancedPBRData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedQuadBuffer, quadInstances, shaderio::InstancedQuadData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedCircleBuffer, circleInstances, shaderio::InstancedCircleData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedTextBuffer, textInstances, shaderio::InstancedTextData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedLineBuffer, lineInstances, shaderio::InstancedLineData, 0);
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_MeshInfoBuffer, allMeshInfos, shaderio::MeshInfo, 0);
        
        static const std::vector<uint32_t> resetValue = { 0 };
        
        for (uint32_t p = 0; p < static_cast<uint32_t>(shaderio::PipelineType_Count); p++)
        {
            uint64_t totalDraws = sizeof(shaderio::DrawIndexedIndirectCommand) * std::max(1ull, allMeshInfos.size());
            
            if (!m_IndirectBuffers[p] || m_IndirectBuffers[p]->GetSize() < totalDraws)
                m_IndirectBuffers[p].reset(IndirectBuffer::Create(totalDraws));
            
            UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_CountBuffers[p], resetValue, uint32_t, 0);
            
            s_Data.SceneData.indirectAddresses[p] =
                m_IndirectBuffers[p]->GetBufferAddress();

            s_Data.SceneData.countAddresses[p] =
                m_CountBuffers[p]->GetBufferAddress();
        }
        
        s_Data.SceneData.vertexAddress = m_InstancedVertexBuffer->GetBufferAddress();
        s_Data.SceneData.pbrAddress = m_InstancedPBRBuffer->GetBufferAddress();
        s_Data.SceneData.quadAddress = m_InstancedQuadBuffer->GetBufferAddress();
        s_Data.SceneData.meshInfoAddress = m_MeshInfoBuffer->GetBufferAddress();
        s_Data.SceneData.circleAddress = m_InstancedCircleBuffer->GetBufferAddress();
        s_Data.SceneData.textAddress = m_InstancedTextBuffer->GetBufferAddress();
        s_Data.SceneData.lineAddress = m_InstancedLineBuffer->GetBufferAddress();
        uint32_t meshCount = 0;
        for (uint32_t p = 0; p < shaderio::PipelineType_Count; ++p)
            meshCount += static_cast<uint32_t>(RegistryMesh::getMeshInfo(static_cast<shaderio::PipelineType>(p)).size());

        s_Data.SceneData.numMeshes = meshCount;
        
        uniformScene->Update(cmd, &s_Data.SceneData, sizeof(s_Data.SceneData));
        
        {
            VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, m_InstancedVertexBuffer.get(), m_IndirectBuffers, m_CountBuffers);
        
            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawIndirectPipeline);
           
            RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Compute, uniformScene.get(), 1, 0, 0);
        
            RenderCommand::DispatchCompute(computePass, (meshCount + 64 - 1) / 64, 1, 1); // matches [numthreads(64,1,1)] in shader

            RenderCommand::EndComputePass(computePass);
        }
        
        {
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_LOAD, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
            colorAttachments.emplace_back(VanK_FORMAT_R32_SINT, VanK_LOADOP_LOAD, VanK_STOREOP_STORE, VanK_FColor{.i = {-1}});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_LOAD, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);
            
            VanKViewport viewPort = { 0, 0, m_ViewportSize.width, m_ViewportSize.height, 0, 1 };
            RenderCommand::SetViewport(cmd, 1, viewPort);

            VankRect rect = { 0, 0, m_ViewportSize.width, m_ViewportSize.height };
            RenderCommand::SetScissor(cmd, 1, rect);
            
            RenderCommand::SetLineWidth(cmd, m_LineWidth);
            
            RenderCommand::SetCullMode(cmd, VanK_CULL_MODE_NONE);
            
            RenderCommand::BindIndexBuffer(cmd, *m_InstancedIndexBuffer, VanKIndexElementSize::Uint32);
            
            for (uint32_t p = 0; p < shaderio::PipelineType::PipelineType_Count; p++)
            {
                RenderCommand::BindPipeline(
                    cmd,
                    VanKPipelineBindPoint::Graphics,
                    pipelines[p]);
                
                RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Graphics, uniformScene.get(), 1, 0, 0);
                
                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                RenderCommand::DrawIndexedIndirectCount(
                    cmd,
                    *m_IndirectBuffers[p],
                    0,
                    *m_CountBuffers[p],
                    0,
                    meshCount,
                    sizeof(shaderio::DrawIndexedIndirectCommand));
            }
            
            RenderCommand::EndRendering(cmd);
        }
    }

    struct PipelineReloadEntry
    {
        VanKPipeLine* Pipeline;
        VanKGraphicsPipelineSpecification* graphicsSpec;
        VanKComputePipelineSpecification* computeSpec;
        std::string ShaderKey;
        std::string FileName; // e.g., "GraphicsCubeShader.slang"
        VanKShaderStageFlags flag;
    };
    
    inline static std::vector<PipelineReloadEntry> s_PipelineReloadEntries;

    void Renderer::RegisterPipelineForShaderWatcher
    (
        const std::string& shaderKey,
        const std::string& fileName,
        VanKGraphicsPipelineSpecification* graphicsSpec,
        VanKComputePipelineSpecification* computeSpec,
        VanKPipeLine* pipeline,
        VanKShaderStageFlags flag
    )
    {
        s_PipelineReloadEntries.push_back({ pipeline, graphicsSpec, computeSpec, shaderKey, fileName, flag });
    }

    void Renderer::WatchShaderFiles()
    {
        for (const std::string& path : GetShaderLibrary().GetAllShaderPaths())
        {
            s_ShaderWatcher.emplace_back(std::make_unique<filewatch::FileWatch<std::string>>(path,
                [](const std::string& file, const filewatch::Event change_type)
                {
                    if (!IsShaderReloadFinished && change_type == filewatch::Event::modified)
                    {
                        std::cout << "[FileWatcher] Shader file changed: " << file << '\n';

                        IsShaderReloadFinished = true;

                        changedFile = file;

                        ReloadTimer = Timer();
                    
                        Application::Get().SubmitToMainThread([]()
                        {
                            s_ShaderWatcher.clear();
                            s_IsPipelineReloadFinished = true;
                        });
                    }
                }));
        }
    }

    void Renderer::ReloadPipelines()
    {
        VK_CORE_WARN("Reloading took {}ms", ReloadTimer.ElapsedMillis());

        for (auto& entry : s_PipelineReloadEntries)
        {
            if (entry.FileName != changedFile)
                continue;

            RenderCommand::waitForGraphicsQueueIdle();
            
            RenderCommand::DestroyPipeline(*entry.Pipeline);
            
            GetShaderLibrary().Remove(entry.ShaderKey);
            
            auto Shader = GetShaderLibrary().Load(entry.ShaderKey, changedFile);

            if (entry.flag == VanKGraphics)
            {
                entry.graphicsSpec->ShaderStageCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createGraphicsPipeline(*entry.graphicsSpec);
            }
            else
            {
                entry.computeSpec->ComputePipelineCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createComputeShaderPipeline(*entry.computeSpec); 
            }
        }
    }
}

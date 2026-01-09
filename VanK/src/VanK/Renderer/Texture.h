#pragma once

#include <ktx.h>

#include "imgui.h"
#include "glm/glm.hpp"

#include "VanK/Core/core.h"
#include "VanK/Asset/Asset.h"
#include "VanK/Core/Buffer.h"

namespace VanK
{
    enum class ImageFormat
    {
        None = 0,
        RGB8,
        RGBA8,
        SRGBA8,
        R16G16
    };
    
    enum class VanKFilter
    {
        filterNearest,
        filterLinear
    };
    
    enum class VanKSamplerMipmapMode
    {
        mipmapModeNearest,
        mipmapModeLinear
    };
    
    enum class VanKSamplerAddressMode
    {
        addressModeRepeat,
        addressModeMirrorRepeat,
        addressModeClampToEdge,
        addressModeClampToBorder,
        addressModeMirrorClampToEdge,
    };

    enum class VanKCompareOp
    {
        compareOpNever,
        compareOpLess,
        compareOpEqual,
        compareOpLessOrEqual,
        compareOpGreater,
        compareOpNotEqual,
        compareOpGreaterOrEqual,
        compareOpAlways
    };
    
    struct VanKSamplerInfo
    {
        VanKFilter magFilter = VanKFilter::filterLinear;
        VanKFilter minFilter = VanKFilter::filterLinear;
        VanKSamplerMipmapMode mipmapMode = VanKSamplerMipmapMode::mipmapModeLinear;
        VanKSamplerAddressMode addressModeU = VanKSamplerAddressMode::addressModeRepeat;
        VanKSamplerAddressMode addressModeV = VanKSamplerAddressMode::addressModeRepeat;
        VanKSamplerAddressMode addressModeW = VanKSamplerAddressMode::addressModeRepeat;
        float mipLodBias = 0.0f;
        bool anisotopyEnable = true;
        bool compareEnable = false;
        VanKCompareOp compareOp = VanKCompareOp::compareOpAlways;
        float minLod = 0.0f;
    };
    
    struct TextureSpecification
    {
        std::string Name = "";
        uint32_t Width = 1;
        uint32_t Height = 1;
        ImageFormat Format = ImageFormat::SRGBA8;
        bool GenerateMips = true;
        bool FlipTexture = false;
        
        glm::vec4 defaultColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        
        VanKSamplerInfo SamplerInfo;
        
        ktxTexture2* ktTexture = nullptr;
    };
    
    class Texture : public Asset
    {
    public:
        virtual ~Texture() = default;
        
        virtual const TextureSpecification& GetSpecification() const = 0;
        
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetTextureIndex() const = 0;
    };
    
    class Texture2D : public Texture
    {
    public:
        virtual ImTextureID getImTextureID() = 0;
        static size_t GetNumImGuiTextures() { return s_ImGuiTextureCount; }
        static void SetNumImGuiTextures(size_t count) { s_ImGuiTextureCount = count; }
        static Ref<Texture2D> Create(const TextureSpecification& specification, Buffer data = Buffer());
        
        static AssetType GetStaticType() { return AssetType::Texture2D; }
        virtual AssetType GetType() const { return GetStaticType(); }
    private:
        static size_t s_ImGuiTextureCount;
    };
}
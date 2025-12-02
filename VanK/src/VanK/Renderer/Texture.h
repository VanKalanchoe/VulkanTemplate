#pragma once

#include <ktx.h>

#include "imgui.h"

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
    };
    
    struct TextureSpecification
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        ImageFormat Format = ImageFormat::RGBA8;
        bool GenerateMips = true;
        bool FlipTexture = false;
        
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
        static Ref<Texture2D> Create(const TextureSpecification& specification, Buffer data = Buffer());
        
        static AssetType GetStaticType() { return AssetType::Texture2D; }
        virtual AssetType GetType() const { return GetStaticType(); }
    private:
        static size_t s_ImGuiTextureCount;
    };
}
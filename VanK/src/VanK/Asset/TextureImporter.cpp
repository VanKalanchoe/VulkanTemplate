#include "TextureImporter.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "VanK/Core/logger.h"
#include "VanK/Debug/Instrumentor.h"
#include "VanK/Project/Project.h"

namespace VanK
{
    void flipKtxTexture2(ktxTexture2* tex)
    {
        const uint32_t mipCount = tex->numLevels;
        const uint32_t channels = 4; // because we transcode to RGBA32

        for (uint32_t level = 0; level < mipCount; level++)
        {
            ktx_size_t offset;
            ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &offset);

            uint32_t w = std::max(1u, tex->baseWidth  >> level);
            uint32_t h = std::max(1u, tex->baseHeight >> level);

            uint8_t* mipData = tex->pData + offset;

            uint32_t rowSize = w * channels;
            std::vector<uint8_t> row(rowSize);

            for (uint32_t y = 0; y < h / 2; y++)
            {
                uint8_t* top = mipData + y * rowSize;
                uint8_t* bottom = mipData + (h - 1 - y) * rowSize;

                memcpy(row.data(), top, rowSize);
                memcpy(top, bottom, rowSize);
                memcpy(bottom, row.data(), rowSize);
            }
        }
    }
    
    Ref<Texture2D> TextureImporter::ImportTexture2D(AssetHandle handle, const AssetMetadata& metadata)
    {
        return LoadTexture2D(Project::GetActiveAssetDirectory() / metadata.FilePath);
    }

    Ref<Texture2D> TextureImporter::LoadTexture2D(const std::filesystem::path& path, const TextureSpecification& spec)
    {
        std::vector paths{ path };
        
        if (path.extension() == ".png" || path.extension() == ".jpg" || path.extension() == ".jpeg")
            return LoadImageTexture(path, spec);
        
        return LoadTexture2D(paths, spec);
    }
    
    Ref<Texture2D> TextureImporter::LoadImageTexture(const std::filesystem::path& path, TextureSpecification spec)
    {
        if (spec.FlipTexture)
            stbi_set_flip_vertically_on_load(true);
        
        int w, h, c;
        stbi_uc *pixels = stbi_load(path.string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        
        if (!pixels)
            throw std::runtime_error("failed to load texture image!");

        spec.Width  = w;
        spec.Height = h;

        Buffer data(w * h * 4);
        memcpy(data.Data, pixels, data.Size);
        
        /*int pixelCount = w * h;
        for (int i = 0; i < pixelCount; ++i)
        {
            std::swap(data.Data[i*4 + 0], data.Data[i*4 + 2]); // swap R <-> B
        }*/

        Ref<Texture2D> tex = Texture2D::Create(spec, data);

        stbi_image_free(pixels);
        data.Release();

        return tex;
    }
    
    Ref<Texture2D> TextureImporter::LoadTexture2D(const std::vector<std::filesystem::path>& path, TextureSpecification spec)
    {
        std::cout << "TextureImporter::LoadTexture2D " << path[0].string() << '\n';
        
        if (!path[0].string().empty())
        {
            spec.Name = path[0].stem().string();
            
            ktxTexture2* ktx_texture = nullptr;
            
            KTX_error_code result = ktxTexture2_CreateFromNamedFile
            (
              path[0].string().c_str(),
              KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
              &ktx_texture
            );
            
            if (result != KTX_SUCCESS) 
                throw std::runtime_error("Could not load the requested image file.");
            
            if (ktxTexture2_NeedsTranscoding(ktx_texture))
            {
                // target format currently uncompressed check here what actual fmt format to use with that function https://docs.vulkan.org/samples/latest/samples/performance/texture_compression_basisu/README.html
                result = ktxTexture2_TranscodeBasis(ktx_texture, KTX_TTF_RGBA32, 0);
                if (result != KTX_SUCCESS)
                {
                    throw std::runtime_error("Could not transcode the input texture to the selected target format.");
                }
            }
            
            // ---- FLIP VERTICALLY HERE ---- only works with uncompressed not all need it
            if (spec.FlipTexture)
                flipKtxTexture2(ktx_texture);
            
            // --- Dimensions ---
            uint32_t texWidth = ktx_texture->baseWidth;
            uint32_t texHeight = ktx_texture->baseHeight;
            
            // --- Full data ---
            ktx_size_t imageSize = ktx_texture->dataSize; // total size of all mip levels
            ktx_uint8_t* ktxTextureData = ktx_texture->pData;
            
            // Set spec fields
            spec.ktTexture = ktx_texture;
            spec.Width = texWidth;
            spec.Height = texHeight;
            
            // Allocate buffer of exact size
            Buffer data(imageSize);
            
            // Copy KTX texture bytes into your Buffer
            memcpy(data.Data, ktxTextureData, imageSize);
            
            // Create texture
            Ref<Texture2D> texture = Texture2D::Create(spec, data);
        
            // Free temporary buffer
            data.Release();
            
            // Cleanup KTX resources inside vulkantexture class
            /*ktxTexture_Destroy(kTexture);*/
            
            return texture;
        }
        else
        {
            spec.Name = "Default Texture";
            
            // Allocate 4 bytes for 1 pixel RGBA
            Buffer data(spec.Width * spec.Height * 4);
      
            uint8_t r = static_cast<uint8_t>(glm::clamp(spec.defaultColor.r, 0.0f, 1.0f) * 255.0f);
            uint8_t g = static_cast<uint8_t>(glm::clamp(spec.defaultColor.g, 0.0f, 1.0f) * 255.0f);
            uint8_t b = static_cast<uint8_t>(glm::clamp(spec.defaultColor.b, 0.0f, 1.0f) * 255.0f);
            uint8_t a = static_cast<uint8_t>(glm::clamp(spec.defaultColor.a, 0.0f, 1.0f) * 255.0f);
            
            for (uint32_t i = 0; i < spec.Width * spec.Height; i++)
            {
                data.Data[i * 4 + 0] = r;
                data.Data[i * 4 + 1] = g;
                data.Data[i * 4 + 2] = b;
                data.Data[i * 4 + 3] = a;
            }
            
            // Create texture
            Ref<Texture2D> texture = Texture2D::Create(spec, data);

            // Free temporary buffer
            data.Release();
            
            return texture;
        }
    }
}

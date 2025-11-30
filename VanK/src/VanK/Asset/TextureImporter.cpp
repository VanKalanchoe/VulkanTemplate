#include "TextureImporter.h"

#include "VanK/Core/logger.h"
#include "VanK/Debug/Instrumentor.h"
#include "VanK/Project/Project.h"

namespace VanK
{
    Ref<Texture2D> TextureImporter::ImportTexture2D(AssetHandle handle, const AssetMetadata& metadata)
    {
        return LoadTexture2D(Project::GetActiveAssetDirectory() / metadata.FilePath);
    }

    Ref<Texture2D> TextureImporter::LoadTexture2D(const std::filesystem::path& path)
    {
        TextureSpecification spec;
        spec.Width = 1;
        spec.Height = 1;
        spec.Format = ImageFormat::RGBA8;
        spec.GenerateMips = true;
        
        std::vector<std::filesystem::path> paths;
        paths.push_back(path);
        
        return LoadTexture2D(paths, spec);
    }
    
    Ref<Texture2D> TextureImporter::LoadTexture2D(const std::vector<std::filesystem::path>& path, TextureSpecification spec)
    {
        std::cout << "TextureImporter::LoadTexture2D " << path[0].string() << '\n';
        
        if (!path[0].string().empty())
        {
            ktxTexture* kTexture = nullptr;
            
            KTX_error_code result = ktxTexture_CreateFromNamedFile
            (
              path[0].string().c_str(),
              KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
              &kTexture
            );
            
            if (result != KTX_SUCCESS) 
                throw std::runtime_error("failed to load ktx texture image!");
            
            // --- Dimensions ---
            uint32_t texWidth = kTexture->baseWidth;
            uint32_t texHeight = kTexture->baseHeight;
            
            // --- Full data ---
            ktx_size_t imageSize = ktxTexture_GetDataSize(kTexture); // total size of all mip levels
            ktx_uint8_t* ktxTextureData = ktxTexture_GetData(kTexture);
            
            // --- Determine pixel format ---
            ImageFormat fmt = ImageFormat::RGBA8; // fallback
            
            // Always set ktTexture regardless of KTX version
            spec.ktTexture = kTexture;
            
            // Check if the KTX texture has a format
            if (kTexture->classId == ktxTexture2_c)
            {
                // KTX2 specific handling if needed
            }
            else
            {
                // For KTX1 files or if we can't determine the format, use a reasonable default
                // For KTX1 files, use a reasonable default
                fmt = ImageFormat::RGBA8;
            }
            
            // Set spec fields
            spec.Width = texWidth;
            spec.Height = texHeight;
            spec.Format = fmt;
            
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
            // Allocate 4 bytes for 1 pixel RGBA
            Buffer data(spec.Width * spec.Height * 4);
            
            // Set all 4 bytes to 255 (white)
            memset(data.Data, 255, data.Size);
            
            // Create texture
            Ref<Texture2D> texture = Texture2D::Create(spec, data);

            // Free temporary buffer
            data.Release();
            
            return texture;
        }
    }
}

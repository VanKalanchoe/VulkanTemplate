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
            
            // --- Dimensions ---
            uint32_t texWidth = ktx_texture->baseWidth;
            uint32_t texHeight = ktx_texture->baseHeight;
            
            // --- Full data ---
            ktx_size_t imageSize = ktx_texture->dataSize; // total size of all mip levels
            ktx_uint8_t* ktxTextureData = ktx_texture->pData;
            
            // Always set ktTexture regardless of KTX version
            spec.ktTexture = ktx_texture;
            
            // Set spec fields
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

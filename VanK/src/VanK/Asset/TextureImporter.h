#pragma once

#include <filesystem>

#include "Asset.h"
#include "AssetMetadata.h"
#include "VanK/Core/core.h"
#include "VanK/Renderer/Texture.h"

namespace VanK
{
    class TextureImporter
    {
    public:
        // AssetMetadata filepath is relative to project asset directory
        static Ref<Texture2D> ImportTexture2D(AssetHandle handle, const AssetMetadata& metadata);

        // Reads file directory from filesystem (i.e path has to be relative/absolute to working direcory)
        static Ref<Texture2D> LoadTexture2D(const std::filesystem::path& path, const TextureSpecification& spec = {});
        static Ref<Texture2D> LoadDDSTexture(const std::filesystem::path& path, TextureSpecification spec);
        static Ref<Texture2D> LoadImageTexture(const std::filesystem::path& path, TextureSpecification spec);

        // Reads file directory from filesystem (i.e path has to be relative/absolute to working direcory)
        static Ref<Texture2D> LoadTexture2D(const std::vector<std::filesystem::path>& path, TextureSpecification spec);
    };
}

#pragma once

#include <filesystem>

#include "Asset.h"
#include "AssetMetadata.h"

#include "VanK/Core/core.h"
#include "VanK/Scene/Scene.h"

namespace VanK
{
    class SceneImporter
    {
    public:
        // AssetMetadata filepath is relative to project asset directory
        static Ref<Scene> ImportScene(AssetHandle handle, const AssetMetadata& metadata);

        // Load from filepath
        static Ref<Scene> LoadScene(const std::filesystem::path& path);

        static void SaveScene(Ref<Scene> scene, const std::filesystem::path& path);
    };
}

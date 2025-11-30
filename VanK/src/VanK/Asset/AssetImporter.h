#pragma once
#include "Asset.h"
#include "AssetMetadata.h"
#include "VanK/Core/core.h"

namespace VanK
{
    class AssetImporter
    {
    public:
        static Ref<Asset> ImportAsset(AssetHandle handle, const AssetMetadata& metadata);
    };
}

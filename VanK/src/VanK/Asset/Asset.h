#pragma once

#include <string_view>

#include "VanK/Core/Ref.h"
#include "VanK/Core/UUID.h"

namespace VanK
{
    using AssetHandle = UUID;

    enum class AssetType : uint16_t
    {
        None = 0,
        Scene,
        Texture2D
    };

    std::string_view AssetTypeToString(AssetType type);
    AssetType AssetTypeFromString(std::string_view assetType);

    class Asset : public RefCounted
    {
    public:
        AssetHandle Handle; // generate handle

        virtual AssetType GetType() const = 0;
    };
}
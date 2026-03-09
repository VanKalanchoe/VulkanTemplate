#pragma once
#include "VoxelWorld.h"
#include "TerrainGenerator.h"

namespace VanK
{
    class VoxelWorldManager
    {
    public:
        void Init(VoxelWorld* world);

        void initTerrain(float frequency, float amplitude, uint32_t seed, TerrainGenerator::Mode mode);
        void generateChunks(glm::ivec3 gridSize, glm::ivec3 chunkSize);

    private:
        VoxelWorld* m_voxelWorld = nullptr;
        TerrainGenerator m_generator;
    };
}
#pragma once
#include "VoxelWorld.h"
#include "FastNoise/FastNoise.h"

namespace VanK
{
    class TerrainGenerator
    {
    public:
        enum class Mode
        {
            Flat,
            Procedural
        };

        TerrainGenerator() = default;

        // Initialize generator parameters
        void init(float frequency = 0.05f, float amplitude = 32.0f, uint32_t seed = 1337, Mode mode = Mode::Procedural);
        void generateChunkLocal(Chunk& chunk);

        // Fill a chunk with terrain
        void generateChunk(VoxelWorld& world, Ref<Chunk> chunk);

        void setMode(Mode mode) { m_mode = mode; }

    private:
        float m_frequency = 0.05f;
        float m_amplitude = 32.0f;
        uint32_t m_seed = 1337;
        Mode m_mode = Mode::Procedural;

        FastNoise::SmartNode<FastNoise::Simplex> m_noise;
    };
}
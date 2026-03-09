#include "TerrainGenerator.h"
#include <algorithm>

namespace VanK
{
    void TerrainGenerator::init(float frequency, float amplitude, uint32_t seed, Mode mode)
    {
        m_frequency = frequency;
        m_amplitude = amplitude;
        m_seed = seed;
        m_mode = mode;

        // Recreate noise node
        m_noise = FastNoise::New<FastNoise::Simplex>();
    }
    
    void TerrainGenerator::generateChunkLocal(Chunk& chunk)
{
    const int sizeX = chunk.size.x;
    const int sizeY = chunk.size.y;
    const int sizeZ = chunk.size.z;

    // base color per chunk
    uint8_t baseR = static_cast<uint8_t>((std::abs(chunk.position.x) * 73 + 50) % 256);
    uint8_t baseG = static_cast<uint8_t>((std::abs(chunk.position.z) * 31 + 80) % 256);
    uint8_t baseB = static_cast<uint8_t>((std::abs(chunk.position.x + chunk.position.z) * 17 + 120) % 256);

    std::vector<float> noiseData(sizeX * sizeZ);

    if (m_mode == Mode::Procedural)
    {
        m_noise->GenUniformGrid2D(
            noiseData.data(),
            static_cast<float>(chunk.position.x * sizeX),
            static_cast<float>(chunk.position.z * sizeZ),
            sizeX, sizeZ,
            m_frequency, m_frequency,
            m_seed
        );
    }

    for (int x = 0; x < sizeX; ++x)
    {
        for (int z = 0; z < sizeZ; ++z)
        {
            float height = (m_mode == Mode::Flat) ? m_amplitude
                : (noiseData[x + z * sizeX] + 1.0f) * 0.5f * m_amplitude;

            for (int y = 0; y < sizeY; ++y)
            {
                if (y < static_cast<int>(height))
                {
                    int brickX = x / VoxelWorld::BRICK_SIZE;
                    int brickY = y / VoxelWorld::BRICK_SIZE;
                    int brickZ = z / VoxelWorld::BRICK_SIZE;

                    int voxelX = x % VoxelWorld::BRICK_SIZE;
                    int voxelY = y % VoxelWorld::BRICK_SIZE;
                    int voxelZ = z % VoxelWorld::BRICK_SIZE;

                    int brickIndex = brickX + brickY * (chunk.size.x / VoxelWorld::BRICK_SIZE) +
                                     brickZ * (chunk.size.x / VoxelWorld::BRICK_SIZE) * (chunk.size.y / VoxelWorld::BRICK_SIZE);

                    if (brickIndex >= 0 && brickIndex < static_cast<int>(chunk.bricks.size()))
                    {
                        Brick& brick = chunk.bricks[brickIndex];
                        brick.voxels.r[voxelX | (voxelY << 3) | (voxelZ << 6)] = baseR;
                        brick.voxels.g[voxelX | (voxelY << 3) | (voxelZ << 6)] = baseG;
                        brick.voxels.b[voxelX | (voxelY << 3) | (voxelZ << 6)] = baseB;
                    }
                }
            }
        }
    }
}

    void TerrainGenerator::generateChunk(VoxelWorld& world, Ref<Chunk> chunk)
    {
        if (!chunk) return;
        Chunk& c = *chunk;

        const int sizeX = c.size.x;
        const int sizeY = c.size.y;
        const int sizeZ = c.size.z;

        // 1. Generate unique base color for this chunk (same as Flat mode)
        uint8_t baseR = static_cast<uint8_t>((std::abs(c.position.x) * 73 + 50) % 256);
        uint8_t baseG = static_cast<uint8_t>((std::abs(c.position.z) * 31 + 80) % 256);
        uint8_t baseB = static_cast<uint8_t>((std::abs(c.position.x + c.position.z) * 17 + 120) % 256);

        if (m_mode == Mode::Flat)
        {
            int flatHeight = static_cast<int>(m_amplitude);
            for (int x = 0; x < sizeX; ++x)
                for (int z = 0; z < sizeZ; ++z)
                    for (int y = 0; y < sizeY; ++y)
                    {
                        int worldY = c.position.y * sizeY + y;
                        if (worldY < flatHeight)
                        {
                            world.addVoxel(chunk, {c.position.x * sizeX + x, worldY, c.position.z * sizeZ + z}, baseR, baseG, baseB);
                        }
                    }
        }
        else // Mode::Procedural
        {
            // 2. Generate a 2D noise grid for the Chunk's footprint (X and Z)
            std::vector<float> noiseData(sizeX * sizeZ);

            // We use GenUniformGrid2D because terrain height is a function of X and Z
            m_noise->GenUniformGrid2D(
                noiseData.data(),
                static_cast<float>(c.position.x * sizeX), // World X start
                static_cast<float>(c.position.z * sizeZ), // World Z start (now negative)
                sizeX, sizeZ,
                m_frequency, m_frequency,
                m_seed
            );

            for (int x = 0; x < sizeX; ++x)
            {
                for (int z = 0; z < sizeZ; ++z)
                {
                    // Get the noise value for this column (-1.0 to 1.0 range)
                    float noiseVal = noiseData[x + z * sizeX];

                    // Map noise to height: (0.0 to 1.0) * amplitude
                    float height = (noiseVal + 1.0f) * 0.5f * m_amplitude;

                    for (int y = 0; y < sizeY; ++y)
                    {
                        int worldY = c.position.y * sizeY + y;

                        if (worldY < static_cast<int>(height))
                        {
                            // 3. Apply the chunk color + some height-based shading
                            uint8_t r = std::clamp(baseR + (y * 2), 0, 255);
                            uint8_t g = std::clamp(baseG + (y * 2), 0, 255);

                            world.addVoxel(chunk,
                                           {c.position.x * sizeX + x, worldY, c.position.z * sizeZ + z},
                                           r, g, baseB);
                        }
                    }
                }
            }
        }
    }
}

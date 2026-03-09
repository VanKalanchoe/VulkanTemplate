#include "VoxelWorldManager.h"

#include <chrono>

#include "VanK/Core/TaskSystem.h"

namespace VanK
{
    void VoxelWorldManager::Init(VoxelWorld* world)
    {
        m_voxelWorld = world;
    }

    void VoxelWorldManager::initTerrain(float frequency, float amplitude, uint32_t seed, TerrainGenerator::Mode mode)
    {
        m_generator.init(frequency, amplitude, seed, mode);
    }

    void VoxelWorldManager::generateChunks(glm::ivec3 gridSize, glm::ivec3 chunkSize)
    {
        if (!m_voxelWorld) return;

        auto start = std::chrono::high_resolution_clock::now();

        int total = gridSize.x * gridSize.y * gridSize.z;

        // Multithreaded generation
        TaskSystem::Get().parallelFor(0, total, [this, gridSize, chunkSize](int i) {
            int x = i % gridSize.x;
            int y = (i / gridSize.x) % gridSize.y;
            int z = i / (gridSize.x * gridSize.y);

            glm::ivec3 pos(x, y, -z);

            // Create chunk locally
            Ref<Chunk> localChunk(new Chunk()); 
            localChunk->position = pos;
            localChunk->size = chunkSize;

            // Fill bricks locally
            int bricksX = (chunkSize.x + VoxelWorld::BRICK_SIZE - 1) / VoxelWorld::BRICK_SIZE;
            int bricksY = (chunkSize.y + VoxelWorld::BRICK_SIZE - 1) / VoxelWorld::BRICK_SIZE;
            int bricksZ = (chunkSize.z + VoxelWorld::BRICK_SIZE - 1) / VoxelWorld::BRICK_SIZE;

            for (int bx = 0; bx < bricksX; ++bx)
                for (int by = 0; by < bricksY; ++by)
                    for (int bz = 0; bz < bricksZ; ++bz)
                    {
                        Brick brick;
                        brick.position = {bx, by, bz};
                        localChunk->bricks.push_back(brick);
                    }

            // Generate voxel data locally
            m_generator.generateChunk(*m_voxelWorld, localChunk);

            // Merge into VoxelWorld (thread-safe)
            m_voxelWorld->addChunk(localChunk);
        });

        TaskSystem::Get().wait();

        // Single-threaded instancing
        auto chunks = m_voxelWorld->getChunksSnapshot();
        for (auto& chunk : chunks)
            m_voxelWorld->instanceChunkPublic(chunk);

        RenderCommand::createTopLevelAS();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << std::dec << "Chunk generation took: " << duration.count() << " ms\n";
    }
}

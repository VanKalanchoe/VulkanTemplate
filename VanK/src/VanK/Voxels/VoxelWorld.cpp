#include "VoxelWorld.h"

#include <glm/ext/matrix_transform.hpp>

#include "VanK/Core/Application.h"

namespace VanK
{
    uint32_t VoxelWorld::createBrickBLAS()
    {
        shaderio::Aabb aabb;
        aabb.minimum = glm::vec3(0.0f);
        aabb.maximum = glm::vec3(BRICK_SIZE + 1e-4f);

        return RenderCommand::createBottomLevelASAABB(aabb);
    }

    void VoxelWorld::initialize(BufferManager& bufferManager)
    {
        m_bufferManager = &bufferManager;

        m_brickBLAS = createBrickBLAS();

        m_brickCapacity = 1024; // initial capacity

        size_t bufferSize = m_brickCapacity * sizeof(shaderio::BrickVoxelData);

        m_voxelBuffer = bufferManager.Create<StorageBuffer>(bufferSize);

        m_voxelTransfer = bufferManager.Create<TransferBuffer>(bufferSize, VanKTransferBufferUsageUpload);
    }

    void VoxelWorld::ensureCapacity(size_t required)
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        if (required <= m_brickCapacity)
            return;

        while (m_brickCapacity < required)
            m_brickCapacity *= 2;

        size_t newSize = m_brickCapacity * sizeof(shaderio::BrickVoxelData);

        m_voxelBuffer =
            m_bufferManager->Create<StorageBuffer>(newSize);

        m_voxelTransfer =
            m_bufferManager->Create<TransferBuffer>(
                newSize,
                VanKTransferBufferUsageUpload
            );
    }

    Ref<Chunk> VoxelWorld::createChunk(glm::ivec3 pos, glm::ivec3 size)
    {
        auto chunk = new Chunk();
        chunk->position = pos;
        chunk->size = size;

        generateChunkBricks(*chunk);
        instanceChunk(*chunk);

        Ref<Chunk> refChunk(chunk);
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            m_chunks.push_back(refChunk);
        }

        return refChunk;
    }

    void VoxelWorld::generateChunkBricks(Chunk& chunk)
    {
        for (int x = 0; x < chunk.size.x; x += BRICK_SIZE)
            for (int y = 0; y < chunk.size.y; y += BRICK_SIZE)
                for (int z = 0; z < chunk.size.z; z += BRICK_SIZE)
                {
                    Brick brick;
                    brick.position = glm::ivec3(
                        x / BRICK_SIZE,
                        y / BRICK_SIZE,
                        z / BRICK_SIZE
                    );

                    chunk.bricks.push_back(brick);
                }
    }

    void VoxelWorld::instanceChunk(Chunk& chunk)
    {
        glm::vec3 chunkOrigin = glm::vec3(chunk.position) * glm::vec3(chunk.size);

        for (auto& brick : chunk.bricks)
        {
            glm::vec3 brickOffset(
                brick.position.x * BRICK_SIZE,
                brick.position.y * BRICK_SIZE,
                brick.position.z * BRICK_SIZE
            );

            glm::vec3 worldPos = chunkOrigin + brickOffset;

            glm::mat4 transform =
                glm::translate(glm::mat4(1.0f), worldPos);

            brick.instanceIndex =
                RenderCommand::createInstanceASAABB(m_brickBLAS, transform);
        }
    }

    void VoxelWorld::setVoxel(
    Brick& brick,
    int x,
    int y,
    int z,
    uint8_t r,
    uint8_t g,
    uint8_t b)
    {
        int index = x | (y << 3) | (z << 6);

        brick.voxels.r[index] = r;
        brick.voxels.g[index] = g;
        brick.voxels.b[index] = b;
    }

    void VoxelWorld::addVoxel(Ref<Chunk> chunk, glm::ivec3 pos, uint8_t r, uint8_t g, uint8_t b)
    {
        if (!chunk) return;
        Chunk& c = *chunk;

        // 1. Convert World Position to Local Position (0-31 range)
        glm::ivec3 localPos = pos - (c.position * c.size);

        // 2. Determine which brick this is inside the chunk (0-3 range)
        glm::ivec3 brickPos = localPos >> 3;

        // 3. Determine voxel position inside that brick (0-7 range)
        glm::ivec3 voxelInBrick = localPos & 7;

        // 4. Calculate the local brick index
        // Formula: index = x + (y * bricksPerAxis) + (z * bricksPerAxis^2)
        int bricksPerAxis = c.size.x >> 3;
        int brickIndex = brickPos.x +
            (brickPos.y * bricksPerAxis) +
            (brickPos.z * bricksPerAxis * bricksPerAxis);

        if (brickIndex >= static_cast<int>(c.bricks.size()) || brickIndex < 0) return;

        // 5. Set the voxel using the local brick and local voxel coordinates
        setVoxel(c.bricks[brickIndex], voxelInBrick.x, voxelInBrick.y, voxelInBrick.z, r, g, b);
    }

    void VoxelWorld::removeVoxel(Ref<Chunk> chunk, glm::ivec3 pos)
    {
        addVoxel(chunk, pos, 0, 0, 0);
    }
    
    void VoxelWorld::fillBrick(Ref<Chunk> chunk, glm::ivec3 brickPos, uint8_t r, uint8_t g, uint8_t b)
    {
        if (!chunk) return;

        Chunk& c = *chunk;

        int bricksPerAxis = c.size.x >> 3;
        int index = brickPos.x + brickPos.y * bricksPerAxis + brickPos.z * bricksPerAxis * bricksPerAxis;

        if (index >= static_cast<int>(c.bricks.size())) return;

        Brick& brick = c.bricks[index];

        auto& data = brick.voxels;
        for (int i = 0; i < BRICK_VOXELS; i++)
        {
            data.r[i] = r;
            data.g[i] = g;
            data.b[i] = b;
        }
    }

    void VoxelWorld::uploadVoxelBuffer(VanKCommandBuffer cmd)
    {
        size_t brickCount = 0;

        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);

            for (auto& chunk : m_chunks)
                brickCount += chunk->bricks.size();
        }

        std::vector<shaderio::BrickVoxelData> allBricks;
        allBricks.reserve(brickCount);

        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);

            for (auto& chunk : m_chunks)
            {
                for (auto& brick : chunk->bricks)
                {
                    allBricks.push_back(brick.voxels);
                }
            }
        }

        ensureCapacity(brickCount);

        m_bufferManager
            ->Get<TransferBuffer>(m_voxelTransfer)
            ->Upload(
                cmd,
                *m_bufferManager->Get<StorageBuffer>(m_voxelBuffer),
                allBricks,
                0
            );
    }
}

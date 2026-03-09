#pragma once

#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "VanK/Renderer/RenderCommand.h"

namespace VanK
{
    struct Brick
    {
        glm::ivec3 position{};
        uint32_t instanceIndex = UINT32_MAX;
        shaderio::BrickVoxelData voxels{};
    };

    struct Chunk : RefCounted
    {
        glm::ivec3 position{};
        glm::ivec3 size{};
        std::vector<Brick> bricks;
    };
    
    class VoxelWorld
    {
    public:

        static constexpr int BRICK_SIZE = 8;
        static constexpr int BRICK_VOXELS = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

    public:

        void initialize(BufferManager& bufferManager);

        Ref<Chunk> createChunk(glm::ivec3 pos, glm::ivec3 size);

        void addVoxel(Ref<Chunk> chunk, glm::ivec3 pos, uint8_t r, uint8_t g, uint8_t b);
        void removeVoxel(Ref<Chunk> chunk, glm::ivec3 pos);

        void fillBrick(Ref<Chunk> chunk, glm::ivec3 brickPos, uint8_t r, uint8_t g, uint8_t b);

        void uploadVoxelBuffer(VanKCommandBuffer cmd);
        
        BufferHandle GetVoxelBuffer() const { return m_voxelBuffer; }
        
        // Thread-safe addition of pre-generated chunks
        void addChunk(Ref<Chunk> chunk)
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            m_chunks.push_back(chunk);
        }

        // Instance a single chunk (after multithreaded generation)
        void instanceChunkPublic(Ref<Chunk> chunk)
        {
            instanceChunk(*chunk); // private function used safely
        }

        std::vector<Ref<Chunk>> getChunksSnapshot()
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            return m_chunks; // copy to allow iteration safely
        }

    private:

        static uint32_t createBrickBLAS();

        void generateChunkBricks(Chunk& chunk);
        void instanceChunk(Chunk& chunk);

        void setVoxel(Brick& brick, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b);

        void ensureCapacity(size_t required);

    private:
        BufferManager* m_bufferManager = nullptr;
        std::vector<Ref<Chunk>> m_chunks;
        std::mutex m_chunkMutex;
        std::mutex m_bufferMutex;

        uint32_t m_brickBLAS = 0;

        size_t m_brickCapacity = 0;

        BufferHandle m_voxelBuffer{};
        BufferHandle m_voxelTransfer{};
    };

}
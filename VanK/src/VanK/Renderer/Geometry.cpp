#include "Geometry.h"

#include "Renderer.h"

namespace VanK
{
    void Geometry::AppendGeometry(const std::string& name, const std::vector<shaderio::InstancedVertexData>& vertices,
              std::vector<uint32_t> indices)
    {
        // 1. Remember current offsets
        uint32_t vertexOffset = CurrentVertexOffset;
        uint32_t indexOffset  = CurrentIndexOffset;

        // 2. Adjust indices so they reference the correct vertex range
        for (auto& idx : indices)
            idx += vertexOffset;

        // 3. Upload vertices into the big vertex buffer
        /*Renderer::m_InstancedVertexBuffer->Upload
        (
            vertices.data(),
            vertices.size() * sizeof(shaderio::InstancedVertexData),
            vertexOffset * sizeof(shaderio::InstancedVertexData)   // write at the correct offset
        );*/

        // Save offset + count
        Renderer::InstancedVertexRanges[name] = { vertexOffset, static_cast<uint32_t>(vertices.size()) };

        // 4. Upload indices into the big index buffer
        Renderer::m_InstancedIndexBuffer->Upload
        (
            indices.data(),
            indices.size() * sizeof(uint32_t),
            indexOffset * sizeof(uint32_t)  // write at the correct offset
        );

        // Save offset + count
        Renderer::InstancedIndexRanges[name] = { indexOffset, static_cast<uint32_t>(indices.size()) };

        // 5. Advance global offsets for the next geometry
        CurrentVertexOffset += static_cast<uint32_t>(vertices.size());
        CurrentIndexOffset  += static_cast<uint32_t>(indices.size());
    }
    
    void Geometry::AppendGeometryData(VanKCommandBuffer cmd, const std::string& name, const std::vector<shaderio::InstancedStorageData>& data)
    {
        if (data.empty()) return;

        if (Renderer::InstancedDataRanges.empty())
            CurrentStorageOffset = 0;

        uint32_t offset = CurrentStorageOffset;

        // Upload to GPU at the correct offset
        UploadBufferToGpuWithTransferRing(cmd, Renderer::m_TransferRingBuffer, Renderer::m_InstancedStorageBuffer, data, shaderio::InstancedStorageData, offset);

        // Check if this name already exists in the ranges
        if (Renderer::InstancedDataRanges.find(name) != Renderer::InstancedDataRanges.end())
        {
            // If name exists, append to existing range (extend the count)
            auto& existingRange = Renderer::InstancedDataRanges[name];
            existingRange.second += static_cast<uint32_t>(data.size());  // Add to count
        }
        else
        {
            // If name doesn't exist, create new range
            Renderer::InstancedDataRanges[name] = { offset, static_cast<uint32_t>(data.size()) };
        }
        
        // Advance global offset for next batch
        CurrentStorageOffset += static_cast<uint32_t>(data.size());
    }
}

#include "RenderGraph.h"

#include <fstream>
#include <queue>

#include <functional>

namespace std
{
    template<>
    struct hash<VanK::ResourceID>
    {
        size_t operator()(const VanK::ResourceID& id) const noexcept
        {
            if (id.type == VanK::ResourceType::Buffer)
                return std::hash<void*>()(id.buffer);
            else
                return (static_cast<size_t>(id.type) << 32) ^ id.index;
        }
    };
}

namespace VanK
{
    ResourceState StateFromUsage(ResourceUsage usage)
    {
        switch (usage)
        {
        case ResourceUsage::ComputeRead:   return {ResourceState::Stage::Compute, ResourceState::Access::ShaderRead, ResourceState::Layout::General};
        case ResourceUsage::ComputeWrite:  return {ResourceState::Stage::Compute, ResourceState::Access::ShaderWrite, ResourceState::Layout::General};
        case ResourceUsage::ColorAttachment:return {ResourceState::Stage::ColorOutput, ResourceState::Access::ColorWrite, ResourceState::Layout::ColorAttachment};
        case ResourceUsage::DepthAttachment:return {ResourceState::Stage::DepthOutput, ResourceState::Access::DepthWrite, ResourceState::Layout::DepthAttachment};
        case ResourceUsage::ShaderRead:    return {ResourceState::Stage::Fragment, ResourceState::Access::ShaderRead, ResourceState::Layout::ShaderReadOnly};
        case ResourceUsage::TransferSrc:   return {ResourceState::Stage::Transfer, ResourceState::Access::TransferRead, ResourceState::Layout::TransferSrc};
        case ResourceUsage::TransferDst:   return {ResourceState::Stage::Transfer, ResourceState::Access::TransferWrite, ResourceState::Layout::TransferDst};
        case ResourceUsage::IndirectRead: return {ResourceState::Stage::DrawIndirect,  ResourceState::Access::IndirectRead, ResourceState::Layout::General};
        }
        return {};
    }
    
    inline bool operator==(const ResourceState& a, const ResourceState& b)
    {
        return a.stage == b.stage && a.access == b.access && a.layout == b.layout;
    }

    inline bool operator!=(const ResourceState& a, const ResourceState& b)
    {
        return !(a == b);
    }
    
    Pass& RenderGraph::AddPass(const std::string& name)
    {
        passes.push_back(Pass{ .name = name });
        return passes.back();
    }

    void RenderGraph::Build()
    {
        BuildEdges();
        TopologicalSort();
    }
    
    bool RenderGraph::IsResourceUsedLaterInGraph(const ResourceID& id, Pass* currentPass) const
    {
        bool foundCurrent = false;
        for (Pass* pass : sorted)
        {
            if (pass == currentPass) { foundCurrent = true; continue; }
            if (!foundCurrent) continue;

            // Check if any writes/read use this resource
            for (auto& r : pass->reads)
                if (r.id == id) return true;
            for (auto& w : pass->writes)
                if (w.id == id) return true;
        }
        return false;
    }
    
    void RenderGraph::Execute(VanKCommandBuffer cmd)
{
    std::unordered_map<ResourceID, ResourceState> lastState;

    for (Pass* pass : sorted)
    {
        // ---- READ barriers ----
        for (auto& r : pass->reads)
        {
            ResourceState desired = StateFromUsage(r.usage);
            ResourceState old = lastState.contains(r.id) ? lastState[r.id] : ResourceState::Undefined();
            if (old != desired)
                RenderCommand::InsertBarrier(cmd, r.id, old, desired);
            lastState[r.id] = desired;
        }

        std::vector<VanKColorTargetInfo> colorAttachments;
        std::optional<VanKDepthStencilTargetInfo> depthAttachment;

        // ---- WRITE barriers & collect attachments ----
        for (auto& w : pass->writes)
        {
            ResourceState desired = StateFromUsage(w.usage);
            ResourceState old = lastState.contains(w.id) ? lastState[w.id] : ResourceState::Undefined();
            if (old != desired)
                RenderCommand::InsertBarrier(cmd, w.id, old, desired);
            lastState[w.id] = desired;

            switch (w.usage)
            {
                case ResourceUsage::ColorAttachment:
                    colorAttachments.emplace_back
                    (
                        w.id.index,
                        w.format,
                        w.loadOp,
                        w.storeOp,
                        w.clearColor
                    );
                    break;

                case ResourceUsage::ResolveAttachment:
                    // Only barrier transition, no BeginRendering
                    break;

                case ResourceUsage::DepthAttachment:
                    depthAttachment = VanKDepthStencilTargetInfo
                    {
                        w.id.index,
                        w.format,
                        w.loadOp,
                        w.storeOp,
                        w.clearColor
                    };
                    break;

                default:
                    break;
            }
        }

        // ---- Begin rendering once per pass ----
        if (!colorAttachments.empty() || depthAttachment.has_value())
        {
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), *depthAttachment);
        }

        // ---- Execute pass ----
        pass->execute();
        
        // ---- End rendering once per pass ----
        if (!colorAttachments.empty() || depthAttachment.has_value())
        {
            RenderCommand::EndRendering(cmd);
        }
        
        // ---- Post-pass barriers ----
        for (auto& w : pass->writes)
        {
            if (w.finalUsage && !IsResourceUsedLaterInGraph(w.id, pass))
            {
                ResourceState finalState = StateFromUsage(*w.finalUsage);
                if (lastState[w.id] != finalState)
                {
                    RenderCommand::InsertBarrier(cmd, w.id, lastState[w.id], finalState);
                    lastState[w.id] = finalState;
                }
            }
        }
    }
}


    void RenderGraph::Reset()
    {
        passes.clear();
        edges.clear();
        sorted.clear();
    }

    void RenderGraph::BuildEdges()
    {
        edges.clear();
        edges.resize(passes.size());

        std::unordered_map<ResourceID, uint32_t> lastWriter;

        for (uint32_t i = 0; i < passes.size(); ++i)
        {
            // ---- READ dependencies (RAW) ----
            for (auto& r : passes[i].reads)
            {
                if (lastWriter.contains(r.id))
                {
                    edges[lastWriter[r.id]].push_back({
                        i,
                        r.id,
                        r.usage,
                        true   // read
                    });
                }
            }

            // ---- WRITE dependencies (WAW) ----
            for (auto& w : passes[i].writes)
            {
                if (lastWriter.contains(w.id))
                {
                    edges[lastWriter[w.id]].push_back({
                        i,
                        w.id,
                        w.usage,
                        false  // write
                    });
                }

                // update writer AFTER adding dependency
                lastWriter[w.id] = i;
            }
        }
    }

    void RenderGraph::TopologicalSort()
    {
        sorted.clear();

        std::vector<uint32_t> indegree(passes.size(), 0);

        // compute indegree
        for (auto& edgeList : edges)
            for (auto& e : edgeList)       // e is now Edge
                indegree[e.to]++;          // use e.to

        std::queue<uint32_t> q;
        for (uint32_t i = 0; i < indegree.size(); ++i)
            if (indegree[i] == 0)
                q.push(i);

        while (!q.empty())
        {
            uint32_t n = q.front();
            q.pop();

            sorted.push_back(&passes[n]);

            for (auto& e : edges[n])
            {
                if (--indegree[e.to] == 0)
                    q.push(e.to);
            }
        }

        if (sorted.size() != passes.size())
        {
            throw std::runtime_error("RenderGraph has a cycle!");
        }
    }
    
    void RenderGraph::DumpGraphviz(const std::string& filename) const
    {
        std::ofstream file(filename);
        if (!file.is_open()) return;

        file << "digraph RenderGraph {\n";
        file << "  rankdir=LR;\n";
        file << "  node [shape=box];\n";

        // Pass nodes
        for (size_t i = 0; i < passes.size(); ++i)
            file << "  " << i << " [label=\"" << passes[i].name << "\"];\n";

        // Resource edges with usage info
        for (size_t from = 0; from < edges.size(); ++from)
        {
            for (auto& e : edges[from])
            {
                std::string label;
                switch (e.usage)
                {
                case ResourceUsage::ComputeRead:    label = "ComputeRead"; break;
                case ResourceUsage::ComputeWrite:   label = "ComputeWrite"; break;
                case ResourceUsage::ShaderRead:     label = "ShaderRead"; break;
                case ResourceUsage::ColorAttachment:label = "ColorAttachment"; break;
                case ResourceUsage::ResolveAttachment: label = "ResolveAttachment"; break;
                case ResourceUsage::DepthAttachment: label = "DepthAttachment"; break;
                case ResourceUsage::TransferSrc:    label = "TransferSrc"; break;
                case ResourceUsage::TransferDst:    label = "TransferDst"; break;
                case ResourceUsage::IndirectRead:   label = "IndirectRead"; break;
                default: label = "Unknown"; break;
                }

                if (e.resource.type == ResourceType::Image)
                    label += " Image" + std::to_string(e.resource.index);
                else
                    label += " Buffer";

                std::string color = e.isRead ? "blue" : "red"; // reads = blue, writes = red

                file << "  " << from << " -> " << e.to
                     << " [label=\"" << label << "\", color=" << color << "];\n";
            }
        }

        file << "}\n";
    }

}

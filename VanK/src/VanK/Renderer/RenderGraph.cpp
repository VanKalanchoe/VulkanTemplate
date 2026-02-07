#include "RenderGraph.h"

#include <fstream>
#include <queue>

#include <functional>
#include <unordered_set>

namespace std
{
    template <>
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
        case ResourceUsage::ComputeRead: return {ResourceState::Stage::Compute, ResourceState::Access::ShaderRead, ResourceState::Layout::General};
        case ResourceUsage::ComputeWrite: return {ResourceState::Stage::Compute, ResourceState::Access::ShaderWrite, ResourceState::Layout::General};
        case ResourceUsage::ColorAttachment: return {ResourceState::Stage::ColorOutput, ResourceState::Access::ColorWrite, ResourceState::Layout::ColorAttachment};
        case ResourceUsage::DepthAttachment: return {ResourceState::Stage::DepthOutput, ResourceState::Access::DepthWrite, ResourceState::Layout::DepthAttachment};
        case ResourceUsage::ShaderRead: return {ResourceState::Stage::Fragment, ResourceState::Access::ShaderRead, ResourceState::Layout::ShaderReadOnly};
        case ResourceUsage::TransferSrc: return {ResourceState::Stage::Transfer, ResourceState::Access::TransferRead, ResourceState::Layout::TransferSrc};
        case ResourceUsage::TransferDst: return {ResourceState::Stage::Transfer, ResourceState::Access::TransferWrite, ResourceState::Layout::TransferDst};
        case ResourceUsage::IndirectRead: return {ResourceState::Stage::DrawIndirect, ResourceState::Access::IndirectRead, ResourceState::Layout::General};
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

    Subpass& Pass::AddSubpass(const std::string& name, std::function<void()> fn)
    {
        subpasses.push_back({name, std::move(fn)});
        return subpasses.back();
    }

    Pass& RenderGraph::AddPass(const std::string& name)
    {
        passes.push_back(Pass{.name = name});
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
            if (pass == currentPass)
            {
                foundCurrent = true;
                continue;
            }
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
            if (!pass->subpasses.empty())
            {
                for (auto& sub : pass->subpasses)
                    sub.execute();
            }
            else if (pass->execute)
            {
                pass->execute();
            }

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
                        true // read
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
                        false // write
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
            for (auto& e : edgeList) // e is now Edge
                indegree[e.to]++; // use e.to

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
        file << "  compound=true;\n";
        file << "  node [shape=box, style=\"filled\", fontcolor=white];\n";
        file << "  edge [fontcolor=white];\n\n";

        auto usageToString = [](ResourceUsage usage) -> std::string
        {
            switch (usage)
            {
            case ResourceUsage::ComputeRead: return "ComputeRead";
            case ResourceUsage::ComputeWrite: return "ComputeWrite";
            case ResourceUsage::ShaderRead: return "ShaderRead";
            case ResourceUsage::ColorAttachment: return "ColorAttachment";
            case ResourceUsage::ResolveAttachment: return "ResolveAttachment";
            case ResourceUsage::DepthAttachment: return "DepthAttachment";
            case ResourceUsage::TransferSrc: return "TransferSrc";
            case ResourceUsage::TransferDst: return "TransferDst";
            case ResourceUsage::IndirectRead: return "IndirectRead";
            default: return "Unknown";
            }
        };

        auto getResourceNodeId = [](const ResourceID& id) -> std::string
        {
            switch (id.type)
            {
            case ResourceType::Image: return "res_img" + std::to_string(id.index);
            case ResourceType::Buffer: return "res_buf" + std::to_string(reinterpret_cast<uintptr_t>(id.buffer));
            case ResourceType::Dummy: return "res_dummy" + std::to_string(reinterpret_cast<uintptr_t>(&id));
            }
            return "res_unknown";
        };

        std::unordered_set<std::string> resourceNodes;
        std::unordered_map<ResourceID, ResourceUsage> lastUsage;

        // -----------------------------
        // Pass clusters
        // -----------------------------
        for (size_t i = 0; i < passes.size(); ++i)
        {
            const Pass& pass = passes[i];
            std::string clusterName = "cluster_pass" + std::to_string(i);
            std::string passCenter = "pass" + std::to_string(i) + "_center";

            file << "  subgraph " << clusterName << " {\n";
            file << "    label=\"" << pass.name << "\";\n";
            file << "    style=rounded;\n";
            file << "    color=\"#444444\";\n\n";

            // --- Input cluster ---
            file << "    subgraph cluster_inputs" << i << " {\n";
            file << "      label=\"Inputs\";\n";
            file << "      style=dashed;\n";
            file << "      color=\"#666666\";\n";

            for (auto& r : pass.reads)
            {
                std::string resNode = getResourceNodeId(r.id);
                if (resourceNodes.insert(resNode).second)
                {
                    std::string label = !r.name.empty()
                                            ? r.name
                                            : (r.id.type == ResourceType::Image ? "Image" : "Buffer");

                    // Show last barrier if exists, otherwise use current read usage
                    ResourceUsage usageToShow = lastUsage.contains(r.id) ? lastUsage[r.id] : r.usage;
                    label += "\\n" + usageToString(usageToShow);

                    file << "      " << resNode
                        << " [label=\"" << label
                        << "\", shape=ellipse, fillcolor=\"#607D8B\"];\n";
                }
            }
            file << "    }\n";

            // --- Subpasses ---
            for (size_t s = 0; s < pass.subpasses.size(); ++s)
            {
                std::string nodeId = "pass" + std::to_string(i) + "_sub" + std::to_string(s);
                file << "    " << nodeId
                    << " [label=\"" << pass.subpasses[s].name
                    << "\", fillcolor=\"#2d2d2d\"];\n";
            }
            for (size_t s = 0; s + 1 < pass.subpasses.size(); ++s)
            {
                file << "    pass" << i << "_sub" << s
                    << " -> pass" << i << "_sub" << s + 1
                    << " [color=\"#888888\"];\n";
            }

            // Invisible center for outputs
            file << "    " << passCenter
                << " [label=\"" << pass.name << "\", width=0, height=0, style=invis];\n";

            // --- Output cluster ---
            file << "    subgraph cluster_outputs" << i << " {\n";
            file << "      label=\"Outputs\";\n";
            file << "      style=dashed;\n";
            file << "      color=\"#666666\";\n";
            for (auto& w : pass.writes)
            {
                std::string resNode = getResourceNodeId(w.id);
                if (resourceNodes.insert(resNode).second)
                {
                    std::string label = !w.name.empty() ? w.name : (w.id.type == ResourceType::Image ? "Image" : "Buffer");

                    std::string before = usageToString(w.usage);
                    std::string after = w.finalUsage.has_value() ? usageToString(*w.finalUsage) : before;

                    if (before != after)
                        label += "\\n" + before + " → " + after;
                    else
                        label += "\\n" + before;

                    file << "      " << resNode
                        << " [label=\"" << label
                        << "\", shape=ellipse, fillcolor=\"#607D8B\"];\n";
                }

                // Update lastUsage for next pass
                lastUsage[w.id] = w.finalUsage.has_value() ? *w.finalUsage : w.usage;
            }
            file << "    }\n";

            file << "  }\n\n"; // Close pass cluster
        }

        // -----------------------------
        // Draw edges
        // -----------------------------
        for (size_t from = 0; from < edges.size(); ++from)
        {
            for (auto& e : edges[from])
            {
                std::string resNode = getResourceNodeId(e.resource);
                if (e.isRead)
                {
                    const Pass& targetPass = passes[e.to];
                    std::string toNode = targetPass.subpasses.empty()
                                             ? "pass" + std::to_string(e.to) + "_center"
                                             : "pass" + std::to_string(e.to) + "_sub0";

                    file << "  " << resNode << " -> " << toNode
                        << " [label=\"" << usageToString(e.usage)
                        << "\", color=\"#4FC3F7\", style=dashed];\n";
                }
                else
                {
                    const Pass& srcPass = passes[from];
                    std::string fromNode = srcPass.subpasses.empty()
                                               ? "pass" + std::to_string(from) + "_center"
                                               : "pass" + std::to_string(from) + "_sub" + std::to_string(srcPass.subpasses.size() - 1);

                    file << "  " << fromNode << " -> " << resNode
                        << " [label=\"" << usageToString(e.usage)
                        << "\", color=\"#FF5252\", style=bold];\n";
                }
            }
        }

        file << "}\n"; // Close digraph
    }
}
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "VanK/Renderer/RenderCommand.h"

namespace VanK
{
    struct Subpass
    {
        std::string name;
        std::function<void()> execute;
    };

    struct Pass
    {
        std::string name;
        std::vector<ResourceReadWrite> reads;
        std::vector<ResourceReadWrite> writes;
        std::function<void()> execute;

        std::vector<Subpass> subpasses;

        Subpass& AddSubpass(const std::string& name, std::function<void()> fn);
    };

    struct Edge
    {
        uint32_t to;
        ResourceID resource; // the ID that caused the dependency
        ResourceUsage usage;
        bool isRead; // true = read, false = write
    };

    class RenderGraph
    {
    public:
        Pass& AddPass(const std::string& name);
        void Build();
        bool IsResourceUsedLaterInGraph(const ResourceID& id, Pass* currentPass) const;
        void Execute(VanKCommandBuffer cmd);
        void Reset();
        void DumpGraphviz(const std::string& filename) const;

    private:
        std::vector<Pass> passes;
        std::vector<std::vector<Edge>> edges;
        std::vector<Pass*> sorted;

        void BuildEdges();
        static bool WritesWhatBReads(const Pass& A, const Pass& B);
        void TopologicalSort();
    };
}

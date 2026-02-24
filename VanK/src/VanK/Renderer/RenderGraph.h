#pragma once

#include <functional>
#include <string>
#include <vector>

#include "VanK/Core/ProfilerAPI.h"
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
    
    struct FinalOutput
    {
        uint32_t renderImageIndex;
        ImTextureID imGuiID;
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
        
        // --- Final output getter/setter ---
        void SetFinalOutput(uint32_t id, ImTextureID imGuiID) { finalOutput = FinalOutput{id, imGuiID};   }
        FinalOutput GetFinalOutput() const { return finalOutput; }

    private:
        std::vector<Pass> passes;
        std::vector<std::vector<Edge>> edges;
        std::vector<Pass*> sorted;
        
        FinalOutput finalOutput = {};

        void BuildEdges();
        void TopologicalSort();
    };
}

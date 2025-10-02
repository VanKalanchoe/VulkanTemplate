#pragma once
#include "VanK/Core/Layer.h"

namespace VanK
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
        void OnEvent(Event& event) override;
    };
}

#pragma once

#include "VanK/Core/Layer.h"

namespace VanK
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override;
        
        void ShutDown();
        void OnUpdate(Timestep ts) override;
        void OnRender() override;
        void OnImGuiRender() override;
        
        void SetDarkThemeColors();
        
        uint32_t GetActiveWidgetID() const;
    };
}

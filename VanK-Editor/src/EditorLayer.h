#pragma once
#include "VanK/Core/Layer.h"

namespace VanK
{
    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
    };
}
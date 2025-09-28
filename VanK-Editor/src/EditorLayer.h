#pragma once
#include "VanK/Core/Layer.h"

class EditorLayer : public VanK::Layer
{
    public:
    EditorLayer();
    virtual ~EditorLayer() override;

    virtual void OnUpdate(float ts) override;
    virtual void OnRender() override;

    private:
    
};
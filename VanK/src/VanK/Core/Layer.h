#pragma once

#include "Event.h"

namespace VanK
{
    class Layer
    {
    public:
        virtual ~Layer() = default;

        virtual void OnEvent(Event& event) {}

        virtual void OnUpdate(float ts) {}
        virtual void OnRender() {}
    };
}
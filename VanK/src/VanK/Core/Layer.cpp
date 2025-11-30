#include "Layer.h"

namespace VanK
{
    Layer::Layer(const std::string& m_Name)
        : m_Name(m_Name)
    {
    };
    
    void Layer::QueueTransition(std::unique_ptr<Layer> toLayer)
    {
        
    }
}
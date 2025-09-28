#include "EditorLayer.h"

#include <iostream>
#include <ostream>

#include "VanK/Core/Application.h"
#include "VanK/Renderer/RenderCommand.h"

EditorLayer::EditorLayer()
{
    
}

EditorLayer::~EditorLayer()
{
    
}

void EditorLayer::OnUpdate(float ts)
{
    
}

void EditorLayer::OnRender()
{
    VanK::RenderCommand::Render();
}
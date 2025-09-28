#pragma once
#include <string>
#include <SDL3/SDL_init.h>

namespace VanK
{
    struct WindowSpecification
    {
        std::string Title;
        uint32_t Width = 1280;
        uint32_t Height = 720;
        bool IsResizeable = true;
        bool VSync = true;
    };
    
    class Window
    {
    public:
        Window(const WindowSpecification& specification = WindowSpecification());
        ~Window();
        
        void initWindow();
        void Destroy();

    public:
        virtual SDL_Window* getWindowHandle() { return window; }
    private:
        WindowSpecification m_Specification;
        
        SDL_Window* window = nullptr;
    };
}

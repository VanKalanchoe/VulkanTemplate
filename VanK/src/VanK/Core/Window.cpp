#include "Window.h"

#include <SDL3/SDL_log.h>

namespace VanK
{
    Window::Window(const WindowSpecification& specification) : m_Specification(specification)
    {
    }

    Window::~Window()
    {
        Destroy();
    }

    void Window::initWindow()
    {
        /*VK_CORE_INFO("Create Window");*/
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        }

        window = SDL_CreateWindow("Vulkan", m_Specification.Width, m_Specification.Height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

        if (!window)
        {
            SDL_Log("Couldn't create window: %s", SDL_GetError());
        }

        SDL_SetWindowMinimumSize(window, 640, 480); // maybe change this dependeing on 16/9 17/4 soemthing
    }

    void Window::Destroy()
    {
        /*VK_CORE_INFO("delete Window");*/
        SDL_DestroyWindow(window);
    }
}

#include "Renderer.h"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_SetAppMetadata("Example Renderer Clear", "1.0", "com.example.renderer-clear");

    try
    {
        auto app = std::make_unique<Renderer>();
        app->init();

        *appstate = app.release();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto app = static_cast<Renderer*>(appstate);
    app->render();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto app = static_cast<Renderer*>(appstate);

    ImGui_ImplSDL3_ProcessEvent(event);

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        {
            return SDL_APP_SUCCESS;
        }
    case SDL_EVENT_WINDOW_RESIZED:
        {
            app->setFramebufferResized(true);
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_WINDOW_MINIMIZED:
        {
            app->setWindowMinimized(true);
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_WINDOW_RESTORED:
        {
            app->setWindowMinimized(false);
            app->setFramebufferResized(true); // force swapchain recreation
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_KEY_DOWN:
        {
            if (event->key.scancode == SDL_SCANCODE_ESCAPE)
            {
                return SDL_APP_SUCCESS;
            }
        }
    default:
        return SDL_APP_CONTINUE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto app = static_cast<Renderer*>(appstate);
    app->shutdown();
    delete app;
}

// SDL2 launcher rendering. Coordinates stay in logical window units.
#ifndef INCLUDED_GUI_RENDERER_H
#define INCLUDED_GUI_RENDERER_H

#include <SDL.h>
#include "Pkgs/imgui/imgui.h"
#include "Pkgs/imgui/imgui_impl_sdlrenderer2.h"

inline void RenderLauncherDrawData(ImDrawData* drawData, SDL_Renderer* renderer)
{
    // SDL_RenderGeometryRaw does not apply ImGui's framebuffer scale itself.
    // The backend detects this SDL scale and keeps its clip rectangles logical.
    SDL_RenderSetScale(renderer, drawData->FramebufferScale.x, drawData->FramebufferScale.y);
    ImGui_ImplSDLRenderer2_RenderDrawData(drawData, renderer);
}

#endif

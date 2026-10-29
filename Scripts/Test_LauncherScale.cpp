// Offscreen regression test: Retina geometry and clip rectangles fill the target.
#define SDL_MAIN_HANDLED
#include "OSD/SDL/GuiRenderer.h"
#include <cstdio>
#include <stdexcept>

static void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static void Run(float scale)
{
    const int width = int(100 * scale), height = int(80 * scale);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    Check(surface != nullptr, "Create target");
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    Check(renderer != nullptr, "Create renderer");
    ImGui::CreateContext();
    Check(ImGui_ImplSDLRenderer2_Init(renderer), "Initialize ImGui renderer");
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(100, 80);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(255, 0, 0, 255));
    draw->PushClipRect(ImVec2(0, 0), ImVec2(50, 80), true);
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 255, 0, 255));
    draw->PopClipRect();
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    RenderLauncherDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
    auto pixel = [&](int x, int y, bool green) {
        const Uint32 value = *(const Uint32*)((const Uint8*)surface->pixels + y * surface->pitch + x * 4);
        Uint8 r, g, b, a;
        SDL_GetRGBA(value, surface->format, &r, &g, &b, &a);
        return r == (green ? 0 : 255) && g == (green ? 255 : 0) && b == 0 && a == 255;
    };
    Check(pixel(width - 3, height - 3, false), "Geometry did not reach the lower-right corner");
    Check(pixel(width / 4, height - 3, true), "Left-side clipping/scale mismatch");
    Check(pixel(width * 3 / 4, height / 2, false), "Right-side clipping/scale mismatch");
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    std::printf("PASS: launcher geometry and clipping at %.1fx\n", scale);
}

int main()
{
    SDL_SetMainReady();
    try { Run(1); Run(1.5f); Run(2); }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s (%s)\n", e.what(), SDL_GetError()); return 1; }
    return 0;
}

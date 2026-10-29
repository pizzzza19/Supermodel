// ROM-free native Metal smoke test. See Docs/Metal.md.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "Graphics/Metal3D/MetalRenderer.h"
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <vector>

static unsigned errors = 0;
void DebugLog(const char*, ...) {}
void InfoLog(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt); vprintf(fmt, args); va_end(args); puts("");
}
Result ErrorLog(const char* fmt, ...)
{
  ++errors;
  va_list args;
  va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); fputs("\n", stderr);
  return Result::FAIL;
}
static void Check(bool ok, const char* operation)
{
  if (!ok) throw std::runtime_error(operation);
}
static void Run(SDL_Window* window)
{
  Metal3D::CMetalContext context;
  Check(context.Init(window, false) == Result::OKAY, "Metal context initialization");
  Util::Config::Node config("Global");
  config.Set("WideScreen", false);
  config.Set("WideBackground", false);
  config.Set("NoWhiteFlash", false);
  auto bottom = std::make_shared<TileGenBuffer>();
  auto top = std::make_shared<TileGenBuffer>();
  for (unsigned y = 0; y < 384; ++y)
    for (unsigned x = 0; x < 496; ++x) {
      bottom->data[y * 512 + x] = ((x / 16 + y / 16) & 1) ? 0xff40a020 : 0xff2040d0;
      // An asymmetric transparent overlay makes orientation errors easy to see.
      top->data[y * 512 + x] = x < 80 && y < 80 ? 0x800000ff : 0;
    }
  std::vector<uint32_t> culling(0x100000);
  std::vector<uint16_t> textures(2048 * 2048, 0x7fff);
  // Disable the synthetic root viewport and terminate its linked list.
  culling[0] = 0x20;
  culling[1] = 0x01000000;
  Metal3D::CMetal2D output(config, context);
  Metal3D::CMetal3D scene(config, context);
  scene.SetStepping(Game::Stepping::MODEL3_2_1);
  scene.AttachMemory(culling.data(), culling.data(), culling.data(), culling.data(), textures.data());
  for (int pass = 0; pass < 8; ++pass) {
    if (pass == 4) {
      SDL_SetWindowSize(window, 960, 720);
      SDL_PumpEvents();
      Check(context.Resize(), "Resize");
    }
    if (pass == 6) {
      Check(context.Resize(320, 240), "Fixed render resolution");
      Check(context.Width() <= 320 && context.Height() <= 240, "Render resolution bound");
    }
    unsigned w = context.Width(), h = context.Height();
    Check(w > 0 && h > 0, "Drawable dimensions");
    Check(output.Init(0, 0, w, h, w, h, 0, static_cast<UpscaleMode>(pass % 4)) == Result::OKAY, "2D initialization");
    Check(scene.Init(0, 0, w, h, w, h, 0) == Result::OKAY, "3D initialization");
    output.AttachDrawBuffers(bottom, top);
    unsigned rendered = 0;
    for (unsigned attempt = 0; attempt < 200 && rendered < 12; ++attempt) {
      SDL_PumpEvents();
      if (!context.BeginFrame()) { SDL_Delay(10); continue; }
      output.BeginFrame(); scene.BeginFrame(); output.PreRenderFrame();
      output.RenderFrameBottom();
      // Alternate white flash and a transparent empty 3D scene.
      scene.SetBlockCulling((rendered & 1) != 0);
      // Exercise full, partial, cross-page and clipped edge transfers.
      switch (rendered % 4) {
        case 0: scene.UploadTextures(0, 0, 0, 2048, 2048); break;
        case 1:
          scene.UploadTextures(0, 16, 24, 32, 32);
          scene.UploadTextures(0, 80, 1100, 64, 16);
          break;
        case 2: scene.UploadTextures(0, 128, 1016, 32, 16); break;
        case 3:
          scene.UploadTextures(0, 2040, 2040, 32, 32);
          scene.UploadTextures(0, 2048, 0, 32, 32);
          scene.UploadTextures(0, 0, 0, 0, 32);
          break;
      }
      scene.RenderFrame(); output.RenderFrameTop(); scene.EndFrame(); output.EndFrame();
      context.EndFrame();
      Check(errors == 0, "Metal shader, allocation or GPU command error");
      ++rendered;
    }
    Check(rendered == 12, "Present requested frames");
  }
}
int main()
{
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  SDL_Window* window = SDL_CreateWindow("Native Metal smoke test", SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED, 496, 384, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
  int result = 0;
  try {
    Check(window != nullptr, SDL_GetError());
    Run(window); Run(window);
    Check(errors == 0, "Metal errors logged");
    puts("PASS: shader/pipeline creation, tile/page uploads, four filters, 3D clears, presentation, resize and context recreation.");
  } catch (const std::exception& e) {
    fprintf(stderr, "FAIL: %s\n", e.what()); result = 1;
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}

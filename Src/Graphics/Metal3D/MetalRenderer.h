/* Native macOS Metal backend. GPL-3.0-or-later; see Docs/LICENSE.txt. */
#ifndef INCLUDED_METAL_RENDERER_H
#define INCLUDED_METAL_RENDERER_H

#include "Graphics/IRender2D.h"
#include "Graphics/IRender3D.h"
#include "Util/NewConfig.h"
#include <memory>

struct SDL_Window;

namespace Metal3D {

// Objective-C and Metal types stay in the .mm implementation.
class CMetalContext
{
public:
  CMetalContext();
  ~CMetalContext();
  Result Init(SDL_Window* window, bool vsync);
  // Optional fixed render size; the layer scales it to the window/display.
  bool Resize(unsigned renderWidth = 0, unsigned renderHeight = 0);
  unsigned Width() const;
  unsigned Height() const;
  bool BeginFrame();
  void EndFrame();
private:
  struct Impl;
  std::unique_ptr<Impl> m;
  friend class CMetal2D;
  friend class CMetal3D;
};

class CMetal2D final : public IRender2D
{
public:
  CMetal2D(const Util::Config::Node& config, CMetalContext& context);
  ~CMetal2D() override;
  Result Init(unsigned x, unsigned y, unsigned width, unsigned height,
              unsigned totalWidth, unsigned totalHeight, unsigned target, UpscaleMode upscale) override;
  void BeginFrame() override;
  void PreRenderFrame() override;
  void RenderFrameBottom() override;
  void RenderFrameTop() override;
  void EndFrame() override;
  void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top) override;
private:
  struct Impl;
  std::unique_ptr<Impl> m;
};

class CMetal3D final : public IRender3D
{
public:
  CMetal3D(const Util::Config::Node& config, CMetalContext& context);
  ~CMetal3D() override;
  Result Init(unsigned x, unsigned y, unsigned width, unsigned height,
              unsigned totalWidth, unsigned totalHeight, unsigned target) override;
  void BeginFrame() override;
  void RenderFrame() override;
  void EndFrame() override;
  void UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height) override;
  void AttachMemory(const uint32_t* lo, const uint32_t* hi, const uint32_t* polygon,
                    const uint32_t* vrom, const uint16_t* texture) override;
  void SetStepping(Game::Stepping stepping) override;
  void SetSunClamp(bool enable) override;
  void SetBlockCulling(bool enable) override;
  float GetLosValue(int layer) override;
private:
  struct Impl;
  std::unique_ptr<Impl> m;
};

} // namespace Metal3D
#endif

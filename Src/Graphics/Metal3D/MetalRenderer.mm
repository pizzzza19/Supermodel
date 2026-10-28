/*
 * Supermodel native Metal renderer. GPL-3.0-or-later.
 * Real3D material, fog and draw ordering derived from Supermodel-VK,
 * Copyright 2026 The Supermodel-VK authors, and Supermodel,
 * Copyright 2003-2026 The Supermodel Team. See Docs/LICENSE.txt.
 */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL.h>
#include <SDL_metal.h>
#include "MetalRenderer.h"
#include "Graphics/Vulkan3D/R3DSceneBuilder.h"
#include "Graphics/R3DShared.h"
#include "Graphics/Vulkan3D/PictureLayout.h"
#include "OSD/Logger.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "Shaders/real3d_vert.msl.h"
#include "Shaders/real3d_frag.msl.h"
#include "Shaders/fog_vert.msl.h"
#include "Shaders/fog_frag.msl.h"
#include "Shaders/composite_vert.msl.h"
#include "Shaders/composite_frag.msl.h"

namespace Metal3D {
using namespace Vulkan3D; // GPU-independent Real3D layouts and scene decoder.

static id<MTLFunction> Compile(id<MTLDevice> device, const char* source, NSString* entry)
{
  NSError* error = nil;
  MTLCompileOptions* options = [MTLCompileOptions new];
  options.languageVersion = MTLLanguageVersion2_1;
  options.fastMathEnabled = NO;
  id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source]
                                              options:options error:&error];
  if (!library)
    ErrorLog("Metal: shader %s failed: %s", entry.UTF8String, error.localizedDescription.UTF8String);
  id<MTLFunction> function = [library newFunctionWithName:entry];
  if (library && !function)
    ErrorLog("Metal: missing shader entry %s", entry.UTF8String);
  return function;
}

static id<MTLTexture> Texture(id<MTLDevice> device, MTLPixelFormat format,
                              unsigned width, unsigned height, bool target)
{
  MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                              width:width height:height mipmapped:NO];
  desc.storageMode = target ? MTLStorageModePrivate :
                     (device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged);
  desc.usage = target ? MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead : MTLTextureUsageShaderRead;
  id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
  if (!texture)
    ErrorLog("Metal: failed to allocate %ux%u texture (format %u)", width, height, (unsigned)format);
  return texture;
}

static void Blend(MTLRenderPipelineColorAttachmentDescriptor* attachment, bool enabled, bool premultiplied)
{
  attachment.blendingEnabled = enabled;
  attachment.sourceRGBBlendFactor = premultiplied ? MTLBlendFactorOne : MTLBlendFactorSourceAlpha;
  attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
  attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
}

static id<MTLRenderPipelineState> Pipeline(id<MTLDevice> device, id<MTLFunction> vertex,
  id<MTLFunction> fragment, MTLVertexDescriptor* layout, bool depth, bool blend, bool premultiplied)
{
  if (!vertex || !fragment) return nil;
  MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
  desc.vertexFunction = vertex;
  desc.fragmentFunction = fragment;
  desc.vertexDescriptor = layout;
  desc.colorAttachments[0].pixelFormat = depth ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatBGRA8Unorm;
  desc.depthAttachmentPixelFormat = depth ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
  Blend(desc.colorAttachments[0], blend, premultiplied);
  NSError* error = nil;
  id<MTLRenderPipelineState> result = [device newRenderPipelineStateWithDescriptor:desc error:&error];
  if (!result) ErrorLog("Metal: pipeline creation failed: %s", error.localizedDescription.UTF8String);
  return result;
}

struct CMetalContext::Impl
{
  SDL_Window* window = nullptr;
  SDL_MetalView view = nullptr;
  CAMetalLayer* layer = nil;
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  id<MTLCommandBuffer> command = nil;
  id<CAMetalDrawable> drawable = nil;
  id<MTLTexture> sceneColor = nil;
  unsigned width = 0, height = 0;
  unsigned renderWidth = 0, renderHeight = 0;
  bool failed = false;
  bool profile = std::getenv("SUPERMODEL_METAL_PROFILE") != nullptr;
  uint64_t frameStart = 0, lastFrameStart = 0;
  unsigned profileFrames = 0;
  double intervalMs = 0, drawableMs = 0, encodeMs = 0, waitMs = 0, gpuMs = 0;
  double Milliseconds(uint64_t ticks) const {
    return ticks * 1000.0 / SDL_GetPerformanceFrequency();
  }
};

CMetalContext::CMetalContext() : m(new Impl) {}
CMetalContext::~CMetalContext()
{
  @autoreleasepool {
    // Normally EndFrame has drained the queue. Also handle an aborted frame.
    if (m->command && m->command.status == MTLCommandBufferStatusNotEnqueued) {
      [m->command commit];
      [m->command waitUntilCompleted];
    }
    m->command = nil;
    m->drawable = nil;
    m->sceneColor = nil;
    m->layer = nil;
    if (m->view) SDL_Metal_DestroyView(m->view);
  }
}
Result CMetalContext::Init(SDL_Window* window, bool vsync)
{
  @autoreleasepool {
    m->window = window;
    m->device = MTLCreateSystemDefaultDevice();
    if (!m->device) return ErrorLog("Metal: no compatible GPU found.");
    m->queue = [m->device newCommandQueue];
    if (!m->queue) return ErrorLog("Metal: cannot create command queue.");
    m->view = SDL_Metal_CreateView(window);
    if (!m->view) return ErrorLog("Metal: SDL_Metal_CreateView failed: %s", SDL_GetError());
    m->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(m->view);
    if (!m->layer) return ErrorLog("Metal: SDL did not provide a CAMetalLayer.");
    m->layer.device = m->device;
    m->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m->layer.framebufferOnly = YES;
    m->layer.displaySyncEnabled = vsync;
    if (!Resize()) return Result::FAIL;
    InfoLog("Metal: native renderer on %s (%ux%u).", m->device.name.UTF8String, m->width, m->height);
    return Result::OKAY;
  }
}
bool CMetalContext::Resize(unsigned renderWidth, unsigned renderHeight)
{
  int width = 0, height = 0;
  SDL_Metal_GetDrawableSize(m->window, &width, &height);
  if (renderWidth && renderHeight) {
    m->renderWidth = renderWidth;
    m->renderHeight = renderHeight;
  }
  const unsigned previousWidth = m->width, previousHeight = m->height;
  m->width = std::max(width, 0);
  m->height = std::max(height, 0);
  if (m->width && m->height && m->renderWidth && m->renderHeight) {
    // Bound rendering by the requested size while keeping display proportions.
    // CAMetalLayer scales the drawable to its bounds; mismatched proportions
    // here would stretch both the 3D scene and the 2D overlays.
    const double scale = std::min(double(m->renderWidth) / m->width,
                                  double(m->renderHeight) / m->height);
    m->width = std::max(1u, (unsigned)std::lround(m->width * scale));
    m->height = std::max(1u, (unsigned)std::lround(m->height * scale));
  }
  m->layer.drawableSize = CGSizeMake(m->width, m->height);
  if (m->width != previousWidth || m->height != previousHeight)
    InfoLog("Metal: render resolution %ux%u, display drawable %dx%d.", m->width, m->height, width, height);
  return true;
}
unsigned CMetalContext::Width() const { return m->width; }
unsigned CMetalContext::Height() const { return m->height; }
bool CMetalContext::BeginFrame()
{
  @autoreleasepool {
    if (m->failed || !m->width || !m->height) return false;
    const uint64_t start = m->profile ? SDL_GetPerformanceCounter() : 0;
    // SDL may refresh the layer size after a fullscreen/Retina layout event.
    // Keep the acquired drawable at the renderer's configured pixel size.
    if (m->layer.drawableSize.width != m->width || m->layer.drawableSize.height != m->height)
      m->layer.drawableSize = CGSizeMake(m->width, m->height);
    m->drawable = [m->layer nextDrawable];
    if (!m->drawable) return false; // Minimized or temporarily unavailable.
    if (m->profile) {
      m->frameStart = SDL_GetPerformanceCounter();
      m->drawableMs += m->Milliseconds(m->frameStart - start);
      if (m->lastFrameStart) m->intervalMs += m->Milliseconds(start - m->lastFrameStart);
      m->lastFrameStart = start;
    }
    m->command = [m->queue commandBuffer];
    return m->command != nil;
  }
}
void CMetalContext::EndFrame()
{
  @autoreleasepool {
    if (!m->command) return;
    const uint64_t submitted = m->profile ? SDL_GetPerformanceCounter() : 0;
    [m->command presentDrawable:m->drawable];
    [m->command commit];
    // One frame in flight makes CPU texture writes, geometry growth, readback
    // and resize safe on both discrete Intel GPUs and Apple unified memory.
    [m->command waitUntilCompleted];
    if (m->command.status == MTLCommandBufferStatusError) {
      ErrorLog("Metal: GPU command failed: %s", m->command.error.localizedDescription.UTF8String);
      m->failed = true;
    }
    if (m->profile) {
      m->encodeMs += m->Milliseconds(submitted - m->frameStart);
      m->waitMs += m->Milliseconds(SDL_GetPerformanceCounter() - submitted);
      m->gpuMs += (m->command.GPUEndTime - m->command.GPUStartTime) * 1000.0;
      if (++m->profileFrames == 120) {
        InfoLog("Metal profile %ux%u (drawable %lux%lu): interval %.2f ms, drawable %.2f ms, CPU encode %.2f ms, GPU wait %.2f ms, GPU execution %.2f ms (120-frame averages).",
          m->width, m->height, (unsigned long)m->drawable.texture.width,
          (unsigned long)m->drawable.texture.height, m->intervalMs / 120, m->drawableMs / 120,
          m->encodeMs / 120, m->waitMs / 120, m->gpuMs / 120);
        m->profileFrames = 0;
        m->intervalMs = m->drawableMs = m->encodeMs = m->waitMs = m->gpuMs = 0;
      }
    }
    m->command = nil;
    m->drawable = nil;
  }
}

struct CMetal2D::Impl
{
  const Util::Config::Node& config;
  CMetalContext::Impl& context;
  std::shared_ptr<TileGenBuffer> buffers[2];
  id<MTLTexture> tiles[2] = {nil, nil};
  id<MTLRenderPipelineState> pipeline = nil;
  id<MTLSamplerState> sampler = nil;
  unsigned x = 0, y = 0, width = 496, height = 384;
  int correction = 0;
  UpscaleMode upscale = UpscaleMode::Bilinear;
  bool bottom = false, top = false;
  Impl(const Util::Config::Node& cfg, CMetalContext::Impl& ctx) : config(cfg), context(ctx) {}
};
CMetal2D::CMetal2D(const Util::Config::Node& config, CMetalContext& context) : m(new Impl(config, *context.m)) {}
CMetal2D::~CMetal2D() = default;
Result CMetal2D::Init(unsigned x, unsigned y, unsigned width, unsigned height,
                      unsigned, unsigned, unsigned, UpscaleMode upscale)
{
  @autoreleasepool {
    m->x = x; m->y = y; m->width = width; m->height = height; m->upscale = upscale;
    m->correction = (int)((float)height / 384.0f * 2.0f + 0.5f);
    if (m->pipeline) return Result::OKAY;
    auto device = m->context.device;
    m->tiles[0] = Texture(device, MTLPixelFormatRGBA8Unorm, 496, 384, false);
    m->tiles[1] = Texture(device, MTLPixelFormatRGBA8Unorm, 496, 384, false);
    auto vert = Compile(device, composite_vert_msl, @"composite_vert");
    auto frag = Compile(device, composite_frag_msl, @"composite_frag");
    m->pipeline = Pipeline(device, vert, frag, nil, false, true, false);
    MTLSamplerDescriptor* sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = MTLSamplerMinMagFilterLinear;
    sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    m->sampler = [device newSamplerStateWithDescriptor:sampler];
    return m->pipeline && m->sampler && m->tiles[0] && m->tiles[1] ? Result::OKAY : Result::FAIL;
  }
}
void CMetal2D::AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top)
{ m->buffers[0] = bottom; m->buffers[1] = top; }
void CMetal2D::BeginFrame() { m->bottom = m->top = false; }
void CMetal2D::PreRenderFrame()
{
  for (int i = 0; i < 2; ++i)
    if (m->buffers[i] && m->context.command)
      [m->tiles[i] replaceRegion:MTLRegionMake2D(0, 0, 496, 384) mipmapLevel:0
                      withBytes:m->buffers[i]->data bytesPerRow:512 * 4];
}
void CMetal2D::RenderFrameBottom() { m->bottom = true; }
void CMetal2D::RenderFrameTop() { m->top = true; }
void CMetal2D::EndFrame()
{
  @autoreleasepool {
    auto& ctx = m->context;
    if (!ctx.command || !m->pipeline) return;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = ctx.drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLRenderCommandEncoder> enc = [ctx.command renderCommandEncoderWithDescriptor:pass];
    [enc setRenderPipelineState:m->pipeline];
    [enc setFragmentSamplerState:m->sampler atIndex:0];
    [enc setFragmentTexture:m->tiles[0] atIndex:0];
    [enc setFragmentTexture:(ctx.sceneColor ? ctx.sceneColor : m->tiles[0]) atIndex:1];
    [enc setFragmentTexture:m->tiles[1] atIndex:2];
    bool wideBg = m->config["WideBackground"].ValueAs<bool>();
    bool wide = m->config["WideScreen"].ValueAs<bool>();
    int px, py, pw, ph;
    ComputePictureRect(ctx.width, ctx.height, m->x, m->y, m->width, m->height,
                       m->correction, wideBg, wide, px, py, pw, ph);
    auto draw = [&](uint32_t layer, int x, int y, int w, int h) {
      int left = std::max({0, px, x}), top = std::max({0, py, y});
      int right = std::min({(int)ctx.width, px + pw, x + w});
      int bottom = std::min({(int)ctx.height, py + ph, y + h});
      if (w <= 0 || h <= 0 || right <= left || bottom <= top) return;
      [enc setViewport:MTLViewport{(double)x, (double)y, (double)w, (double)h, 0, 1}];
      [enc setScissorRect:MTLScissorRect{(NSUInteger)left, (NSUInteger)top,
                                       (NSUInteger)(right-left), (NSUInteger)(bottom-top)}];
      struct { uint32_t layer; int32_t upscale; } push = {layer,
        layer == 1 ? (int32_t)UpscaleMode::Bilinear : (int32_t)m->upscale};
      [enc setFragmentBytes:&push length:sizeof(push) atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    };
    int y = (int)ctx.height - ((int)m->y + m->correction + (int)m->height);
    if (m->bottom && m->buffers[0]) {
      if (wideBg) draw(0, 0, 0, ctx.width, ctx.height);
      else draw(0, (int)m->x - m->correction, y, m->width, m->height);
    }
    if (ctx.sceneColor) draw(1, 0, 0, ctx.width, ctx.height);
    if (m->top && m->buffers[1]) draw(2, (int)m->x - m->correction, y, m->width, m->height);
    [enc endEncoding];
  }
}

struct CMetal3D::Impl
{
  const Util::Config::Node& m_config;
  CMetalContext::Impl& context;
  CR3DSceneBuilder m_scene;
  std::atomic<bool> m_sunClamp{true}, m_blockCulling{false};
  bool m_noWhiteFlash = false;
  unsigned x = 0, y = 0, width = 496, height = 384, totalX = 496, totalY = 384;
  id<MTLRenderPipelineState> opaque = nil, translucent = nil, fog = nil;
  id<MTLDepthStencilState> depthWrite = nil, depthOverlay = nil, depthOff = nil;
  id<MTLTexture> color = nil, depth = nil, pages[2] = {nil, nil};
  id<MTLSamplerState> sampler = nil;
  id<MTLBuffer> rom = nil, ram = nil, los = nil;
  size_t romCount = 0, ramCount = 0;
  const uint16_t* textureRAM = nullptr;
  std::mutex textureMutex, losMutex;
  struct DirtyRegion {
    unsigned left = 0, top = 0, right = 2048, bottom = 1024;
    bool pending = true;
  };
  DirtyRegion dirty[2];
  bool losReady[4] = {};
  float losValues[4] = {};
  unsigned profileFrames = 0;
  double buildMs = 0, textureMs = 0, geometryMs = 0, drawMs = 0;
  Impl(const Util::Config::Node& config, CMetalContext::Impl& ctx) : m_config(config), context(ctx)
  { m_noWhiteFlash = config["NoWhiteFlash"].ValueAs<bool>(); }
  bool EnsureTargets();
  bool UploadGeometry();
  void FillMaterial(R3DMaterial& material, const New3D::Mesh& mesh, const New3D::Model& model);
  void FillPush(R3DPush& push, const New3D::Model& model, const New3D::Viewport& vp,
                uint32_t materialIndex, bool discardAlpha) const;
  bool Viewport(id<MTLRenderCommandEncoder> enc, const New3D::Viewport& vp);
  void DrawFog(id<MTLRenderCommandEncoder> enc);
  void DrawScene();
  void CopyLos(int priority);
};

CMetal3D::CMetal3D(const Util::Config::Node& config, CMetalContext& context) : m(new Impl(config, *context.m)) {}
CMetal3D::~CMetal3D() { m->context.sceneColor = nil; }
Result CMetal3D::Init(unsigned x, unsigned y, unsigned width, unsigned height,
                      unsigned totalX, unsigned totalY, unsigned)
{
  @autoreleasepool {
    m->x = x; m->y = y; m->width = width; m->height = height; m->totalX = totalX; m->totalY = totalY;
    auto device = m->context.device;
    if (!m->opaque) {
      auto vert = Compile(device, real3d_vert_msl, @"real3d_vert");
      auto frag = Compile(device, real3d_frag_msl, @"real3d_frag");
      MTLVertexDescriptor* layout = [MTLVertexDescriptor vertexDescriptor];
      const MTLVertexFormat formats[] = {MTLVertexFormatFloat3, MTLVertexFormatFloat3,
        MTLVertexFormatFloat2, MTLVertexFormatUInt, MTLVertexFormatUInt,
        MTLVertexFormatFloat, MTLVertexFormatFloat, MTLVertexFormatFloat3};
      const NSUInteger offsets[] = {0, 12, 24, 32, 36, 40, 44, 48};
      for (int i = 0; i < 8; ++i) {
        layout.attributes[i].format = formats[i];
        layout.attributes[i].offset = offsets[i];
        layout.attributes[i].bufferIndex = 8;
      }
      layout.layouts[8].stride = sizeof(R3DVertex);
      m->opaque = Pipeline(device, vert, frag, layout, true, false, false);
      m->translucent = Pipeline(device, vert, frag, layout, true, true, false);
      m->fog = Pipeline(device, Compile(device, fog_vert_msl, @"fog_vert"),
                        Compile(device, fog_frag_msl, @"fog_frag"), nil, true, true, false);
      MTLDepthStencilDescriptor* ds = [MTLDepthStencilDescriptor new];
      ds.depthCompareFunction = MTLCompareFunctionGreater;
      ds.depthWriteEnabled = YES;
      m->depthWrite = [device newDepthStencilStateWithDescriptor:ds];
      ds.depthCompareFunction = MTLCompareFunctionGreaterEqual;
      m->depthOverlay = [device newDepthStencilStateWithDescriptor:ds];
      ds.depthWriteEnabled = NO;
      ds.depthCompareFunction = MTLCompareFunctionAlways;
      m->depthOff = [device newDepthStencilStateWithDescriptor:ds];
      m->pages[0] = Texture(device, MTLPixelFormatR16Uint, 2048, 1024, false);
      m->pages[1] = Texture(device, MTLPixelFormatR16Uint, 2048, 1024, false);
      MTLSamplerDescriptor* sampler = [MTLSamplerDescriptor new];
      m->sampler = [device newSamplerStateWithDescriptor:sampler];
      m->los = [device newBufferWithLength:4 * 256 options:MTLResourceStorageModeShared];
      if (m->los) std::memset(m->los.contents, 0, m->los.length);
    }
    if (!m->opaque || !m->translucent || !m->fog || !m->depthWrite || !m->depthOverlay || !m->depthOff ||
        !m->pages[0] || !m->pages[1] || !m->sampler || !m->los) return Result::FAIL;
    return m->EnsureTargets() ? Result::OKAY : Result::FAIL;
  }
}
bool CMetal3D::Impl::EnsureTargets()
{
  if (!context.width || !context.height) return false;
  if (color.width == context.width && color.height == context.height && depth) return true;
  color = Texture(context.device, MTLPixelFormatRGBA8Unorm, context.width, context.height, true);
  depth = Texture(context.device, MTLPixelFormatDepth32Float, context.width, context.height, true);
  context.sceneColor = color;
  return color && depth;
}
void CMetal3D::AttachMemory(const uint32_t* lo, const uint32_t* hi, const uint32_t* polygon,
                           const uint32_t* vrom, const uint16_t* texture)
{
  m->m_scene.AttachMemory(lo, hi, polygon, vrom);
  std::lock_guard<std::mutex> guard(m->textureMutex);
  m->textureRAM = texture;
  for (auto& region : m->dirty) region = Impl::DirtyRegion{};
}
void CMetal3D::SetStepping(Game::Stepping stepping)
{
  int step = 0x10;
  switch (stepping) {
    case Game::Stepping::MODEL3_1_5: step = 0x15; break;
    case Game::Stepping::MODEL3_2_0: step = 0x20; break;
    case Game::Stepping::MODEL3_2_1: step = 0x21; break;
    default: break;
  }
  m->m_scene.SetStepping(step);
}
void CMetal3D::SetSunClamp(bool enable) { m->m_sunClamp = enable; }
void CMetal3D::SetBlockCulling(bool enable) { m->m_blockCulling = enable; }
void CMetal3D::UploadTextures(unsigned, unsigned x, unsigned y, unsigned width, unsigned height)
{
  std::lock_guard<std::mutex> guard(m->textureMutex);
  if (x >= 2048 || y >= 2048 || !width || !height) return;
  const unsigned right = x + std::min(width, 2048u - x);
  const unsigned bottom = y + std::min(height, 2048u - y);
  for (unsigned page = 0; page < 2; ++page) {
    const unsigned baseY = page * 1024;
    const unsigned top = std::max(y, baseY);
    const unsigned end = std::min(bottom, baseY + 1024);
    if (top >= end) continue;
    auto& region = m->dirty[page];
    if (!region.pending) {
      region.left = x; region.right = right;
      region.top = top - baseY; region.bottom = end - baseY;
    } else {
      region.left = std::min(region.left, x);
      region.right = std::max(region.right, right);
      region.top = std::min(region.top, top - baseY);
      region.bottom = std::max(region.bottom, end - baseY);
    }
    region.pending = true;
  }
}
float CMetal3D::GetLosValue(int layer)
{
  std::lock_guard<std::mutex> guard(m->losMutex);
  return layer >= 0 && layer < 4 ? m->losValues[layer] : 0.0f;
}
void CMetal3D::BeginFrame()
{
  std::lock_guard<std::mutex> guard(m->losMutex);
  for (int i = 0; i < 4; ++i) {
    if (!m->losReady[i]) continue;
    float depth;
    std::memcpy(&depth, (uint8_t*)m->los.contents + i * 256, sizeof(depth));
    float z = depth / 1e-3f;
    auto bytes = reinterpret_cast<uint8_t*>(&z);
    if (depth == 0) bytes[0] |= 1; else bytes[0] &= 0xFE;
    m->losValues[i] = z;
    m->losReady[i] = false;
  }
}
void CMetal3D::EndFrame() {}

bool CMetal3D::Impl::UploadGeometry()
{
  auto upload = [&](id<MTLBuffer> __strong& buffer, size_t& count, const std::vector<New3D::FVertex>& verts, bool reset) {
    size_t old = reset ? 0 : count;
    if (verts.empty()) { count = 0; return true; }
    size_t required = verts.size() * sizeof(R3DVertex);
    if (!buffer || buffer.length < required) {
      buffer = [context.device newBufferWithLength:required + required / 4 options:MTLResourceStorageModeShared];
      old = 0;
    }
    if (!buffer) { ErrorLog("Metal: geometry allocation failed (%zu bytes).", required); return false; }
    R3DVertex* dst = (R3DVertex*)buffer.contents;
    for (size_t i = old; i < verts.size(); ++i) {
      const auto& src = verts[i];
      R3DVertex v = {};
      std::memcpy(v.pos, src.base.pos, sizeof(v.pos));
      std::memcpy(v.normal, src.base.normal, sizeof(v.normal));
      std::memcpy(v.uv, src.base.texcoords, sizeof(v.uv));
      std::memcpy(v.faceNormal, src.faceNormal, sizeof(v.faceNormal));
      std::memcpy(&v.colour, src.faceColour, sizeof(v.colour));
      v.fixedShade = src.base.fixedShade; v.textureNP = src.textureNP;
      dst[i] = v;
    }
    count = verts.size();
    return true;
  };
  bool ok = upload(rom, romCount, m_scene.RomVerts(), m_scene.RomReset()) &&
            upload(ram, ramCount, m_scene.RamVerts(), true);
  if (ok) m_scene.ClearRomReset();
  return ok;
}
bool CMetal3D::Impl::Viewport(id<MTLRenderCommandEncoder> enc, const New3D::Viewport& vp)
{
  int y = (int)context.height - (vp.y + vp.height);
  int left = std::max(vp.x, 0), top = std::max(y, 0);
  int right = std::min(vp.x + vp.width, (int)context.width);
  int bottom = std::min(y + vp.height, (int)context.height);
  if (vp.width <= 0 || vp.height <= 0 || right <= left || bottom <= top) return false;
  [enc setViewport:MTLViewport{(double)vp.x, (double)y, (double)vp.width, (double)vp.height, 0, 1}];
  [enc setScissorRect:MTLScissorRect{(NSUInteger)left, (NSUInteger)top,
                                   (NSUInteger)(right-left), (NSUInteger)(bottom-top)}];
  return true;
}
void CMetal3D::Impl::CopyLos(int priority)
{
  for (const auto& node : m_scene.Nodes()) {
    const auto& vp = node.viewport;
    if (vp.priority != priority || !(vp.losPosX || vp.losPosY)) continue;
    int lx = x + vp.losPosX * (width / 496.0f);
    int ly = (int)context.height - 1 - (int)(y + (384 - vp.losPosY) * (height / 384.0f));
    if (lx < 0 || ly < 0 || lx >= (int)context.width || ly >= (int)context.height) return;
    id<MTLBlitCommandEncoder> enc = [context.command blitCommandEncoder];
    [enc copyFromTexture:depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(lx, ly, 0)
             sourceSize:MTLSizeMake(1, 1, 1) toBuffer:los destinationOffset:priority * 256
             destinationBytesPerRow:256 destinationBytesPerImage:256];
    [enc endEncoding];
    losReady[priority] = true;
    return;
  }
}
void CMetal3D::Impl::DrawScene()
{
  auto begin = [&](bool clearColor, bool clearDepth) {
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = clearColor ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = clearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 0;
    return [context.command renderCommandEncoderWithDescriptor:pass];
  };
  auto drawLayer = [&](id<MTLRenderCommandEncoder> enc, int priority, bool overlay,
                        New3D::Layer layer, bool discardAlpha, bool blend) {
    bool hasOverlay = false;
    [enc setRenderPipelineState:blend ? translucent : opaque];
    [enc setDepthStencilState:overlay ? depthOverlay : depthWrite];
    [enc setDepthBias:overlay ? 1.0f : 0.0f slopeScale:overlay ? 1.0f : 0.0f clamp:0.0f];
    [enc setCullMode:MTLCullModeNone]; // Polygon-normal rejection happens in the shader.
    for (int i = 0; i < 2; ++i) {
      [enc setFragmentTexture:pages[i] atIndex:i];
      [enc setFragmentSamplerState:sampler atIndex:i];
    }
    for (auto& node : m_scene.Nodes()) {
      auto& vp = node.viewport;
      if (vp.priority != priority || node.models.empty() || !Viewport(enc, vp)) continue;
      ViewportUBO ubo = {};
      std::memcpy(ubo.spotEllipse, vp.spotEllipse, sizeof(ubo.spotEllipse));
      ubo.spotEllipse[1] = context.height - vp.spotEllipse[1];
      std::memcpy(ubo.spotRange, vp.spotRange, sizeof(vp.spotRange));
      std::memcpy(ubo.spotColor, vp.spotColor, sizeof(vp.spotColor));
      std::memcpy(ubo.spotFogColor, vp.spotFogColor, sizeof(vp.spotFogColor));
      [enc setFragmentBytes:&ubo length:sizeof(ubo) atIndex:1];
      for (auto& model : node.models) {
        if (!model.meshes) continue;
        for (auto& mesh : *model.meshes) {
          hasOverlay |= mesh.highPriority;
          if (!mesh.Render(layer, model.alpha) || mesh.highPriority != overlay || mesh.vertexCount <= 0) continue;
          size_t count = model.dynamic ? ramCount : romCount;
          if (mesh.vboOffset < 0 || (size_t)mesh.vboOffset + mesh.vertexCount > count) continue;
          R3DMaterial material;
          FillMaterial(material, mesh, model);
          [enc setFragmentBytes:&material length:sizeof(material) atIndex:2];
          R3DPush push;
          FillPush(push, model, vp, 0, discardAlpha);
          [enc setVertexBytes:&push length:sizeof(push) atIndex:0];
          [enc setFragmentBytes:&push length:sizeof(push) atIndex:0];
          [enc setVertexBuffer:model.dynamic ? ram : rom offset:0 atIndex:8];
          [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:mesh.vboOffset vertexCount:mesh.vertexCount];
        }
      }
    }
    return hasOverlay;
  };
  id<MTLRenderCommandEncoder> enc = begin(true, true);
  if (m_blockCulling && !m_noWhiteFlash) {
    // Use a fog quad to restrict the white flash to the emulated picture.
    New3D::Viewport vp = {};
    vp.x = x; vp.y = y; vp.width = width; vp.height = height;
    if (Viewport(enc, vp)) {
      R3DFogPush push = {};
      push.fogColour[0] = push.fogColour[1] = push.fogColour[2] = push.fogColour[3] = 1;
      push.fogAmbientAtt[0] = 1;
      push.spotEllipse[2] = push.spotEllipse[3] = 1;
      [enc setRenderPipelineState:fog];
      [enc setDepthStencilState:depthOff];
      [enc setFragmentBytes:&push length:sizeof(push) atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    [enc endEncoding];
    return;
  }
  DrawFog(enc);
  [enc endEncoding];
  for (int priority = 0; priority < 4; ++priority) {
    bool any = false;
    for (const auto& node : m_scene.Nodes()) any |= node.viewport.priority == priority && !node.models.empty();
    if (!any) continue;
    for (int overlay = 0; overlay < 2; ++overlay) {
      enc = begin(false, true);
      bool hasOverlay = drawLayer(enc, priority, overlay, New3D::Layer::colour, true, false);
      [enc endEncoding];
      if (!overlay) CopyLos(priority);
      enc = begin(false, false);
      drawLayer(enc, priority, overlay, New3D::Layer::trans1, false, true);
      drawLayer(enc, priority, overlay, New3D::Layer::trans2, false, true);
      [enc endEncoding];
      if (!hasOverlay) break;
    }
  }
}
void CMetal3D::RenderFrame()
{
  @autoreleasepool {
    if (!m->context.command || !m->EnsureTargets()) return;
    const uint64_t start = m->context.profile ? SDL_GetPerformanceCounter() : 0;
    m->m_scene.BeginFrame();
    m->m_scene.SetViewport(m->x, m->y, m->width, m->height, m->totalX, m->totalY,
                           m->m_config["WideScreen"].ValueAs<bool>());
    if (!(m->m_blockCulling && !m->m_noWhiteFlash)) m->m_scene.Build();
    const uint64_t built = m->context.profile ? SDL_GetPerformanceCounter() : 0;
    {
      std::lock_guard<std::mutex> guard(m->textureMutex);
      if (m->textureRAM) {
        for (unsigned i = 0; i < 2; ++i) {
          auto& region = m->dirty[i];
          if (!region.pending) continue;
          [m->pages[i] replaceRegion:MTLRegionMake2D(region.left, region.top,
                         region.right - region.left, region.bottom - region.top) mipmapLevel:0
                         withBytes:m->textureRAM + (i * 1024 + region.top) * 2048 + region.left
                         bytesPerRow:2048 * 2];
          region.pending = false;
        }
      }
    }
    const uint64_t textured = m->context.profile ? SDL_GetPerformanceCounter() : 0;
    bool geometryReady = m->UploadGeometry();
    const uint64_t uploaded = m->context.profile ? SDL_GetPerformanceCounter() : 0;
    if (geometryReady) m->DrawScene();
    if (m->context.profile) {
      m->buildMs += m->context.Milliseconds(built - start);
      m->textureMs += m->context.Milliseconds(textured - built);
      m->geometryMs += m->context.Milliseconds(uploaded - textured);
      m->drawMs += m->context.Milliseconds(SDL_GetPerformanceCounter() - uploaded);
      if (++m->profileFrames == 120) {
        InfoLog("Metal profile 3D: scene %.2f ms, textures %.2f ms, geometry %.2f ms, draw encoding %.2f ms (120-frame averages).",
          m->buildMs / 120, m->textureMs / 120, m->geometryMs / 120, m->drawMs / 120);
        m->profileFrames = 0;
        m->buildMs = m->textureMs = m->geometryMs = m->drawMs = 0;
      }
    }
  }
}

void CMetal3D::Impl::FillMaterial(R3DMaterial& material, const New3D::Mesh& mesh, const New3D::Model& model)
{
	material = {};
	material.shininess = mesh.shininess;
	material.specularValue = mesh.specularValue;
	material.fogIntensity = mesh.fogIntensity;
	material.microMinLOD = mesh.microTextureMinLOD;
	material.texX = mesh.x + model.textureOffsetX;
	material.texY = mesh.y + model.textureOffsetY;
	material.texWidth = mesh.width;
	material.texHeight = mesh.height;
	material.texFormat = mesh.format;
	material.texturePage = (mesh.page ^ model.page) & 1;
	material.wrapModeX = (int32_t)mesh.wrapModeU;
	material.wrapModeY = (int32_t)mesh.wrapModeV;
	material.microTextureID = mesh.microTextureID & 7;
	if (mesh.textured)
		material.flags |= kMatTextured;
	if (mesh.alphaTest)
		material.flags |= kMatAlphaTest;
	if (mesh.textureAlpha)
		material.flags |= kMatTextureAlpha;
	if (mesh.lighting)
		material.flags |= kMatLighting;
	if (mesh.fixedShading)
		material.flags |= kMatFixedShade;
	if (mesh.specular)
		material.flags |= kMatSpecular;
	if (mesh.translatorMap)
		material.flags |= kMatTranslator;
	if (mesh.inverted)
		material.flags |= kMatInverted;
	if (mesh.smoothShading)
		material.flags |= kMatSmooth;
	if (mesh.polyAlpha)
		material.flags |= kMatPolyAlpha;
	if (mesh.microTexture)
		material.flags |= kMatMicro;
}

void CMetal3D::Impl::FillPush(R3DPush& push, const New3D::Model& model, const New3D::Viewport& vp, uint32_t materialIndex, bool discardAlpha) const
{
	std::memcpy(push.modelView, model.modelMat, sizeof(push.modelView));
	std::memcpy(push.projection, vp.projectionMatrix.currentMatrix, sizeof(push.projection));
	push.projection[1] = -push.projection[1];
	push.projection[5] = -push.projection[5];
	push.projection[9] = -push.projection[9];
	push.projection[13] = -push.projection[13];
	push.lightDir[0] = vp.lightingParams[0];
	push.lightDir[1] = vp.lightingParams[1];
	push.lightDir[2] = vp.lightingParams[2];
	push.lightDir[3] = vp.lightingParams[3];
	push.lightParams[0] = vp.lightingParams[4];
	push.lightParams[1] = m_sunClamp ? 1.0f : 0.0f;
	push.lightParams[2] = vp.intensityClamp ? 1.0f : 0.0f;
	push.lightParams[3] = (vp.step15Luminous ? 21.0f : 0.0f);
	push.fogColour[0] = vp.fogColour[0];
	push.fogColour[1] = vp.fogColour[1];
	push.fogColour[2] = vp.fogColour[2];
	push.fogColour[3] = vp.fogDensity;
	push.fogParams[0] = vp.fogStart;
	push.fogParams[1] = vp.fogAttenuation;
	push.fogParams[2] = vp.fogAmbient;
	push.fogParams[3] = model.alpha;
	push.extra[0] = vp.cota;
	push.extra[1] = model.scale == 0.0f ? 1.0f : model.scale;
	push.extra[2] = discardAlpha ? 1.0f : 0.0f;
	std::memcpy(&push.extra[3], &materialIndex, sizeof(materialIndex));
}

void CMetal3D::Impl::DrawFog(id<MTLRenderCommandEncoder> enc)
{
	const New3D::Viewport* scrollNode = nullptr;

	// Highest scroll fog value, starting from the lowest priority layer.
	for (int layer = 0; layer < 4 && scrollNode == nullptr; layer++)
	{
		float best = 0.0f;
		for (const auto& node : m_scene.Nodes())
		{
			const New3D::Viewport& vp = node.viewport;
			if (vp.priority != layer || node.models.empty())
				continue;
			if (vp.scrollFog > best)
			{
				best = vp.scrollFog;
				scrollNode = &vp;
			}
		}
	}

	// Ambient fog: lowest fogAmbient in the first priority layer with viewports.
	const New3D::Viewport* ambientNode = nullptr;
	if (scrollNode == nullptr)
	{
		for (int layer = 0; layer < 4; layer++)
		{
			bool hasPriority = false;
			const New3D::Viewport* best = nullptr;
			float bestAmbient = 1.0f;
			for (const auto& node : m_scene.Nodes())
			{
				const New3D::Viewport& vp = node.viewport;
				if (vp.priority != layer || node.models.empty())
					continue;
				hasPriority = true;
				if (vp.scrollFog > 0.0f)
					continue;
				// Without a fog density or start the effect is disabled (Lost World)
				if (vp.fogDensity <= 0.0f && vp.fogStart <= 0.0f)
					continue;
				if (vp.fogAmbient < bestAmbient)
				{
					bestAmbient = vp.fogAmbient;
					best = &vp;
				}
			}
			if (best != nullptr)
				ambientNode = best;
			if (ambientNode != nullptr || hasPriority)
				break;
		}
	}

	if (scrollNode == nullptr && ambientNode == nullptr)
		return;

	[enc setRenderPipelineState:fog];
	[enc setDepthStencilState:depthOff];

	auto drawFogQuad = [&](const New3D::Viewport& vp, const float rgba[4], float attenuation, float ambient,
		const float spotFogColor[3], const float spotColour[4])
	{
		if (!Viewport(enc, vp)) return;

		R3DFogPush push = {};
		push.fogColour[0] = rgba[0];
		push.fogColour[1] = rgba[1];
		push.fogColour[2] = rgba[2];
		push.fogColour[3] = rgba[3];
		push.fogAmbientAtt[0] = ambient;
		push.fogAmbientAtt[1] = attenuation;
		push.spotEllipse[0] = spotColour[0];
		push.spotEllipse[1] = (float)context.height - spotColour[1];
		push.spotEllipse[2] = spotColour[2];
		push.spotEllipse[3] = spotColour[3];
		push.spotFogColor[0] = spotFogColor[0];
		push.spotFogColor[1] = spotFogColor[1];
		push.spotFogColor[2] = spotFogColor[2];
		[enc setFragmentBytes:&push length:sizeof(push) atIndex:0];
		[enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	};

	if (scrollNode != nullptr)
	{
		// Find a viewport with the same fog colour that actually enables fogging.
		// That is where the attenuation, ambient and spotlight parameters come from.
		const New3D::Viewport* source = scrollNode;
		bool found = false;
		for (const auto& node : m_scene.Nodes())
		{
			const New3D::Viewport& vp = node.viewport;
			if (vp.fogColour[0] != scrollNode->fogColour[0] ||
				vp.fogColour[1] != scrollNode->fogColour[1] ||
				vp.fogColour[2] != scrollNode->fogColour[2])
				continue;

			bool spotOnFog = false;
			for (float c : vp.spotFogColor)
				if (c > 0.0f) { spotOnFog = true; break; }

			bool fog = vp.fogDensity > 0.0f || vp.fogStart > 0.0f || vp.scrollAtt > 0.0f;
			bool spotlightFog = vp.fogAttenuation > 0.0f && spotOnFog;

			if (fog || spotlightFog)
			{
				source = &vp;
				found = true;
				break;
			}
		}
		if (!found)
			return;

		// If regular fog is enabled the hardware seems to only apply that and
		// skips the spotlight on fog (ECA).
		float attenuation = source->fogAttenuation;
		bool regularFog = source->fogDensity > 0.0f || source->fogStart > 0.0f || source->scrollAtt > 0.0f;
		bool spotOnFog = false;
		for (float c : source->spotFogColor)
			if (c > 0.0f) { spotOnFog = true; break; }
		if (regularFog && spotOnFog)
			attenuation = 0.0f;

		float rgba[4] = { scrollNode->fogColour[0], scrollNode->fogColour[1], scrollNode->fogColour[2], scrollNode->scrollFog };
		drawFogQuad(*scrollNode, rgba, attenuation, source->fogAmbient, source->spotFogColor, source->spotEllipse);
	}
	else
	{
		// Ambient fog is a black quad whose alpha dims whatever is behind it.
		float rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f - ambientNode->fogAmbient };
		drawFogQuad(*ambientNode, rgba, 0.0f, 1.0f, ambientNode->spotFogColor, ambientNode->spotEllipse);
	}
}



} // namespace Metal3D

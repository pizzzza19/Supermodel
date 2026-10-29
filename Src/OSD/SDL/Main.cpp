/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Main.cpp
 *
 * Main program driver for the SDL port.
 *
 * Bugs and Issues to Address
 * --------------------------
 * - -gfx-state crashes when ENABLE_DEBUGGER is also enabled:
 *
 *    #0  0x00007ff6586392a0 in CSoundBoard::GetDSB (this=0x1128) at Src/Model3/SoundBoard.cpp:552
 *    #1  0x00007ff6587072cf in Debugger::CSupermodelDebugger::CreateDSBCPUDebug (model3=0x0) at Src/Debugger/SupermodelDebugger.cpp:269
 *    #2  0x00007ff6587076e9 in Debugger::CSupermodelDebugger::AddCPUs (this=0x185da75df40) at Src/Debugger/SupermodelDebugger.cpp:361
 *    #3  0x00007ff6586f91a6 in Debugger::CDebugger::Attach (this=0x185da75df40) at Src/Debugger/Debugger.cpp:263
 *    #4  0x00007ff658705312 in Debugger::CConsoleDebugger::Attach (this=0x185da75df40) at Src/Debugger/ConsoleDebugger.cpp:2707
 *    #5  0x00007ff65862c6fc in Supermodel (game=..., rom_set=0x6cc8dff0e0, Model3=0x185da7598d0, Inputs=0x185da752590, Outputs=0x0, Debugger=std::shared_ptr<Debugger::CDebugger> (use count 3, weak count 0) = {...})
 *        at Src/OSD/SDL/Main.cpp:978
 *    #6  0x00007ff658634ba3 in SDL_main (argc=3, argv=0x185da4e68a0) at Src/OSD/SDL/Main.cpp:2056
 *    #7  0x00007ff658720141 in main_getcmdline ()
 *    #8  0x00007ff6585b13b1 in __tmainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:321
 *    #9  0x00007ff6585b14e6 in mainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:202
 * - Need to address all the stale TO-DOs below and clear them out :)
 *
 * To Do Before Next Release
 * -------------------------
 * - Thoroughly test config system (do overrides work as expected? XInput
 *   force settings?)
 * - Remove all occurrences of "using namespace std" from Nik's code.
 * - Standardize variable naming (recently introduced vars_like_this should be
 *   converted back to varsLikeThis).
 * - Update save state file revision (strings > 1024 chars are now supported).
 * - Fix BlockFile.cpp to use fstream!
 * - Check to make sure save states use explicitly-sized types for 32/64-bit
 *   compatibility (i.e., size_t, int, etc. not allowed).
 * - Make sure quitting while paused works.
 * - Add UI keys for balance setting?
 * - 5.1 audio support?
 *
 * Compile-Time Options
 * --------------------
 * - SUPERMODEL_WIN32: Define this if compiling on Windows.
 * - SUPERMODEL_OSX: Define this if compiling on Mac OS X.
 * - SUPERMODEL_DEBUGGER: Enable the debugger.
 * - DEBUG: Debug mode (use with caution, produces large logs of game behavior)
 */

#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <vector>
#include <algorithm>

#ifdef SUPERMODEL_WIN32
#include "DirectInputSystem.h"
#include "WinOutputs.h"
#endif
#include "NetOutputs.h"

#include "Supermodel.h"
#include "Util/Format.h"
#include "Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "OSD/FileSystemPath.h"
#include "GameLoader.h"
#include "SDLInputSystem.h"
#include "SDLIncludes.h"
#include "Debugger/SupermodelDebugger.h"
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "Network/NetServer.h"
#include "Graphics/IRender2D.h"
#include "OSD/Audio.h"
#include "OSD/ConsoleInput.h"
#ifndef SUPERMODEL_METAL
#define SUPERMODEL_VULKAN
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Graphics/Vulkan3D/Vulkan2D.h"
#include "Graphics/Vulkan3D/Vulkan3D.h"
#endif
#ifdef SUPERMODEL_METAL
#include "Graphics/Metal3D/MetalRenderer.h"
#endif
#include "Sound/MPEG/MpegAudio.h"

#include <iostream>

#include "OSD/ConfigFile.h"
#include "Gui.h"


/******************************************************************************
 Global Run-time Config
******************************************************************************/

static const std::string s_analysisPath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Analysis);
static const std::string s_configFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Supermodel.ini";
static const std::string s_gameXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Games.xml";
static const std::string s_musicXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Music.xml";
static const std::string s_logFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Log) << "Supermodel.log";

static Util::Config::Node s_runtime_config("Global");


/******************************************************************************
 Display Management
******************************************************************************/

SDL_Window *s_window = nullptr;
#ifdef SUPERMODEL_VULKAN
static std::unique_ptr<Vulkan3D::CVulkanContext> s_vkContext;
#endif
#ifdef SUPERMODEL_METAL
static std::unique_ptr<Metal3D::CMetalContext> s_metalContext;
#endif

/*
 * Position and size of rectangular region within native display to render to.
 * Unlike the config tree, these end up containing the actual resolution (and
 * computed offsets within the viewport) that will be rendered based on what
 * was obtained from SDL.
 */
static unsigned  xOffset, yOffset;      // offset of renderer output within native viewport
static unsigned  xRes, yRes;            // renderer output resolution (can be smaller than native viewport)
static unsigned  totalXRes, totalYRes;  // total resolution (the whole native viewport)
// In windows with an nvidia card (sorry not tested anything else) you can customise the resolution.
// This also allows you to set a totally custom refresh rate. Apparently you can drive most monitors at
// 57.5fps with no issues. Anyway this code will automatically pick up your custom refresh rate, and set it if it exists.
// If it doesn't exist, then it'll probably just default to 60 or whatever your refresh rate is.
static void SetFullScreenRefreshRate()
{
    float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());

    if (refreshRateHz > 57.f && refreshRateHz < 58.f) {

        int display_in_use = 0; /* Only using first display */

        int display_mode_count = SDL_GetNumDisplayModes(display_in_use);
        if (display_mode_count < 1) {
            return;
        }

        for (int i = 0; i < display_mode_count; ++i) {

            SDL_DisplayMode mode;

            if (SDL_GetDisplayMode(display_in_use, i, &mode) != 0) {
                return;
            }

            if (SDL_BITSPERPIXEL(mode.format) >= 24 && (unsigned)mode.w == totalXRes && (unsigned)mode.h == totalYRes) {
                if (mode.refresh_rate == 57 || mode.refresh_rate == 58) {       // nvidia is fairly flexible in what refresh rate windows will show, so we can match either 57 or 58,
                    int result = SDL_SetWindowDisplayMode(s_window, &mode);     // both are totally non standard frequencies and shouldn't be set incorrectly
                    if (result == 0) {
                        printf("Custom fullscreen mode set: %ix%i@57.524 Hz\n", mode.w, mode.h);
                    }
                    break;
                }
            }
        }
    }
}

#ifdef SUPERMODEL_VULKAN
// Vulkan geometry comes from the swapchain extent.
static void SetVulkanGeometry(bool keepAspectRatio)
{
  totalXRes = xRes = s_vkContext->Extent().width;
  totalYRes = yRes = s_vkContext->Extent().height;
  if (keepAspectRatio)
  {
    if (uint64_t(xRes) * 384 > uint64_t(yRes) * 496)
      xRes = yRes * 496 / 384;
    else
      yRes = xRes * 384 / 496;
  }
  xOffset = (totalXRes - xRes) / 2;
  yOffset = (totalYRes - yRes) / 2;
}

static Result CreateVulkanScreen()
{
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    return ErrorLog("Unable to initialize SDL video: %s", SDL_GetError());
  s_window = SDL_CreateWindow("Supermodel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    xRes, yRes, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  if (!s_window)
    return ErrorLog("Unable to create Vulkan window: %s", SDL_GetError());
  s_vkContext = std::make_unique<Vulkan3D::CVulkanContext>();
  if (s_vkContext->InitDevice(s_window, false, s_runtime_config["VSync"].ValueAs<bool>()) != Result::OKAY)
    return Result::FAIL;
  SetVulkanGeometry(false);
  return Result::OKAY;
}

static Result ResizeVulkanScreen(bool keepAspectRatio, bool fullScreen)
{
  if (SDL_SetWindowFullscreen(s_window, fullScreen ? SDL_WINDOW_FULLSCREEN : 0) < 0)
    return ErrorLog("Unable to change fullscreen mode: %s", SDL_GetError());
  if (!s_vkContext->RecreateSwapchain())
    return Result::FAIL;
  SetVulkanGeometry(keepAspectRatio);
  return Result::OKAY;
}

#endif // SUPERMODEL_VULKAN

#ifdef SUPERMODEL_METAL
static void SetMetalGeometry(bool keepAspectRatio)
{
  totalXRes = xRes = s_metalContext->Width();
  totalYRes = yRes = s_metalContext->Height();
  if (keepAspectRatio)
  {
    if (uint64_t(xRes) * 384 > uint64_t(yRes) * 496)
      xRes = yRes * 496 / 384;
    else
      yRes = xRes * 384 / 496;
  }
  xOffset = (totalXRes - xRes) / 2;
  yOffset = (totalYRes - yRes) / 2;
}

static Result CreateMetalScreen()
{
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    return ErrorLog("Unable to initialize SDL video: %s", SDL_GetError());
  s_window = SDL_CreateWindow("Supermodel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    xRes, yRes, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
  if (!s_window) return ErrorLog("Unable to create Metal window: %s", SDL_GetError());
  s_metalContext = std::make_unique<Metal3D::CMetalContext>();
  if (s_metalContext->Init(s_window, s_runtime_config["VSync"].ValueAs<bool>()) != Result::OKAY)
    return Result::FAIL;
  SetMetalGeometry(false);
  return Result::OKAY;
}

static Result ResizeMetalScreen(bool keepAspectRatio, bool fullScreen)
{
  if (SDL_SetWindowFullscreen(s_window, fullScreen ? SDL_WINDOW_FULLSCREEN : 0) < 0)
    return ErrorLog("Unable to change fullscreen mode: %s", SDL_GetError());
  if (!s_metalContext->Resize(s_runtime_config["XResolution"].ValueAs<unsigned>(),
                              s_runtime_config["YResolution"].ValueAs<unsigned>())) return Result::FAIL;
  SetMetalGeometry(keepAspectRatio);
  return Result::OKAY;
}
#endif

static void DestroyScreen()
{
  if (s_window != nullptr)
  {
#ifdef SUPERMODEL_METAL
    s_metalContext.reset();
#endif
#ifdef SUPERMODEL_VULKAN
    s_vkContext.reset();
#endif
    SDL_DestroyWindow(s_window);
    s_window = nullptr;
  }
}

static Result ResizeScreen(bool keepAspectRatio, bool fullScreen)
{
#ifdef SUPERMODEL_METAL
  if (s_metalContext) return ResizeMetalScreen(keepAspectRatio, fullScreen);
#endif
#ifdef SUPERMODEL_VULKAN
  if (s_vkContext) return ResizeVulkanScreen(keepAspectRatio, fullScreen);
#endif
  return Result::FAIL;
}

#ifdef DEBUG
#include "Model3/Model3GraphicsState.h"
static std::string s_gfxStatePath;
#endif

/******************************************************************************
 Save States and NVRAM

 Save states and NVRAM use the same basic format. When anything changes that
 breaks compatibility with previous versions of Supermodel, the save state
 and NVRAM version numbers must be incremented as needed.

 Header block name: "Supermodel Save State" or "Supermodel NVRAM State"
 Data: Save state file version (4-byte integer), ROM set ID (up to 9 bytes,
 including terminating \0).

 Different subsystems output their own blocks.
******************************************************************************/

static const int STATE_FILE_VERSION = 6;  // save state file version
static const int NVRAM_FILE_VERSION = 0;  // NVRAM file version
static unsigned s_saveSlot = 0;           // save state slot #

static bool CheckSaveStatesSupported(IEmulator *Emu)
{
  if (Emu->SupportsSaveStates())
    return true;
  puts("Save states are not supported for this system.");
  InfoLog("Save states are not supported for this system.");
  return false;
}

static void SaveState(IEmulator *Emu)
{
  if (!CheckSaveStatesSupported(Emu))
    return;

  CBlockFile  SaveState;

  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Emu->GetGame().name << ".st" << s_saveSlot;
  if (Result::OKAY != SaveState.Create(file_path, "Supermodel Save State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save state to '%s'.", file_path.c_str());
    return;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = STATE_FILE_VERSION;
  SaveState.Write(&fileVersion, sizeof(fileVersion));
  SaveState.Write(Emu->GetGame().name);

  // Save state
  Emu->SaveState(&SaveState);
  SaveState.Close();
  printf("Saved state to '%s'.\n", file_path.c_str());
  InfoLog("Saved state to '%s'.", file_path.c_str());
}

static void LoadState(IEmulator *Emu, std::string file_path = std::string())
{
  if (!CheckSaveStatesSupported(Emu))
    return;

  CBlockFile  SaveState;

  // Generate file path
  if (file_path.empty())
    file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Emu->GetGame().name << ".st" << s_saveSlot;

  // Open and check to make sure format is correct
  if (Result::OKAY != SaveState.Load(file_path))
  {
    ErrorLog("Unable to load state from '%s'.", file_path.c_str());
    return;
  }

  if (Result::OKAY != SaveState.FindBlock("Supermodel Save State"))
  {
    ErrorLog("'%s' does not appear to be a valid save state file.", file_path.c_str());
    return;
  }

  int32_t fileVersion;
  SaveState.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != STATE_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return;
  }

  // Load
  Emu->LoadState(&SaveState);
  SaveState.Close();
  printf("Loaded state from '%s'.\n", file_path.c_str());
  InfoLog("Loaded state from '%s'.", file_path.c_str());
}

static void SaveNVRAM(IEmulator *Emu)
{
  CBlockFile  NVRAM;

  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Emu->GetGame().name << ".nv";
  if (Result::OKAY != NVRAM.Create(file_path, "Supermodel NVRAM State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save NVRAM to '%s'. Make sure directory exists!", file_path.c_str());
    return;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = NVRAM_FILE_VERSION;
  NVRAM.Write(&fileVersion, sizeof(fileVersion));
  NVRAM.Write(Emu->GetGame().name);

  // Save NVRAM
  Emu->SaveNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Saved NVRAM to '%s'.", file_path.c_str());
}

static void LoadNVRAM(IEmulator *Emu)
{
  CBlockFile  NVRAM;

  // Generate file path
  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Emu->GetGame().name << ".nv";

  // Open and check to make sure format is correct
  if (Result::OKAY != NVRAM.Load(file_path))
  {
    //ErrorLog("Unable to restore NVRAM from '%s'.", filePath);
    return;
  }

  if (Result::OKAY != NVRAM.FindBlock("Supermodel NVRAM State"))
  {
    ErrorLog("'%s' does not appear to be a valid NVRAM file.", file_path.c_str());
    return;
  }

  int32_t fileVersion;
  NVRAM.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != NVRAM_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return;
  }

  // Load
  Emu->LoadNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Loaded NVRAM from '%s'.", file_path.c_str());
}


bool BeginFrameVideo()
{
#ifdef SUPERMODEL_METAL
  if (s_metalContext) return s_metalContext->BeginFrame();
#endif
#ifdef SUPERMODEL_VULKAN
  if (s_vkContext) return s_vkContext->BeginFrame();
#endif
  return true;
}

void EndFrameVideo()
{
#ifdef SUPERMODEL_METAL
  if (s_metalContext)
  {
    s_metalContext->EndFrame();
    return;
  }
#endif
#ifdef SUPERMODEL_VULKAN
  if (s_vkContext)
  {
    s_vkContext->EndFrame();
    return;
  }
#endif

}


/******************************************************************************
 Frame Timing
******************************************************************************/

static uint64_t s_perfCounterFrequency = 0;

static uint64_t GetDesiredRefreshRateMilliHz()
{
  // The refresh rate is expressed as mHz (millihertz -- Hz * 1000) in order to
  // be expressable as an integer. E.g.: 57.524 Hz -> 57524 mHz.
  float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());
  uint64_t refreshRateMilliHz = uint64_t(1000.0 * refreshRateHz);
  return refreshRateMilliHz;
}

static void SuperSleepUntil(const uint64_t target)
{
  uint64_t time = SDL_GetPerformanceCounter();

  // If we're ahead of the target, we're done
  if (time >= target)
  {
    return;
  }

  // Because OS sleep is not accurate,
  // we actually sleep until a maximum of 2 milliseconds are left.
  while (int64_t(target - time) * 1000 > 2 * int64_t(s_perfCounterFrequency))
  {
    SDL_Delay(1);
    time = SDL_GetPerformanceCounter();
  }

  // Spin until requested time
  int64_t remain;
  do
  {
    // according to all available processor documentation for x86 and arm,
    // spinning should pause the processor for a short while for better
    // power efficiency and (surprisingly) overall faster system performance
    #ifdef SDL_CPUPauseInstruction
    SDL_CPUPauseInstruction();
    #endif
    remain = target - SDL_GetPerformanceCounter();
  } while (remain > 0);
}


/******************************************************************************
 Main Program Loop
******************************************************************************/

// Creates the 2D layer renderer that the emulator needs
static IRender2D *CreateRender2D()
{
#ifdef SUPERMODEL_VULKAN
  if (s_vkContext)
    return new Vulkan3D::CVulkan2D(s_runtime_config, *s_vkContext);
#endif
#ifdef SUPERMODEL_METAL
  if (s_metalContext)
    return new Metal3D::CMetal2D(s_runtime_config, *s_metalContext);
#endif
  return nullptr;
}

#ifdef SUPERMODEL_DEBUGGER
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Emu, CInputs *Inputs, COutputs *Outputs, std::shared_ptr<Debugger::CDebugger> Debugger)
{
  std::shared_ptr<CLogger> oldLogger;
#else
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Emu, CInputs *Inputs, COutputs *Outputs)
{
#endif // SUPERMODEL_DEBUGGER
  std::string initialState = s_runtime_config["InitStateFile"].ValueAs<std::string>();
  uint64_t    prevFPSTicks;
  unsigned    fpsFramesElapsed;
  bool        quit = false;
  bool        paused = false;
  bool        dumpTimings = false;

  // Initialize and load ROMs
  if (Result::OKAY != Emu->Init(game, *rom_set))
    return 1;
  *rom_set = ROMSet();  // free up this memory we won't need anymore

  // Customized music for games with MPEG boards
  MpegDec::LoadCustomTracks(s_musicXMLFilePath, game);

  // Load NVRAM
  LoadNVRAM(Emu);

  // Set the video mode
  char baseTitleStr[128];
  char titleStr[128];
  totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
  totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
  snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel - %s", game.title.c_str());
  SDL_SetWindowTitle(s_window, baseTitleStr);
  SDL_SetWindowSize(s_window, totalXRes, totalYRes);

  int xpos = s_runtime_config["WindowXPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED);
  int ypos = s_runtime_config["WindowYPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED);
  SDL_SetWindowPosition(s_window, xpos, ypos);

  if (s_runtime_config["BorderlessWindow"].ValueAs<bool>())
  {
    SDL_SetWindowBordered(s_window, SDL_FALSE);
  }

  SetFullScreenRefreshRate();

  bool stretch = s_runtime_config["Stretch"].ValueAs<bool>();
  bool fullscreen = s_runtime_config["FullScreen"].ValueAs<bool>();
  if (Result::OKAY != ResizeScreen(!stretch, fullscreen))
    return 1;


  // Initialize audio system
  SetAudioType(game.audio);
  if (Result::OKAY != OpenAudio(s_runtime_config))
    return 1;

  Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());

  // Frame timing
  s_perfCounterFrequency = SDL_GetPerformanceFrequency();
  uint64_t perfCountPerFrame = s_perfCounterFrequency * 1000 / GetDesiredRefreshRateMilliHz();
  uint64_t nextTime = 0;

  // Initialize the renderers
  IRender2D *Render2D = CreateRender2D();
  IRender3D *Render3D = nullptr;
#ifdef SUPERMODEL_METAL
  if (s_metalContext)
    Render3D = new Metal3D::CMetal3D(s_runtime_config, *s_metalContext);
  else
#endif
#ifdef SUPERMODEL_VULKAN
  if (s_vkContext)
    Render3D = new Vulkan3D::CVulkan3D(s_runtime_config, Emu->GetGame().name,
      *s_vkContext, *static_cast<Vulkan3D::CVulkan2D*>(Render2D));
  else
#endif
  {
    delete Render2D;
    ErrorLog("No native graphics backend is available.");
    return 1;
  }

  UpscaleMode upscaleMode = (UpscaleMode)s_runtime_config["UpscaleMode"].ValueAs<int>();

  if (Result::OKAY != Render2D->Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes, 0, upscaleMode))
    goto QuitError;
  if (Result::OKAY != Render3D->Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes, 0))
    goto QuitError;

  Emu->AttachRenderers(Render2D, Render3D);

  // Reset emulator
  Emu->Reset();

  // Load initial save state if requested
  if (!initialState.empty())
    LoadState(Emu, initialState);

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, set it as logger and attach it to system
  oldLogger = GetLogger();
  if (Debugger != NULL)
  {
    SetLogger(Debugger);
    Debugger->Attach();
  }
#endif // SUPERMODEL_DEBUGGER

  // Emulate!
  fpsFramesElapsed = 0;
  prevFPSTicks = SDL_GetPerformanceCounter();
  quit = false;
  paused = false;
  dumpTimings = false;
  while (!quit)
  {
    // Poll the inputs
    if (!Inputs->Poll(&game, xOffset, yOffset, xRes, yRes))
      quit = true;

    // Render if paused, otherwise run a frame
    if (paused)
      Emu->RenderFrame();
    else
      Emu->RunFrame();

#ifdef SUPERMODEL_DEBUGGER
    bool processUI = true;
    if (Debugger != NULL)
    {
      Debugger->Poll();

      // Check if debugger requests exit or pause
      if (Debugger->CheckExit())
      {
        quit = true;
        processUI = false;
      }
      else if (Debugger->CheckPause())
      {
        paused = true;
        processUI = false;
      }
    }
    if (processUI)
    {
#endif // SUPERMODEL_DEBUGGER

    // Check UI controls
    if (Inputs->uiExit->Pressed())
    {
      // Quit emulator
      quit = true;
    }
    else if (Inputs->uiReset->Pressed())
    {
      if (!paused)
      {
        Emu->PauseThreads();
        SetAudioEnabled(false);
      }

      // Reset emulator
      Emu->Reset();

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it too
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (!paused)
      {
        Emu->ResumeThreads();
        SetAudioEnabled(true);
      }

      puts("Model 3 reset.");
    }
    else if (Inputs->uiPause->Pressed())
    {
      // Toggle emulator paused flag
      paused = !paused;

      if (paused)
      {
        Emu->PauseThreads();
        SetAudioEnabled(false);
        snprintf(titleStr, sizeof(titleStr), "%s (Paused)", baseTitleStr);
        SDL_SetWindowTitle(s_window, titleStr);
      }
      else
      {
        Emu->ResumeThreads();
        SetAudioEnabled(true);
        SDL_SetWindowTitle(s_window, baseTitleStr);
      }

      // Send paused value as output
      if (Outputs != NULL)
        Outputs->SetValue(OutputPause, paused);
    }
    else if (Inputs->uiFullScreen->Pressed())
    {
      // Toggle emulator fullscreen
      s_runtime_config.Get("FullScreen").SetValue(!s_runtime_config["FullScreen"].ValueAs<bool>());

      // Resize screen
      totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
      totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
      bool stretchc = s_runtime_config["Stretch"].ValueAs<bool>();
      bool fullscreenc = s_runtime_config["FullScreen"].ValueAs<bool>();
      if (Result::OKAY != ResizeScreen(!stretchc, fullscreenc))
        goto QuitError;

      // Recreate renderers and attach to the emulator
      if (Result::OKAY != Render2D->Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes, 0, upscaleMode))
        goto QuitError;
      if (Result::OKAY != Render3D->Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes, 0))
        goto QuitError;

      Emu->AttachRenderers(Render2D, Render3D);

      Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
    }
    else if (Inputs->uiSaveState->Pressed())
    {
      if (!paused)
      {
        Emu->PauseThreads();
        SetAudioEnabled(false);
      }

      // Save game state
      SaveState(Emu);

      if (!paused)
      {
        Emu->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiChangeSlot->Pressed())
    {
      // Change save slot
      ++s_saveSlot;
      s_saveSlot %= 10; // clamp to [0,9]
      printf("Save slot: %d\n", s_saveSlot);
    }
    else if (Inputs->uiLoadState->Pressed())
    {
      if (!paused)
      {
        Emu->PauseThreads();
        SetAudioEnabled(false);
      }

      // Load game state
      LoadState(Emu);

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it after loading state
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (!paused)
      {
        Emu->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiMusicVolUp->Pressed())
    {
      // Increase music volume by 10%
      if (Emu->GetGame().mpeg_board != Game::MPEG_BOARD_NONE)
      {
        int vol = (std::min)(200, s_runtime_config["MusicVolume"].ValueAs<int>() + 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (200 == vol)
          puts(" (maximum)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiMusicVolDown->Pressed())
    {
      // Decrease music volume by 10%
      if (Emu->GetGame().mpeg_board != Game::MPEG_BOARD_NONE)
      {
        int vol = (std::max)(0, s_runtime_config["MusicVolume"].ValueAs<int>() - 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (0 == vol)
          puts(" (muted)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiSoundVolUp->Pressed())
    {
      // Increase sound volume by 10%
    int vol = (std::min)(200, s_runtime_config["SoundVolume"].ValueAs<int>() + 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (200 == vol)
        puts(" (maximum)");
      else
        printf("\n");
    }
    else if (Inputs->uiSoundVolDown->Pressed())
    {
      // Decrease sound volume by 10%
      int vol = (std::max)(0, s_runtime_config["SoundVolume"].ValueAs<int>() - 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (0 == vol)
        puts(" (muted)");
      else
        printf("\n");
    }
#ifdef SUPERMODEL_DEBUGGER
    else if (Inputs->uiDumpInpState->Pressed())
    {
      // Dump input states
      Inputs->DumpState(&game);
    }
    else if (Inputs->uiDumpTimings->Pressed())
    {
      dumpTimings = !dumpTimings;
    }
#endif
    else if (Inputs->uiClearNVRAM->Pressed())
    {
      // Clear NVRAM
      Emu->ClearNVRAM();
      puts("NVRAM cleared.");
    }
    else if (Inputs->uiToggleFrLimit->Pressed())
    {
      // Toggle frame limiting
      s_runtime_config.Get("Throttle").SetValue(!s_runtime_config["Throttle"].ValueAs<bool>());
      printf("Frame limiting: %s\n", s_runtime_config["Throttle"].ValueAs<bool>() ? "On" : "Off");
    }
#ifdef SUPERMODEL_DEBUGGER
      else if (Debugger != NULL && Inputs->uiEnterDebugger->Pressed())
      {
        // Break execution and enter debugger
        Debugger->ForceBreak(true);
      }
    }
#endif // SUPERMODEL_DEBUGGER

    // Refresh rate (frame limiting)
    if (paused || s_runtime_config["Throttle"].ValueAs<bool>())
    {
        SuperSleepUntil(nextTime);
        nextTime = SDL_GetPerformanceCounter() + perfCountPerFrame;
    }

    // Measure frame rate
    uint64_t currentFPSTicks = SDL_GetPerformanceCounter();
    if (s_runtime_config["ShowFrameRate"].ValueAs<bool>())
    {
      fpsFramesElapsed += 1;
      uint64_t measurementTicks = currentFPSTicks - prevFPSTicks;
      if (measurementTicks >= s_perfCounterFrequency) // update FPS every 1 second (s_perfCounterFrequency is how many perf ticks in one second)
      {
        double fps = double(fpsFramesElapsed) / (double(measurementTicks) / double(s_perfCounterFrequency));
        snprintf(titleStr, sizeof(titleStr), "%s - %1.3f FPS%s", baseTitleStr, fps, paused ? " (Paused)" : "");
        SDL_SetWindowTitle(s_window, titleStr);
        prevFPSTicks = currentFPSTicks;   // reset tick count
        fpsFramesElapsed = 0;             // reset frame count
      }
    }

    if (dumpTimings && !paused)
    {
      if (CModel3 *M = dynamic_cast<CModel3 *>(Emu))
        M->DumpTimings();
    }
  }

  // Make sure all threads are paused before shutting down
  Emu->PauseThreads();

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, detach it from system and restore old logger
  if (Debugger != NULL)
  {
    Debugger->Detach();
    SetLogger(oldLogger);
  }
#endif // SUPERMODEL_DEBUGGER

  // Save NVRAM
  SaveNVRAM(Emu);

  // Close audio
  CloseAudio();

  // Shut down renderers
  delete Render3D;
  delete Render2D;

  return 0;

  // Quit with an error
QuitError:
  delete Render3D;
  delete Render2D;

  return 1;
}


/******************************************************************************
 Entry Point and Command Line Processing
******************************************************************************/

// Configuration file is generated whenever it is not present. We no longer
// distribute it with Supermodel to make it easier to upgrade Supermodel from
// .zip archives without accidentally overwriting the configuration.
static void WriteDefaultConfigurationFileIfNotPresent()
{
    // Test whether file exists by opening it
    FILE* fp = fopen(s_configFilePath.c_str(), "r");
    if (fp)
    {
        fclose(fp);
        return;
    }

    // Write config
    fp = fopen(s_configFilePath.c_str(), "w");
    if (!fp)
    {
        ErrorLog("Unable to write default configuration file to %s", s_configFilePath.c_str());
        return;
    }
    fputs(DefaultConfigFileContents().c_str(), fp);
    fclose(fp);
    InfoLog("Wrote default configuration file to %s", s_configFilePath.c_str());
}

// Create and configure inputs
static Result ConfigureInputs(CInputs *Inputs, Util::Config::Node *fileConfig, Util::Config::Node *runtimeConfig, const Game &game, bool configure)
{
  Inputs->LoadFromConfig(*runtimeConfig);

  // If the user wants to configure the inputs, do that now
  if (configure)
  {
    std::string title("Supermodel - ");
    if (game.name.empty())
      title.append("Configuring Default Inputs...");
    else
      title.append(Util::Format() << "Configuring Inputs for: " << game.title);
    SDL_SetWindowTitle(s_window, title.c_str());

    // Extract the relevant INI section (which will be the global section if no
    // game was specified, otherwise the game's node) in the file config, which
    // will be written back to disk
    Util::Config::Node *fileConfigRoot = game.name.empty() ? fileConfig : fileConfig->TryGet(game.name);
    if (fileConfigRoot == nullptr)
    {
      fileConfigRoot = &fileConfig->Add(game.name);
    }

    // Configure the inputs
    if (Inputs->ConfigureInputs(game, xOffset, yOffset, xRes, yRes))
    {
      // Write input configuration and input system settings to config file
      Inputs->StoreToConfig(fileConfigRoot);
      Util::Config::WriteINIFile(s_configFilePath, *fileConfig, TimestampedConfigFileComment("Updated from input configuration."));

      // Also save to runtime configuration in case we proceed and play
      Inputs->StoreToConfig(runtimeConfig);
    }
    else
      puts("Configuration aborted...");
    puts("");
  }

  return Result::OKAY;
}

// Print game list
static void PrintGameList(const std::string &xml_file, const std::map<std::string, Game> &games)
{
  if (games.empty())
  {
    puts("No games defined.");
    return;
  }
  printf("Games defined in %s:\n", xml_file.c_str());
  puts("");
  puts("    ROM Set          Title");
  puts("    -------          -----");
  for (auto &v: games)
  {
    const Game &game = v.second;
    printf("    %s", game.name.c_str());
    for (size_t i = game.name.length(); i < 10; i++)  // pad for alignment
      printf(" ");
    if (!game.version.empty())
      printf("       %s (%s)\n", game.title.c_str(), game.version.c_str());
    else
      printf("       %s\n", game.title.c_str());
  }
}

static void LogConfig(const Util::Config::Node &config)
{
  InfoLog("Runtime configuration:");
  for (auto &child: config)
  {
    if (child.Empty())
      InfoLog("  %s=<empty>", child.Key().c_str());
    else
      InfoLog("  %s=%s", child.Key().c_str(), child.ValueAs<std::string>().c_str());
  }
  InfoLog("");
}

Util::Config::Node DefaultConfig()
{
  Util::Config::Node config("Global");

  // Names the GUI shows for a setting's list of values, in list order (the config file keeps the values)
  auto SetLabels = [&config](const std::string& key, const std::vector<std::string>& labels)
  {
    config[key].GetValue()->GetValueRange()->SetLabels(labels);
  };

  config.Set("GameXMLFile", s_gameXMLFilePath);
  config.Set("InitStateFile", "");
  // CModel3
  config.Set("PowerPCFrequency", 0u, "Core", 0u, 200u);
  config["PowerPCFrequency"].GetValue()->GetValueRange()->SetSliderFormat("%u MHz", "Auto"); // 0 = chosen by default game stepping
  config.Set("MultiThreaded", true,"Core");
  config.Set("GPUMultiThreaded", true, "Core");
  config.Set<std::string>("ROMDirectory", FileSystemPath::GetPath(FileSystemPath::ROMs), "Core", "", ""); // where the GUI looks for <romset>.zip
  // 2D and 3D graphics engines

  // CSoundBoard
  config.Set("EmulateSound", true, "Sound");
  config.Set("Balance", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceLeftRight", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceFrontRear", 0.0f, "Sound", -100.f, 100.f);
  config.Set("NbSoundChannels", 4, "Sound", 0, 0, { 1,2,4 });
  SetLabels("NbSoundChannels", { "Mono", "Stereo", "Quad (4 channels)" });
  // CDSB

  config.Set("EmulateDSB", true, "Sound");
  config.Set("SoundVolume", 100, "Sound", 0, 200);
  config.Set("MusicVolume", 100, "Sound", 0, 200);
  // CDriveBoard
  config.Set("ForceFeedback", false, "ForceFeedback");

  // Platform-specific/UI
  config.Set("XResolution", 496, "Video");
  config.Set("YResolution", 384, "Video");
  config.SetEmpty("WindowXPosition");
  config.SetEmpty("WindowYPosition");
  config.Set("FullScreen", false, "Video");
  config.Set("BorderlessWindow", false, "Video");
  config.Set("UpscaleMode", 2, "Video", 0, 0, { 0,1,2,3 });
  SetLabels("UpscaleMode", { "None (sharp pixels)", "Biquintic", "Bilinear", "Bicubic" });
  config.Set("WideScreen", false, "Video");
  config.Set("Stretch", false, "Video");
  config.Set("WideBackground", false, "Video");
  config.Set("VSync", true, "Video");
  config.Set("Throttle", true, "Video");
  config.Set("RefreshRate", 60.0f, "Video", 0.0f, 0.0f, { 57.524f, 60.f });
  SetLabels("RefreshRate", { "57.524 Hz (original hardware)", "60 Hz" });
  config.Set("ShowFrameRate", false, "Video");
  config.Set("NoWhiteFlash", false, "Video");
  config.Set("FlipStereo", false, "Sound");
#ifdef SUPERMODEL_WIN32
  config.Set<std::string>("InputSystem", "dinput", "Core", "", "", { "sdl","sdlgamepad","dinput","xinput","rawinput" });
  SetLabels("InputSystem", { "SDL (joystick)", "SDL (gamepad)", "DirectInput", "XInput", "Raw Input (multiple mice/keyboards)" });
  // DirectInput ForceFeedback
  config.Set("DirectInputConstForceLeftMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputConstForceRightMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputVibrateMax", 100, "ForceFeedback", 0, 100);
  // XInput ForceFeedback
  config.Set("XInputConstForceThreshold", 30, "ForceFeedback", 0, 100);
  config.Set("XInputConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputStereoVibration", true, "ForceFeedback");
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#else
  config.Set<std::string>("InputSystem", "sdl", "Core", "", "", { "sdl","sdlgamepad" });
  SetLabels("InputSystem", { "SDL (joystick)", "SDL (gamepad)" });
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#endif
  // PortIn is the net board server port. PortOut and AddressOut name the next machine.
  config.Set("Network", false, "Network");
  config.Set("SimulateNet", true, "Network");
  config.Set("PortIn", unsigned(NetProtocol::DEFAULT_PORT), "Network");
  config.Set("PortOut", unsigned(1971), "Network");
  config.Set<std::string>("AddressOut", "127.0.0.1", "Network", "", "");

#ifdef SUPERMODEL_WIN32
  config.Set<std::string>("Outputs", "none", "Misc", "", "", { "none","win","net" });
  SetLabels("Outputs", { "None", "Windows messages", "Network (TCP/UDP)" });
#else
  config.Set<std::string>("Outputs", "none", "Misc", "", "", { "none","net" });
  SetLabels("Outputs", { "None", "Network (TCP/UDP)" });
#endif
  config.Set<bool>("OutputsWithLF", false, "Misc");
  config.Set<unsigned int>("OutputsTCPPort", 8000, "Misc", 1024, 65535);
  config.Set<unsigned int>("OutputsUDPBroadcastPort", 8001, "Misc", 1024, 65535);

  config.Set("DumpMemory", false, "Misc");
  config.Set("DumpTextures", false, "Misc");

  //
  // Input sensitivity
  //
  config.Set<unsigned>("InputDigitalSensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputDigitalDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeySensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeyDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);

  config.Set<unsigned>("InputMouseXDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 99);
  config.Set<unsigned>("InputMouseYDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 99);
  config.Set<unsigned>("InputMouseZDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 99);

  // Joystick axes, for all joysticks (InputJoy<n>... settings for an individual joystick override these)
  static const char* joyAxisIds[] = { "X", "Y", "Z", "RX", "RY", "RZ", "S1", "S2" };
  for (const char* axis : joyAxisIds)
    config.Set<unsigned>(std::string("InputJoy") + axis + "DeadZone", DEFAULT_JOY_DEADZONE, "Sensitivity", 0, 99);
  for (const char* axis : joyAxisIds)
    config.Set<unsigned>(std::string("InputJoy") + axis + "Saturation", DEFAULT_JOY_SATURATION, "Sensitivity", 1, 200);

  //
  // controls
  //

  // Common
  config.Set<std::string>("InputStart1", "KEY_1,JOY1_BUTTON9", "Input", "", "");
  config.Set<std::string>("InputStart2", "KEY_2,JOY2_BUTTON9", "Input", "", "");
  config.Set<std::string>("InputCoin1", "KEY_3,JOY1_BUTTON10", "Input", "", "");
  config.Set<std::string>("InputCoin2", "KEY_4,JOY2_BUTTON10", "Input", "", "");
  config.Set<std::string>("InputServiceA", "KEY_5", "Input", "", "");
  config.Set<std::string>("InputServiceB", "KEY_7", "Input", "", "");
  config.Set<std::string>("InputTestA", "KEY_6", "Input", "", "");
  config.Set<std::string>("InputTestB", "KEY_8", "Input", "", "");

  // 4-way digital joysticks
  config.Set<std::string>("InputJoyUp", "KEY_UP,JOY1_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown", "KEY_DOWN,JOY1_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft", "KEY_LEFT,JOY1_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight", "KEY_RIGHT,JOY1_RIGHT", "Input", "", "");
  config.Set<std::string>("InputJoyUp2", "JOY2_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown2", "JOY2_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft2", "JOY2_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight2", "JOY2_RIGHT", "Input", "", "");

  // Fighting game buttons
  config.Set<std::string>("InputPunch", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape", "KEY_F,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputPunch2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard2", "JOY2_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape2", "JOY2_BUTTON4", "Input", "", "");

  // Spikeout buttons
  config.Set<std::string>("InputShift", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputBeat", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputCharge", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputJump", "KEY_F,JOY1_BUTTON4", "Input", "", "");

  // Virtua Striker buttons
  config.Set<std::string>("InputShortPass", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputShortPass2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot2", "JOY2_BUTTON3", "Input", "", "");

  // Steering wheel
  config.Set<std::string>("InputSteeringLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputSteeringRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSteering", "JOY1_XAXIS", "Input", "", "");

  // Pedals
  config.Set<std::string>("InputAccelerator", "KEY_UP,JOY1_UP", "Input", "", "");
  config.Set<std::string>("InputBrake", "KEY_DOWN,JOY1_DOWN", "Input", "", "");

  // Up/down shifter manual transmission (all racers)
  config.Set<std::string>("InputGearShiftUp", "KEY_Y", "Input", "", "");
  config.Set<std::string>("InputGearShiftDown", "KEY_H", "Input", "", "");

  // 4-Speed manual transmission (Daytona 2, Sega Rally 2, Scud Race)
  config.Set<std::string>("InputGearShift1", "KEY_Q,JOY1_BUTTON5", "Input", "", "");
  config.Set<std::string>("InputGearShift2", "KEY_W,JOY1_BUTTON6", "Input", "", "");
  config.Set<std::string>("InputGearShift3", "KEY_E,JOY1_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputGearShift4", "KEY_R,JOY1_BUTTON8", "Input", "", "");
  config.Set<std::string>("InputGearShiftN", "KEY_T", "Input", "", "");

  // VR4 view change buttons (Daytona 2, Le Mans 24, Scud Race)
  config.Set<std::string>("InputVR1", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputVR2", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputVR3", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputVR4", "KEY_F,JOY1_BUTTON4", "Input", "", "");

  // Single view change button (Dirt Devils, ECA, Harley-Davidson, Sega Rally 2)
  config.Set<std::string>("InputViewChange", "KEY_A,JOY1_BUTTON1", "Input", "", "");

  // Handbrake (Sega Rally 2)
  config.Set<std::string>("InputHandBrake", "KEY_S,JOY1_BUTTON2", "Input", "", "");

  // Harley-Davidson controls
  config.Set<std::string>("InputRearBrake", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputMusicSelect", "KEY_D,JOY1_BUTTON3", "Input", "", "");

  // Virtual On macros
  config.Set<std::string>("InputTwinJoyTurnLeft", "KEY_Q,JOY1_RXAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurnRight", "KEY_W,JOY1_RXAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyForward", "KEY_UP,JOY1_YAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyReverse", "KEY_DOWN,JOY1_YAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeLeft", "KEY_LEFT,JOY1_XAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeRight", "KEY_RIGHT,JOY1_XAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyJump", "KEY_E,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputTwinJoyCrouch", "KEY_R,JOY1_BUTTON2", "Input", "", "");

  // Virtual On individual joystick mapping
  config.Set<std::string>("InputTwinJoyLeft1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown2", "NONE", "Input", "", "");

  // Virtual On buttons
  config.Set<std::string>("InputTwinJoyShot1", "KEY_A,JOY1_BUTTON5", "Input", "", "");
  config.Set<std::string>("InputTwinJoyShot2", "KEY_S,JOY1_BUTTON6", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurbo1", "KEY_Z,JOY1_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurbo2", "KEY_X,JOY1_BUTTON8", "Input", "", "");

  // Analog joystick (Star Wars Trilogy)
  config.Set<std::string>("InputAnalogJoyLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyX", "JOY_XAXIS,MOUSE_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyY", "JOY_YAXIS,MOUSE_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger", "KEY_A,JOY_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent", "KEY_S,JOY_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger2", "KEY_D,JOY_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent2", "NONE", "Input", "", "");

  // Light guns (Lost World)
  config.Set<std::string>("InputGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger", "KEY_A,JOY1_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputOffscreen", "KEY_S,JOY1_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger", "0", "Input", "", "");
  config.Set<std::string>("InputGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunX2", "JOY2_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputOffscreen2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger2", "0", "Input", "", "");

  // Analog guns (Ocean Hunter, LA Machineguns)
  config.Set<std::string>("InputAnalogGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft", "KEY_A,JOY1_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight", "KEY_S,JOY1_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight2", "NONE", "Input", "", "");

  // Ski Champ controls
  config.Set<std::string>("InputSkiLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputSkiRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSkiUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputSkiDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputSkiX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiPollLeft", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputSkiPollRight", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputSkiSelect1", "KEY_Q,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputSkiSelect2", "KEY_W,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputSkiSelect3", "KEY_E,JOY1_BUTTON5", "Input", "", "");

  // Magical Truck Adventure controls
  config.Set<std::string>("InputMagicalLeverUp1", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown1", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLever1", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalLever2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal1", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal2", "KEY_S,JOY2_BUTTON1", "Input", "", "");

  // Sega Bass Fishing / Get Bass controls
  config.Set<std::string>("InputFishingRodLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputFishingRodRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputFishingRodUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputFishingRodDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputFishingStickLeft", "KEY_A", "Input", "", "");
  config.Set<std::string>("InputFishingStickRight", "KEY_D", "Input", "", "");
  config.Set<std::string>("InputFishingStickUp", "KEY_W", "Input", "", "");
  config.Set<std::string>("InputFishingStickDown", "KEY_S", "Input", "", "");
  config.Set<std::string>("InputFishingRodX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingRodY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickX", "JOY1_RXAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickY", "JOY1_RYAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingReel", "KEY_SPACE,JOY1_ZAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputFishingCast", "KEY_Z,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputFishingSelect", "KEY_X,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputFishingTension", "KEY_T,JOY1_ZAXIS_NEG", "Input", "", "");



  return config;
}

static void Title(void)
{
  puts("Supermodel: A Sega Model 3 Arcade Emulator (Version " SUPERMODEL_VERSION ")");
  puts("Copyright 2003-2026 by The Supermodel Team");
}

static void Help(void)
{
  Util::Config::Node defaultConfig = DefaultConfig();
  puts("Usage: Supermodel <romset> [options]");
  puts("ROM set must be a valid ZIP file containing a single game.");
  puts("");
  puts("General Options:");
  puts("  -?, -h, -help, --help   Print this help text");
  puts("  -print-games            List supported games and quit");
  printf("  -game-xml-file=<file>   ROM set definition file [Default: %s]\n", s_gameXMLFilePath.c_str());
  printf("  -log-output=<outputs>   Log output destination(s) [Default: %s]\n", s_logFilePath.c_str());
  puts("  -log-level=<level>      Logging threshold [Default: info]");
  puts("");
  puts("Core Options:");
  puts("  -ppc-frequency=<mhz>    PowerPC frequency (default varies by stepping)");
  puts("  -no-threads             Disable multi-threading entirely");
  puts("  -gpu-multi-threaded     Run graphics rendering in separate thread [Default]");
  puts("  -no-gpu-thread          Run graphics rendering in main thread");
  puts("  -load-state=<file>      Load save state after starting");
  puts("");
  puts("Video Options:");
  puts("  -res=<x>,<y>            Resolution [Default: 496,384]");
  puts("  -window-pos=<x>,<y>     Window position [Default: centered]");
  puts("  -window                 Windowed mode [Default]");
  puts("  -borderless             Windowed mode with no border");
  puts("  -fullscreen             Full screen mode");
  puts("  -wide-screen            Expand 3D field of view to screen width");
  puts("  -wide-bg                When wide-screen mode is enabled, also expand the 2D");
  puts("                          background layer to screen width");
  puts("  -stretch                Fit viewport to resolution, ignoring aspect ratio");
  puts("  -upscalemode=<n>        2D layer upscaling filter mode (range 0-3)");
  puts("  -no-throttle            Disable frame rate lock");
  puts("  -vsync                  Lock to vertical refresh rate [Default]");
  puts("  -no-vsync               Do not lock to vertical refresh rate");
  puts("  -true-hz                Use true Model 3 refresh rate of 57.524 Hz");
  puts("  -show-fps               Display frame rate in window title bar");
  puts("  -no-white-flash         Disables white flash when games disable 3D rendering");
  puts("");
  puts("Audio Options:");
  puts("  -sound-volume=<vol>     Volume of SCSP-generated sound in %, applies only");
  puts("                          when Digital Sound Board is present [Default: 100]");
  puts("  -music-volume=<vol>     Digital Sound Board volume in % [Default: 100]");
  puts("  -balance=<bal>          Relative front/rear balance in % [Default: 0]");
  puts("  -channels=<c>           Number of sound channels to use on host [Default: 4]");
  puts("  -flip-stereo            Swap left and right audio channels");
  puts("  -no-sound               Disable sound board emulation (sound effects)");
  puts("  -no-dsb                 Disable Digital Sound Board (MPEG music)");
  puts("");
  puts("Net Options:");
  puts("  -no-net                 Disable net board [Default]");
  puts("  -net                    Enable net board");
  puts("  -simulate-netboard      Simulate the net board [Default]");
  puts("  -emulate-netboard       Emulate the net board (requires -no-threads)");
  printf("  -port=<n>, -port-in=<n> TCP port of this machine (net board)\n"
         "                          [Default: %u]\n", defaultConfig["PortIn"].ValueAs<unsigned>());
  printf("  -port-out=<n>           TCP port of the next machine [Default: %u]\n", defaultConfig["PortOut"].ValueAs<unsigned>());
  printf("  -address-out=<addr>     Address of the next machine [Default: %s]\n", defaultConfig["AddressOut"].ValueAs<std::string>().c_str());
  puts("");
  puts("Input Options:");
  puts("  -force-feedback         Enable force feedback (DirectInput, XInput)");
  puts("  -config-inputs          Configure keyboards, mice, and game controllers");
#ifdef SUPERMODEL_WIN32
  printf("  -input-system=<s>       Input system [Default: %s]\n", defaultConfig["InputSystem"].ValueAs<std::string>().c_str());
#endif
  puts("  -print-inputs           Prints current input configuration");
  puts("");
  puts("Output Options:");
  printf("  -outputs=<s>            Outputs [Default: %s]\n", defaultConfig["Outputs"].ValueAs<std::string>().c_str());
  puts("");
  puts("Debug Options:");
  puts("  -dump-memory            Write memory regions to files on exit");
  puts("  -dump-textures          Write textures to bitmap image files on exit");
#ifdef SUPERMODEL_DEBUGGER
  puts("  -disable-debugger       Completely disable debugger functionality");
  puts("  -enter-debugger         Enter debugger at start of emulation");
#endif // SUPERMODEL_DEBUGGER
#ifdef DEBUG
  puts("  -gfx-state=<file>       View graphics from a save state");
#endif
  puts("");
}

struct ParsedCommandLine
{
  Util::Config::Node config = Util::Config::Node("CommandLine");
  std::vector<std::string> rom_files;
  bool error = false;
  bool print_help = false;
  bool print_games = false;
  bool config_inputs = false;
  bool print_inputs = false;
  bool disable_debugger = false;
  bool enter_debugger = false;
#ifdef DEBUG
  std::string gfx_state;
#endif

  ParsedCommandLine()
  {
    // Logging is special: it is only parsed from the command line and
    // therefore, defaults are needed early
    config.Set("LogOutput", s_logFilePath.c_str());
    config.Set("LogLevel", "info");
  }
};

static ParsedCommandLine ParseCommandLine(int argc, char **argv)
{
  ParsedCommandLine cmd_line;
  static const std::map<std::string, std::string> valued_options
  { // -option=value
    { "-game-xml-file",         "GameXMLFile"             },
    { "-load-state",            "InitStateFile"           },
    { "-ppc-frequency",         "PowerPCFrequency"        },
    { "-sound-volume",          "SoundVolume"             },
    { "-music-volume",          "MusicVolume"             },
    { "-balance",               "Balance"                 },
    { "-channels", 	            "NbSoundChannels"         },
    { "-input-system",          "InputSystem"             },
    { "-outputs",               "Outputs"                 },
    { "-port",                  "PortIn"                  },
    { "-port-in",               "PortIn"                  },
    { "-port-out",              "PortOut"                 },
    { "-address-out",           "AddressOut"              },
    { "-log-output",            "LogOutput"               },
    { "-log-level",             "LogLevel"                }
  };
  static const std::map<std::string, std::pair<std::string, bool>> bool_options
  { // -option
    { "-threads",             { "MultiThreaded",    true } },
    { "-no-threads",          { "MultiThreaded",    false } },
    { "-gpu-multi-threaded",  { "GPUMultiThreaded", true } },
    { "-no-gpu-thread",       { "GPUMultiThreaded", false } },
    { "-window",              { "FullScreen",       false } },
    { "-fullscreen",          { "FullScreen",       true } },
    { "-borderless",          { "BorderlessWindow", true } },
    { "-no-wide-screen",      { "WideScreen",       false } },
    { "-wide-screen",         { "WideScreen",       true } },
    { "-stretch",             { "Stretch",          true } },
    { "-no-stretch",          { "Stretch",          false } },
    { "-wide-bg",             { "WideBackground",   true } },
    { "-no-wide-bg",          { "WideBackground",   false } },
    { "-throttle",            { "Throttle",         true } },
    { "-no-throttle",         { "Throttle",         false } },
    { "-vsync",               { "VSync",            true } },
    { "-no-vsync",            { "VSync",            false } },
    { "-show-fps",            { "ShowFrameRate",    true } },
    { "-no-fps",              { "ShowFrameRate",    false } },
    { "-no-flip-stereo",      { "FlipStereo",       false } },
    { "-flip-stereo",         { "FlipStereo",       true } },
    { "-sound",               { "EmulateSound",     true } },
    { "-no-sound",            { "EmulateSound",     false } },
    { "-dsb",                 { "EmulateDSB",       true } },
    { "-no-dsb",              { "EmulateDSB",       false } },
    { "-no-white-flash",      { "NoWhiteFlash",     true } },
    { "-white-flash",         { "NoWhiteFlash",     false } },
    { "-net",                 { "Network",          true } },
    { "-no-net",              { "Network",          false } },
    { "-simulate-netboard",   { "SimulateNet",      true } },
    { "-emulate-netboard",    { "SimulateNet",      false } },
    { "-no-force-feedback",   { "ForceFeedback",    false } },
    { "-force-feedback",      { "ForceFeedback",    true } },
    { "-dump-memory",         { "DumpMemory",       true } },
    { "-dump-textures",       { "DumpTextures",     true } },
  };
  for (int i = 1; i < argc; i++)
  {
    std::string arg(argv[i]);
    if (arg[0] == '-')
    {
      // First, check maps
      size_t idx_equals = arg.find_first_of('=');
      if (idx_equals != std::string::npos)
      {
        std::string option(arg.begin(), arg.begin() + idx_equals);
        std::string value(arg.begin() + idx_equals + 1, arg.end());
        if (value.empty())
        {
          ErrorLog("Argument to '%s' cannot be blank.", option.c_str());
          cmd_line.error = true;
          continue;
        }
        auto it = valued_options.find(option);
        if (it != valued_options.end())
        {
          const std::string &config_key = it->second;
          cmd_line.config.Set(config_key, value);
          continue;
        }
      }
      else
      {
        auto it = bool_options.find(arg);
        if (it != bool_options.end())
        {
          const std::string &config_key = it->second.first;
          bool value = it->second.second;
          cmd_line.config.Set(config_key, value);
          continue;
        }
        else if (valued_options.find(arg) != valued_options.end())
        {
          ErrorLog("'%s' requires an argument.", argv[i]);
          cmd_line.error = true;
          continue;
        }
      }
      // Fell through -- handle special cases
      if (arg == "-?" || arg == "-h" || arg == "-help" || arg == "--help")
        cmd_line.print_help = true;
      else if (arg == "-print-games")
        cmd_line.print_games = true;
      else if (arg == "-res" || arg.find("-res=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
          cmd_line.error = true;
        }
        else
        {
          unsigned  x, y;
          if (2 == sscanf(&argv[i][4],"=%u,%u", &x, &y))
          {
            std::string xres = Util::Format() << x;
            std::string yres = Util::Format() << y;
            cmd_line.config.Set("XResolution", xres);
            cmd_line.config.Set("YResolution", yres);
          }
          else
          {
            ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
            cmd_line.error = true;
          }
        }
      }
      else if (arg == "-window-pos" || arg.find("-window-pos=") == 0)
      {
          std::vector<std::string> parts = Util::Format(arg).Split('=');
          if (parts.size() != 2)
          {
              ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
              cmd_line.error = true;
          }
          else
          {
              int xpos, ypos;
              if (2 == sscanf(&argv[i][11], "=%d,%d", &xpos, &ypos))
              {
                  cmd_line.config.Set("WindowXPosition", xpos);
                  cmd_line.config.Set("WindowYPosition", ypos);
              }
              else
              {
                  ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-upscalemode" || arg.find("-upscalemode=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 0, 3);

                  cmd_line.config.Set("UpscaleMode", val);
              }
              catch (...) {
                  ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-true-hz")
        cmd_line.config.Set("RefreshRate", 57.524f);
      else if (arg == "-config-inputs")
        cmd_line.config_inputs = true;
      else if (arg == "-print-inputs")
        cmd_line.print_inputs = true;
#ifdef SUPERMODEL_DEBUGGER
      else if (arg == "-disable-debugger")
        cmd_line.disable_debugger = true;
      else if (arg == "-enter-debugger")
        cmd_line.enter_debugger = true;
#endif
#ifdef DEBUG
      else if (arg == "-gfx-state" || arg.find("-gfx-state=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {
          ErrorLog("'-gfx-state' requires a file name.");
          cmd_line.error = true;
        }
        else
          cmd_line.gfx_state = parts[1];
      }
#endif
      else
      {
        ErrorLog("Ignoring unrecognized option: %s", argv[i]);
        cmd_line.error = true;
      }
    }
    else
      cmd_line.rom_files.emplace_back(arg);
  }
  return cmd_line;
}

/*
 * main(argc, argv):
 *
 * Program entry point.
 */
int main(int argc, char **argv)
{
  Title();
  WriteDefaultConfigurationFileIfNotPresent();

  bool loadGUI = false;
  if (argc <= 1)
  {
    //Help();
    loadGUI = true;
  }

  // Before command line is parsed, console logging only
  SetLogger(std::make_shared<CConsoleErrorLogger>());

  // Load config and parse command line
  auto cmd_line = ParseCommandLine(argc, argv);
  if (cmd_line.error)
  {
    return 1;
  }

  if (loadGUI) {
      // Load up what settings we have so far. The complete file is passed to the GUI,
      // which edits only the global section and preserves any game-specific sections.
      // Editing the game-specific sections themselves is not supported atm but we can
      // look at this later.
      Util::Config::Node fileConfig("Global");
      Util::Config::FromINIFile(&fileConfig, s_configFilePath);
    ClearRetiredVideoSettings(fileConfig);

      cmd_line.rom_files = RunGUI(s_configFilePath, fileConfig);

      if (cmd_line.rom_files.empty()) {
          return 0;
      }
  }

  // Create logger as specified by command line
  auto logger = CreateLogger(cmd_line.config);
  if (!logger)
  {
    ErrorLog("Unable to initialize logging system.");
    return 1;
  }
  SetLogger(logger);
  InfoLog("Supermodel Version " SUPERMODEL_VERSION);
  InfoLog("Started as:");
  for (int i = 0; i < argc; i++)
    InfoLog("  argv[%d] = %s", i, argv[i]);

  // Finish processing command line
  if (cmd_line.print_help)
  {
    Help();
    return 0;
  }
#ifdef DEBUG
  s_gfxStatePath.assign(cmd_line.gfx_state);
#endif
  bool print_games = cmd_line.print_games;
  bool rom_specified = !cmd_line.rom_files.empty();
  if (!rom_specified && !print_games && !cmd_line.config_inputs && !cmd_line.print_inputs)
  {
    ErrorLog("No ROM file specified.");
    return 0;
  }

  // Load game and resolve run-time config
  Game game;
  ROMSet rom_set;

  Util::Config::Node fileConfig("Global");
  {
    Util::Config::Node fileConfigWithDefaults("Global");
    Util::Config::Node config3("Global");
    Util::Config::Node config4("Global");
    Util::Config::FromINIFile(&fileConfig, s_configFilePath);
    ClearRetiredVideoSettings(fileConfig);
    Util::Config::MergeINISections(&fileConfigWithDefaults, DefaultConfig(), fileConfig); // apply .ini file's global section over defaults
    Util::Config::MergeINISections(&config3, fileConfigWithDefaults, cmd_line.config);    // apply command line overrides
    if (rom_specified || print_games)
    {
      std::string xml_file = config3["GameXMLFile"].ValueAs<std::string>();
      GameLoader loader(xml_file);
      if (print_games)
      {
        PrintGameList(xml_file, loader.GetGames());
        return 0;
      }
      if (loader.Load(&game, &rom_set, *cmd_line.rom_files.begin()))
        return 1;
      Util::Config::MergeINISections(&config4, config3, fileConfig[game.name]);   // apply game-specific config
    }
    else
      config4 = config3;
    Util::Config::MergeINISections(&s_runtime_config, config4, cmd_line.config);  // apply command line overrides once more
  }
  LogConfig(s_runtime_config);
  if (rom_specified && game.platform != Game::PLATFORM_MODEL3)
    { ErrorLog("Pro-1000 support has been removed; this build runs Model 3 games only."); return 1; }

  // Initialize SDL (individual subsystems get initialized later)
  if (SDL_Init(0) != 0)
  {
    ErrorLog("Unable to initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // Flag as DPI-aware, otherwise the window content might be scaled by some graphics drivers
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");

  // Begin initializing various subsystems...
  int exitCode = 0;
  IEmulator *Emu = nullptr;
  std::shared_ptr<CNetServer> netServer;
  std::shared_ptr<CInputSystem> InputSystem;
  CInputs *Inputs = nullptr;
  COutputs *Outputs = nullptr;
  std::unique_ptr<CConsoleInput> consoleInput;  // must outlive emulator and debugger
#ifdef SUPERMODEL_DEBUGGER
  std::shared_ptr<Debugger::CSupermodelDebugger> Debugger;
#endif // SUPERMODEL_DEBUGGER
  std::string selectedInputSystem = s_runtime_config["InputSystem"].ValueAs<std::string>();


  // Create a window
  xRes = 496;
  yRes = 384;
  if (!rom_specified)
  {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    s_window = SDL_CreateWindow("Supermodel Input Setup", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, xRes, yRes, SDL_WINDOW_SHOWN);
    if (!s_window) { exitCode = 1; goto Exit; }
  }
  else
  {
#ifdef SUPERMODEL_METAL
    if (CreateMetalScreen() != Result::OKAY) { exitCode = 1; goto Exit; }
#elif defined(SUPERMODEL_VULKAN)
    if (CreateVulkanScreen() != Result::OKAY) { exitCode = 1; goto Exit; }
#else
#error A native graphics backend is required
#endif
  }

  // Create input system
  if (selectedInputSystem == "sdl")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, false));
  else if (selectedInputSystem == "sdlgamepad")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, true));
#ifdef SUPERMODEL_WIN32
  else if (selectedInputSystem == "dinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, false));
  else if (selectedInputSystem == "xinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, true));
  else if (selectedInputSystem == "rawinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, true, false));
#endif // SUPERMODEL_WIN32
  else
  {
    ErrorLog("Unknown input system: %s\n", selectedInputSystem.c_str());
    exitCode = 1;
    goto Exit;
  }

  // Create inputs from input system (configuring them if required)
  Inputs = new CInputs(InputSystem);
  if (!Inputs->Initialize())
  {
    ErrorLog("Unable to initalize inputs.\n");
    exitCode = 1;
    goto Exit;
  }

  // NOTE: fileConfig is passed so that the global section is used for input settings
  // and because this function may write out a new config file, which must preserve
  // all sections. We don't want to pollute the output with built-in defaults.
  if (ConfigureInputs(Inputs, &fileConfig, &s_runtime_config, game, cmd_line.config_inputs) != Result::OKAY)
  {
    exitCode = 1;
    goto Exit;
  }

  if (cmd_line.print_inputs)
  {
    Inputs->PrintInputs(NULL);
    InputSystem->PrintSettings();
  }

  if (!rom_specified)
    goto Exit;

  // Create outputs
  {
    std::string outputs = s_runtime_config["Outputs"].ValueAs<std::string>();
    if (outputs == "none")
      Outputs = NULL;
    else if (outputs == "net")
    {
        CNetOutputs* netOutputs = new CNetOutputs();
        if (s_runtime_config["OutputsWithLF"].ValueAs<bool>())
            netOutputs->SetFrameEnding(std::string("\r\n"));
        netOutputs->SetTcpPort(s_runtime_config["OutputsTCPPort"].ValueAs<unsigned int>());
        netOutputs->SetUdpBroadcastPort(s_runtime_config["OutputsUDPBroadcastPort"].ValueAs<unsigned int>());
        Outputs = (COutputs*)netOutputs;
#ifdef SUPERMODEL_WIN32
    } else if (outputs == "win") {
      Outputs = new CWinOutputs();
#endif // SUPERMODEL_WIN32
    }
    else
    {
      ErrorLog("Unknown outputs: %s\n", outputs.c_str());
      exitCode = 1;
      goto Exit;
    }
  }

  // Initialize outputs
  if (Outputs != NULL && !Outputs->Initialize())
  {
    ErrorLog("Unable to initialize outputs.\n");
    exitCode = 1;
    goto Exit;
  }

  // Start the net board server when networking is enabled.
  if (game.netboard_present && s_runtime_config["Network"].ValueAs<bool>())
  {
    netServer = std::make_shared<CNetServer>(s_runtime_config["PortIn"].ValueAs<unsigned>(), "Supermodel " SUPERMODEL_VERSION " (" + game.name + ")");
    netServer->Start();
  }

  // Only the debugger consumes console input.
  {
    bool needConsoleInput = false;
#ifdef SUPERMODEL_DEBUGGER
    needConsoleInput |= !cmd_line.disable_debugger;
#endif // SUPERMODEL_DEBUGGER
    if (needConsoleInput)
      consoleInput = std::make_unique<CConsoleInput>();
  }

  // Create emulator for the appropriate platform
#ifdef DEBUG
  if (!s_gfxStatePath.empty())
    Emu = new CModel3GraphicsState(s_runtime_config, s_gfxStatePath);
  else
#endif
    Emu = new CModel3(s_runtime_config, Inputs, Outputs, netServer);

#ifdef SUPERMODEL_DEBUGGER
  // Create Supermodel debugger unless debugging is disabled
  if (!cmd_line.disable_debugger)
  {
    Debugger = std::make_shared<Debugger::CSupermodelDebugger>(Emu, Inputs, logger);
    if (consoleInput)
      Debugger->AttachConsoleInput(consoleInput->CreateInput());
    // If -enter-debugger option was set force debugger to break straightaway
    if (cmd_line.enter_debugger)
      Debugger->ForceBreak(true);
  }
  // Fire up Supermodel with debugger
  exitCode = Supermodel(game, &rom_set, Emu, Inputs, Outputs, Debugger);
#else
  // Fire up Supermodel
  exitCode = Supermodel(game, &rom_set, Emu, Inputs, Outputs);
#endif // SUPERMODEL_DEBUGGER
  delete Emu;

Exit:
  netServer.reset();
  delete Inputs;
  delete Outputs;
  DestroyScreen();
  SDL_Quit();

  if (exitCode)
    InfoLog("Program terminated due to an error.");
  else
    InfoLog("Program terminated normally.");

  return exitCode;
}


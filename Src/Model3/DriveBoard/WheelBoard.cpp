/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2021 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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
 * WheelBoard.cpp
 *
 * This code is enhanced with Claude by ANthropic. 
 * HLE (High Level Emulation) replacement for the drive board Z80 ROM.
 * Force feedback commands from the PPC are decoded directly here and
 * forwarded to SDL Haptic — no Z80 ROM required.
 *
 * Supported games:
 *   - Scud Race          (command set A: 0x1x/2x/3x/5x/6x/8x/Cx/Dx)
 *   - Daytona 2          (command set A: compatible with Scud Race)
 *   - Sega Rally 2       (command set B: encoder-based via ports 0x42/0x46)
 *
 * HLE strategy:
 *   m_simulated is forced TRUE always — CDriveBoard Z80 core is never started.
 *   Write() dispatches to HLEWrite() which auto-detects the active game by
 *   inspecting the cabinet type byte sent during initialisation (0xB0/0xB1).
 *   SimulateRead() returns a fast fake init sequence so the game boots without
 *   the usual drive board delay.
 */

#include "WheelBoard.h"

#include "Supermodel.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#if defined(_WIN32) || defined(__linux__)
  #define HLE_USE_STEERING_AXIS 1
#else
  #define HLE_USE_STEERING_AXIS 0
#endif

// ---------------------------------------------------------------------------
// FF backend selection
//
// Priority:
//   1. SDL_Haptic  — steering wheels (DirectInput/evdev FF effects)
//   2. SDL_GameController Rumble — Xbox / generic gamepads
// ---------------------------------------------------------------------------

// SDL_Haptic backend
static SDL_Haptic* s_haptic          = nullptr;
static int  s_effectConstant         = -1;
static int  s_effectSpring           = -1;
static int  s_effectFriction         = -1;
static int  s_effectVibrate          = -1;
static bool s_hasConstant            = false;
static bool s_hasSpring              = false;
static bool s_hasFriction            = false;
static bool s_hasSine                = false;

// SDL_GameController Rumble backend (Xbox / generic gamepad fallback)
static SDL_GameController* s_gamepad = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline Sint16 NormToSint16(float f)
{
  return (Sint16)std::clamp((int)(f * 32767.0f), -32767, 32767);
}

// Set the direction on a haptic effect in a platform-appropriate way.
// For periodic / constant effects: dir points along X axis (left/right).
// For condition effects (spring, friction): direction is unused by SDL
// (axis is inferred from which channel is filled), so we set CARTESIAN
// as a safe neutral on all platforms.
static void SetEffectDirection(SDL_HapticDirection& dir, bool isCondition = false)
{
  if (isCondition)
  {
    // Spring / Friction — direction field is informational only in SDL2;
    // CARTESIAN X-axis is safe everywhere.
    dir.type     = SDL_HAPTIC_CARTESIAN;
    dir.dir[0]   = 1;
    dir.dir[1]   = 0;
    dir.dir[2]   = 0;
    return;
  }

#if HLE_USE_STEERING_AXIS
  // Windows / Linux: use the dedicated steering-axis hint so the driver
  // can route the force correctly even without knowing the axis index.
  dir.type     = SDL_HAPTIC_STEERING_AXIS;
  dir.dir[0]   = 0;
#else
  // macOS: CARTESIAN X-axis (index 0) maps to the wheel's primary axis
  // for most HID-compliant steering wheels.
  dir.type     = SDL_HAPTIC_CARTESIAN;
  dir.dir[0]   = 1;   // positive = right
  dir.dir[1]   = 0;
  dir.dir[2]   = 0;
#endif
}

static int UploadEffect(int existingId, SDL_HapticEffect& eff)
{
  if (!s_haptic) return -1;
  if (existingId >= 0)
  {
    if (SDL_HapticUpdateEffect(s_haptic, existingId, &eff) == 0)
      return existingId;
    SDL_HapticDestroyEffect(s_haptic, existingId);
  }
  return SDL_HapticNewEffect(s_haptic, &eff);
}

// ---------------------------------------------------------------------------
// SDL Haptic lifecycle
// ---------------------------------------------------------------------------

bool CWheelBoard::InitSDLHaptic()
{
  if (SDL_InitSubSystem(SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0)
  {
    ErrorLog("SDL FF init failed: %s\n", SDL_GetError());
    return false;
  }

  // --- Try SDL_Haptic first (steering wheels) ---
  int numHaptics = SDL_NumHaptics();
  for (int i = 0; i < numHaptics; i++)
  {
    SDL_Haptic *h = SDL_HapticOpen(i);
    if (!h) continue;

    unsigned int caps = SDL_HapticQuery(h);
    // Accept if it supports at least one wheel-type effect
    if (caps & (SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SPRING | SDL_HAPTIC_FRICTION | SDL_HAPTIC_SINE))
    {
      s_haptic      = h;
      s_hasConstant = (caps & SDL_HAPTIC_CONSTANT)  != 0;
      s_hasSpring   = (caps & SDL_HAPTIC_SPRING)     != 0;
      s_hasFriction = (caps & SDL_HAPTIC_FRICTION)   != 0;
      s_hasSine     = (caps & SDL_HAPTIC_SINE)       != 0;
      SDL_HapticRumbleInit(s_haptic);
      InfoLog("SDL Haptic opened: %s (CONSTANT=%d SPRING=%d FRICTION=%d SINE=%d)",
              SDL_HapticName(i),
              s_hasConstant, s_hasSpring, s_hasFriction, s_hasSine);
      return true;
    }
    SDL_HapticClose(h);
  }

  // --- Fallback: SDL_GameController Rumble (Xbox / generic gamepad) ---
  int numJoys = SDL_NumJoysticks();
  for (int i = 0; i < numJoys; i++)
  {
    if (!SDL_IsGameController(i)) continue;
    SDL_GameController *gc = SDL_GameControllerOpen(i);
    if (!gc) continue;
    // SDL_GameControllerRumble requires SDL 2.0.9+
    // Test rumble support with a 0-strength call
    if (SDL_GameControllerRumble(gc, 0, 0, 0) == 0)
    {
      s_gamepad = gc;
      InfoLog("SDL GameController rumble opened: %s", SDL_GameControllerName(gc));
      return true;
    }
    SDL_GameControllerClose(gc);
  }

  ErrorLog("No SDL haptic or gamepad rumble devices found.\n");
  return false;
}

void CWheelBoard::CloseSDLHaptic()
{
  if (s_haptic)
  {
    SDL_HapticStopAll(s_haptic);
    auto Destroy = [](int& id) {
      if (id >= 0) { SDL_HapticDestroyEffect(s_haptic, id); id = -1; }
    };
    Destroy(s_effectConstant);
    Destroy(s_effectSpring);
    Destroy(s_effectFriction);
    Destroy(s_effectVibrate);
      SDL_HapticClose(s_haptic);
    s_haptic = nullptr;
  }
  if (s_gamepad)
  {
    SDL_GameControllerRumble(s_gamepad, 0, 0, 0);
    SDL_GameControllerClose(s_gamepad);
    s_gamepad = nullptr;
  }
}

// ===========================================================================
// CWheelBoard public interface
// ===========================================================================

Game::DriveBoardType CWheelBoard::GetType(void) const
{
  return Game::DRIVE_BOARD_WHEEL;
}

void CWheelBoard::Get7SegDisplays(UINT8 &seg1Digit1, UINT8 &seg1Digit2,
                                   UINT8 &seg2Digit1, UINT8 &seg2Digit2) const
{
  seg1Digit1 = m_seg1Digit1;
  seg1Digit2 = m_seg1Digit2;
  seg2Digit1 = m_seg2Digit1;
  seg2Digit2 = m_seg2Digit2;
}

// ---------------------------------------------------------------------------
// Reset — force HLE mode unconditionally
// ---------------------------------------------------------------------------

void CWheelBoard::Reset(void)
{
  CDriveBoard::Reset();

  m_seg1Digit1 = 0xFF;  m_seg1Digit2 = 0xFF;
  m_seg2Digit1 = 0xFF;  m_seg2Digit2 = 0xFF;

  m_adcPortRead = 0;    m_adcPortBit  = 0;
  m_port42Out   = 0;    m_port46Out   = 0;
  m_prev42Out   = 0;    m_prev46Out   = 0;

  m_uncenterVal1 = 0;   m_uncenterVal2 = 0;

  m_lastConstForce = 0; m_lastSelfCenter = 0;
  m_lastFriction   = 0; m_lastVibrate    = 0;

  // HLE: always bypass Z80
  m_simulated = true;

  // Detect game type from cabinet byte received during init (reset to unknown)
  m_hleGameType = HLE_GAME_UNKNOWN;

  if (!m_config["ForceFeedback"].ValueAsDefault<bool>(false))
    Disable();

  if (!IsDisabled())
    SendStopAll();
}

// ---------------------------------------------------------------------------
// Read / Write — pure HLE path
// ---------------------------------------------------------------------------

UINT8 CWheelBoard::Read(void)
{
  if (IsDisabled()) return 0xFF;
  return HLERead();        // never calls Z80
}

void CWheelBoard::Write(UINT8 data)
{
  if (IsDisabled()) return;
  HLEWrite(data);          // never calls Z80
}

void CWheelBoard::RunFrame(void)
{
  if (IsDisabled()) return;
  HLEFrame();              // never calls CDriveBoard::RunFrame()
}

// ===========================================================================
// HLE core
// ===========================================================================

// ---------------------------------------------------------------------------
// HLERead — fast fake initialisation sequence, then normal status reads
// ---------------------------------------------------------------------------

UINT8 CWheelBoard::HLERead(void)
{
  if (!m_initialized)
  {
    // Compressed init: answer the 4-step handshake immediately so the game
    // does not stall waiting for the board to boot.
    switch (m_initState / 2)      // 2 frames per step instead of 5
    {
      case 0:  return 0xCF;
      case 1:  return 0xCE;
      case 2:  return 0xCD;
      case 3:  return 0xCC;
      default:
        m_initialized = true;
        return 0x80;
    }
  }

  // Normal read — same as SimulateRead()
  switch (m_readMode)
  {
    case 0x0: return m_statusFlags;
    case 0x1: return m_dip1;
    case 0x2: return m_dip2;
    case 0x3: return m_wheelCenter;
    case 0x4: return 0x80;                             // cockpit banking center
    case 0x5: return (UINT8)m_inputs->steering->value; // wheel position
    case 0x6: return 0x80;                             // cockpit banking position
    case 0x7: return m_echoVal;
    default:  return 0xFF;
  }
}

// ---------------------------------------------------------------------------
// HLEFrame — advance init counter
// ---------------------------------------------------------------------------

void CWheelBoard::HLEFrame(void)
{
  if (!m_initialized)
    m_initState++;
}

// ---------------------------------------------------------------------------
// HLEWrite — top-level dispatcher
//
// Cabinet type byte (0xB0 / 0xB1) sent during PPC init lets us detect which
// game is running so we can apply the correct command decoder.
//
// Scud Race / Daytona 2 use identical high-nibble command set (set A).
// Sega Rally 2 uses a completely different encoder protocol (set B) that
// arrives via Z80 ports 0x42/0x46 — but because we bypass the Z80 we
// instead intercept the *PPC-level* encoder commands that would have been
// forwarded through the Z80: they arrive as raw 0x42/0x46 port writes
// redirected here through IOWrite8() → ProcessEncoderCmd().
// ---------------------------------------------------------------------------

void CWheelBoard::HLEWrite(UINT8 data)
{
  // Detect cabinet / game type from init byte
  if (data == 0xB0 || data == 0xB1)
  {
    // 0xB0 = standard cabinet (Scud Race / Daytona 2)
    // 0xB1 = deluxe / twin cabinet (also Sega Rally 2 twin)
    // We distinguish Rally 2 later by its unique command patterns.
    m_hleCabinetType = data;
    DebugLog("[HLE] Cabinet type: %02X\n", data);
    return;
  }

  // Reset command (0xCB) — common to all games
  if (data == 0xCB)
  {
    SendStopAll();
    m_initialized = false;
    m_initState   = 0;
    return;
  }

  // Route by detected game type.
  // If not yet determined, try command-set A first (covers most commands).
  // Sega Rally 2 encoder commands arrive via ProcessEncoderCmd() separately.
  HLEDecodeCommandSetA(data);
}

// ---------------------------------------------------------------------------
// Command Set A: Scud Race + Daytona 2
// (Daytona 2 is documented as compatible with Scud Race — verified same map)
// ---------------------------------------------------------------------------

void CWheelBoard::HLEDecodeCommandSetA(UINT8 cmd)
{
  UINT8 type = cmd >> 4;
  UINT8 val  = cmd & 0xF;

  switch (type)
  {
  // ------------------------------------------------------------------
  // 0x00-0F  Play preset sequence
  // Known sequences observed in Scud Race / Daytona 2 traces:
  //   0x01 = light jolt right
  //   0x02 = light jolt left
  //   0x03 = rumble burst (road texture)
  //   0x04 = strong collision
  //   0x05 = curb vibration
  //   0x06 = sustained rumble
  // ------------------------------------------------------------------
  case 0x0:
    switch (val)
    {
    case 0x0: SendStopAll();             break; // 0x00 stop all
    case 0x1: PlaySequenceJolt(+30);     break; // jolt right
    case 0x2: PlaySequenceJolt(-30);     break; // jolt left
    case 0x3: PlaySequenceRumble(80);    break; // road rumble
    case 0x4: PlaySequenceJolt(+80);     break; // hard collision
    case 0x5: PlaySequenceRumble(40);    break; // curb
    case 0x6: PlaySequenceRumble(60);    break; // sustained rumble
    default:  DebugLog("[HLE] Unknown sequence 0x0%X\n", val); break;
    }
    break;

  // ------------------------------------------------------------------
  // 0x10-1F  Self-centering spring strength
  //          0x10 = disable, 0x11-0x1F = weakest→strongest
  // ------------------------------------------------------------------
  case 0x1:
    SendSelfCenter(val == 0 ? 0 : val * 0x11);
    break;

  // ------------------------------------------------------------------
  // 0x20-2F  Friction strength
  //          0x20 = disable, 0x21-0x2F = weakest→strongest
  // ------------------------------------------------------------------
  case 0x2:
    SendFriction(val == 0 ? 0 : val * 0x11);
    break;

  // ------------------------------------------------------------------
  // 0x30-3F  Uncentering / vibration strength
  //          0x30 = disable, 0x31-0x3F = weakest→strongest
  // ------------------------------------------------------------------
  case 0x3:
    SendVibrate(val == 0 ? 0 : val * 0x11);
    break;

  // ------------------------------------------------------------------
  // 0x40-4F  Power-slide sequence
  //          Strength encoded in low nibble (0 = stop)
  // ------------------------------------------------------------------
  case 0x4:
    if (val == 0)
      SendVibrate(0);
    else
      PlaySequencePowerSlide(val * 0x11);
    break;

  // ------------------------------------------------------------------
  // 0x50-5F  Constant force right  (0x51 weakest … 0x5F strongest)
  // ------------------------------------------------------------------
  case 0x5:
    SendConstantForce((INT8)((val + 1) * 0x5));
    break;

  // ------------------------------------------------------------------
  // 0x60-6F  Constant force left   (0x61 weakest … 0x6F strongest)
  // ------------------------------------------------------------------
  case 0x6:
    SendConstantForce(-(INT8)((val + 1) * 0x5));
    break;

  // ------------------------------------------------------------------
  // 0x70-7F  Steering sensitivity / deadzone parameters
  //          (stored but not currently mapped to an SDL effect)
  // ------------------------------------------------------------------
  case 0x7:
    m_steeringParam = val;
    DebugLog("[HLE] Steering param: %X\n", val);
    break;

  // ------------------------------------------------------------------
  // 0x80-8F  Test / diagnostic mode commands
  // ------------------------------------------------------------------
  case 0x8:
    switch (val & 0x7)
    {
    case 0: SendStopAll();                                     break; // 0x80 stop
    case 1: SendConstantForce(20);                             break; // 0x81 roll right
    case 2: SendConstantForce(-20);                            break; // 0x82 roll left
    case 3: /* clutch on  — no clutch in HLE */                break;
    case 4: /* clutch off */                                   break;
    case 5: m_wheelCenter = (UINT8)m_inputs->steering->value;  break; // 0x85 set center
    case 6: /* cockpit banking — ignore */                     break;
    case 7: /* lamp on/off — ignore */                         break;
    }
    break;

  // ------------------------------------------------------------------
  // 0x90-9F / 0xA0-AF  Unknown, observed to have no effect with ROM
  // ------------------------------------------------------------------
  case 0x9:
  case 0xA:
    DebugLog("[HLE] Unimplemented cmd %02X\n", cmd);
    break;

  // ------------------------------------------------------------------
  // 0xB0-BF  Cabinet type (handled above in HLEWrite, should not reach here)
  // ------------------------------------------------------------------
  case 0xB:
    break;

  // ------------------------------------------------------------------
  // 0xC0-CF  Board mode / reset
  //          0xCB = full reset (handled in HLEWrite)
  //          0xC0-0xCA = set board mode
  // ------------------------------------------------------------------
  case 0xC:
    SendStopAll();
    m_boardMode = val;
    break;

  // ------------------------------------------------------------------
  // 0xD0-DF  Set read mode (which value HLERead() returns)
  // ------------------------------------------------------------------
  case 0xD:
    m_readMode = val & 0x7;
    break;

  // ------------------------------------------------------------------
  // 0xE0-EF  Invalid / reserved
  // ------------------------------------------------------------------
  case 0xE:
    break;

  // ------------------------------------------------------------------
  // 0xF0-FF  Echo test
  // ------------------------------------------------------------------
  case 0xF:
    m_echoVal = val;
    break;
  }
}

// ---------------------------------------------------------------------------
// Command Set B: Sega Rally 2 — encoder protocol via ports 0x42 / 0x46
//
// The PPC writes motor data to Z80 port 0x2A (→ port42Out) and motor
// control to Z80 port 0x2E (→ port46Out).  Because we bypass the Z80,
// IOWrite8() feeds these directly into ProcessEncoderCmd().
// ---------------------------------------------------------------------------

void CWheelBoard::ProcessEncoderCmd(void)
{
  if (m_prev42Out == m_port42Out && m_prev46Out == m_port46Out)
    return;

  DebugLog("[HLE] Encoder: port46=%02X port42=%02X\n", m_port46Out, m_port42Out);

  switch (m_port46Out)
  {
  case 0xFB:
    // Friction during power-slide. Strength = port42Out (0xFF strongest)
    SendFriction(m_port42Out);
    break;

  case 0xFC:
    // Centering (bit2=1) or uncentering/vibrate (bit2=0)
    if (m_port42Out & 0x04)
    {
      if (m_port42Out & 0x80)
        SendSelfCenter(0);
      else
      {
        UINT8 strength = ((m_port42Out & 0x78) >> 3) * 0x10 + 0xF;
        SendSelfCenter(strength);
      }
    }
    else
    {
      // Uncentering: 4 sequential nibble writes build the strength value
      UINT8  seqNum = m_port42Out & 0x03;
      UINT16 d      = (m_port42Out & 0xF0) >> 4;
      switch (seqNum)
      {
        case 0: m_uncenterVal1  = d << 4; break;
        case 1: m_uncenterVal1 |= d;      break;
        case 2: m_uncenterVal2  = d << 4; break;
        case 3: m_uncenterVal2 |= d;      break;
      }
      if (seqNum == 0 && m_uncenterVal1 == 0)
        SendVibrate(0);
      else if (seqNum == 3 && m_uncenterVal1 > 0)
      {
        UINT8 strength = ((m_uncenterVal1 >> 1) - 7) * 0x50
                       + ((m_uncenterVal2 >> 1) - 5) * 0x10 + 0xF;
        SendVibrate(strength);
      }
    }
    break;

  case 0xFD:
    // Velocity-dependent centering (similar to spring, strength from port42Out)
    if (m_port42Out == 0)
      SendSelfCenter(0);
    else
      SendSelfCenter(m_port42Out);
    break;

  case 0xFE:
    // Constant force: 0x80=stop, 0x81-0xC0=left, 0x40-0x7F=right
    if (m_port42Out > 0x81)
      SendConstantForce(m_port42Out <= 0xC0 ? (INT8)(2 * (0x81 - m_port42Out)) : (INT8)(-0x80));
    else if (m_port42Out < 0x7F)
      SendConstantForce(m_port42Out >= 0x40 ? (INT8)(2 * (0x7F - m_port42Out)) : (INT8)(0x7F));
    else
      SendConstantForce(0);
    break;

  case 0xFF:
    if (m_port42Out == 0xFF) SendStopAll();
    break;

  default:
    DebugLog("[HLE] Unknown encoder cmd: port46=%02X port42=%02X\n", m_port46Out, m_port42Out);
    break;
  }

  m_prev42Out = m_port42Out;
  m_prev46Out = m_port46Out;
}

// ===========================================================================
// Preset sequence helpers
// ===========================================================================

// Short constant-force jolt (one frame burst simulated as a strong pulse)
void CWheelBoard::PlaySequenceJolt(INT8 strength)
{
  SendConstantForce(strength);
  // The effect will be overridden on the next Write() naturally; no timer needed.
  DebugLog("[HLE] Jolt: %d\n", (int)strength);
}

// Vibration burst for rumble / road texture / curb effects
void CWheelBoard::PlaySequenceRumble(UINT8 strength)
{
  SendVibrate(strength);
  DebugLog("[HLE] Rumble: %u\n", (unsigned)strength);
}

// Power-slide: combine friction + mild vibration
void CWheelBoard::PlaySequencePowerSlide(UINT8 strength)
{
  SendFriction(strength);
  SendVibrate(strength >> 1);
  DebugLog("[HLE] PowerSlide: %u\n", (unsigned)strength);
}

// ===========================================================================
// SDL Haptic output — SendXxx implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// Gamepad rumble helper (Xbox / SDL_GameController backend)
// large = low-freq motor [0,1], small = high-freq motor [0,1]
// duration_ms = 0 means stop
// ---------------------------------------------------------------------------
// Boost factor: multiplies all rumble intensities to compensate for
// gamepads being weaker than direct-drive wheels.
// 1.0 = no boost, 2.0 = double strength (clamped to max 65535)
static constexpr float RUMBLE_BOOST = 2.0f;

static void GamepadRumble(float large, float small, Uint32 duration_ms = SDL_HAPTIC_INFINITY)
{
  if (!s_gamepad) return;
  // Apply boost and clamp
  large = std::clamp(large * RUMBLE_BOOST, 0.0f, 1.0f);
  small = std::clamp(small * RUMBLE_BOOST, 0.0f, 1.0f);
  Uint16 lo = (Uint16)(large * 65535.0f);
  Uint16 hi = (Uint16)(small * 65535.0f);
  // SDL_HAPTIC_INFINITY is not valid for GameControllerRumble;
  // use a repeating short duration instead
  Uint32 dur = (duration_ms == (Uint32)SDL_HAPTIC_INFINITY) ? 200 : duration_ms;
  SDL_GameControllerRumble(s_gamepad, lo, hi, dur);
}

void CWheelBoard::SendStopAll(void)
{
  if (s_haptic)   SDL_HapticStopAll(s_haptic);
  if (s_gamepad)  SDL_GameControllerRumble(s_gamepad, 0, 0, 0);
  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction   = 0;
  m_lastVibrate    = 0;
}

void CWheelBoard::SendConstantForce(INT8 val)
{
  if (val == m_lastConstForce) return;
  if (!s_haptic && !s_gamepad) { m_lastConstForce = val; return; }

  if (val == 0)
  {
    if (s_effectConstant >= 0) SDL_HapticStopEffect(s_haptic, s_effectConstant);
    m_lastConstForce = 0;
    return;
  }

  float norm = std::abs((float)val / (val >= 0 ? 127.0f : 128.0f));

  if (s_gamepad && !s_haptic)
  {
    // Xbox fallback: constant force → large motor intensity
    GamepadRumble(norm, norm * 0.5f);  // large=force, small=texture
  }
  else
  {
    float signed_norm = (float)val / (val >= 0 ? 127.0f : 128.0f);
    SDL_HapticEffect eff;
    memset(&eff, 0, sizeof(eff));
    eff.type                      = SDL_HAPTIC_CONSTANT;
    SetEffectDirection(eff.constant.direction, false);
    eff.constant.length           = SDL_HAPTIC_INFINITY;
    eff.constant.level            = NormToSint16(signed_norm);
    s_effectConstant = UploadEffect(s_effectConstant, eff);
    if (s_effectConstant >= 0) SDL_HapticRunEffect(s_haptic, s_effectConstant, 1);
  }
  m_lastConstForce = val;
}

void CWheelBoard::SendSelfCenter(UINT8 val)
{
  if (val == m_lastSelfCenter) return;
  if (!s_haptic && !s_gamepad) { m_lastSelfCenter = val; return; }

  if (val == 0)
  {
    if (s_gamepad && !s_haptic)
      SDL_GameControllerRumble(s_gamepad, 0, 0, 0);
    else if (s_effectSpring >= 0)
      SDL_HapticStopEffect(s_haptic, s_effectSpring);
    m_lastSelfCenter = 0;
    return;
  }

  if (s_gamepad && !s_haptic)
  {
    // Xbox fallback: self-center → gentle constant rumble on large motor
    float norm = (float)val / 255.0f;
    GamepadRumble(norm * 1.0f, norm * 0.7f);  // self-center spring feel
  }
  else
  {
    Sint16 level = NormToSint16((float)val / 255.0f);
    SDL_HapticEffect eff;
    memset(&eff, 0, sizeof(eff));
    eff.type                     = SDL_HAPTIC_SPRING;
    eff.condition.length         = SDL_HAPTIC_INFINITY;
    eff.condition.right_sat[0]   = 0x7FFF;
    eff.condition.left_sat[0]    = 0x7FFF;
    eff.condition.right_coeff[0] = level;
    eff.condition.left_coeff[0]  = level;
    eff.condition.deadband[0]    = 0;
    eff.condition.center[0]      = 0;
    s_effectSpring = UploadEffect(s_effectSpring, eff);
    if (s_effectSpring >= 0) SDL_HapticRunEffect(s_haptic, s_effectSpring, 1);
  }
  m_lastSelfCenter = val;
}

void CWheelBoard::SendFriction(UINT8 val)
{
  if (val == m_lastFriction) return;
  if (!s_haptic && !s_gamepad) { m_lastFriction = val; return; }

  if (val == 0)
  {
    if (s_gamepad && !s_haptic)
      SDL_GameControllerRumble(s_gamepad, 0, 0, 0);
    else if (s_effectFriction >= 0)
      SDL_HapticStopEffect(s_haptic, s_effectFriction);
    m_lastFriction = 0;
    return;
  }

  if (s_gamepad && !s_haptic)
  {
    // Xbox fallback: friction → small motor (high-freq texture feel)
    float norm = (float)val / 255.0f;
    GamepadRumble(norm * 0.8f, norm);  // friction: both motors
  }
  else
  {
    Sint16 level = NormToSint16((float)val / 255.0f);
    SDL_HapticEffect eff;
    memset(&eff, 0, sizeof(eff));
    eff.type                     = SDL_HAPTIC_FRICTION;
    eff.condition.length         = SDL_HAPTIC_INFINITY;
    eff.condition.right_sat[0]   = 0x7FFF;
    eff.condition.left_sat[0]    = 0x7FFF;
    eff.condition.right_coeff[0] = level;
    eff.condition.left_coeff[0]  = level;
    s_effectFriction = UploadEffect(s_effectFriction, eff);
    if (s_effectFriction >= 0) SDL_HapticRunEffect(s_haptic, s_effectFriction, 1);
  }
  m_lastFriction = val;
}

void CWheelBoard::SendVibrate(UINT8 val)
{
  if (val == m_lastVibrate) return;
  if (!s_haptic && !s_gamepad) { m_lastVibrate = val; return; }

  if (val == 0)
  {
    if (s_gamepad && !s_haptic)
      SDL_GameControllerRumble(s_gamepad, 0, 0, 0);
    else if (s_effectVibrate >= 0)
      SDL_HapticStopEffect(s_haptic, s_effectVibrate);
    m_lastVibrate = 0;
    return;
  }

  if (s_gamepad && !s_haptic)
  {
    // Xbox fallback: vibrate → both motors for rumble feel
    float norm = (float)val / 255.0f;
    GamepadRumble(norm, norm);  // vibrate: both motors full
  }
  else
  {
    SDL_HapticEffect eff;
    memset(&eff, 0, sizeof(eff));
    eff.type                      = SDL_HAPTIC_SINE;
    SetEffectDirection(eff.periodic.direction, false);
    eff.periodic.length           = SDL_HAPTIC_INFINITY;
    eff.periodic.period           = 100;
    eff.periodic.magnitude        = NormToSint16((float)val / 255.0f);
    s_effectVibrate = UploadEffect(s_effectVibrate, eff);
    if (s_effectVibrate >= 0) SDL_HapticRunEffect(s_haptic, s_effectVibrate, 1);
  }
  m_lastVibrate = val;
}

// ===========================================================================
// Z80 I/O port handlers (still called from CDriveBoard but now HLE-aware)
// ===========================================================================

UINT8 CWheelBoard::IORead8(UINT32 portNum)
{
  switch (portNum)
  {
  case 0x20: return m_dip1;
  case 0x21: return m_dip2;
  case 0x28: return m_dataSent;
  case 0x2c: return 0x00;  // no encoder error
  case 0x24: case 0x25: case 0x26: case 0x27:
    if (portNum == m_adcPortRead && m_adcPortBit-- > 0)
    {
      UINT8 adcVal = 0;
      switch (portNum)
      {
      case 0x24: adcVal = ReadADCChannel1(); break;
      case 0x25: adcVal = ReadADCChannel2(); break;
      case 0x26: adcVal = ReadADCChannel3(); break;
      case 0x27: adcVal = ReadADCChannel4(); break;
      }
      return (adcVal >> m_adcPortBit) & 0x01;
    }
    return 0xFF;
  default:
    return 0xFF;
  }
}

void CWheelBoard::IOWrite8(UINT32 portNum, UINT8 data)
{
  switch (portNum)
  {
  case 0x11:
    m_allowInterrupts = (data == 0x57);
    return;
  case 0x20: m_seg1Digit1 = data; return;
  case 0x21: m_seg1Digit2 = data; return;
  case 0x22: m_seg2Digit1 = data; return;
  case 0x23: m_seg2Digit2 = data; return;
  case 0x24: case 0x25: case 0x26: case 0x27:
    m_adcPortRead = portNum;
    m_adcPortBit  = 8;
    return;
  case 0x29:
    m_dataReceived = data;
    if (data == 0xCC) m_initialized = true;
    return;
  case 0x2a:                       // Sega Rally 2 encoder motor data
    m_port42Out = data;
    ProcessEncoderCmd();
    return;
  case 0x2e:                       // Sega Rally 2 encoder motor control
    m_port46Out = data;
    return;
  default:
    return;
  }
}

// ===========================================================================
// ADC helpers
// ===========================================================================

uint8_t CWheelBoard::ReadADCChannel1() const { return 0x00; }
uint8_t CWheelBoard::ReadADCChannel2() const
{
  return m_initialized ? (UINT8)m_inputs->steering->value : 0x80;
}
uint8_t CWheelBoard::ReadADCChannel3() const { return 0x80; }
uint8_t CWheelBoard::ReadADCChannel4() const { return 0x00; }

// ===========================================================================
// Save / Load state
// ===========================================================================

void CWheelBoard::SaveState(CBlockFile *SaveState)
{
  CDriveBoard::SaveState(SaveState);
  SaveState->NewBlock("WheelBoard", __FILE__);
  SaveState->Write(&m_simulated,     sizeof(m_simulated));
  SaveState->Write(&m_dip1,          sizeof(m_dip1));
  SaveState->Write(&m_dip2,          sizeof(m_dip2));
  SaveState->Write(&m_adcPortRead,   sizeof(m_adcPortRead));
  SaveState->Write(&m_adcPortBit,    sizeof(m_adcPortBit));
  SaveState->Write(&m_uncenterVal1,  sizeof(m_uncenterVal1));
  SaveState->Write(&m_uncenterVal2,  sizeof(m_uncenterVal2));
}

void CWheelBoard::LoadState(CBlockFile *SaveState)
{
  if (SaveState->FindBlock("WheelBoard") != Result::OKAY)
  {
    LoadLegacyState(SaveState);
    return;
  }
  bool wasSimulated;
  SaveState->Read(&wasSimulated, sizeof(wasSimulated));
  // HLE is always simulated; if state says otherwise just continue
  SaveState->Read(&m_dip1,         sizeof(m_dip1));
  SaveState->Read(&m_dip2,         sizeof(m_dip2));
  SaveState->Read(&m_adcPortRead,  sizeof(m_adcPortRead));
  SaveState->Read(&m_adcPortBit,   sizeof(m_adcPortBit));
  SaveState->Read(&m_uncenterVal1, sizeof(m_uncenterVal1));
  SaveState->Read(&m_uncenterVal2, sizeof(m_uncenterVal2));
  CDriveBoard::LoadState(SaveState);
}

void CWheelBoard::LoadLegacyState(CBlockFile *SaveState)
{
  if (SaveState->FindBlock("DriveBoard") != Result::OKAY)
  {
    ErrorLog("Unable to load wheel drive board state. Save state file is corrupt.");
    Disable();
    return;
  }

  CDriveBoard::LegacyDriveBoardState state;
  bool isEnabled = !IsDisabled(), wasEnabled = false, wasSimulated = false;
  SaveState->Read(&wasEnabled, sizeof(wasEnabled));
  if (wasEnabled)
  {
    SaveState->Read(&wasSimulated, sizeof(wasSimulated));
    if (!wasSimulated)
    {
      SaveState->Read(&state.dip1,           sizeof(state.dip1));
      SaveState->Read(&state.dip2,           sizeof(state.dip2));
      SaveState->Read(state.ram,             0x2000);
      SaveState->Read(&state.initialized,    sizeof(state.initialized));
      SaveState->Read(&state.allowInterrupts,sizeof(state.allowInterrupts));
      SaveState->Read(&state.dataSent,       sizeof(state.dataSent));
      SaveState->Read(&state.dataReceived,   sizeof(state.dataReceived));
      SaveState->Read(&state.adcPortRead,    sizeof(state.adcPortRead));
      SaveState->Read(&state.adcPortBit,     sizeof(state.adcPortBit));
      SaveState->Read(&state.uncenterVal1,   sizeof(state.uncenterVal1));
      SaveState->Read(&state.uncenterVal2,   sizeof(state.uncenterVal2));
    }
  }

  if (wasEnabled != isEnabled)
  {
    Disable();
    ErrorLog("Halting drive board emulation due to mismatch in active and restored states.");
  }
  else
    CDriveBoard::LoadLegacyState(state, SaveState);
}

void CWheelBoard::Disable(void)
{
  SendStopAll();
  CDriveBoard::Disable();
}

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

CWheelBoard::CWheelBoard(const Util::Config::Node &config)
  : CDriveBoard(config)
{
  m_dip1 = 0xCF;
  m_dip2 = 0xFF;

  m_seg1Digit1 = 0;  m_seg1Digit2 = 0;
  m_seg2Digit1 = 0;  m_seg2Digit2 = 0;

  m_adcPortRead = 0; m_adcPortBit  = 0;
  m_port42Out   = 0; m_port46Out   = 0;
  m_prev42Out   = 0; m_prev46Out   = 0;

  m_uncenterVal1 = 0; m_uncenterVal2 = 0;

  m_lastConstForce = 0; m_lastSelfCenter = 0;
  m_lastFriction   = 0; m_lastVibrate    = 0;

  m_hleGameType    = HLE_GAME_UNKNOWN;
  m_hleCabinetType = 0;
  m_steeringParam  = 0;

  // HLE always active
  m_simulated = true;

  DebugLog("Built Drive Board (wheel) [HLE / SDL Haptic]\n");
}

CWheelBoard::~CWheelBoard(void)
{
  CloseSDLHaptic();
}

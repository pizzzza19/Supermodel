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
 * Implementation of the CWheelBoard class: drive board (force feedback for wheel)
 * emulation.
 *
 * DriveBoardHLE enables command-level fallback for Scud, Daytona 2 and Rally 2.
 * Preset waveforms and transfer curves are approximations without drive firmware.
 */

#include "WheelBoard.h"

#include "Supermodel.h"
#include <cstdio>
#include <algorithm>

Game::DriveBoardType CWheelBoard::GetType(void) const
{
  return Game::DRIVE_BOARD_WHEEL;
}

void CWheelBoard::Get7SegDisplays(UINT8 &seg1Digit1, UINT8 &seg1Digit2, UINT8 &seg2Digit1, UINT8 &seg2Digit2) const
{
  seg1Digit1 = m_seg1Digit1;
  seg1Digit2 = m_seg1Digit2;
  seg2Digit1 = m_seg2Digit1;
  seg2Digit2 = m_seg2Digit2;
}

void CWheelBoard::SaveState(CBlockFile *SaveState)
{
  CDriveBoard::SaveState(SaveState);

  SaveState->NewBlock("WheelBoard", __FILE__);
  SaveState->Write(m_simulated);
  if (m_simulated)
  {
    SaveState->NewBlock("WheelBoardHLE.1", __FILE__);
    SaveState->Write(m_initialized);
    SaveState->Write(m_initState);
    SaveState->Write(m_statusFlags);
    SaveState->Write(m_boardMode);
    SaveState->Write(m_readMode);
    SaveState->Write(m_wheelCenter);
    SaveState->Write(m_echoVal);
    SaveState->Write(m_dip1);
    SaveState->Write(m_dip2);
    SaveState->Write(m_hle.state.torque);
    SaveState->Write(m_hle.state.jolt);
    SaveState->Write(m_hle.state.center);
    SaveState->Write(m_hle.state.vibration);
    SaveState->Write(m_hle.state.gain);
    SaveState->Write(m_hle.state.motor);
    SaveState->Write(m_hle.state.burst);
    SaveState->Write(m_hle.state.slide);
    SaveState->Write(m_hle.state.joltTicks);
    SaveState->Write(m_hle.state.burstTicks);
    SaveState->Write(m_hle.state.slideTicks);
  }
  else
  {
    // Save DIP switches and digit displays
    SaveState->Write(m_dip1);
    SaveState->Write(m_dip2);

    SaveState->Write(m_adcPortRead);
    SaveState->Write(m_adcPortBit);
    SaveState->Write(m_uncenterVal1);
    SaveState->Write(m_uncenterVal2);

    // In a block of its own, so that older save states still load
    SaveState->NewBlock("WheelBoardServo", __FILE__);
    SaveState->Write(m_servoGain);
  }
}

void CWheelBoard::LoadState(CBlockFile *SaveState)
{
  if (SaveState->FindBlock("WheelBoard") != Result::OKAY)
  {
    return;
  }

  bool wasSimulated;
  SaveState->Read(&wasSimulated, sizeof(wasSimulated));
  if (wasSimulated)
  {
    if (SaveState->FindBlock("WheelBoardHLE.1") != Result::OKAY)
    {
      Disable();
      ErrorLog("Missing HLE drive board state.");
      return;
    }
    SaveState->Read(m_initialized);
    SaveState->Read(m_initState);
    SaveState->Read(m_statusFlags);
    SaveState->Read(m_boardMode);
    SaveState->Read(m_readMode);
    SaveState->Read(m_wheelCenter);
    SaveState->Read(m_echoVal);
    SaveState->Read(m_dip1);
    SaveState->Read(m_dip2);
    SaveState->Read(m_hle.state.torque);
    SaveState->Read(m_hle.state.jolt);
    SaveState->Read(m_hle.state.center);
    SaveState->Read(m_hle.state.vibration);
    SaveState->Read(m_hle.state.gain);
    SaveState->Read(m_hle.state.motor);
    SaveState->Read(m_hle.state.burst);
    SaveState->Read(m_hle.state.slide);
    SaveState->Read(m_hle.state.joltTicks);
    SaveState->Read(m_hle.state.burstTicks);
    SaveState->Read(m_hle.state.slideTicks);
  }
  else
  {
    // Load DIP switches and digit displays
    SaveState->Read(m_dip1);
    SaveState->Read(m_dip2);

    SaveState->Read(m_adcPortRead);
    SaveState->Read(m_adcPortBit);
    SaveState->Read(m_uncenterVal1);
    SaveState->Read(m_uncenterVal2);

    if (SaveState->FindBlock("WheelBoardServo") == Result::OKAY)
      SaveState->Read(m_servoGain);
    else
      m_servoGain = 0x7F;
  }

  CDriveBoard::LoadState(SaveState);
  if (m_simulated)
  {
    SendStopAll();
    if (!IsDisabled()) UpdateHLEEffects();
  }
}

void CWheelBoard::Disable(void)
{
  SendStopAll();
  CDriveBoard::Disable();
}

void CWheelBoard::Reset(void)
{
  CDriveBoard::Reset();

  m_seg1Digit1 = 0xFF;
  m_seg1Digit2 = 0xFF;
  m_seg2Digit1 = 0xFF;
  m_seg2Digit2 = 0xFF;

  m_adcPortRead = 0;
  m_adcPortBit = 0;
  m_port42Out = 0;
  m_port46Out = 0;
  m_prev42Out = 0;
  m_prev46Out = 0;

  m_uncenterVal1 = 0;
  m_uncenterVal2 = 0;

  m_servoGain = 0x7F;
  m_constForce = 0;
  m_vibrate = 0;

  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction = 0;
  m_lastVibrate = 0;

  m_simulated = m_config["DriveBoardHLE"].ValueAsDefault<bool>(false) &&
                m_hle.profile != CWheelBoardHLE::Profile::Unsupported;
  m_hle.Stop();
  m_statusFlags = 0;
  m_echoVal = 0;
  m_hle.joltDuration = std::max(1u, std::min(65535u, m_config["DriveBoardHLEJoltDurationFrames"].ValueAsDefault<unsigned>(6)));
  m_hle.rumbleDuration = std::max(1u, std::min(65535u, m_config["DriveBoardHLERumbleDurationFrames"].ValueAsDefault<unsigned>(12)));
  m_hle.slideDuration = std::max(1u, std::min(65535u, m_config["DriveBoardHLESlideDurationFrames"].ValueAsDefault<unsigned>(144)));
  InfoLog("Drive board: HLE=%s profile=%u force-feedback=%s",
          m_simulated ? "enabled" : "disabled", (unsigned)m_hle.profile,
          m_config["ForceFeedback"].ValueAsDefault<bool>(false) ? "enabled" : "disabled");

  if (!m_config["ForceFeedback"].ValueAsDefault<bool>(false))
    Disable();

  // Stop any effects that may still be playing
  if (!IsDisabled())
    SendStopAll();
}

UINT8 CWheelBoard::Read(void)
{
  if (IsDisabled())
  {
    return 0xFF;
  }

  // TODO - simulate initialization sequence even when emulating to get rid of long pause at boot up (drive board can
  // carry on booting whilst game starts)
  if (m_simulated)
    return SimulateRead();
  else
    return CDriveBoard::Read();
}

void CWheelBoard::Write(UINT8 data)
{
  if (IsDisabled())
  {
    return;
  }

  //if (data >= 0x01 && data <= 0x0F ||
  //  data >= 0x20 && data <= 0x2F ||
  //  data >= 0x30 && data <= 0x3F ||
  //  data >= 0x40 && data <= 0x4F ||
  //  data >= 0x70 && data <= 0x7F)
  //  DebugLog("DriveBoard.Write(%02X)\n", data);
  if (m_simulated)
    SimulateWrite(data);
  else
  {
    CDriveBoard::Write(data);
    if (data == 0xCB)
      m_initialized = false;
  }
}

UINT8 CWheelBoard::SimulateRead(void)
{
  if (m_initialized)
  {
    switch (m_readMode)
    {
      case 0x0: return m_statusFlags;                    // Status flags
      case 0x1: return m_dip1;                           // DIP switch 1 value
      case 0x2: return m_dip2;                           // DIP switch 2 value
      case 0x3: return m_wheelCenter;                    // Wheel center
      case 0x4: return 0x80;                             // Cockpit banking center
      case 0x5: return (UINT8)m_inputs->steering->value; // Wheel position
      case 0x6: return 0x80;                             // Cockpit banking position
      case 0x7: return m_echoVal;                        // Init status/echo test
      default:  return 0xFF;
    }
  }
  else
  {
    switch (m_initState / 5)
    {
      case 0:  return 0xCF;  // Initiate start
      case 1:  return 0xCE;
      case 2:  return 0xCD;
      case 3:  return 0xCC;  // Centering wheel
      default:
        m_initialized = true;
        return 0x80;
    }
  }
}

void CWheelBoard::UpdateHLEEffects()
{
  if (!m_inputs || !m_inputs->steering || IsDisabled()) return;
  const auto effects = m_hle.Get();
  SendConstantForce((INT8)std::lround(effects.torque * (effects.torque >= 0 ? 127.0f : 128.0f)));
  SendSelfCenter((UINT8)std::lround(effects.center * 255.0f));
  SendVibrate((UINT8)std::lround(effects.vibration * 255.0f));
}

void CWheelBoard::SimulateWrite(UINT8 cmd)
{
  m_hle.Write(cmd);
  const UINT8 type = cmd >> 4, val = cmd & 15;
  if (type == 0xD) m_readMode = val & 7;
  else if (type == 0xF) m_echoVal = val;
  else if (type == 8 && (val & 7) == 5 && m_inputs)
    m_wheelCenter = (UINT8)m_inputs->steering->value;
  else if (type == 0xC)
  {
    m_boardMode = val;
    if (val >= 0xB) { m_initialized = false; m_initState = 0; }
    else if (cmd == 0xC7 && m_hle.profile == CWheelBoardHLE::Profile::Rally2)
      m_initialized = true;
  }
  UpdateHLEEffects();
}

void CWheelBoard::RunFrame(void)
{
  if (IsDisabled())
  {
    return;
  }

  if (m_simulated)
    SimulateFrame();
  else
  {
    CDriveBoard::RunFrame();

    // The frame's ~9 NMIs run in one go: the wheel only gets the last torque of the frame, so send that one only
    UpdateForceFeedback();
  }
}

void CWheelBoard::SimulateFrame(void)
{
  if (!m_initialized)
    m_initState++;
  m_hle.Tick();
  UpdateHLEEffects();
}

UINT8 CWheelBoard::IORead8(UINT32 portNum)
{
  UINT8 adcVal;

  switch (portNum)
  {
  case 0x20: // DIP 1 value
    return m_dip1;
  case 0x21: // DIP 2 value
    return m_dip2;
  case 0x24: // ADC channel 1 - Y analog axis for joystick
  case 0x25: // ADC channel 2 - steering wheel position (0x00 = full left, 0x80 = center, 0xFF = full right) and X analog axis for joystick
  case 0x26: // ADC channel 3 - cockpit bank position (deluxe cabinets) (0x00 = full left, 0x80 = center, 0xFF = full right)
  case 0x27: // ADC channel 4 - not connected
    if (portNum == m_adcPortRead && m_adcPortBit-- > 0)
    {
      switch (portNum)
      {
      case 0x24: // Y analog axis for joystick
        adcVal = ReadADCChannel1();
        break;
      case 0x25: // Steering wheel for twin racing cabinets - TODO - check actual range of steering, suspect it is not really 0x00-0xFF
        adcVal = ReadADCChannel2();
        break;
      case 0x26: // Cockpit bank position for deluxe racing cabinets
        adcVal = ReadADCChannel3();
        break;
      case 0x27: // Not connected
        adcVal = ReadADCChannel4();
        break;
      default:
        DebugLog("Unhandled Z80 input on ADC port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
        return 0xFF;
      }
      return (adcVal >> m_adcPortBit) & 0x01;
    }
    else
    {
      DebugLog("Unhandled Z80 input on ADC port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
      return 0xFF;
    }
  case 0x28: // PPC command
    return m_dataSent;
  case 0x2c: // Encoder error reporting (kept at 0x00 for no error)
    // Bit 1 0
    //     0 0 = encoder okay, no error
    //     0 1 = encoder error 1 - overcurrent error
    //     1 0 = encoder error 2 - overheat error
    //     1 1 = encoder error 3 - encoder error, reinitializes board
    return 0x00;
  default:
    DebugLog("Unhandled Z80 input on port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
    return 0xFF;
  }
}

void CWheelBoard::IOWrite8(UINT32 portNum, UINT8 data)
{
  CDriveBoard::IOWrite8(portNum, data);

  switch (portNum)
  {
  case 0x1c: // Unsure? - two bytes 0xFF, 0xFF sent at initialization only
  case 0x1d: // Unsure? - two bytes 0x0F, 0x17 sent at initialization only
  case 0x1e: // Unsure? - same as port 28
  case 0x1f: // Unsure? - same as port 31
    return;
  case 0x20: // Left digit of 7-segment display 1
    m_seg1Digit1 = data;
    return;
  case 0x21: // Right digit of 7-segment display 1
    m_seg1Digit2 = data;
    return;
  case 0x22: // Left digit of 7-segment display 2
    m_seg2Digit1 = data;
    return;
  case 0x23: // Right digit of 7-segment display 2
    m_seg2Digit2 = data;
    return;
  case 0x24: // ADC channel 1 control
  case 0x25: // ADC channel 2 control
  case 0x26: // ADC channel 3 control
  case 0x27: // ADC channel 4 control
    m_adcPortRead = portNum;
    m_adcPortBit = 8;
    return;
  case 0x29: // Reply for PPC
    m_dataReceived = data;
    if (data == 0xCC)
      m_initialized = true;
    return;
  case 0x2a: // Encoder motor data (x axis)
    m_port42Out = data;
    ProcessEncoderCmd();
    return;
  case 0x2d: // Clutch/lamp control (deluxe cabinets) ( or y axis)
    return;
  case 0x2e: // Encoder motor control
    m_port46Out = data;
    return;
  default:
    DebugLog("Unhandled Z80 output on port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
    return;
  }
}

void CWheelBoard::ProcessEncoderCmd(void)
{
  if (m_prev42Out != m_port42Out || m_prev46Out != m_port46Out)
  {
    //DebugLog("46 [%02X] / 42 [%02X]\n", m_port46Out, m_port42Out);
    switch (m_port46Out)
    {
      case 0xFB:
        // Torque gain, 0x80 (none) to 0xFF (full) [guess, from the programs]: set by the game's 2x commands within
        // the curve its 7x command picked (Scud Race, Daytona 2), half after a power slide, 0xFF in Sega Rally 2.
        // 0x05, written only at start-up, is not a gain
        if (m_port42Out & 0x80)
          m_servoGain = m_port42Out & 0x7F;
        break;

      case 0xFC:
        // Sub-register in bits 0-2
        if (m_port42Out&0x04)
        {
          // 4: centering
          // Bit 7 = on for disable, off for enable
          if (m_port42Out&0x80)
          {
            // Disable centering
            SendSelfCenter(0);
          }
          else
          {
            // Bits 3-6 = centering strength 0x0-0xF.  This is scaled to range 0x0F-0xFF
            UINT8 strength = ((m_port42Out&0x78)>>3) * 0x10 + 0xF;
            SendSelfCenter(strength);
          }
        }
        else
        {
          // 0-3: vibration (the game's 3x commands). Each sub-register holds a nibble, in bits 4-7: 0 and 1 the low
          // and high nibbles of a first byte (0x0E, 0x10, 0x12 for 31-35, 36-3A, 3B-3F), 2 and 3 those of a
          // second one (0x0A to 0x12 within each group); 30, and the mode changes (FC 0A, FC 0B), zero them. The
          // programs write 0 to 3 in turn: the vibration is set when 3 is written
          UINT8 subRegister = m_port42Out&0x03;
          UINT8 nibble = m_port42Out>>4;
          UINT8 &param = (subRegister & 2) ? m_uncenterVal2 : m_uncenterVal1;
          if (subRegister & 1)
            param = (param & 0x0F) | (nibble << 4);
          else
            param = (param & 0xF0) | nibble;
          if (subRegister == 3)
          {
            if (m_uncenterVal1 == 0 || m_uncenterVal2 == 0)
              m_vibrate = 0;
            else
            {
              // What the two bytes do on the servo board is unknown: 31-3F are mapped to 0x0F-0xEF, in 15 steps
              int strength = ((m_uncenterVal1>>1) - 7) * 0x50 + ((m_uncenterVal2>>1) - 5) * 0x10 + 0xF;
              m_vibrate = (UINT8)std::clamp(strength, 0, 0xFF);
            }
          }
        }
        break;

      case 0xFD:
        // TODO - unsure?  Sent as velocity changes, similar to self-centering
        break;

      case 0xFE:
        // Apply constant force to wheel
        // Value is: 0x80 = stop motor, 0x81-0xC0 = roll wheel left, 0x40-0x7F = roll wheel right, scale to range -0x80-0x7F
        // Note: seems to often output 0x7F or 0x81 for stop motor, so narrowing wheel ranges to 0x40-0x7E and 0x82-0xC0
        if (m_port42Out > 0x81)
        {
          if (m_port42Out <= 0xC0)
            m_constForce = 2 * (0x81 - m_port42Out);
          else
            m_constForce = -0x80;
        }
        else if (m_port42Out < 0x7F)
        {
          if (m_port42Out >= 0x40)
            m_constForce = 2 * (0x7F - m_port42Out);
          else
            m_constForce = 0x7F;
        }
        else
          m_constForce = 0;
        break;

      case 0xFF:
        // Motor idle, whatever the value: FF FF, and FF 80 after the attract mode's centring of the wheel
        SendStopAll();
        break;

      default:
        //DebugLog("Unknown = 46 [%02X] / 42 [%02X]\n", m_port46Out, m_port42Out);
        break;
    }

    m_prev42Out = m_port42Out;
    m_prev46Out = m_port46Out;
  }
}

void CWheelBoard::UpdateForceFeedback(void)
{
  // The servo board's gain scales the torque and the vibration, not its own spring
  SendConstantForce(m_constForce * m_servoGain / 0x7F);
  SendVibrate(m_vibrate * m_servoGain / 0x7F);
}


void CWheelBoard::SendStopAll(void)
{
  //DebugLog(">> Stop All Effects\n");

  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFStop;

  if (m_inputs) {
      if (m_inputs->steering) {
          m_inputs->steering->SendForceFeedbackCmd(ffCmd);
      }
  }
  
  m_constForce     = 0;
  m_vibrate        = 0;
  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction   = 0;
  m_lastVibrate    = 0;
}

void CWheelBoard::SendConstantForce(INT8 val)
{
  if (val == m_lastConstForce)
    return;
  /*
  if (val > 0)
  {
    DebugLog(">> Force Right %02X [%8s", val, "");
    for (unsigned i = 0; i < 8; i++)
      DebugLog(i == 0 || i <= (val + 1) / 16 ? ">" : " ");
    DebugLog("]\n");
  }
  else if (val < 0)
  {
    DebugLog(">> Force Left  %02X [", -val);
    for (unsigned i = 0; i < 8; i++)
      DebugLog(i == 7 || i >= (val + 128) / 16 ? "<" : " ");
    DebugLog("%8s]\n", "");
  }
  else
    DebugLog(">> Stop Force     [%16s]\n", "");
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFConstantForce;
  ffCmd.force = (float)val / (val >= 0 ? 127.0f : 128.0f);

  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastConstForce = val;
}

void CWheelBoard::SendSelfCenter(UINT8 val)
{
  if (val == m_lastSelfCenter)
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Self-Center\n");
  else
    DebugLog(">> Self-Center %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFSelfCenter;
  ffCmd.force = (float)val / 255.0f;

  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastSelfCenter = val;
}


void CWheelBoard::SendFriction(UINT8 val)
{
  if (val == m_lastFriction)
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Friction\n");
  else
    DebugLog(">> Friction %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFFriction;
  ffCmd.force = (float)val / 255.0f;
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastFriction = val;
}

void CWheelBoard::SendVibrate(UINT8 val)
{
  if (val == m_lastVibrate)
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Vibrate\n");
  else
    DebugLog(">> Vibrate %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFVibrate;
  ffCmd.force = (float)val / 255.0f;
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastVibrate = val;
}

uint8_t CWheelBoard::ReadADCChannel1() const
{
  return 0x00;
}

uint8_t CWheelBoard::ReadADCChannel2() const
{
  if (m_initialized)
    return (UINT8)m_inputs->steering->value;
  else
    return 0x80; // If not initialized, return 0x80 so that wheel centering test does not fail
}

uint8_t CWheelBoard::ReadADCChannel3() const
{
  return 0x80;
}

uint8_t CWheelBoard::ReadADCChannel4() const
{
  return 0x00;
}

CWheelBoard::CWheelBoard(const Util::Config::Node &config, const std::string& game)
  : CDriveBoard(config), m_hle(game)
{
  m_dip1 = 0xCF;
  m_dip2 = 0xFF;

  m_seg1Digit1 = 0;
  m_seg1Digit2 = 0;
  m_seg2Digit1 = 0;
  m_seg2Digit2 = 0;

  m_adcPortRead = 0;
  m_adcPortBit = 0;

  m_port42Out = 0;
  m_port46Out = 0;

  m_prev42Out = 0;
  m_prev46Out = 0;

  m_uncenterVal1 = 0;
  m_uncenterVal2 = 0;

  m_servoGain = 0x7F;
  m_constForce = 0;
  m_vibrate = 0;

  // Feedback state
  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction = 0;
  m_lastVibrate = 0;

  DebugLog("Built Drive Board (wheel)\n");
}

CWheelBoard::~CWheelBoard(void)
{

}

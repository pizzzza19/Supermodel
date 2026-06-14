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
 * WheelBoard.h
 *
 * Header for the CWheelBoard (force feedback emulation for wheel) class.
 *
 * HLE mode: Z80 ROM is not required. All force feedback commands from the PPC
 * are decoded directly and forwarded to SDL Haptic.
 */

#ifndef INCLUDED_WHEELBOARD_H
#define INCLUDED_WHEELBOARD_H

#include "DriveBoard.h"
#include "Util/NewConfig.h"
#include "Game.h"

/*
 * CWheelBoard
 */
class CWheelBoard : public CDriveBoard
{
public:
  /*
   * GetType(void):
   *
   * Returns:
   *    Drive board type.
   */
  Game::DriveBoardType GetType(void) const;

  /*
   * Get7SegDisplays(seg1Digit1, seg1Digit2, seg2Digit1, seg2Digit2):
   *
   * Reads the 7-segment displays.
   *
   * Parameters:
   *    seg1Digit1  Reference of variable to store digit 1 of the first 7-
   *                segment display to.
   *    seg1Digit2  First display, second digit.
   *    seg2Digit1  Second display, first digit.
   *    seg2Digit2  Second display, second digit.
   */
  void Get7SegDisplays(UINT8 &seg1Digit, UINT8 &seg1Digit2, UINT8 &seg2Digit1, UINT8 &seg2Digit2) const;

  /*
   * SaveState(SaveState):
   *
   * Saves the drive board state.
   *
   * Parameters:
   *    SaveState  Block file to save state information to.
   */
  void SaveState(CBlockFile *SaveState);

  /*
   * LoadState(SaveState):
   *
   * Restores the drive board state.
   *
   * Parameters:
   *    SaveState  Block file to load save state information from.
   */
  void LoadState(CBlockFile *SaveState);

  /*
   * Reset(void):
   *
   * Resets the drive board. Forces HLE mode (m_simulated = true).
   */
  void Reset(void);

  /*
   * Read():
   *
   * Reads data from the drive board.
   *
   * Returns:
   *    Data read.
   */
  UINT8 Read(void);

  /*
   * Write(data):
   *
   * Writes data to the drive board.
   *
   * Parameters:
   *    data  Data to send.
   */
  void Write(UINT8 data);

  /*
   * RunFrame(void):
   *
   * Emulates a single frame's worth of time on the drive board.
   */
  void RunFrame(void);

  /*
   * InitSDLHaptic():
   * CloseSDLHaptic():
   *
   * Open/close the SDL haptic device. InitSDLHaptic() must be called once
   * after SDL_Init(SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK).
   *
   * Returns (Init):
   *    True on success, false on failure.
   */
  bool InitSDLHaptic();
  void CloseSDLHaptic();

  /*
   * CWheelBoard(config):
   * ~CWheelBoard():
   *
   * Constructor and destructor. Memory is freed by destructor.
   *
   * Parameters:
   *    config  Run-time configuration. The reference should be held because
   *            this changes at run-time.
   */
  CWheelBoard(const Util::Config::Node &config);
  ~CWheelBoard(void);

  /*
   * IORead8(portNum):
   *
   * Methods for reading from Z80's IO space. Required by CBus.
   *
   * Parameters:
   *    portNum  Port address (0-255).
   *
   * Returns:
   *    A byte of data from the port.
   */
  UINT8 IORead8(UINT32 portNum);

  /*
   * IOWrite8(portNum, data):
   *
   * Methods for writing to Z80's IO space. Required by CBus.
   *
   * Parameters:
   *    portNum  Port address (0-255).
   *    data     Byte to write.
   */
  void IOWrite8(UINT32 portNum, UINT8 data);

protected:
  void Disable(void);

private:
  // -------------------------------------------------------------------------
  // Legacy state loader
  // -------------------------------------------------------------------------
  void LoadLegacyState(CBlockFile *SaveState);

  // -------------------------------------------------------------------------
  // 7-segment display state
  // -------------------------------------------------------------------------
  UINT8 m_seg1Digit1;   // Current value of left digit on 7-segment display 1
  UINT8 m_seg1Digit2;   // Current value of right digit on 7-segment display 1
  UINT8 m_seg2Digit1;   // Current value of left digit on 7-segment display 2
  UINT8 m_seg2Digit2;   // Current value of right digit on 7-segment display 2

  // -------------------------------------------------------------------------
  // ADC / encoder port state
  // -------------------------------------------------------------------------
  UINT16 m_adcPortRead;  // ADC port currently reading from
  UINT8  m_adcPortBit;   // Bit number currently reading on ADC port

  UINT8 m_port42Out;     // Last value sent to Z80 I/O port 42 (encoder motor data)
  UINT8 m_port46Out;     // Last value sent to Z80 I/O port 46 (encoder motor control)
  UINT8 m_prev42Out;     // Previous value sent to Z80 I/O port 42
  UINT8 m_prev46Out;     // Previous value sent to Z80 I/O port 46

  UINT8 m_uncenterVal1;  // First part of pending uncenter command
  UINT8 m_uncenterVal2;  // Second part of pending uncenter command

  // -------------------------------------------------------------------------
  // Force feedback output state
  // -------------------------------------------------------------------------
  INT8  m_lastConstForce;  // Last constant force command sent
  UINT8 m_lastSelfCenter;  // Last self center command sent
  UINT8 m_lastFriction;    // Last friction command sent
  UINT8 m_lastVibrate;     // Last vibrate command sent

  // -------------------------------------------------------------------------
  // HLE game-type detection
  // -------------------------------------------------------------------------
  enum HLEGameType
  {
    HLE_GAME_UNKNOWN    = 0,
    HLE_GAME_SCUD_RACE  = 1,  // command set A
    HLE_GAME_DAYTONA2   = 2,  // command set A (compatible with Scud Race)
    HLE_GAME_SEGA_RALLY2 = 3  // command set B (encoder via port 0x42/0x46)
  };

  HLEGameType m_hleGameType;    // Detected game type
  UINT8       m_hleCabinetType; // 0xB0 = standard, 0xB1 = deluxe/twin
  UINT8       m_steeringParam;  // Last 0x7x steering sensitivity byte

  // -------------------------------------------------------------------------
  // HLE core — replace Z80 emulation entirely
  // -------------------------------------------------------------------------
  UINT8 HLERead(void);
  void  HLEWrite(UINT8 data);
  void  HLEFrame(void);

  // Command set A: Scud Race / Daytona 2
  void HLEDecodeCommandSetA(UINT8 cmd);

  // Command set B: Sega Rally 2 (encoder protocol via ports 0x42/0x46)
  void ProcessEncoderCmd(void);

  // -------------------------------------------------------------------------
  // Preset sequence helpers
  // -------------------------------------------------------------------------
  void PlaySequenceJolt(INT8 strength);
  void PlaySequenceRumble(UINT8 strength);
  void PlaySequencePowerSlide(UINT8 strength);

  // -------------------------------------------------------------------------
  // Force feedback output (SDL Haptic)
  // -------------------------------------------------------------------------
  void SendStopAll(void);
  void SendConstantForce(INT8 val);
  void SendSelfCenter(UINT8 val);
  void SendFriction(UINT8 val);
  void SendVibrate(UINT8 val);

  // -------------------------------------------------------------------------
  // ADC channel helpers
  // -------------------------------------------------------------------------
  uint8_t ReadADCChannel1() const;
  uint8_t ReadADCChannel2() const;
  uint8_t ReadADCChannel3() const;
  uint8_t ReadADCChannel4() const;

  // -------------------------------------------------------------------------
  // Removed in HLE version (kept as comment for reference)
  // -------------------------------------------------------------------------
  // SimulateRead / SimulateWrite / SimulateFrame are replaced by
  // HLERead / HLEWrite / HLEFrame respectively.
};

#endif  // INCLUDED_WHEELBOARD_H

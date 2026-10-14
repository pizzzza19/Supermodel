#include "../Src/Model3/DriveBoard/WheelBoardHLE.h"
#include <cassert>
#include <iostream>

static bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
int main()
{
  CWheelBoardHLE rally("srally2");
  for (unsigned magnitude = 0; magnitude <= 63; ++magnitude)
  {
    rally.Write(uint8_t(magnitude));
    assert(Near(rally.Get().torque, -float(magnitude) / 63));
    rally.Write(uint8_t(0x40 | magnitude));
    assert(Near(rally.Get().torque, float(magnitude) / 63));
    for (unsigned frame = 0; frame < 200; ++frame) rally.Tick();
    assert(Near(rally.Get().torque, float(magnitude) / 63));
    assert(rally.Get().vibration == 0 && rally.Get().center == 0);
  }
  rally.Write(0x40); assert(rally.Get().torque == 0);
  rally.Write(0x7f); rally.Write(0xc7); assert(rally.Get().torque == 0);

  CWheelBoardHLE scud("scud"), daytona("dayto2pe");
  scud.Write(0x57); assert(Near(scud.Get().torque, 1));
  scud.Write(0x53); assert(Near(scud.Get().torque, 3.0f / 7));
  scud.Write(0x63); assert(Near(scud.Get().torque, -3.0f / 7));
  scud.Write(0x20); assert(scud.Get().torque == 0);
  scud.Write(0x2f); assert(Near(scud.Get().torque, -3.0f / 7));
  scud.Write(0x70); assert(Near(scud.Get().torque, -3.0f / 14));
  scud.Write(0x7e); assert(Near(scud.Get().torque, -3.0f / 14));
  scud.Write(0x75);
  daytona.Write(0x58); assert(Near(daytona.Get().torque, 8.0f / 15));
  daytona.Write(0x5e); assert(Near(daytona.Get().torque, 14.0f / 15));
  daytona.Write(0x6e); assert(Near(daytona.Get().torque, -14.0f / 15));
  daytona.Write(0x50); assert(daytona.Get().torque == 0);

  // One-shot expiry must not cancel persistent vibration or torque.
  scud.Stop(); scud.rumbleDuration = 2; scud.joltDuration = 2;
  scud.Write(0x3f); scud.Write(0x03); scud.Tick(); scud.Tick();
  assert(Near(scud.Get().vibration, 1));
  scud.Write(0x53); scud.Write(0x01); scud.Tick(); scud.Tick();
  assert(Near(scud.Get().torque, 3.0f / 7));
  scud.Write(0x17); scud.Write(0x03); scud.Write(0x00);
  assert(Near(scud.Get().vibration, 1) && Near(scud.Get().center, 1));
  scud.Write(0x30); assert(scud.Get().vibration == 0);

  // The two power-slide commands must initially push in opposite directions.
  scud.Stop(); daytona.Stop(); scud.slideDuration = daytona.slideDuration = 12;
  scud.Write(0x47); daytona.Write(0x4f);
  scud.Tick(); daytona.Tick();
  assert(scud.Get().torque > 0 && daytona.Get().torque < 0);
  for (int frame = 0; frame < 12; ++frame) { scud.Tick(); daytona.Tick(); }
  assert(scud.Get().torque == 0 && daytona.Get().torque == 0);
  scud.Write(0x57); scud.Write(0x3f); scud.Write(0xc6);
  assert(scud.Get().torque == 0 && scud.Get().vibration == 0);
  std::cout << "PASS: all Rally 2 magnitudes, hold/stop, profiles, gain, effect independence and slide direction\n";
}

#include "../Src/Model3/DriveBoard/WheelBoardHLE.h"
#include <cassert>
#include <iostream>

static bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
int main()
{
  for (const char* game : {"scud", "daytona2", "dayto2pe"})
  {
    CWheelBoardHLE h(game);
    h.Write(0x67);
    assert(h.GetGamepad().torque < 0);
    for (unsigned i = 0; i < h.gamepadTorqueDuration + 2; ++i) { h.Write(0x67); h.Tick(); }
    assert(h.Get().torque < 0 && h.GetGamepad().torque == 0);
    h.Write(0x57); assert(h.GetGamepad().torque > 0);
    h.Write(0x80); assert(h.GetGamepad().torque == 0);
    h.Write(0x57); h.Write(0x04); h.Write(0x47); h.Write(0x3f);
    for (int i = 0; i < 200; ++i)
    {
      auto e = h.GetGamepad();
      assert(std::abs(e.torque) + e.vibration <= 0.35001f);
      h.Tick();
    }
    assert(h.GetGamepad().torque == 0 && Near(h.GetGamepad().vibration, 0.35f));
    h.Write(0x30); assert(h.GetGamepad().vibration == 0);
    h.Stop(); h.Write(0x03);
    assert(Near(h.GetGamepad().vibration, 0.25f * 80 / 255));
  }
  CWheelBoardHLE rally("srally2");
  for (int cmd = 0; cmd < 128; ++cmd)
  {
    rally.Write(cmd);
    for (int i = 0; i < 30; ++i) { assert(Near(rally.Get().torque, rally.GetGamepad().torque)); rally.Tick(); }
  }
  std::cout << "PASS: held torque preserved, pad fade/stop/rearm, combined limit, direct vibration, Rally2 unchanged\n";
}

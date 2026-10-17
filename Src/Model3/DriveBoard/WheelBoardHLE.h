#ifndef INCLUDED_WHEELBOARDHLE_H
#define INCLUDED_WHEELBOARDHLE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Command-level fallback, not an emulation of the missing firmware's effect tables.
// Direct commands retain their magnitude, direction and explicit stop timing.
// Preset and power-slide waveforms remain configurable approximations.
class CWheelBoardHLE
{
public:
  enum class Profile { Unsupported, Scud, Daytona2, Rally2 };
  struct State
  {
    int8_t torque = 0, jolt = 0;
    uint8_t center = 0, vibration = 0, gain = 15, motor = 5;
    uint8_t burst = 0, slide = 0;
    uint32_t joltTicks = 0, burstTicks = 0, slideTicks = 0;
  } state;
  struct Effects { float torque, center, vibration; };

  explicit CWheelBoardHLE(const std::string& game)
  {
    profile = game.compare(0, 4, "scud") == 0 ? Profile::Scud :
              (game == "daytona2" || game == "dayto2pe") ? Profile::Daytona2 :
              game.compare(0, 7, "srally2") == 0 ? Profile::Rally2 : Profile::Unsupported;
  }
  Profile profile;
  uint32_t joltDuration = 6, rumbleDuration = 12, slideDuration = 144;
  // Presentation state only: never change the emulated held wheel torque.
  uint32_t gamepadTorqueDuration = 18, gamepadTorqueTicks = 0;

  void Stop() { state = State{}; gamepadTorqueTicks = 0; }
  void StopEffects()
  {
    const uint8_t motor = state.motor, gain = state.gain;
    Stop();
    state.motor = motor;
    state.gain = gain;
  }
  void StopSequence()
  {
    state.jolt = 0; state.burst = 0;
    state.joltTicks = state.burstTicks = 0;
  }
  void Write(uint8_t cmd)
  {
    if (profile == Profile::Rally2)
    {
      // Rally 2 sends 6 magnitude bits and a direction bit. Hold until updated.
      if (cmd < 0x80)
        state.torque = (cmd & 0x40) ? int8_t(cmd & 0x3f) : -int8_t(cmd & 0x3f);
      else if (cmd == 0x80 || cmd == 0xc1 || cmd == 0xc6 || cmd == 0xc7 || cmd == 0xcb)
        StopEffects();
      return;
    }
    const uint8_t type = cmd >> 4, val = cmd & 15;
    switch (type)
    {
      case 0:
        // Preserve the vibration_new fallback strengths; these are NOT ROM-derived.
        StopSequence();
        switch (val & 7)
        {
          case 1: state.jolt = 30; state.joltTicks = joltDuration; break;
          case 2: state.jolt = -30; state.joltTicks = joltDuration; break;
          case 3: state.burst = 80; state.burstTicks = rumbleDuration; break;
          case 4: state.jolt = 80; state.joltTicks = joltDuration; break;
          case 5: state.burst = 40; state.burstTicks = rumbleDuration; break;
          case 6: state.burst = 60; state.burstTicks = rumbleDuration; break;
        }
        break;
      case 1: state.center = val & 7; break;
      case 2: state.gain = val; break; // command gain, not friction
      case 3: state.vibration = val; break;
      case 4:
        state.slide = val;
        state.slideTicks = (val & 7) ? slideDuration : 0;
        break;
      case 5: case 6:
      {
        // Scud's observed direct levels use 3 bits; Daytona 2 also sends 58/5E.
        const int magnitude = profile == Profile::Scud ? val & 7 : val;
        const int8_t torque = int8_t(type == 5 ? magnitude : -magnitude);
        if (torque != state.torque)
          gamepadTorqueTicks = torque ? gamepadTorqueDuration : 0;
        state.torque = torque;
        break;
      }
      case 7:
        // 70..75: service-menu motor strength 50..100%. 7E is an init command.
        if (val <= 5) state.motor = val;
        break;
      case 8:
        if ((val & 7) == 0) StopEffects();
        break;
      case 0xc: StopEffects(); break;
      default: break;
    }
  }
  void Tick()
  {
    if (gamepadTorqueTicks) --gamepadTorqueTicks;
    if (state.joltTicks && --state.joltTicks == 0) state.jolt = 0;
    if (state.burstTicks && --state.burstTicks == 0) state.burst = 0;
    if (state.slideTicks) --state.slideTicks;
  }
  Effects Get() const
  {
    if (profile == Profile::Rally2)
      return {state.torque / 63.0f, 0, 0};
    const float motor = (5 + state.motor) / 10.0f;
    // Linear normalization is provisional: original board's transfer curve is unknown.
    const float gain = state.gain / 15.0f;
    const float range = profile == Profile::Scud ? 7.0f : 15.0f;
    float torque = state.torque / range * gain;
    torque += state.jolt / (state.jolt >= 0 ? 127.0f : 128.0f);
    if (state.slideTicks && slideDuration)
    {
      const float phase = float(slideDuration - state.slideTicks) / slideDuration;
      const float direction = (state.slide & 8) ? -1.0f : 1.0f;
      torque += direction * (state.slide & 7) / 7.0f * std::sin(phase * 6.283185307f);
    }
    return {std::max(-1.0f, std::min(1.0f, torque * motor)),
            state.center / 7.0f * motor,
            std::min(1.0f, (state.vibration / 15.0f * gain + state.burst / 255.0f) * motor)};
  }

  Effects GetGamepad(float maximum = 0.35f, float approximateScale = 0.25f) const
  {
    if (profile != Profile::Scud && profile != Profile::Daytona2) return Get();
    const float motor = (5 + state.motor) / 10.0f;
    const float gain = state.gain / 15.0f;
    const float range = profile == Profile::Scud ? 7.0f : 15.0f;
    const float envelope = gamepadTorqueDuration ?
        std::min(1.0f, float(gamepadTorqueTicks) / gamepadTorqueDuration) : 0.0f;
    float torque = state.torque / range * gain * envelope;
    torque += approximateScale * state.jolt / (state.jolt >= 0 ? 127.0f : 128.0f);
    if (state.slideTicks && slideDuration)
    {
      const float phase = float(slideDuration - state.slideTicks) / slideDuration;
      torque += approximateScale * ((state.slide & 8) ? -1.0f : 1.0f) *
                (state.slide & 7) / 7.0f * std::sin(phase * 6.283185307f);
    }
    torque *= motor;
    float vibration = (state.vibration / 15.0f * gain + approximateScale * state.burst / 255.0f) * motor;
    // XInput adds both channels. Budget the sum to avoid clipping at full speed.
    const float total = std::abs(torque) + vibration;
    const float limit = std::max(0.0f, std::min(1.0f, maximum));
    if (total > limit && total > 0) { torque *= limit / total; vibration *= limit / total; }
    return {torque, 0, vibration};
  }
};
#endif

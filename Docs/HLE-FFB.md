# HLE force feedback build

Supported games: Scud Race (`scud*`), Daytona USA 2 (`daytona2`, `dayto2pe`),
Sega Rally 2 (`srally2*`). Enable `DriveBoardHLE=1` and `ForceFeedback=1`.
The `-drive-board-hle` command-line option also enables HLE.
Normal Z80 emulation remains the default when HLE is disabled.

HLE can create an attached wheel board even if the optional drive ROM is missing.
Existing dummy drive ROMs may be left in the game ZIPs. Game ROM files are unchanged.

## Xbox controller

Use `InputSystem=xinput`, `XInputConstForceThreshold=0`,
`XInputConstForceMax=100`, and `XInputVibrateMax=100`.
The threshold must be zero to preserve weak command amplitudes.
`XInputStereoVibration=1` sends torque-derived vibration to both motors;
setting it to zero uses a motor selected by torque direction.
An Xbox controller converts wheel forces into vibration; it does not reproduce
the physical torque or centering of the original wheel.

### Scud / Daytona 2 gamepad adaptation

For `InputSystem=xinput`, these profiles preserve held wheel torque internally,
but translate changes in direct torque into a short, fading gamepad pulse.
Repeated commands with identical torque do not prolong that pulse. Direct
vibration commands retain their explicit stop timing. Preset and slide effects
remain enabled with reduced amplitude. The summed torque/vibration contribution
is limited before the XInput mix to avoid full-speed overlap.

```ini
XInputWheelRumbleAdaptation = 1
XInputWheelRumbleMax = 35
XInputWheelApproxScale = 25
XInputWheelTorqueDurationFrames = 18
```

The percentages and 18-frame fade (about 0.31 seconds) are gamepad comfort
settings, not verified original firmware behavior. Sega Rally 2 and non-XInput
backends keep their existing output. Set `XInputWheelRumbleAdaptation=0` to
restore the prior XInput mapping. Loading a save state clears the transient
pad pulse without changing the saved emulated torque or the save format.

## Implemented behavior and limits

- Rally 2: signed 6-bit torque commands, held until a new command or stop arrives.
  Seat/cockpit motion is excluded. No lamp or cabin output is used as rumble.
- Scud and Daytona 2: direct torque commands, live `2X` gain, `70..75`
  motor strength, vibration start/stop, separate persistent and temporary effects.
- Game-specific torque interpretation: Scud uses the lower 3 bits;
  Daytona 2 preserves all 4 bits, including the observed `58` and `5E` commands.
- Linear strength conversion is a fallback, not a verified firmware transfer curve.
- `0X` preset pulse amplitudes retain the vibration_new approximations.
  `4X` power-slide uses a bounded sine approximation with direction from bit 3.
  Exact original effect tables, envelopes and timings are unavailable without
  firmware analysis or hardware measurements.
- HLE state is saved in a versioned block. Old HLE save states lacking that block
  are incompatible; normal non-HLE saves retain their existing layout.
- HLE ticks after the main CPU has finished a frame, avoiding concurrent updates
  to command/effect state from a separate drive-board thread.

Approximate effect timings, in emulated frames (57.524 Hz):

```ini
DriveBoardHLEJoltDurationFrames = 6
DriveBoardHLERumbleDurationFrames = 12
DriveBoardHLESlideDurationFrames = 144
```

Timers never cancel persistent torque/vibration from later commands.

## Build and checks

Windows x64 build with the MinGW toolchain and `Makefiles/Makefile.Win32`.
The HLE unit test is `Scripts/Test_WheelBoardHLE.cpp`.
The XInput mixing fix was checked using the actual source branch and a mock device.
Build/tests do not establish accuracy of the unverified firmware effect tables,
or verify vibration on a physical controller.

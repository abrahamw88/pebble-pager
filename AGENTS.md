# Pebble Pager

Two matching palm-size pagers on a Seeed XIAO ESP32-C3. They swap "pulse messages" (patterns of long and short button presses, played back as light and buzz) over Wi-Fi through [ntfy](https://ntfy.sh), and every message also reaches a parent's iPhone.

**`README.md` is the spec.** Read the relevant section before changing pins, behavior, ring states, power or ntfy topics, and update it in the same change when the design moves. Do not copy its content here.

## Status

Spec and breadboard build are written. Firmware is **not written yet**: both sketches are skeletons.

## Layout

```
README.md                          spec: behavior, parts, pins, power, ntfy, risks
docs/build-steps.md                breadboard wiring and test steps
docs/concept.html                  visual concept sheet (open in a browser): look, colorways, every ring state
firmware/prototype/prototype.ino   breadboard test firmware; pick test 0-5 with TEST (see its header)
firmware/pebble-pager/pebble-pager.ino  the full pager firmware
```

An Arduino sketch must sit in a folder with the same name as its `.ino` file. Keep it that way.

## Pins and hardware rules

| Function | XIAO pin | GPIO |
|---|---|---|
| Battery sensor (220 kΩ / 220 kΩ divider) | D1 | 3 |
| Button 1, large: view and send (to GND, internal pull-up) | D2 | 4 |
| Button 2, small: battery, setup, off (to GND, internal pull-up) | D3 | 5 |
| Vibration motor (via PN2222A) | D4 | 6 |
| Ring power switch (via PN2222A) | D5 | 7 |
| Ring data (through 330 Ω) | D10 | 10 |

- Never use D0, D6, D7, D8, D9 (boot-mode and serial pins).
- The ring is 12 SK6812 **RGBW** pixels: use `NEO_GRBW + NEO_KHZ800`. Keep brightness around 30 of 255.
- Power the ring through D5 only while it is showing something.
- Keep tunable values (2 s send pause, 12 presses, 15 s recording, 30 s check, 5 min away scan) as named constants.

## Build, upload, monitor

`arduino-cli` is in `~/.local/bin`. The ESP32 core and the Adafruit NeoPixel library are installed.

```bash
arduino-cli board list                                    # find the serial port
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 firmware/prototype
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C3 -p <PORT> firmware/prototype
arduino-cli monitor -p <PORT> -c baudrate=115200
```

Use `firmware/pebble-pager` for the full build. USB CDC On Boot is enabled by default for this board, so Serial works over USB. If upload fails, hold the XIAO's BOOT button while plugging it in, then retry.

## Rules for AI tools

- Compile before claiming a change works. Flash only when asked, and confirm the serial port first with `arduino-cli board list`.
- Never commit Wi-Fi names, passwords, ntfy topics or tokens. Put them in `secrets.h` (git-ignored) and include it.
- Measure battery current on battery with USB unplugged; USB keeps the chip awake.
- Do not add dependencies or folders beyond the layout above without asking.
- The device supplements a phone. Do not add emergency, location or calling features.

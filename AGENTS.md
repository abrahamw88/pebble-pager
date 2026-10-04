# Pebble Pager

Two matching palm-size pager devices on a Seeed XIAO ESP32-C3. They swap "pulse messages" (a pattern of long and short button presses that plays back as light and buzz) over Wi-Fi through [ntfy](https://ntfy.sh). Every message is also posted to a parent's iPhone, which can post back.

This file is the entry point for any AI coding tool. Full detail is in `docs/`.

## Status

Hardware spec and breadboard build are written. Firmware is **not written yet**: both sketches are skeletons.

## Layout

```
AGENTS.md                      this file
CLAUDE.md                      Claude-only notes (imports this file)
docs/build-spec.md             source of truth: spec, behavior, parts, pins, power, ntfy, risks, build steps
docs/concept.html              visual concept sheet (open in a browser): look, colorways, every ring state
firmware/prototype/prototype.ino   breadboard test firmware (test sketches 1-4 from the spec)
firmware/pebble/pebble.ino         the full pager firmware
```

An Arduino sketch must sit in a folder with the same name as its `.ino` file. Keep it that way.

## Hardware (details in `docs/build-spec.md`)

| Function | XIAO pin | GPIO |
|---|---|---|
| Battery sensor (220 kΩ / 220 kΩ divider) | D1 | 3 |
| Button 1, large: view and send (to GND, internal pull-up) | D2 | 4 |
| Button 2, small: battery, setup, off (to GND, internal pull-up) | D3 | 5 |
| Vibration motor (via PN2222A) | D4 | 6 |
| Ring power switch (via PN2222A) | D5 | 7 |
| Ring data (through 330 Ω) | D10 | 10 |

- Never use D0, D6, D7, D8, D9 (boot-mode and serial pins).
- The ring is 12 SK6812 **RGBW** pixels: use `NEO_GRBW + NEO_KHZ800`. Keep brightness around 30 of 255 (3V3 pin budget).
- Buttons are on GPIO0-5, which can wake the chip from deep sleep.
- Power the ring through D5 only when it is showing something.

## Behavior summary

- Button 1 tap: play the waiting message, tap again to replay. Hold: record presses, send 2 s after the last one. A single tap never sends.
- Button 2 tap: battery level. Hold 5 s: Wi-Fi setup. Hold 10 s: turn off.
- Ring states and timings are in `docs/build-spec.md` (Behavior) and shown in `docs/concept.html`.
- Starting values to tune, keep them as named constants: 2 s send pause, 12 presses max, 15 s max recording, 30 s check interval, 5 min away scan.

## Build, upload, monitor

`arduino-cli` lives in `~/.local/bin` (on PATH in new terminals). The ESP32 core and the Adafruit NeoPixel library are installed.

```bash
arduino-cli board list                                    # find the serial port
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 firmware/prototype
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C3 -p <PORT> firmware/prototype
arduino-cli monitor -p <PORT> -c baudrate=115200
```

Use `firmware/pebble` instead of `firmware/prototype` for the full build. USB CDC On Boot is already enabled by default for this board, so Serial works over USB. If upload fails, hold the XIAO's BOOT button while plugging it in, then retry.

## Rules for AI tools

- Compile before claiming a change works. Flash only when asked, and confirm the serial port first with `arduino-cli board list`.
- Never commit Wi-Fi names, passwords, ntfy topics or tokens. Put them in `secrets.h` (git-ignored) and include it.
- Measure battery current on battery with USB unplugged; USB keeps the chip awake.
- Do not add dependencies or folders beyond the layout above without asking.
- The device supplements a phone. Do not add emergency, location or calling features.

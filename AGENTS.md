# Pebble Pager

Two matching palm-size pagers on a Seeed XIAO ESP32-C3. They swap "pulse messages" (patterns of long and short button presses, played back as light and buzz) over Wi-Fi through [ntfy](https://ntfy.sh), and every message also reaches a parent's iPhone.

**`README.md` is the spec.** Read the relevant section before changing pins, behavior, ring states, power or ntfy topics, and update it in the same change when the design moves. Do not copy its content here.

## Status

Spec and breadboard build are written. The firmware runs on the bare board with tested settings, setup page, Wi-Fi manager, messages between devices with receipts, button logic with recording, deep sleep with a 30 s check, and remote updates. Still to build: ring, motor and battery (button 2's tap is a stub until then). The serial console can simulate presses (`press 1 700 300 600`), which is how button logic is tested without hands on the board. Buttons, ring, motor and battery wait on parts.

## Layout

```
README.md                      spec: behavior, parts, pins, power, ntfy, risks
docs/build-steps.md            breadboard wiring and test steps
docs/concept.html              visual concept sheet (open in a browser): look, colorways, every ring state
docs/ring-preview.html         the ring animation in a browser, for tuning; holds a JavaScript copy of ring.h
firmware/pebble-pager/         the one sketch; each tab is a module
  pebble-pager.ino             main flow, build flags (listed in its header)
  config.h                     pins, values to tune, saved settings, serial output and console
  power.h                      deep sleep, wake reasons, the stall guard
  wifi_manager.h               Wi-Fi connection manager
  ntfy.h                       post to topics, read the device's inbox
  pulse.h                      the message format
  messages.h                   sending, receiving, receipts, outbox and unread list
  input.h                      buttons: taps, holds and recording
  ring.h                       light-ring animation engine: pure math, no hardware
  ota.h                        remote firmware updates
  portal.h                     setup page
  tests.h                      hardware checks, built instead of the firmware with -DTEST=n
.secrets                       git-ignored: TOPIC_BASE=... for test scripts on this computer only
```

There is one sketch: the tests call the same modules as the firmware, and compile out of it. The sketch folder and its `.ino` must share a name. Do not name a tab after a core library (`wifi.h`, `update.h`): macOS file names ignore case, so it would replace `WiFi.h` or `Update.h`. Tabs use `#ifndef` include guards, not `#pragma once`, which the build does not honor here.

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
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs firmware/pebble-pager
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs -p <PORT> firmware/pebble-pager
arduino-cli monitor -p <PORT> -c baudrate=115200
```

Add build flags with `--build-property compiler.cpp.extra_flags="-DFW_VERSION=48 -DTEST=1"`. The `PartitionScheme=min_spiffs` option gives each of the two update slots 1.9 MB (the default is 1.25 MB) and must be on every compile and upload; changing the layout needs a USB flash, not an update. USB CDC On Boot is enabled by default for this board, so Serial works over USB. If upload fails, hold the XIAO's BOOT button while plugging it in, then retry.

## Rules for AI tools

- Compile before claiming a change works, including the test builds and flags a change touches. Flash only when asked, and confirm the serial port first with `arduino-cli board list`.
- The firmware sleeps, so the USB port comes and goes, and macOS can stop showing it until the cable is replugged. To work over USB, post `awake` to the device's inbox first (10 minutes awake) or flash a `-DNO_SLEEP` build. Without USB, deliver a build as a release and post `update`; `status` reports wake counts and time awake to the phone. Never trigger "off" (button 2 held 10 s) remotely on the bare board: nothing can wake it.
- A reset over USB can leave the board in download mode ("waiting for download"): open the port with DTR and RTS released, pulse RTS only, and check the boot banner.
- Never commit or compile in Wi-Fi names, passwords, ntfy topics or tokens. Released firmware is public, so they live only in the device's settings (setup page or serial console). Before publishing a release, check the `.bin` with `strings` for the topic base and network names.
- Measure battery current on battery with USB unplugged; USB keeps the chip awake.
- Push only to the `test` branch until development is complete. Never push to `main` unless asked.
- Verify anything that is meant to use ntfy (sending, receiving, receipts, update reports) through the real ntfy path: read what the board posts (poll `https://ntfy.sh/<TOPIC_BASE>-phone` with `curl`, base from `.secrets`) and send commands to the device's inbox as the phone would. Internal behavior (buttons, ring, timing, sleep, Wi-Fi joins, development) can be checked over serial. An ntfy failure in an ntfy feature is a test failure; report it.
- After finishing a major piece of functionality, test it thoroughly before calling it done: normal paths, failure paths (no network, bad data, interrupted steps, power cycles) and repeated runs. Check real ntfy messages by reading the phone topic with `curl`, and report what was and wasn't covered.
- `docs/ring-preview.html` carries a line-for-line JavaScript copy of `ring.h` between `ENGINE-START` and `ENGINE-END`. Change both together, then check they agree: run the same press patterns through both (compile `ring.h` with `clang++` in a small harness, run the JavaScript with macOS's `jsc`) and compare the 12 RGBW values per frame; they should match to within 1 of 255.
- Do not add dependencies or folders beyond the layout above without asking.
- The device supplements a phone. Do not add emergency, location or calling features.

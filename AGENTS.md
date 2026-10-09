# Pebble Pager

Two matching palm-size pagers on a Seeed XIAO ESP32-C3. They swap "pulse messages" (patterns of long and short button presses, played back as light and buzz) over Wi-Fi through [ntfy](https://ntfy.sh), and every message also reaches a parent's iPhone.

**`README.md` is the spec.** Read the relevant section before changing pins, behavior, ring states, power or ntfy topics, and update it in the same change when the design moves. Do not copy its content here.

## Design Principle: Simplicity Through Intuitive Cues

Keep the product simple but effective. Communicate through the interface itself (motion, shape, color, sound, haptics) instead of adding labels, menus, or settings. Absorb complexity in the design, not in the user's workflow. Use creative cues only when they improve clarity, and always provide a plain fallback.

### Guidelines
- Minimize steps between user intent and outcome.
- Prefer built-in cues over added UI (tooltips, tutorials, extra screens).
- Use familiar, real-world patterns; don't invent new ones.
- Give every clever interaction a visible hint or a standard alternative.
- Remove anything users wouldn't miss.

### Examples
Pull-to-refresh, iPhone silent switch, typing indicator, shake on wrong password, progress that feels alive, two-button pager

### Validation Checklist
- Can a first-time user succeed without instructions?
- Could this label or explanation be carried by the element itself?
- Does the cue stay clear without being the only way to complete the task?

## Status

Spec and breadboard build are written. The firmware runs on the bare board with tested settings, setup page, Wi-Fi manager, messages between devices with receipts, button logic with recording, deep sleep with a 30 s check, and remote updates. The ring, motor and battery code is written and compiles, and its animation math matches the browser copy, but it has not run on the parts or been flashed: test it on the breadboard (`docs/build-steps.md`, steps 6, 8 and 9) before calling it done. The serial console can simulate presses (`press 1 700 300 600`) and show each ring display (`show received`), which is how they are tested without hands on the board.

## Layout

```
README.md                      spec: behavior, parts, pins, power, ntfy, risks
docs/build-steps.md            breadboard wiring and test steps
docs/concept.html              visual concept sheet (open in a browser): look, colorways, every ring state
docs/virtual-pager.html        a second pager in a browser, live on ntfy; a JavaScript port of the firmware's message and button logic
docs/ring-preview.html         the ring animation in a browser, for tuning
docs/ring-engine.js            JavaScript copy of ring.h, shared by both pages
docs/ring-view.js              draws the ring on a canvas, shared by both pages
firmware/pebble-pager/         the one sketch; each tab is a module
  pebble-pager.ino             main flow, build flags (listed in its header)
  config.h                     pins, values to tune, saved settings, serial output and console
  power.h                      deep sleep, wake reasons, the stall guard
  wifi_manager.h               Wi-Fi connection manager
  ntfy.h                       post to topics, read the device's inbox
  pulse.h                      the message format
  input.h                      buttons: taps, holds and recording
  ring.h                       light-ring animation engine: pure math, no hardware
  output.h                     the ring and motor as hardware, and what the device shows on them
  battery.h                    battery level, low-battery blink, hourly report
  messages.h                   sending, receiving, receipts, outbox and unread list
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
- The ring is 12 SK6812 **RGBW** pixels: use `NEO_GRBW + NEO_KHZ800`. The firmware has no brightness setting: `ring.h` keeps each frame inside a current budget (180 mA). The hardware checks use brightness 30 of 255.
- Power the ring through D5 only while it is showing something, and release the data pin while it is off (`ringPower()` in `output.h` does both).
- Change the animation only while holding a `RingTurn` (`output.h`): a task draws the ring while `loop()` runs.
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
- The firmware sleeps, so the USB port comes and goes, and macOS can stop showing it until the cable is replugged. To work over USB, post `awake` to the device's inbox first (10 minutes awake) or flash a `-DNO_SLEEP` build. Without USB, deliver a build as a release and post `update`; `status` reports wake counts and time awake to the phone. Never trigger "off" (button 2 held 10 s) remotely on the bare board: nothing can wake it except RESET or replugging USB.
- A reset over USB can leave the board in download mode ("waiting for download"): open the port with DTR and RTS released, pulse RTS only, and check the boot banner.
- Never commit or compile in Wi-Fi names, passwords, ntfy topics or tokens. Released firmware is public, so they live only in the device's settings (setup page or serial console). Before publishing a release, check the `.bin` with `strings` for the topic base and network names.
- Measure battery current on battery with USB unplugged; USB keeps the chip awake.
- Push only to the `test` branch until development is complete. Never push to `main` unless asked.
- Verify anything that is meant to use ntfy (sending, receiving, receipts, update reports) through the real ntfy path: read what the board posts (poll `https://ntfy.sh/<TOPIC_BASE>-phone` with `curl`, base from `.secrets`) and send commands to the device's inbox as the phone would. Internal behavior (buttons, ring, timing, sleep, Wi-Fi joins, development) can be checked over serial. An ntfy failure in an ntfy feature is a test failure; report it.
- After finishing a major piece of functionality, test it thoroughly before calling it done: normal paths, failure paths (no network, bad data, interrupted steps, power cycles) and repeated runs. Check real ntfy messages by reading the phone topic with `curl`, and report what was and wasn't covered.
- `docs/ring-engine.js` is a line-for-line JavaScript copy of `ring.h`. Change both together, then check they agree: run the same press patterns through both (compile `ring.h` with `clang++` in a small harness, run the JavaScript with macOS's `jsc`) and compare the 12 RGBW values per frame; they should match to within 1 of 255.
- `docs/virtual-pager.html` ports `pulse.h`, `messages.h`, `input.h` and the display rules in `output.h` to JavaScript. When their behavior changes, change the page too. It holds no topic base: that is typed into its Setup and kept in the browser.
- Do not add dependencies or folders beyond the layout above without asking.
- The device supplements a phone. Do not add emergency, location or calling features.

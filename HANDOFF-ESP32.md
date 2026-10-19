# Handoff: flash the Smart Bottle ESP32

For a local Claude Code session on Daniel's PC (Windows), started from the root of this repo
(`danielhaj-code/mc24`). Read this whole file before running anything.

## Goal

Compile `smart-bottle/esp32/SmartBottleBridge/` and upload it to the ESP32 plugged in over USB,
then confirm it works. Nothing else: no refactors, no changes to the web app or Supabase.

## Context

- University project "Smart Bottle" (USV). Final architecture: STM32F429I-DISC1 reads the water
  sensor; the ESP32 is **only** a Bluetooth adapter (UART <-> BLE, Nordic UART Service).
  The professor requires the STM32 to do the sensor reading.
- The STM32 isn't wired yet, so the sketch defaults to `#define MOCK_STM32 1`: the ESP32 emits the
  same lines the STM32 would (`L,<raw>,<pct>,<stable>` once a second, `A,C` / `E,C`) and answers the
  app's `C,<empty>,<full>` and `R` commands. Protocol: `smart-bottle/PROTOCOL.md`.
- With `MOCK_STM32 0` it becomes a transparent bridge on `Serial2` (GPIO16 RX, GPIO17 TX, 115200).
- `MockStm32.h` sits next to the `.ino` and is part of the sketch. The folder name must keep matching
  the `.ino` name.
- The sketch is written to build on both ESP32 Arduino core 2.x and 3.x. It was syntax-checked on a
  PC against stub headers only; it has **never** been compiled with the real toolchain or flashed, so
  real compile errors are possible. Fix them minimally and keep both MOCK modes building.
- The web app is live on Vercel (auto-deploys from `main`) and connects over Web Bluetooth in
  Chrome/Edge to a device advertising as `SmartBottle`.

## Steps

1. Check what's installed: `arduino-cli version`. If missing: `winget install ArduinoSA.CLI`
   (ask Daniel before installing anything).
2. ESP32 core, if `arduino-cli core list` doesn't show `esp32:esp32`:
   ```
   arduino-cli config init
   arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core update-index
   arduino-cli core install esp32:esp32
   ```
3. Find the port: `arduino-cli board list`. If no port appears, the USB-serial driver is missing
   (CP210x or CH340, depending on the board): tell Daniel which one; don't install drivers yourself.
4. Compile from the repo root (this checks the mock build first, then the bridge build):
   ```
   arduino-cli compile --fqbn esp32:esp32:esp32 smart-bottle/esp32/SmartBottleBridge
   arduino-cli compile --fqbn esp32:esp32:esp32 --build-property "compiler.cpp.extra_flags=-DMOCK_STM32=0" smart-bottle/esp32/SmartBottleBridge
   ```
   If the BLE library complains about partition size, add `--board-options PartitionScheme=huge_app`.
5. Compile and upload the **mock** build (default) in one step. Don't use plain `upload` here: it
   would flash the cached output of the last compile, which is the `MOCK_STM32=0` build from step 4.
   ```
   arduino-cli compile --upload --fqbn esp32:esp32:esp32 -p COMx smart-bottle/esp32/SmartBottleBridge
   ```
   If it says "Failed to connect", ask Daniel to hold the BOOT button while the upload starts.
6. Verify: `arduino-cli monitor -p COMx -c baudrate=115200`. Expect
   `SmartBottle MOCK pornit (fără STM32). Comenzi: d r l a ?`. The `L,...` lines only print to USB while
   a phone is connected, so ask Daniel to open the website, press **Conectează** and pick
   `SmartBottle`. Then type `d` in the monitor (or press BOOT): the app should log a sip within
   a few seconds. `r` refills, `l` lifts the bottle off, `a` toggles automatic sips.

## Rules

- If a code fix is needed: small, commented in Romanian like the rest of the file, commit to `main`
  with a clear message, and update `smart-bottle/README.md` only if the build steps changed.
- Never commit secrets. The Supabase publishable key in `smart-bottle/web/config.js` is meant to
  be public; nothing else is.
- Don't touch `smart-bottle/supabase/` or `smart-bottle/web/`.
- Report back: core version used, the COM port, whether both builds compiled, whether the upload
  and the BLE test worked, and any change you committed.

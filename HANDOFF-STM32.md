# Handoff: build, flash and test the Smart Bottle STM32 firmware

For a local Claude Code session on Daniel's Windows PC, started from the root of this repo
(`danielhaj-code/mc24`) with the **STM32F429I-DISC1** board plugged in by its mini-USB port
(the one labelled USB ST-LINK / CN1). Read this whole file before running anything.

## Goal

1. Get a command-line toolchain working (compiler, make, flasher, serial monitor).
2. Build `smart-bottle/stm32` and flash it.
3. Prove it works on the board: boot log on the USB serial port, LCD drawing, LEDs, and the
   protocol test in step 7.

No feature work. Small fixes only if something doesn't build or doesn't run (see Rules).

## Context

- University project "Smart Bottle" (USV). The STM32 reads the water sensor and drives the LCD;
  an ESP32 is only a Bluetooth bridge (separate handoff: `HANDOFF-ESP32.md`). Protocol between them:
  `smart-bottle/PROTOCOL.md` (text lines: `L,<raw>,<pct>,<stable>`, `C,<a>,<b>` → `A,C`/`E,C`, `R`, `S,1`/`S,0`).
- `smart-bottle/stm32` is a plain Makefile project (no CubeIDE needed):
  - `Core/` — our code. `main.c` sets 180 MHz from the 8 MHz HSE, ADC1 on PA5, UART5 (PC12 TX,
    PD2 RX) to the ESP32, USART1 (PA9/PA10) = the ST-LINK virtual COM port used as a debug log.
  - `Drivers/` and `Utilities/` (ST HAL, CMSIS, board BSP, LCD fonts) are **not** in git:
    `python tools/fetch_deps.py` downloads them, pinned to exact commits.
  - `make LINK=vcp` builds a variant where the protocol runs on the USB serial port instead of
    UART5, so it can be tested from the PC without the ESP32 (`tools/vcp_test.py`).
- It compiles cleanly with arm-none-eabi-gcc 13.2 (all three variants, no warnings), but it has
  **never run on real hardware**. Expect possible runtime issues, not build issues.
- Every hardware fact below that I couldn't verify is marked *(check)*.

## Steps

1. **See what's already installed** (don't install anything yet):
   `git`, `python --version`, `arm-none-eabi-gcc --version`, `make --version`, `openocd --version`,
   `pyocd --version`, `STM32_Programmer_CLI --version`. Also look for an existing
   STM32CubeIDE / STM32CubeProgrammer install under `C:\ST\` — CubeIDE bundles a GCC toolchain
   (`...\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*\tools\bin`) and
   `make` (`...externaltools.make*\tools\bin`) that can simply be put on PATH for this session.

2. **Install what's missing — ask Daniel first.** Preferred, no ST account needed (xPack builds):
   ```
   npm install --global xpm
   xpm install --global @xpack-dev-tools/arm-none-eabi-gcc@latest @xpack-dev-tools/openocd@latest @xpack-dev-tools/windows-build-tools@latest
   pip install pyserial
   ```
   xpm puts them under `%APPDATA%\xPacks\@xpack-dev-tools\<name>\<version>\.content\bin`; add those
   `bin` folders to PATH for the session (and tell Daniel how to add them permanently).
   Alternative flasher: `pip install pyocd` (then `make flash FLASHER=pyocd`).

3. **USB driver / board detection.** With the board plugged in, Device Manager should show
   *STM32 STLink* and a *STMicroelectronics STLink Virtual COM Port (COMx)*. Windows 10/11 usually
   installs the driver by itself. If it doesn't, the driver is in ST's STSW-LINK009 package (needs an
   ST account, or it comes with STM32CubeProgrammer) — tell Daniel, don't download it yourself.
   **If there is no COM port at all**, the board may be the older STM32F429I-DISCO (ST-LINK/V2, no
   virtual COM port) rather than the DISC1 — flashing still works, but steps 6–7 need a USB-UART
   adapter instead; stop and tell Daniel.

4. **Fetch drivers and build** (from `smart-bottle/stm32`):
   ```
   python tools/fetch_deps.py
   make -j8
   ```
   Expect `build/smart-bottle.elf` at about 36 KB of flash.

5. **Flash:**
   ```
   make flash
   ```
   (OpenOCD with `board/stm32f429disc1.cfg`; `make flash FLASHER=pyocd` or `FLASHER=cubeprog` also work.)
   If OpenOCD can't open the ST-LINK: close STM32CubeIDE/CubeProgrammer if open, replug the board.
   If it reports an old ST-LINK firmware, STM32CubeProgrammer can upgrade it — ask Daniel.

6. **Check the board.** Open the log: `make monitor PORT=COMx` (Ctrl+] exits). Press the black
   RESET button. Expect:
   ```
   [boot] Smart Bottle <git hash>, 180 MHz, senzor analogic PA5, legatura pe UART5 (ESP32)
   [boot] app pornita
   [tx] L,<raw>,<pct>,<stable>      (one per second)
   ```
   Ask Daniel to confirm on the board: the LCD shows "Smart Bottle", a bottle outline with a
   fill level, a percentage and "BLE: neconectat"; no fast-blinking red LED (that means
   `Error_Handler`). With nothing on PA5 the raw value floats; touching PA5 or tying it to
   GND/3V moves it — that proves the ADC path.
   *(check)* If there's no log but the LCD works, the virtual COM port may not be wired to
   PA9/PA10 on this board revision — look at UM1670 (virtual COM port section / solder bridges)
   and report back; don't rewire anything.

7. **Protocol test from the PC** (no ESP32 needed):
   ```
   make clean
   make LINK=vcp flash
   python tools/vcp_test.py COMx
   ```
   It checks periodic readings, `R`, calibration accepted/rejected and the computed percentage,
   then asks Daniel to confirm the green LED LD3 and "BLE: conectat" on the LCD after `S,1`.
   Afterwards put the normal build back: `make clean && make flash`.

8. **Optional, if the ESP32 is also connected** (PC12→GPIO16, PD2←GPIO17, common GND) and running
   the bridge build (`MOCK_STM32=0`, see `HANDOFF-ESP32.md`): the website should show live
   readings from the real board, and the log should show `[rx] S,1` when the phone connects.

## Rules

- Ask Daniel before installing anything, before changing PATH permanently, and before
  updating ST-LINK firmware.
- Code fixes: small, commented in Romanian like the rest, only in `smart-bottle/stm32/`. Rebuild
  all three variants (`make`, `make LINK=vcp`, `make SENSOR=hx711`) without warnings, then commit
  to `main` with a clear message. Never commit `Drivers/`, `Utilities/` or `build/` (gitignored).
- Don't touch `smart-bottle/web/`, `smart-bottle/supabase/` or the ESP32 sketch.
- Never commit secrets.
- Report back: tool versions and where they came from, the COM port, whether flash/boot log/LCD/
  LEDs/`vcp_test.py` worked, and every change you committed.

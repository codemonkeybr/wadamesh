# T-LoRa Pager port — implementation milestones (agent playbook)

This file turns `TLORA_PAGER_PORT.md` (the *why* + research) into an ordered set of
**self-contained milestones for a Claude Sonnet agent to execute one at a time**.
The operator says "execute Milestone N"; the agent implements exactly that phase,
verifies its gate, updates the trackers, and stops.

---

## Operator instructions

Prompt template per phase:

> Read `CLAUDE.md`, `TLORA_PAGER_PORT.md`, and `TLORA_PAGER_PORT_MILESTONES.md`,
> then execute **Milestone N**. Do not start work from any other milestone.

- Milestones are strictly ordered; each assumes the previous ones are merged.
- M1–M6 are pure software (no hardware needed). M7–M8 need the device on a desk
  and a human in the loop. M9–M10 are follow-ups.
- One milestone = one focused commit/PR (repo rule: one topic per PR).

## Global rules for the executing agent (apply to EVERY milestone)

1. **Read first**: `CLAUDE.md` (repo guide), `TLORA_PAGER_PORT.md` (hardware facts,
   pin map, decisions — treat it as the source of truth for pins/defines),
   `CONTRIBUTING.md`.
2. **Never regress the shipping boards.** After your changes, BOTH existing envs
   must build:
   ```bash
   pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch
   pio run -e LilyGo_TDeck_companion_radio_touch
   ```
   From M4 onward, the pager env must build too:
   ```bash
   pio run -e tlora_pager_lr1121_companion_radio_touch
   ```
3. **No refactors — and match this repo's design patterns exactly.** Copy the
   existing shapes (T-Deck variant, Tanmatsu UI branch, `TDeckKeyboard`-style
   drivers). Do not introduce abstraction layers, do not reorganize existing
   files, do not "clean up" unrelated code. Concretely, the conventions to mirror:
   - **Naming**: board classes are `<Board>Board` (`TDeckBoard` → `TLoraPagerBoard`);
     variant entry points are `target.{h,cpp}`; input-driver free functions carry a
     board prefix (`tdeckKeyboardBegin/Poll/ReadKey` → `pagerKeyboardBegin/Poll/ReadKey`);
     UI statics are `s_*`, LVGL globals hang off `g_lv`.
   - **Comments explain WHY, with the hardware constraint or war story** — see the
     HW-CDC note in `platformio.ini`, the PSRAM rationale atop `main.cpp`'s
     `s_si_mem`, the crash-safe note on `DataStore::savePrefs`. New code documents
     non-obvious constraints the same way; it never narrates what the next line does.
   - **Header doc-comments state the driver contract** (who polls, which core,
     ISR-safety) — model on `TDeckKeyboard.h` / `TDeckTrackball.h`.
   - **Memory discipline**: internal DRAM is the scarce pool (Wi-Fi+BLE coexistence
     needs ~50 KB free). Big new buffers/objects (keymap tables, frame buffers) go
     to PSRAM the way `main.cpp` places `the_mesh` and the transport object.
   - **Includes**: wadamesh's own copies of files that also exist in the core lib
     use **quoted** includes (the `MC_VENDORED_TOUCH_APP` pattern — see the comment
     at `main.cpp:15-17`); core-lib headers use angle brackets.
   - **`platformio.ini` env blocks keep the same structure**: grouped `-D` sections
     with `; --- section ---` banner comments, same ordering as the T-Deck env, and
     inline why-comments on any value that differs from the sibling envs.
   - **Feature gating**: board/capability `#if`s, never runtime flags, for anything
     board-specific — matching how `HAS_TDECK_GT911`/`HAS_TANMATSU`/`HAS_EXPANSION_KIT`
     are used today.
4. **Licensing**: new files you author get `// SPDX-License-Identifier: GPL-3.0-or-later`.
   Files cribbed from upstream MeshCore keep their original **MIT** header verbatim —
   never relicense them.
5. **No Claude/AI attribution** in commit messages or PR text. Sign-off per DCO is
   the human's job; just don't add `Co-Authored-By: Claude` or similar.
6. **Pager-specific behavior must be gated** (new `#if` on the pager's board/cap
   macros) so the other boards' binaries are behaviorally unchanged.
7. **Reference material** (read, don't copy blindly):
   - Local working pager port: `~/dev/trail-mate/` —
     `variants/lilygo_tlora_pager/pins_arduino.h` (pin map),
     `boards/tlora_pager/src/tlora_pager_board.cpp` (`initLoRa()`, power rails, SD),
     `boards/tlora_pager/include/boards/tlora_pager/tlora_pager_board.h`.
   - Upstream MeshCore (MIT crib source for radio wrappers + a second opinion on
     everything): `https://github.com/meshcore-dev/MeshCore`, dirs
     `variants/lilygo_tlora_pager/` and `src/helpers/radiolib/`. Fetch raw files
     (shallow clone or raw.githubusercontent) — do NOT add it as a dependency.
   - In-repo templates: `variants/lilygo_tdeck/*`, `variants/heltec_v4/*`,
     `src/helpers/input/TDeck*`, the Tanmatsu branches inside `src/ui-touch/UITask.cpp`.
8. **When done**: tick the matching worklist box + refresh the `Status:` line in
   `TLORA_PAGER_PORT.md`, note any deviation/discovery there (it's the living
   tracker), and report build results honestly (paste failing output if red).
9. Line numbers cited below (e.g. `UITask.cpp ~35706`) were measured at beta_35 —
   treat them as anchors, re-locate by searching the named symbols.

---

## Milestone 1 — Variant skeleton (board class, pins, partitions)

**Objective:** create `variants/lilygo_tlora_pager/` with everything except radio
and display, modeled on `variants/lilygo_tdeck/`.

**Deliverables**
1. `variants/lilygo_tlora_pager/pins_arduino.h` — write our own (do NOT copy
   trail-mate's: it brands `USB_PRODUCT "TRAIL MATE"`). Base it on the T-Deck's
   `pins_arduino.h` shape; USB VID/PID `0x303A`/`0x82D4`, product string
   `"wadamesh T-LoRa Pager"`. Pin values: see the hardware table in
   `TLORA_PAGER_PORT.md` (I²C SDA 3 / SCL 2; shared SPI SCK 35 / MOSI 34 / MISO 33;
   UART0 TX 43 / RX 44).
2. `variants/lilygo_tlora_pager/partitions_tlora_pager_touch.csv` — copy
   `variants/lilygo_tdeck/partitions_tdeck_touch.csv` verbatim layout (2× 3.875 MB
   OTA slots, tiles LittleFS 4.75 MB, spiffs 3.375 MB, coredump) — same 16 MB flash,
   no reason to diverge. Update the header comment for the pager.
3. `variants/lilygo_tlora_pager/TLoraPagerBoard.h/.cpp` — `class TLoraPagerBoard :
   public ESP32Board`, modeled on `TDeckBoard` but:
   - `begin()`: `ESP32Board::begin()`, then bring up the **XL9555 IO expander**
     (SensorLib `ExtensionIOXL9555`, I²C) and enable rails: LORA_EN(ch3),
     GPS_EN(ch4), KB_EN(ch8)+KB_RST(ch2), SD rails (ch10–12) — mirror trail-mate's
     power bring-up order. Handle deep-sleep wake reason like `TDeckBoard.cpp:6-34`.
   - `getBattMilliVolts()`: query the **BQ27220 fuel gauge** (SensorLib
     `GaugeBQ27220`) over I²C — NOT `analogReadMilliVolts` (there is no ADC divider).
     Fall back to a sane constant (e.g. 3700) if the gauge probe failed, so the UI
     never divides by zero.
   - `getManufacturerName()`: `"LilyGo T-LoRa Pager"`.
   - `enterDeepSleep()`/sleep: ext1 wake on `P_LORA_DIO_1` (GPIO 14) + BOOT (GPIO 0),
     mirroring `TDeckBoard.h:26-47`.
   - Keep the charger (BQ25896/XPowersLib) OUT of scope for now — note a TODO;
     the gauge alone covers the UI battery display.

**Not in this milestone:** `target.h/.cpp` (M2), display (M3), env (M4). Nothing
references the new files yet, so the two existing envs build unchanged.

**Gate:** both existing envs build; `git status` shows only the new variant files.

---

## Milestone 2 — LR1121 radio glue

**Objective:** vendored LR1121 wrapper + the variant's `target.{h,cpp}`.

**Deliverables**
1. `variants/lilygo_tlora_pager/CustomLR1121.h` + `CustomLR1121Wrapper.h` —
   **re-check upstream `meshcore-dev/MeshCore` `src/helpers/radiolib/` first**
   (as of 2026-07-06 it has no `CustomLR1121*` and no `variants/lilygo_tlora_pager/`
   — issue #861 "Support for LR1121" is still open, so there's nothing to crib
   yet; it may have landed by the time this milestone runs). If still absent,
   author the pair by adapting the core fork's `CustomLR1110{,Wrapper}.h`
   (`~/dev/MeshCore/src/helpers/radiolib/` or the pulled `core-v1.16.5` lib_dep) —
   LR1110 and LR1121 share RadioLib's `LR11x0` base class, so this is a type
   swap (`LR1110`→`LR1121`), not new logic. Cross-check the RF-switch table /
   `setTCXO`/sync-word/preamble sequence against trail-mate's `initLoRa()`
   (`~/dev/trail-mate/boards/tlora_pager/src/tlora_pager_board.cpp`), which
   drives RadioLib's stock `LR1121` class directly on this exact device. They
   subclass RadioLib's `LR1121` and the core's `RadioLibWrapper`, both already
   on the include path; that's why they can live in the variant dir (zero
   core-fork churn).
2. `variants/lilygo_tlora_pager/target.h` — mirror `variants/lilygo_tdeck/target.h`:
   `RADIOLIB_STATIC_ONLY`, include the local wrapper headers (quoted includes),
   externs for `board`, `radio_driver`, `radio`, `rtc_clock`, `sensors`, and the
   `#ifdef DISPLAY_CLASS` display/user_btn block.
3. `variants/lilygo_tlora_pager/target.cpp` — mirror the T-Deck's:
   - `RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);`
   - `WRAPPER_CLASS radio_driver(radio, board);`
   - `radio_init()`: RTC + `Wire.begin(3, 2)`, SPI begin on 35/33/34, then the
     **LR1121-mandatory sequence** (no `std_init` exists for it):
     `radio.begin(freq, bw, sf, cr, syncword, power, preamble)` with the same
     NodePrefs-driven params the other targets use, then
     `setRfSwitchTable` on **DIO5/DIO6** (`STBY {L,L} / RX {L,H} / TX {H,L} /
     TX_HP {H,L}`, table verbatim from trail-mate `initLoRa()` /
     upstream target). Pass `tcxoVoltage=3.0f` as `begin()`'s 8th arg directly
     rather than calling `setTCXO(3.0f)` again afterward — `LR11x0::modSetup()`
     already applies it internally, so a second call is a no-op (trail-mate's
     `initLoRa()` only needs the follow-up call because it uses the zero-arg
     `begin()`, whose default `tcxoVoltage` is 1.6 V, not 3.0 V). Match sync
     word / preamble / CR to what `RadioLibWrappers.cpp` uses so the pager
     interoperates with the live mesh, and add `setCRC(1)` after `begin()`
     (LR11x0 defaults to a 2-byte CRC; every other MeshCore radio wrapper
     overrides to 1 byte for wire-protocol interop).
   - `radio_new_identity()` via `RadioNoiseListener` (copy T-Deck's actual code
     — not `StdRNG`, which neither T-Deck nor Heltec V4 actually use for this).
4. GPS: wire `EnvironmentSensorManager` like the T-Deck (`ENV_INCLUDE_GPS`).
   **TX/RX direction, resolved**: wadamesh's own core calls
   `Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX)`, and `HardwareSerial::setPins()`
   takes `(rxPin, txPin)` — so `PIN_GPS_TX` supplies the ESP's RX pin and
   `PIN_GPS_RX` supplies the ESP's TX pin (named from the GPS module's
   perspective). trail-mate's `GPS_RX=4`/`GPS_TX=12` macros are named the
   OPPOSITE way (its own `Serial1.begin(baud, cfg, GPS_RX, GPS_TX)` call uses
   `HardwareSerial::begin()`'s native `(rxPin, txPin)` order, so its `GPS_RX`
   already IS the ESP's RX pin). **Use `PIN_GPS_RX=12` / `PIN_GPS_TX=4`** —
   trail-mate's raw values swapped, not copied verbatim. MIA-M10Q baud:
   confirmed 38400 from trail-mate's `Serial1.begin(38400, ...)` (same as
   T-Deck Plus).

**Gate:** both existing envs build (pager files still unreferenced). Manually
re-read the RF-switch table against BOTH references — a wrong table silently kills
TX power (that class of bug cost the T-Deck ~16 dB once; see the
`SX126X_DIO2_AS_RF_SWITCH` war story in `platformio.ini`).

---

## Milestone 3 — ST7796 display driver + backlight

**Objective:** app-side `DISPLAY_CLASS` for the 480×222 panel.

**Deliverables**
1. `src/helpers/ui/ST7796LCDDisplay.{h,cpp}` — implement the core's `DisplayDriver`
   interface exactly as `ST7789LCDDisplay` does (find it in the core lib after a
   build: `.pio/libdeps/<env>/MeshCore/src/helpers/ui/ST7789LCDDisplay.h`). The
   LVGL path in `UITask.cpp` only calls: `begin()`, `width()`, `height()`,
   `startFrame()`/`endFrame()`, `setDisplayRotation(int)`,
   `writePixelsRGB565(x, y, w, h, buf)` (flush at `UITask.cpp` ~2069), plus what
   `main.cpp:244+` uses for the boot screen. Build it on **TFT_eSPI**
   (`ST7796_DRIVER`) the way the Heltec V4 wires TFT_eSPI via `USER_SETUP_LOADED`
   `-D`s. Panel native is **222×480 portrait**; landscape 480×222 comes from
   MADCTL rotation (`setRotation`), same approach as the existing boards.
   Guard the whole TU with the pager's board macro so other envs don't compile it.
2. Backlight: the AW9364 is a **stepped one-wire dimmer** (pulse-counted levels,
   16 steps), not a PWM pin — small helper (crib the pulse timing from trail-mate),
   either inside the display class or as `Aw9364Backlight.{h,cpp}` next to it.
   Expose the same brightness hook `ST7789LCDDisplay` exposes so the existing
   Settings brightness UI keeps working.

**Gate:** both existing envs build. (The new TU is gated off for them; it first
compiles for real in M4 — expect to iterate on it then.)

---

## Milestone 4 — PlatformIO env (FIRST FULL COMPILE — the integration milestone)

**Objective:** `[env:tlora_pager_lr1121_companion_radio_touch]` builds the entire
app for the pager. This is where M1–M3 code meets the compiler; expect iteration
and fix M1–M3 files as needed (that's in-scope here).

**Deliverables**
1. New env in `platformio.ini`, cloned from `[env:LilyGo_TDeck_companion_radio_touch]`,
   with these deltas (everything not listed stays as the T-Deck has it —
   MULTI_TRANSPORT_COMPANION, TCP/WS ports, BLE_PIN_CODE, MAX_CONTACTS,
   LV_* flags, mbedTLS sizes, `extra_scripts`, RadioLib excludes, etc.):
   - `board = lilygo-t-lora-pager`;
     `board_build.partitions = variants/lilygo_tlora_pager/partitions_tlora_pager_touch.csv`
   - Board: `-I variants/lilygo_tlora_pager`, `-D TLORA_PAGER=1` (new board macro),
     drop `LILYGO_TDECK` + all T-Deck pins/caps (`HAS_TDECK_*`, `PIN_TB_*`,
     `PIN_PERF_POWERON`).
   - Radio: `-D USE_LR1121=1`, `RADIO_CLASS=CustomLR1121`,
     `WRAPPER_CLASS=CustomLR1121Wrapper`, `P_LORA_NSS=36`, `P_LORA_RESET=47`,
     `P_LORA_BUSY=48`, `P_LORA_DIO_1=14`, `P_LORA_SCLK=35`, `P_LORA_MISO=33`,
     `P_LORA_MOSI=34`. Drop the SX126X-specific `-D`s (`SX126X_*`, `USE_SX1262`);
     optionally add `RADIOLIB_EXCLUDE_SX126X=1`.
   - Display: `-D DISPLAY_CLASS=ST7796LCDDisplay`, TFT_eSPI set:
     `USER_SETUP_LOADED=1`, `ST7796_DRIVER=1`, `TFT_WIDTH=222`, `TFT_HEIGHT=480`,
     `TFT_MOSI=34`, `TFT_SCLK=35`, `TFT_CS=38`, `TFT_DC=37`, `TFT_RST=-1`,
     `TFT_MISO=33`, `TFT_BL=-1` (AW9364 owns brightness), `SPI_FREQUENCY` per
     trail-mate's panel clock.
   - Input/caps: `PIN_USER_BTN=0`; the pager cap defines you'll consume in M5/M6
     (suggest `HAS_PAGER_KEYBOARD=1`, `HAS_PAGER_ENCODER=1`).
   - GPS: `ENV_INCLUDE_GPS=1`, `ENV_SKIP_GPS_DETECT=1`, `PIN_GPS_RX=12`,
     `PIN_GPS_TX=4` (swapped vs. trail-mate's raw values — see M2's deliverable
     ④ for why), `GPS_BAUD_RATE=38400`.
   - `FIRMWARE_OTA_ENV='"tlora_pager_lr1121_companion_radio_touch"'`.
   - `build_src_filter`: T-Deck's list with `+<../variants/lilygo_tlora_pager/*.cpp>`.
   - `lib_deps`: T-Deck's list **plus** `bodmer/TFT_eSPI @ ^2.5.43`,
     `adafruit/Adafruit TCA8418 @ ^1.0.2`, `lewisxhe/SensorLib` (pin the latest
     release), and (only if M1 ended up needing it) `lewisxhe/XPowersLib`.
   - Do NOT add the env to `default_envs` yet (keeps `pio run` = the two shipping
     boards until the port stabilizes).
2. Whatever fixes M1–M3 files need to make it link.

**Gate (hard):** all **three** envs build green. Record flash/RAM usage of the
pager build in the tracker.

---

## Milestone 5 — Input drivers (TCA8418 keyboard + rotary encoder)

**Objective:** pollable drivers in the established `src/helpers/input/` style
(begin/poll/read API, ring buffers, **zero LVGL inside drivers**).

**Deliverables**
1. `src/helpers/input/PagerKeyboard.{h,cpp}` — TCA8418 over I²C (Adafruit lib),
   INT pin 6 (prefer INT-driven drain; polled fallback fine). API mirrors
   `TDeckKeyboard.h`: `pagerKeyboardBegin()`, `pagerKeyboardPoll()`,
   `pagerKeyboardReadKey()` → **final ASCII/control codes** — i.e. the keymap +
   shift/sym/alt state machine lives HERE, so the UI sees the same char stream the
   T-Deck's C3 keyboard produces and `handleHwKey()` needs no changes. Crib the
   matrix→ASCII layout tables from trail-mate's `LilyGoKeyboard` (find it under
   `~/dev/trail-mate`; it's the working reference for this exact hardware).
   Include keyboard backlight control (pin 46) mirroring
   `tdeckKeyboardSetBacklight()` so the existing settings hook can drive it.
2. `src/helpers/input/PagerEncoder.{h,cpp}` — quadrature on A=40/B=41 with ISR
   edge-counting exactly like `TDeckTrackball.cpp:27-47`, press on GPIO 7 with
   held-state API. Suggested API: `pagerEncoderBegin()`,
   `pagerEncoderReadDelta()` (signed detents since last call),
   `pagerEncoderClickHeld()`.
3. Both TUs gated by their cap macros (`HAS_PAGER_KEYBOARD` / `HAS_PAGER_ENCODER`);
   already picked up by the M4 `build_src_filter` (`+<helpers/input/*.cpp>`).

**Gate:** all three envs build. Driver headers carry a short doc comment stating
the contract (like `TDeckTrackball.h` does).

---

## Milestone 6 — UITask wiring (caps, indev, resolution, 222-px layout)

**Objective:** the pager becomes a first-class UI target. This is the largest
milestone — work through it in the order below, keeping every change behind the
pager gate. Line anchors are beta_35-era; search for the symbols.

**Deliverables**
1. `src/ui-touch/device_caps.h` — new pager block: no touch, hw keyboard, encoder,
   480×222, SD, GPS. Follow the existing cap style; wire `HAS_PAGER_KEYBOARD`/
   `HAS_PAGER_ENCODER`/`TLORA_PAGER` into the caps the UI actually branches on.
2. **Indev registration** (`UITask.cpp` ~35706–35751): extend the Tanmatsu
   keypad-only branch (KEYPAD indev + `s_nav_group`, read_cb `tanmatsuKeypadRead`)
   to also cover the pager cap. Do NOT register the POINTER indev for the pager.
3. **Input drain** (main loop, mirror the T-Deck drain at ~37320): pager branch —
   `pagerKeyboardPoll()` + `while ((k = pagerKeyboardReadKey())) handleHwKey(k);`
   encoder: `pagerEncoderReadDelta()` → `navFifoPush(LV_KEY_NEXT/PREV)` per detent,
   click → ENTER via the FIFO, long-press → ESC. Respect `s_nav_ta_editing`
   semantics already implemented in `handleHwKey()` (~27990) — no changes expected
   there beyond verifying the nav-hotkey cluster doesn't collide with pager typing.
4. **Resolution** (~35626): new branch — pager is fixed 480×222 landscape via
   hardware MADCTL rotation (`hor_res=480, ver_res=222`, no `sw_rotate`, no
   portrait mode). Draw buffer (~1399): width 480.
5. **222-px vertical audit** — the real work. Budget: status bar + tab bar +
   content in 222 px, no on-screen keyboard ever (reuse the T-Deck suppression,
   ~1153/2691/28005). Review `STATUSBAR_H`, `TABBAR_H`, `CHAT_KB_H` (~913–944) and
   the modal/chat height helpers (`tabContentW/H`, `modalAvailW/H`,
   `chatScreenW/H`); introduce pager-gated slimmer values where 222 px overflows.
   Screens to check compile-time/desk-level (full on-device pass is M8): Home,
   chat thread + compose, contacts list, settings list, modals/toasts.
6. Boot screen: `main.cpp:263-267` paints the wordmark via `writePixelsRGB565` —
   make sure the centering math handles 480×222.

**Gate:** all three envs build; T-Deck/V4 binaries behaviorally unchanged (every
edit sits behind the pager gate — spot-check the diff for un-gated changes).
Update the tracker's UI-inventory table with what was actually touched.

---

## Milestone 7 — Headless hardware bring-up (needs device + human)

**Objective:** prove radio, storage, and companion link on real hardware before
polishing UI. The agent prepares, flashes, and reads logs; the human handles the
physical device and the second mesh node.

**Checklist**
1. Flash with the **4-component chain** (`0x0/0x8000/0xe000/0x10000`) — NEVER the
   merged image (wipes NVS). `pio run -t upload -e tlora_pager_lr1121_companion_radio_touch`
   or esptool with the four artifacts.
2. Serial monitor (115200): clean boot, XL9555 rails up, gauge probe result,
   SPIFFS mounts, SD detect (if card present), GPS NMEA flowing.
3. **Radio gate**: against a known-good node (T-Deck/V4 on the same freq/bw/sf):
   adverts seen BOTH directions; DM with ACK round-trip verified several times —
   this specifically probes upstream LR1121 ACK issue
   (meshcore-dev/MeshCore#1376, still open as of 2026-07-06). If ACKs fail,
   check whether upstream has since landed `CustomLR1121`/a pager target (it
   had not as of this writing — see TLORA_PAGER_PORT.md's Decision ② caveat)
   for post-issue fixes before debugging locally.
4. **HW-CDC gate**: connect the companion app over USB; the large device-profile
   frame (node name + keys) must arrive intact. If bytes drop (the Heltec V4
   regression), rebuild with the board JSON's `ARDUINO_USB_MODE` overridden back
   to TinyUSB CDC and record the decision in the tracker.
5. Record RSSI/SNR sanity, TX current draw if measurable, and flash/RAM headroom.

**Gate:** all four checklist gates pass, results logged in the tracker.

---

## Milestone 8 — On-device UI pass

**Objective:** every screen usable with encoder + QWERTY only.

**Checklist** (drive each screen on hardware; fix behind the pager gate):
- Focus-nav coverage: every interactive control reachable in `s_nav_group`
  (`navMaybeRebuild` per-screen collection) — tabs, lists, buttons, toggles,
  text fields, modals, action sheets.
- Chat at 222 px: thread list, bubbles, compose flow (focus field → type on QWERTY
  → send), per-message info sheet.
- Long lists (contacts up to 2000): encoder NEXT/PREV walk is tolerable; if not,
  add pager-gated page-jump keys via `navMoveDir` (risk #5 in the tracker).
- Map: pan via nav keys/encoder, zoom keys.
- Settings: brightness (AW9364 steps), keyboard backlight, sleep/wake (encoder or
  BOOT wakes), lock screen.
- Fonts/legibility at 480-wide; screenshot or photograph anything questionable for
  the human to judge.

**Gate:** human sign-off screen-by-screen; deviations logged in the tracker.

---

## Milestone 9 — Release pipeline

**Objective:** the pager ships through the existing two-channel release flow.

**Deliverables**
1. `scripts/release.sh`: add `tlora_pager_lr1121_companion_radio_touch:wadamesh-tlora-pager`
   to `ENVS`.
2. `deploy/flasher/manifest-tlora-pager.json` + flasher page entry (mirror the
   T-Deck manifest; 4-component chain offsets).
3. Verify `merge-bin.py` + `scripts/build/gen-flasher-meta.py` handle the third
   env (they iterate `ENVS`/manifests — check assumptions).
4. Add the env to `default_envs` now that it's shipping.
5. Do NOT touch the Mesh America catalog or LauncherHub — separate decisions.

**Gate:** a dry-run `scripts/release.sh beta_<next>` (without `WADAMESH_VPS` set)
stages all three boards' artifacts locally; human reviews before any real cut.

---

## Milestone 10 (optional) — SX1262-variant env

**Objective:** serve pager units sold with SX1262 instead of LR1121.

Clone the M4 env as `tlora_pager_sx1262_companion_radio_touch`: same board JSON,
same variant dir, swap the radio block back to the T-Deck's SX1262 set
(`USE_SX1262`, `RADIO_CLASS=CustomSX1262`, `WRAPPER_CLASS=CustomSX1262Wrapper`,
same NSS/RESET/BUSY/DIO1 pins, plus the `SX126X_*` defines — check trail-mate's
SX1262 pager env for TCXO/DIO2-switch values on this board; do NOT assume the
T-Deck's). `target.cpp` needs a small `#if` around the LR1121-only init sequence.

**Gate:** all envs build; radio gate (M7 step 3) re-run on SX1262 hardware when
available.

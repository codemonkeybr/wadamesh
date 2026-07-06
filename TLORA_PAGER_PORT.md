# T-LoRa Pager port — working tracker

Goal: run the **full** wadamesh UI + functionality on the **LilyGo T-LoRa Pager**
(ESP32-S3, **LR1121** radio variant first), kept as **one codebase** with the
existing boards so a UI change ships everywhere at once. This file is the running
plan/status — update it as we go.

Status: **planning — no hardware bring-up yet.** Board JSON verified
(`boards/lilygo-t-lora-pager.json`, landed). Milestone ① (variant skeleton) and
Milestone ② (LR1121 radio glue) landed: `variants/lilygo_tlora_pager/{pins_arduino.h,
partitions_tlora_pager_touch.csv, TLoraPagerBoard.h/.cpp, CustomLR1121.h,
CustomLR1121Wrapper.h, target.h/.cpp}`. Nothing references these files yet (no
platformio.ini env — that's M4), so both shipping envs build unchanged; the new
files are unverified by the compiler until then. Next = step ③ of the worklist
(ST7796 display driver). **Execution playbook: `TLORA_PAGER_PORT_MILESTONES.md`**
— the worklist below, broken into agent-executable milestones with gates and
per-file instructions.

---

## Why this port is cheaper than it looks

The scary part — "the device has no touchscreen" — is **already solved in this
codebase**. UITask carries a complete non-touch navigation layer built for the
Tanmatsu (keypad-only) and the T-Deck trackball D-pad mode:

- **Focus-group nav**: `s_nav_group` (`lv_group_t`), amber focus ring + scroll-into-view
  (`navFocusCb`), per-screen group rebuild (`navMaybeRebuild`), 2-D directional focus
  (`navMoveDir`), tab-bar handling (`navOnTabBar`/`navSwitchTab`) — `UITask.cpp` ~2138–3128.
- **A KEYPAD indev + key FIFO**: `navFifoPush/Pop` feeds `LV_KEY_UP/DOWN/LEFT/RIGHT/
  NEXT/PREV/ENTER/ESC` into `tanmatsuKeypadRead` (`UITask.cpp` ~3000). The Tanmatsu
  registers ONLY this indev (no pointer) — `UITask.cpp` ~35706–35717. **That branch is
  the pager's template.**
- **Physical-keyboard routing**: `handleHwKey()` (`UITask.cpp` ~27990) routes keys
  into the focused textarea (edit mode) or into nav (navigate mode, `s_nav_ta_editing`
  flag), with the on-screen LVGL keyboard suppressed — exactly how the T-Deck works
  today. The pager's QWERTY plugs into this unchanged.
- **Rotary encoder**: no `LV_INDEV_TYPE_ENCODER` needed — encoder ticks map to
  `navFifoPush(LV_KEY_NEXT/PREV)` (focus walk), press → `ENTER`, long-press → `ESC`.
  Reuses everything above.

So the genuinely NEW work is: the **480×222 ST7796 display driver + layout pass**,
the **TCA8418 keyboard driver** (raw matrix → chars, unlike the T-Deck's C3 which
resolves ASCII for us), the **rotary driver**, the **LR1121 radio glue**, and a
**board class** whose battery/power goes through I²C chips (fuel gauge + IO
expander) instead of an ADC pin.

## Hardware / platform facts

Confirmed from the LilyGo product page, CNX-Software (2025-08-12), Meshtastic docs,
and cross-checked against three working/authoritative sources: upstream
`meshcore-dev/MeshCore`'s pager target (see caveat under Decision ②), the official
[LilyGoLib hardware page](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-lora-pager.md),
and `~/dev/trail-mate` (local project with a running LR1121 pager build — pin map
below is from its `variants/lilygo_tlora_pager/pins_arduino.h`, which is
byte-identical on every pin/channel to the canonical
[`espressif/arduino-esp32` pins_arduino.h](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_tlora_pager/pins_arduino.h)
for this board). **Caveat**: the LilyGoLib page's own "Pins Map" table has
internal inconsistencies for the XL9555 channel assignments (e.g. it lists
keyboard-enable at ch10 and SD-detect/enable at ch12/ch14, which contradicts
its own "PowerManage Channel" table on the same page *and* the canonical
arduino-esp32 header). We treat the arduino-esp32 header + trail-mate (which
agree with each other and with the "PowerManage Channel" table) as ground
truth — that's what our own `TLoraPagerBoard.cpp` already uses.

| | |
|---|---|
| SoC | ESP32-**S3** @ 240 MHz, 16 MB flash (QIO), **8 MB QSPI PSRAM** (`memory_type: qio_qspi`) |
| Display | 2.33" IPS **ST7796U**, 480×222 (221 PPI, 262K colors, 450 cd/m²), **480×222 landscape**, SPI, **no touch**. CS 38, DC 37, RST −1, backlight 42 (AW9364 16-level stepped driver) |
| Shared SPI bus | **SCK 35, MOSI 34, MISO 33** — display + LoRa + SD + ST25R3916 NFC all on it (like the T-Deck's 40/41/38 — solved pattern, CS discipline + SPI transactions) |
| Radio | **LR1121** (sub-GHz 830–945 MHz + 2.4 GHz; we use sub-GHz only). CS 36, RST 47, BUSY 48, IRQ/DIO1 14. **Also sold with SX1262** (the LilyGoLib page documents the SX1262 retail SKU as primary) — same board/pins, different defines (cheap 2nd env later) |
| Keyboard | Physical QWERTY via **TCA8418** I²C matrix controller (addr `0x34`), INT 6, backlight 46. Raw matrix events — keymap/shift/sym handled on our side |
| Encoder | Rotary A 40, B 41, **press 7** |
| Buttons | **BOOT = GPIO0** (usable as user button + sleep wake — matches both existing boards' `PIN_USER_BTN=0`). Physical power key is PMU QON, not a GPIO — can only wake the device (1s hold), never programmable |
| Power | **BQ25896** charger PMU (addr `0x6B`) + **BQ27220 fuel gauge** (addr `0x55`, battery % / mV over I²C — NOT an ADC divider). Battery: 3.7 V / 1500 mAh (5.55 Wh). DeepSleep ≈530 µA, LightSleep ≈2.26 mA, Power-off ≈26 µA |
| IO expander | **XL9555** (addr `0x20`) gates power rails: DRV_EN ch0, AMP_EN ch1, KB_RST ch2, LORA_EN ch3, GPS_EN ch4, NFC_EN ch5, GPS_RST ch7, KB_EN ch8, SD_DET ch10, SD_PULLEN ch11, SD_EN ch12 |
| GPS | u-blox **MIA-M10Q**: TX 12, RX 4, PPS 13 |
| I²C bus | SDA 3, SCL 2 — shared by TCA8418 (`0x34`), XL9555 (`0x20`), BQ25896 (`0x6B`), BQ27220 (`0x55`), PCF85063 RTC (`0x51`), BHI260AP IMU (`0x28`), DRV2605 haptics (`0x5A`), ES8311 codec (`0x18`) |
| SD | microSD on the shared SPI bus, CS 21, card-detect via expander (ch10), max 32 GB, **FAT32 only** |
| Audio | ES8311 codec (I2S 10/11/17/18/45) driving an **NS4150B** 3 W Class-D amp (enabled via expander AMP_EN ch1) |
| Misc | PCF85063A RTC (INT 1), BHI260AP IMU (INT 8), ST25R3916 NFC (unused — CS 39, INT 5, powered via expander NFC_EN ch5), USB VID/PID `0x303A:0x82D4` |

## Board JSON — VERIFIED ✅ (landed as `boards/lilygo-t-lora-pager.json`)

The JSON (Meshtastic-lineage; byte-identical to trail-mate's copy) is **correct for
the LR1121 unit but radio-agnostic**: it describes only the S3 module (flash/PSRAM/
USB id/CDC-on-boot), which every radio variant of the pager shares. The radio is
selected by our build flags (`RADIO_CLASS`/`WRAPPER_CLASS` + pins), same as the
existing boards. Notes:

- `partitions: app3M_fat9M_16MB.csv` is just the default — we override with our own
  OTA+tiles+spiffs csv via `board_build.partitions` (see worklist ①).
- `variant: lilygo_tlora_pager` + `variants_dir: variants` → needs
  `variants/lilygo_tlora_pager/pins_arduino.h` (write our own; don't copy
  trail-mate's — it brands `USB_PRODUCT "TRAIL MATE"`).
- `-DARDUINO_USB_MODE=1` (HW-CDC) is in the JSON's extra_flags. The T-Deck ships
  MODE=1 fine; the Heltec V4 regressed on it (large companion frames dropped —
  see the note in `platformio.ini`). **Verify the device-profile frame over USB
  companion early** (worklist ⑦); if it drops bytes, switch to TinyUSB CDC like
  the V4.

## Decisions (architecture)

1. **Normal PlatformIO env, T-Deck model** — NOT the Tanmatsu IDF-subproject route.
   The pager is a plain ESP32-S3 Arduino target; it slots into `platformio.ini`
   next to the existing two envs and into `release.sh`'s env list later.
2. **LR1121 wrappers vendored in the variant dir — zero core-fork churn for
   bring-up.** The core fork (`core-v1.16.5`) only has `CustomLR1110*`.
   **Re-verified 2026-07-06: upstream `meshcore-dev/MeshCore`'s `main` branch does
   NOT currently have `CustomLR1121*` or a `variants/lilygo_tlora_pager/` dir**
   (checked `src/helpers/radiolib/` — only `CustomLR1110{,Wrapper}.h` exists there
   too; issue [meshcore-dev/MeshCore#861](https://github.com/meshcore-dev/MeshCore/issues/861)
   "Support for LR1121" is still open). Earlier research that assumed an
   upstream crib source was wrong or looked at a branch/fork that no longer
   exists — **re-check upstream at Milestone ② time**, but plan for having to
   author `CustomLR1121{,Wrapper}.h` ourselves by adapting the core fork's own
   `CustomLR1110{,Wrapper}.h` pair (same RadioLib `LR11x0` family — swap the
   base type from `LR1110` to `LR1121`), cross-checked against trail-mate's
   `initLoRa()` (which drives RadioLib's stock `LR1121` class directly, no
   custom wrapper) for the RF-switch table / `setTCXO` sequence. Since
   `CustomLR1121{,Wrapper}.h` only subclass RadioLib's `LR1121` and the core's
   `RadioLibWrapper` (both on the include path), they can live in
   `variants/lilygo_tlora_pager/` for now and move into the fork at the next
   `core-*` tag. Precedent: the Tanmatsu keeps its whole radio bridge in its
   variant dir.
3. **LR1121 init is explicit** (no `std_init`): RF-switch table on **DIO5/DIO6**
   (`STBY {L,L} / RX {L,H} / TX {H,L} / TX_HP {H,L}`) + **`setTCXO(3.0f)`** —
   confirmed in both trail-mate (`boards/tlora_pager/src/tlora_pager_board.cpp`,
   `initLoRa()`) and upstream. Sync word / preamble / CR come from the same
   NodePrefs plumbing as the other boards so it interoperates with the mesh.
4. **Display = new `ST7796LCDDisplay` app-side** (in `src/helpers/ui/`), implementing
   the core's `DisplayDriver` interface (`begin/width/height/startFrame/endFrame/
   setDisplayRotation/writePixelsRGB565`) — that's all the LVGL flush path uses.
   TFT_eSPI has `ST7796_DRIVER` and the Heltec V4 already builds on TFT_eSPI, so
   mirror that wiring (`USER_SETUP_LOADED` + `-D` pin set). Backlight is the AW9364
   (stepped pulse dimming), not a plain GPIO PWM — small driver, crib trail-mate.
5. **Input = the Tanmatsu registration branch** (KEYPAD indev + `s_nav_group` only,
   no pointer indev), gated by a new device cap. Rotary → nav FIFO; TCA8418 →
   `handleHwKey()`. New pollable drivers in `src/helpers/input/` following the
   existing style (begin/poll/read API, no LVGL inside the driver).
6. **Battery/power via libraries, not hand-rolled** (CONTRIBUTING rule): lewisxhe
   **SensorLib** covers BQ27220 (`GaugeBQ27220`), XL9555 (`ExtensionIOXL9555`),
   PCF85063, DRV2605; **XPowersLib** covers the BQ25896. `TLoraPagerBoard :
   public ESP32Board` overrides `getBattMilliVolts()` (gauge query),
   `getManufacturerName()`, power-rail bring-up in `begin()` (expander), and sleep.
7. **One codebase**: all pager-specific UI behavior rides existing/new `CAP_*`
   flags in `src/ui-touch/device_caps.h` — no forked screens.

## Worklist (ordered, each step ends build-green for ALL envs)

- [x] ⓪ Research + board JSON verification; land `boards/lilygo-t-lora-pager.json` + this tracker.
- [x] ① **Variant skeleton**: `variants/lilygo_tlora_pager/{pins_arduino.h, TLoraPagerBoard.h/.cpp}` + `partitions_tlora_pager_touch.csv` (T-Deck's OTA/tiles/spiffs/coredump layout, byte-identical offsets). Board class (`TLoraPagerBoard : public ESP32Board`): `begin()` re-inits Wire on SDA3/SCL2, probes the XL9555 expander (addr 0x20) and enables LORA_EN/GPS_EN/KB_EN/KB_RST/SD_EN rails, probes the BQ27220 gauge, handles the deep-sleep RX-packet wake reason (mirrors `TDeckBoard.cpp`). `getBattMilliVolts()` reads the gauge (`refresh()` + `getVoltage()`), falling back to 3700 mV if the probe/refresh fails. `getManufacturerName()` → "LilyGo T-LoRa Pager". `enterDeepSleep()` mirrors `TDeckBoard.h`. BQ25896 charger deliberately left as a TODO (gauge alone covers the UI). `target.{h,cpp}` deferred to ② — nothing references the new files yet, so both shipping envs build unchanged (verified green).
- [x] ② **Radio**: `variants/lilygo_tlora_pager/{CustomLR1121.h, CustomLR1121Wrapper.h, target.h, target.cpp}`.
  `CustomLR1121{,Wrapper}.h` authored by adapting the core fork's own
  `CustomLR1110{,Wrapper}.h` (LR1110/LR1121 share RadioLib's `LR11x0` base —
  confirmed by reading RadioLib 7.6.0 source directly: same protected
  `freqMHz`/`spreadingFactor` members, same `getIrqStatus()`/`getRssiInst()`
  inherited from `LRxxxx`/`LR11x0`), since upstream has nothing to crib (see
  Decision ②). `radio_init()` in `target.cpp`: `spi.begin(...)` →
  `radio.begin(LORA_FREQ, LORA_BW, LORA_SF, cr, ..._SYNC_WORD_PRIVATE,
  LORA_TX_POWER, 8, 3.0f)` → `setRfSwitchTable(DIO5/DIO6)` → `setCRC(1)`.
  **Three deliberate deviations from trail-mate, found by reading RadioLib's
  actual source rather than copying its call sequence — see Decision ② for
  the full reasoning:**
  1. No explicit `radio.reset()` before `begin()` — RadioLib's
     `LR11x0::modSetup()`→`findChip()` already resets the chip internally
     (with retries); trail-mate's explicit reset is redundant, not wrong.
  2. No second `radio.setTCXO(3.0f)` call after `begin()` — trail-mate only
     needs that because its `initLoRa()` calls the *zero-arg* `begin()`
     (default `tcxoVoltage=1.6V`) and fixes it up after. Our `begin()` passes
     `3.0f` as the 8th arg directly, which `LR11x0::modSetup()` already
     applies internally — a second call would be a no-op.
  3. Added `radio.setCRC(1)` after `begin()` (LR11x0's `begin()` defaults to a
     2-byte CRC) to match the 1-byte CRC every other MeshCore radio wrapper
     uses for wire-protocol interop (`CustomSX1262::std_init()` makes the
     identical override) — trail-mate never needed this since its app isn't
     interoperating with MeshCore's own framing.
  Also **not** calling `rtc_clock.begin(Wire)` in `radio_init()` — see the new
  Risk item below (RTC address collision). `RfSwitchMode_t`/`OpMode_t`/DIO5-6
  constants verified to exist with the expected shape directly in the pinned
  `jgromes/RadioLib @ ^7.6.0` source (not just trusted from trail-mate).
  Env defines for M4 to use: `RADIO_CLASS=CustomLR1121`,
  `WRAPPER_CLASS=CustomLR1121Wrapper`, `P_LORA_NSS=36 / _RESET=47 / _BUSY=48 /
  _DIO_1=14`, SPI 35/34/33, **`PIN_GPS_RX=12` / `PIN_GPS_TX=4`** (see the GPS
  risk item below — these are swapped relative to trail-mate's raw
  `GPS_RX`/`GPS_TX` macro values, on purpose), `GPS_BAUD_RATE=38400`. Keep the
  standard `RADIOLIB_EXCLUDE_*` set (LR11X0 stays IN; can also exclude SX126X
  here).
- [ ] ③ **Display**: `src/helpers/ui/ST7796LCDDisplay.{h,cpp}` on TFT_eSPI (`ST7796_DRIVER`, `TFT_WIDTH=222`, `TFT_HEIGHT=480`, MADCTL rotation for landscape) + AW9364 backlight helper. `DISPLAY_CLASS=ST7796LCDDisplay`.
- [ ] ④ **Env**: `[env:tlora_pager_lr1121_companion_radio_touch]` in `platformio.ini` — clone the T-Deck env, swap board/radio/display/input defines, add lib_deps: `adafruit/Adafruit TCA8418`, `lewisxhe/SensorLib`, `lewisxhe/XPowersLib`. **Compile gate: all three envs build.**
- [ ] ⑤ **Input drivers**: `src/helpers/input/PagerKeyboard.{h,cpp}` (TCA8418 INT-driven or polled; local keymap incl. shift/sym/alt — crib trail-mate's `LilyGoKeyboard` layout tables) and `PagerEncoder.{h,cpp}` (quadrature on 40/41 + press 7, ISR edge-counting like `TDeckTrackball`). Same pollable-API style; no LVGL in drivers.
- [ ] ⑥ **UITask wiring**: new cap block in `device_caps.h`; register KEYPAD indev via the Tanmatsu branch (~35706, widen its `#if` gate); drain keyboard → `handleHwKey()` + encoder → `navFifoPush` in the main loop (mirror the T-Deck drain at ~37320); add 480×222 to the `hor_res/ver_res` block (~35626, hardware MADCTL rotation — no sw_rotate needed); draw-buffer width 480 (`g_draw_buf_px`, ~1399); **222-px vertical audit**: `STATUSBAR_H`/`TABBAR_H`/`CHAT_KB_H` (~913–944) and modal/chat height helpers; on-screen keyboard suppressed (reuse the T-Deck path).
- [ ] ⑦ **Headless bring-up gate** (needs hardware): radio joins the live mesh (adverts seen both ways, ACKs verified — watch upstream LR1121 ACK issue meshcore-dev/MeshCore#1376); USB companion link passes the large device-profile frame (see HW-CDC note above); SPIFFS + SD storage OK.
- [ ] ⑧ **On-device UI pass**: nav-coverage audit screen by screen (every interactive control reachable via focus group — the Tanmatsu work paved this), chat layout at 222 px, map pan via encoder/keys, fonts legibility at 480-wide.
- [ ] ⑨ **Release pipeline**: add the env:binname pair to `release.sh` `ENVS`, flasher manifest (`deploy/flasher/manifest-tlora-pager.json`), OTA env name via `FIRMWARE_OTA_ENV`. Separate PR.
- [ ] ⑩ (Optional, cheap) `tlora_pager_sx1262_...` env for SX1262-variant owners — same board JSON + variant, swap the four radio defines back to the T-Deck's SX1262 set.

## UI-changes inventory (what actually changes in `src/ui-touch/`)

| Area | Change | Size |
|---|---|---|
| Indev registration (~35706–35751) | Widen the Tanmatsu keypad-only branch's gate to the pager cap; do NOT register the pointer indev | small |
| Input drain (main loop ~37320) | Pager branch: `pagerKeyboardReadKey()`→`handleHwKey()`, encoder deltas→`navFifoPush(NEXT/PREV)`, press→ENTER | small |
| `handleHwKey()` (~27990) | Mostly reuse; verify nav-hotkey cluster makes sense on the pager's QWERTY layout | small |
| Resolution block (~35626) | New branch: 480×222 landscape (panel native is 222×480 portrait → MADCTL rotate, like T-Deck/V4) | small |
| Draw buffer (~1399) | Width 480 (PSRAM; ~larger stripe buffer, plenty of headroom in 8 MB) | trivial |
| Vertical budget | Audit `STATUSBAR_H`, `TABBAR_H`, `CHAT_KB_H`, modal/chat height helpers for 222 px — likely slimmer bars + no on-screen kb reclaim most of it | **the real work** |
| Focus-nav coverage | Screen-by-screen pass that every control is in `s_nav_group` (Tanmatsu already forced most of this) | medium, on-device |
| device_caps.h | New `CAP_*` block: no touch, hw keyboard, encoder, 480×222, SD, GPS | trivial |

Everything else (map, chat, contacts, channels, settings, companion protocol,
MQTT, OTA) is resolution-agnostic or already keyed off caps.

## Risks / open questions

1. **LR1121 ACK/TX reliability** — upstream issue meshcore-dev/MeshCore#1376 reports
   ACK problems on the pager's LR1121 (confirmed still open). Track it; our wrapper
   crib should include any upstream fix. Gate ⑦ tests this explicitly.
1b. **No upstream `CustomLR1121` crib source exists yet** (re-verified
   2026-07-06 — see Decision ②'s caveat). Milestone ② needs to author the
   wrapper by adapting the core fork's `CustomLR1110{,Wrapper}.h`, not by
   copying an upstream file. Re-check upstream first in case it lands before
   we get there — would save the work.
1c. **GPS `PIN_GPS_RX`/`PIN_GPS_TX` are named from the GPS module's
   perspective in wadamesh's own core (`EnvironmentSensorManager.cpp` calls
   `Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX)`, and `HardwareSerial::setPins()`
   takes `(rxPin, txPin)` — so `PIN_GPS_TX` supplies the ESP's own **RX** pin,
   `PIN_GPS_RX` supplies the ESP's own **TX** pin). trail-mate's/the canonical
   arduino-esp32 `GPS_RX=4`/`GPS_TX=12` macros are named the OPPOSITE way — its
   own `Serial1.begin(baud, cfg, GPS_RX, GPS_TX)` call uses
   `HardwareSerial::begin()`'s `(rxPin, txPin)` order directly, so there
   `GPS_RX` IS the ESP's own RX pin. **Net result: wadamesh's `PIN_GPS_RX` must
   be set to `12` and `PIN_GPS_TX` to `4`** for M4 — the raw trail-mate values
   swapped, not copied verbatim. Verified by reading both projects' actual
   `Serial1.setPins()`/`begin()` call sites and the ESP32 core's
   `HardwareSerial::setPins()`/`begin()` signatures directly, not by trusting
   either project's macro names at face value. Baud confirmed at 38400 (same
   MIA-M10Q as T-Deck Plus).
1d. **RTC auto-discovery would misread this board's real RTC.**
   `AutoDiscoverRTCClock` (core, shared by all boards) only recognizes DS3231
   (`0x68`), RV3028 (`0x52`), and PCF8563 (`0x51`) — its probe is a bare I2C ACK
   check. This board's PCF85063A sits at that same `0x51` address but has a
   different register layout (RTClib's `RTC_PCF8563` driver would misread its
   registers), so calling `rtc_clock.begin(Wire)` would silently produce
   garbage timestamps instead of a clean fallback. `target.cpp`'s `radio_init()`
   deliberately skips that call — same time behavior as T-Deck/Heltec V4 (ESP32
   software clock) rather than a false "RTC found" that's actually wrong. Real
   PCF85063A support (SensorLib's `SensorPCF85063`) is unscheduled follow-up
   work, not part of any milestone ①–⑩ yet.
2. **TCA8418 keymap** — raw matrix + our own shift/sym/alt state machine; the T-Deck
   never needed this (its C3 resolves ASCII). Bounded: trail-mate's layout tables are
   a working reference.
3. **222-px chat screen** — tightest layout wadamesh has shipped (current min is
   240). Mitigations: no on-screen keyboard (physical QWERTY), slimmer status/tab
   bars, landscape chat already exists (320×240 path).
4. **HW-CDC companion frames** (`ARDUINO_USB_MODE=1`) — known-regressed on the V4,
   fine on the T-Deck. Test the big device-profile frame first thing on hardware.
5. **Encoder-only ergonomics** on long lists (contacts @ 2000 max) — NEXT/PREV focus
   walk may need page-jump keys from the QWERTY (cheap: map to `navMoveDir`).
6. **Shared SPI contention** (display flush vs radio IRQ vs SD) — same topology the
   T-Deck ships, so expected fine via SPI transactions + CS discipline; keep an eye
   on SD-write + RX overlap during history flush.
7. **NVS-preserving flash chain** applies here too — 4-component flash, never the
   merged image (wipes saved Wi-Fi creds).

## References

- Upstream MeshCore (MIT): `meshcore-dev/MeshCore` — does **NOT** currently have
  `variants/lilygo_tlora_pager/` or `CustomLR1121*` (re-verified 2026-07-06, see
  Decision ②); only `CustomLR1110{,Wrapper}.h` exists in `src/helpers/radiolib/`,
  same as our own core fork. Flasher precedent (unrelated to firmware source):
  `flasher.meshcore.io/lilygo-t-lora-pager/`
- Local working port (pin map + LR1121 init + keymap + AW9364/BQ27220/XL9555 usage):
  `~/dev/trail-mate/boards/tlora_pager/` (esp. `src/tlora_pager_board.cpp initLoRa()`,
  `include/boards/tlora_pager/tlora_pager_board.h`) and
  `~/dev/trail-mate/variants/lilygo_tlora_pager/pins_arduino.h`
- Canonical pin map (ground truth, matches trail-mate exactly): [`espressif/arduino-esp32`
  `variants/lilygo_tlora_pager/pins_arduino.h`](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_tlora_pager/pins_arduino.h)
- Official hardware doc (chip list, I²C addresses, power-rail table, electrical
  specs): [LilyGoLib `docs/hardware/lilygo-t-lora-pager.md`](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-lora-pager.md) —
  see the caveat under "Hardware / platform facts" above about its Pins Map
  table's internal inconsistencies.
- Hardware docs: LilyGo product page (T-LoRa Pager), CNX-Software 2025-08-12 writeup,
  Meshtastic device page (`meshtastic.org/docs/hardware/devices/lilygo/tpager/`)
- In-repo templates: `variants/lilygo_tdeck/` (S3 + SPI radio + shared bus),
  Tanmatsu keypad-nav path in `src/ui-touch/UITask.cpp`, `TANMATSU_PORT.md` (tracker
  precedent)

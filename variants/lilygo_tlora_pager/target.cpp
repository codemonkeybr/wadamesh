// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include "target.h"

TLoraPagerBoard board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
ClockFloorRTC        rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();

  // rtc_clock.begin(Wire) is intentionally NOT called. This board has a real
  // PCF85063A at I2C addr 0x51, but AutoDiscoverRTCClock (helpers/AutoDiscoverRTCClock.cpp)
  // only knows the register-incompatible PCF8563 at that same address — its
  // probe is just an I2C ACK check, so it would "detect" the PCF85063A as a
  // PCF8563 and read its registers with the wrong layout, silently returning
  // garbage timestamps instead of falling back cleanly. Skipping this just
  // means we fall back to the ESP32 software clock, same as T-Deck/Heltec V4
  // today. Proper PCF85063A support (SensorLib's SensorPCF85063) is a
  // follow-up — see TLORA_PAGER_PORT.md.
  //
  // I2C itself is already up: TLoraPagerBoard::begin() calls Wire.begin(SDA, SCL)
  // and runs before radio_init() in main.cpp's setup(), so no Wire.begin() here.

  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);

#ifdef LORA_CR
  uint8_t cr = LORA_CR;
#else
  uint8_t cr = 5;
#endif

  // LR1121 has no std_init() helper (unlike CustomSX1262) — the core fork's
  // CustomLR1110 doesn't have one either, so this sequence is spelled out here
  // instead of inside the wrapper. The 8-arg begin() already applies
  // tcxoVoltage internally (RadioLib's LR11x0::modSetup() -> setTCXO()), so
  // unlike trail-mate's initLoRa() — which calls the zero-arg begin() (default
  // tcxoVoltage 1.6V) and therefore NEEDS a follow-up setTCXO(3.0f) fixup —
  // passing 3.0f directly here makes a second setTCXO() call redundant.
  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                           RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                           LORA_TX_POWER, 8, 3.0f);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(state);
    return false;
  }

  // RF-switch table on DIO5/DIO6 — verified against both trail-mate's
  // initLoRa() and the RADIOLIB_LR11X0_DIO5/DIO6 + LR11x0::OpMode_t
  // definitions in the pinned RadioLib (7.6.0) source. A wrong table here
  // silently kills TX power without any error return (see the T-Deck's
  // SX126X_DIO2_AS_RF_SWITCH war story in platformio.ini for that failure shape).
  static const uint32_t rfswitch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC,
  };
  static const Module::RfSwitchMode_t rfswitch_table[] = {
    { LR11x0::MODE_STBY,  { LOW, LOW } },
    { LR11x0::MODE_RX,    { LOW, HIGH } },
    { LR11x0::MODE_TX,    { HIGH, LOW } },
    { LR11x0::MODE_TX_HP, { HIGH, LOW } },
    { LR11x0::MODE_TX_HF, { LOW, LOW } },
    { LR11x0::MODE_GNSS,  { LOW, LOW } },
    { LR11x0::MODE_WIFI,  { LOW, LOW } },
    END_OF_MODE_TABLE,
  };
  radio.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

  // LR11x0::begin() defaults to a 2-byte CRC; MeshCore's wire protocol expects
  // 1 byte for interop with the SX126x/SX127x nodes already on the mesh
  // (CustomSX1262::std_init() makes the same override for the same reason).
  radio.setCRC(1);

  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}

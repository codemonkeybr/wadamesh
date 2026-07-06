// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include "TLoraPagerBoard.h"

void TLoraPagerBoard::begin() {

  ESP32Board::begin();

  // XL9555/BQ27220 sit on the shared I2C bus (SDA 3 / SCL 2). ESP32Board::begin()
  // only calls the pin-less Wire.begin() unless PIN_BOARD_SDA/SCL are defined
  // for this env, so re-init explicitly with this board's pins before probing
  // either chip.
  Wire.begin(SDA, SCL);

  // Configure user button
  pinMode(PIN_USER_BTN, INPUT);

  if (io_expander.begin(Wire, PAGER_XL9555_ADDR)) {
    // Enable the rails the rest of the app needs at boot: LoRa, GPS, keyboard
    // (+ its reset line, held high in steady state), and the SD card.
    const uint8_t rails[] = {
      PAGER_EXPAND_LORA_EN,
      PAGER_EXPAND_GPS_EN,
      PAGER_EXPAND_KB_EN,
      PAGER_EXPAND_KB_RST,
      PAGER_EXPAND_SD_EN,
    };
    for (uint8_t ch : rails) {
      io_expander.pinMode(ch, OUTPUT);
      io_expander.digitalWrite(ch, HIGH);
      delay(1); // stagger rail turn-on, mirrors trail-mate's bring-up order
    }
    delay(50); // let rails settle before anything downstream probes them

    io_expander.pinMode(PAGER_EXPAND_SD_DET, INPUT);
    io_expander.pinMode(PAGER_EXPAND_SD_PULLEN, INPUT);
  }

  gauge.begin(Wire);

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET; // received a LoRa packet (while in deep sleep)
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

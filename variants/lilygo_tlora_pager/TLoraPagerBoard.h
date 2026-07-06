// SPDX-License-Identifier: GPL-3.0-or-later
//
// Board class for the LilyGo T-LoRa Pager (ESP32-S3). Unlike the T-Deck/
// Heltec V4, power-rail control and battery reporting go through I2C chips —
// an XL9555 GPIO expander gates LoRa/GPS/keyboard/SD power, and a BQ27220
// fuel gauge replaces the usual VBAT ADC divider (this board doesn't have
// one). begin() brings up just enough of that I2C chain for the rest of the
// app to run.
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>
#include <ExtensionIOXL9555.hpp>
#include <GaugeBQ27220.hpp>

// XL9555 expander channels gating pager power rails (see pins_arduino.h and
// the hardware table in TLORA_PAGER_PORT.md).
#define PAGER_EXPAND_KB_RST    2
#define PAGER_EXPAND_LORA_EN   3
#define PAGER_EXPAND_GPS_EN    4
#define PAGER_EXPAND_KB_EN     8
#define PAGER_EXPAND_SD_DET    10
#define PAGER_EXPAND_SD_PULLEN 11
#define PAGER_EXPAND_SD_EN     12

#define PAGER_XL9555_ADDR 0x20

// Gauge probe/refresh failed — never let the UI divide by zero.
#define PAGER_BATT_MILLIVOLTS_FALLBACK 3700

class TLoraPagerBoard : public ESP32Board {
public:
  void begin();

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH); // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start(); // CPU halts here and never returns!
  }

  uint16_t getBattMilliVolts() {
    if (!gauge.refresh()) return PAGER_BATT_MILLIVOLTS_FALLBACK;
    uint16_t mv = gauge.getVoltage();
    return mv > 0 ? mv : PAGER_BATT_MILLIVOLTS_FALLBACK;
  }

  const char* getManufacturerName() const{
    return "LilyGo T-LoRa Pager";
  }

  // TODO: BQ25896 charger (XPowersLib) bring-up is out of scope for now — the
  // BQ27220 gauge alone covers battery %/mV for the UI. Add charge-status/
  // current reporting once the charger is wired in.

  ExtensionIOXL9555 io_expander;
  GaugeBQ27220 gauge;
};

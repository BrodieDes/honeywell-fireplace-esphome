// Sleep-forever firmware for the Knob's second chip (ESP32-U4WDH).
//
// The knob remote doesn't use this chip, but it stays powered whenever the board has
// power. This puts it into deep sleep with no wake source (about 10 uA instead of tens
// of mA). It still reflashes normally: the CH340 auto-reset pulls EN and IO0, which
// works from deep sleep. To undo, write knob_esp32-u4wdh_4MB_factory.bin back (see
// Knob Factory Backup/README.md).
//
// Pins it might otherwise leave driving: IO32 = PCM5100A XSMT, IO23/IO18 = UART to the
// S3, IO25/26/27 = I2S to the DAC switch. All go to their reset (input) state in deep
// sleep, which is what we want.

#include "esp_sleep.h"

void setup()
{
  delay(2000);   // a short window after reset, so the chip is easy to catch for flashing
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}

void loop() {}

// Fireplace knob screen (the design agreed on the "Fireplace Knob Screens" canvas).
// Call every function with the LVGL lock held.
#pragma once
#include <stdint.h>
#include "lvgl.h"

struct FireView {
  bool have_state;      // false: never heard from the hub
  bool on;
  int flame;            // 1-6
  int fan;              // 0 = off, 1-6
  int mode;             // 0 fire, 1 flame, 2 fan
  uint32_t seconds_left;
  uint8_t max_runtime_hh;  // half hours
  const char *status;   // shown instead of the timer when set (offline, send failed)
  int16_t target_c10;   // thermostat set point, 0.1 C; INT16_MIN = unknown ("--")
  int16_t current_c10;  // thermostat room temperature, 0.1 C; INT16_MIN = unknown
};

void fire_ui_build(void);
void fire_ui_show(const FireView &v);

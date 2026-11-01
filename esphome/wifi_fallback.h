// Setup-portal fallback for fireplace-rf.
//
// Wi-Fi details entered in the setup portal are saved and replace the compiled ones. A typo
// would leave the board retrying a network it can never join, and those retries make the
// setup hotspot hard to use. So the board remembers the last Wi-Fi details that actually
// connected. If different details then fail for 3 minutes, it goes back to the last working
// ones. Details that once worked are never dropped, so a router outage changes nothing.
#pragma once
#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/wifi/wifi_component.h"

namespace fireplace {

struct GoodWifi {
  char ssid[33];
  char password[65];
} __attribute__((packed));

static const uint32_t WIFI_FALLBACK_MS = 180000;

inline const char *ref_str(esphome::StringRef s) { return s.empty() ? "" : s.c_str(); }

// Call every few seconds.
inline void wifi_fallback_check() {
  static const char *const TAG = "wifi_fallback";
  static esphome::ESPPreferenceObject pref =
      esphome::global_preferences->make_preference<GoodWifi>(0x46525746);  // "FRWF"
  static GoodWifi good{};
  static bool loaded = false;
  static uint32_t failing_since = 0;
  static bool was_connected = false;
  if (!loaded) {
    loaded = true;
    if (!pref.load(&good))
      memset(&good, 0, sizeof(good));
  }

  auto *wifi = esphome::wifi::global_wifi_component;
  esphome::wifi::WiFiAP ap = wifi->get_sta();
  const char *ssid = ref_str(ap.get_ssid());
  const char *password = ref_str(ap.get_password());
  bool same = strcmp(ssid, good.ssid) == 0 && strcmp(password, good.password) == 0;

  if (wifi->is_connected()) {
    failing_since = 0;
    // Only details in use when the link came up have proven themselves. Details saved
    // while already connected are still untested.
    bool just_connected = !was_connected;
    was_connected = true;
    if (just_connected && !same) {
      memset(&good, 0, sizeof(good));
      strncpy(good.ssid, ssid, sizeof(good.ssid) - 1);
      strncpy(good.password, password, sizeof(good.password) - 1);
      pref.save(&good);
      esphome::global_preferences->sync();
      ESP_LOGI(TAG, "Remembered working Wi-Fi '%s'", good.ssid);
    }
    return;
  }

  // Not connected. Only unproven details are ever replaced.
  was_connected = false;
  if (same || good.ssid[0] == '\0') {
    failing_since = 0;
    return;
  }
  uint32_t now = esphome::millis();
  if (failing_since == 0) {
    failing_since = now == 0 ? 1 : now;
    return;
  }
  if (now - failing_since < WIFI_FALLBACK_MS)
    return;
  ESP_LOGW(TAG, "Wi-Fi '%s' failed for 3 min: going back to '%s'", ssid, good.ssid);
  failing_since = 0;
  wifi->save_wifi_sta(good.ssid, good.password);
}

}  // namespace fireplace

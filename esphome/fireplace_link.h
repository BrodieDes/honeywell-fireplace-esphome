// Knob <-> hub link over ESP-NOW (shared by the hub firmware and the knob firmware).
//
// Keep this file identical in esphome/ and knob/firmware/*/ (the knob sketch keeps a copy).
//
// The knob is asleep almost all the time, so the knob always asks and the hub answers:
//
//   knob -> hub  GET   {knob_nonce, telemetry}            sent on every wake
//   hub  -> knob STATE {echo, hub_nonce, fire state}      reply to GET or SET
//   knob -> hub  SET   {hub_nonce, desired state}         only after a STATE
//
// Every message ends with a 16-byte HMAC-SHA256 (truncated) over the bytes before it,
// keyed with LINK_KEY from fireplace_link_key.h (private, not in the public repo).
// ESPHome's espnow component has no ESP-NOW encryption, so this is what stops other
// devices sending commands. Replay: a SET must carry the hub_nonce from a STATE the hub
// sent in the last 60 s, and each hub_nonce works once. No counters need to survive
// a reboot on either side.
#pragma once
#include <cstdint>
#include <climits>
#include <cstring>
#include <cmath>
#include "mbedtls/md.h"
#include "esp_random.h"
#include "fireplace_link_key.h"   // static const uint8_t LINK_KEY[32]

namespace fireplace_link {

static const uint8_t MAGIC0 = 'F', MAGIC1 = 'K', VERSION = 2;  // 2: STATE carries the thermostat's temperatures
static const uint8_t MSG_GET = 1, MSG_STATE = 2, MSG_SET = 3;
static const size_t MAC_LEN = 16;
static const uint32_t NONCE_TTL_MS = 60000;
static const int16_t TEMP_UNKNOWN = INT16_MIN;

// Who made the last change (STATE.last_source).
enum Source : uint8_t { SRC_UNKNOWN = 0, SRC_HA = 1, SRC_REMOTE = 2, SRC_MAX_RUNTIME = 3, SRC_BOOT = 4, SRC_KNOB = 5, SRC_RESEND = 6 };

#pragma pack(push, 1)
struct Header {
  uint8_t magic0, magic1, version, type;
};

struct Get {
  Header h;
  uint8_t knob_nonce[8];
  uint16_t battery_mv;     // 0 = unknown
  uint32_t wakes;          // since the knob's last power-on
  uint32_t awake_s;        // total seconds awake since the knob's last power-on
  uint16_t fw;             // knob firmware version, e.g. 0x0100
  uint8_t mac[MAC_LEN];
};

struct State {
  Header h;
  uint8_t echo[8];         // the GET's knob_nonce, or the SET's hub_nonce
  uint8_t hub_nonce[8];    // fresh: use it in the next SET
  uint8_t on;              // 1 = fire on
  uint8_t flame;           // 1..6
  uint8_t fan;             // 0..6
  uint8_t max_runtime_hh;  // max runtime in half hours (2..16)
  uint32_t seconds_left;   // until max runtime turns it off; 0 when off
  uint8_t last_source;     // Source
  int16_t target_c10;      // thermostat set point in 0.1 C; TEMP_UNKNOWN if not known
  int16_t current_c10;     // thermostat room temperature in 0.1 C; TEMP_UNKNOWN if not known
  uint8_t mac[MAC_LEN];
};

struct Set {
  Header h;
  uint8_t hub_nonce[8];
  uint8_t on;
  uint8_t flame;           // 1..6
  uint8_t fan;             // 0..6
  uint8_t max_runtime_hh;  // 0 = leave unchanged
  uint8_t mac[MAC_LEN];
};
#pragma pack(pop)

inline void compute_mac(const void *msg, size_t len_without_mac, uint8_t out[MAC_LEN]) {
  uint8_t full[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), LINK_KEY, sizeof(LINK_KEY),
                  static_cast<const uint8_t *>(msg), len_without_mac, full);
  memcpy(out, full, MAC_LEN);
}

// Fills the header and the trailing MAC. T is Get, State or Set.
template<typename T> inline void seal(T &m, uint8_t type) {
  m.h.magic0 = MAGIC0;
  m.h.magic1 = MAGIC1;
  m.h.version = VERSION;
  m.h.type = type;
  compute_mac(&m, sizeof(T) - MAC_LEN, m.mac);
}

// True if data is a well-formed, correctly signed message of type T.
template<typename T> inline bool open(const uint8_t *data, size_t size, uint8_t type, T &out) {
  if (size != sizeof(T))
    return false;
  memcpy(&out, data, sizeof(T));
  if (out.h.magic0 != MAGIC0 || out.h.magic1 != MAGIC1 || out.h.version != VERSION || out.h.type != type)
    return false;
  uint8_t expect[MAC_LEN];
  compute_mac(&out, sizeof(T) - MAC_LEN, expect);
  uint8_t diff = 0;  // constant time
  for (size_t i = 0; i < MAC_LEN; i++)
    diff |= expect[i] ^ out.mac[i];
  return diff == 0;
}

// Temperature in C (NaN if unknown) to the STATE encoding.
inline int16_t temp_c10(float c) {
  return std::isnan(c) || c < -100 || c > 100 ? TEMP_UNKNOWN : (int16_t) lroundf(c * 10);
}

inline uint8_t msg_type(const uint8_t *data, size_t size) {
  if (size < sizeof(Header) || data[0] != MAGIC0 || data[1] != MAGIC1 || data[2] != VERSION)
    return 0;
  return data[3];
}

// Knob battery: readings above this are the USB supply, not the battery.
static const uint16_t USB_MV = 4300;

// Rough state of charge (0-100) of the knob's 1-cell LiPo from its voltage, read
// before the radio starts. Lithium cells are flat through the middle, so expect
// +/-10%. Meaningless above USB_MV (on USB).
inline uint8_t battery_pct(uint16_t mv) {
  static const uint16_t curve[][2] = {
    {4150, 100}, {4050, 90}, {3970, 80}, {3900, 70}, {3850, 60}, {3800, 50},
    {3760, 40}, {3720, 30}, {3680, 20}, {3600, 10}, {3400, 0}};
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (mv >= curve[0][0]) return 100;
  if (mv <= curve[n - 1][0]) return 0;
  for (int i = 1; i < n; i++)
    if (mv >= curve[i][0])
      return curve[i][1] + (uint32_t) (mv - curve[i][0]) * (curve[i - 1][1] - curve[i][1]) /
                               (curve[i - 1][0] - curve[i][0]);
  return 0;
}

}  // namespace fireplace_link

// Fireplace knob: a battery wall remote (Waveshare ESP32-S3-Knob-Touch-LCD-1.8) that
// talks to the fireplace hub over ESP-NOW (fireplace_link.h). The screen is fire_ui.cpp.
//
// On every wake it sends GET and shows the hub's STATE: fire on/off, flame, fan, max
// runtime, time left and who made the last change.
//
// Controls:
//   - tap the screen: cycles Fire -> Flame -> Fan -> Fire. Every wake starts on Fire.
//   - turn the knob: changes the item shown. Fire: right (clockwise) = ON, left = OFF.
//     Flame: 1-6. Fan: off-6. Sent 1 s after the knob stops. Level changes while the
//     fire is off only set the next turn-on's levels (no RF is sent, like HA).
//   - the click or tap that wakes the knob only wakes it.
// After 10 s with no input it sleeps. Every 6 h it also wakes for a moment with the
// screen off, sends GET (so HA gets a battery reading even on unused days) and sleeps.
//
// Power: the radio is only on while talking to the hub, the backlight runs at about
// 2/3, and the touch chip is left in its auto-sleep (low-power scan) mode for sleep.
//
// fireplace_link.h and fireplace_link_key.h are copies of the ones in esphome/.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_config.h"
#include "fireplace_link.h"
#include "fire_ui.h"

using namespace fireplace_link;

#define FW_VERSION     0x0110
#define PIN_KNOB_CW    8
#define PIN_KNOB_CCW   7
#define PIN_TOUCH_INT  9
#define PIN_BATT_ADC   1
#define PIN_BACKLIGHT  EXAMPLE_PIN_NUM_BK_LIGHT
#define DRV2605_ADDR   0x5A
#define IDLE_SLEEP_MS  10000
#define POLL_MS        10000
#define HEARTBEAT_US   (6ULL * 3600 * 1000000)
#define BACKLIGHT_DUTY 170     // of 255
#define LOW_BATT_PCT   15
#define REPLY_WAIT_MS  60
#define SET_WAIT_MS    600    // turning on transmits RF before the hub replies
#define SEND_DELAY_MS  1000
#define TAP_MAX_MS     800

static const uint8_t HUB_MAC[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // your hub's Wi-Fi MAC (see README)

// Survive deep sleep (lost when the battery is disconnected).
RTC_DATA_ATTR static uint32_t wakes = 0;
RTC_DATA_ATTR static uint32_t awake_ms_total = 0;
RTC_DATA_ATTR static uint8_t hub_channel = 0;      // 0 = unknown, scan
RTC_DATA_ATTR static State last_state = {};
RTC_DATA_ATTR static bool have_state = false;
RTC_DATA_ATTR static uint32_t heartbeats = 0;

static uint16_t batt_now = 0;   // mV, read while the radio is off
static bool radio_up = false, wifi_inited = false, display_up = false;

// Link state, shared between the ESP-NOW callback and the main loop.
static volatile bool reply_ready = false;
static uint8_t reply_buf[sizeof(State)];
static uint8_t expect_echo[8];
static State state;            // latest verified STATE
static bool link_ok = false;
static uint32_t state_ms = 0;  // millis() when `state` arrived

static volatile int knob_steps = 0;
static volatile uint32_t last_input_ms = 0;
enum Mode { MODE_FIRE, MODE_FLAME, MODE_FAN };
static Mode mode = MODE_FIRE;
// Changes waiting to be sent (-1 = none).
static int pending_on = -1, pending_flame = -1, pending_fan = -1;
static uint32_t pending_since = 0;
static bool send_failed = false;


// ---------------------------------------------------------------- ESP-NOW

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (memcmp(info->src_addr, HUB_MAC, 6) != 0 || len != sizeof(State) || reply_ready)
    return;
  memcpy(reply_buf, data, len);
  reply_ready = true;
}

// Radio on only for an exchange; off again straight after (it draws ~70-100 mA).
static bool radio_on(void)
{
  if (radio_up) return true;
  if (!wifi_inited) {
    WiFi.mode(WIFI_STA);     // first time: init + start
    wifi_inited = true;
  } else if (esp_wifi_start() != ESP_OK) {
    return false;
  }
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_now_init() != ESP_OK)
    return false;
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, HUB_MAC, 6);
  peer.channel = 0;     // follow the radio's current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false; // messages are HMAC-signed instead (fireplace_link.h)
  radio_up = esp_now_add_peer(&peer) == ESP_OK;
  return radio_up;
}

static void radio_off(void)
{
  if (!radio_up) return;
  esp_now_deinit();
  esp_wifi_stop();
  radio_up = false;
}

// Sends msg and waits for a STATE echoing `echo`. True with `state` updated.
static bool exchange(const void *msg, size_t len, const uint8_t echo[8], uint32_t wait_ms = REPLY_WAIT_MS)
{
  memcpy(expect_echo, echo, 8);
  reply_ready = false;
  if (esp_now_send(HUB_MAC, (const uint8_t *) msg, len) != ESP_OK)
    return false;
  uint32_t t0 = millis();
  while (millis() - t0 < wait_ms) {
    if (reply_ready) {
      State st;
      if (open(reply_buf, sizeof(State), MSG_STATE, st) && memcmp(st.echo, expect_echo, 8) == 0) {
        state = st;
        state_ms = millis();
        last_state = st;
        have_state = true;
        return true;
      }
      reply_ready = false;  // not ours or bad signature: keep waiting
    }
    delay(1);
  }
  return false;
}

static uint16_t battery_mv(void)
{
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
  return (uint16_t) (mv / 8 * 2);   // 10k/10k divider
}

static bool send_get_on(uint8_t ch)
{
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  // A fresh nonce per attempt, so a late reply to the first attempt (whose hub token
  // the second reply replaces) is never mistaken for the answer to the second.
  for (int attempt = 0; attempt < 2; attempt++) {
    Get g = {};
    esp_fill_random(g.knob_nonce, 8);
    g.battery_mv = batt_now;
    g.wakes = wakes;
    g.awake_s = (awake_ms_total + millis()) / 1000;
    g.fw = FW_VERSION;
    seal(g, MSG_GET);
    if (exchange(&g, sizeof(g), g.knob_nonce))
      return true;
  }
  return false;
}

// GET on the cached channel, then scan 1-13 if the hub doesn't answer.
static bool fetch_state(void)
{
  if (hub_channel && send_get_on(hub_channel))
    return true;
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (ch == hub_channel) continue;
    if (send_get_on(ch)) {
      hub_channel = ch;
      return true;
    }
  }
  return false;
}

// Reads the battery (radio off, so no sag from it), then asks the hub for its state.
static bool poll_hub(void)
{
  batt_now = battery_mv();
  bool ok = radio_on() && fetch_state();
  radio_off();
  return ok;
}

// Sends the desired state using the token from the last STATE. Retries once with a
// fresh token if the hub says ours was stale.
static bool send_set(bool on, int flame, int fan)
{
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!link_ok || millis() - state_ms > NONCE_TTL_MS - 5000) {
      if (!(link_ok = fetch_state())) return false;
    }
    Set m = {};
    memcpy(m.hub_nonce, state.hub_nonce, 8);
    m.on = on;
    m.flame = flame;
    m.fan = fan;
    m.max_runtime_hh = 0;
    seal(m, MSG_SET);
    uint8_t echo[8];
    memcpy(echo, m.hub_nonce, 8);
    if (exchange(&m, sizeof(m), echo, SET_WAIT_MS) && state.on == on && state.flame == flame && state.fan == fan)
      return true;
    link_ok = false;  // force a fresh GET (new token) and try once more
  }
  return false;
}

// ---------------------------------------------------------------- UI

static const char *source_text(uint8_t s)
{
  switch (s) {
    case SRC_HA: return "Home Assistant";
    case SRC_REMOTE: return "remote";
    case SRC_MAX_RUNTIME: return "max runtime";
    case SRC_BOOT: return "hub restart";
    case SRC_KNOB: return "this knob";
    case SRC_RESEND: return "resend";
    default: return "?";
  }
}

static bool has_pending(void) { return pending_on >= 0 || pending_flame >= 0 || pending_fan >= 0; }

static void show_state(void)
{
  const State &s = have_state ? state : last_state;
  FireView v = {};
  v.have_state = have_state;
  v.on = pending_on >= 0 ? pending_on : s.on;
  v.flame = pending_flame >= 0 ? pending_flame : s.flame;
  v.fan = pending_fan >= 0 ? pending_fan : s.fan;
  v.mode = mode;
  v.max_runtime_hh = s.max_runtime_hh ? s.max_runtime_hh : 8;
  uint32_t left = s.seconds_left;
  if (s.on && link_ok) {
    uint32_t gone = (millis() - state_ms) / 1000;
    left = left > gone ? left - gone : 0;
  }
  // Turning on from the knob: show a full ring until the hub confirms.
  if (v.on && !s.on) left = (uint32_t) v.max_runtime_hh * 1800;
  v.seconds_left = left;
  if (!have_state) v.status = "Can't reach fireplace";
  else if (send_failed) v.status = "Not sent - turn again";
  else if (batt_now && batt_now <= USB_MV && battery_pct(batt_now) <= LOW_BATT_PCT) v.status = "Battery low - please charge";
  else if (!link_ok) v.status = "Offline - last known";
  if (!example_lvgl_lock(100)) return;
  fire_ui_show(v);
  example_lvgl_unlock();
}

// ---------------------------------------------------------------- input and sleep

static void knob_task(void *)
{
  // Start from the current levels, so a wake click still held low doesn't count.
  uint8_t prev_cw = digitalRead(PIN_KNOB_CW), prev_ccw = digitalRead(PIN_KNOB_CCW), cnt_cw = 0, cnt_ccw = 0;
  for (;;) {
    uint8_t cw = digitalRead(PIN_KNOB_CW), ccw = digitalRead(PIN_KNOB_CCW);
    if (cw == 0 && prev_cw == 1) { if (++cnt_cw >= 2) { knob_steps++; last_input_ms = millis(); prev_cw = 0; cnt_cw = 0; } }
    else if (cw == 1) { prev_cw = 1; cnt_cw = 0; }
    if (ccw == 0 && prev_ccw == 1) { if (++cnt_ccw >= 2) { knob_steps--; last_input_ms = millis(); prev_ccw = 0; cnt_ccw = 0; } }
    else if (ccw == 1) { prev_ccw = 1; cnt_ccw = 0; }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

static void go_to_sleep(void)
{
  awake_ms_total += millis();
  radio_off();
  if (wifi_inited) WiFi.mode(WIFI_OFF);
  if (display_up) {
    analogWrite(PIN_BACKLIGHT, 0);
    ledcDetach(PIN_BACKLIGHT);
    pinMode(PIN_BACKLIGHT, INPUT);   // 10k pull-down keeps the backlight FET off
    lcd_panel_sleep();
    uint8_t buf[2] = {0x01, 0x40};   // DRV2605 MODE = standby
    i2c_master_write_to_device(I2C_NUM_0, DRV2605_ADDR, buf, 2, pdMS_TO_TICKS(50));
    uint8_t ts[2] = {0xFE, 0x00};    // CST816 DisAutoSleep = 0: low-power scan, still wakes on touch
    i2c_master_write_to_device(I2C_NUM_0, EXAMPLE_TOUCH_ADDR, ts, 2, pdMS_TO_TICKS(50));
  }
  uint64_t mask = 0;
  const int pins[] = {PIN_KNOB_CW, PIN_KNOB_CCW, PIN_TOUCH_INT};
  for (int p : pins) {
    if (digitalRead(p)) mask |= 1ULL << p;
    rtc_gpio_pullup_dis((gpio_num_t) p);
    rtc_gpio_pulldown_dis((gpio_num_t) p);
  }
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(HEARTBEAT_US);
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIN_KNOB_CW, INPUT);
  pinMode(PIN_KNOB_CCW, INPUT);
  pinMode(PIN_TOUCH_INT, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);

  // 6-hourly check-in: report the battery with the screen off, then sleep again.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    heartbeats++;
    bool ok = poll_hub();
    Serial.printf("check-in #%lu: battery %u mV, hub %s\n", (unsigned long) heartbeats, batt_now, ok ? "ok" : "no reply");
    go_to_sleep();
  }
  wakes++;

  // Ask the hub first, so the answer is usually back before the screen is ready.
  uint32_t t0 = millis();
  link_ok = poll_hub();
  bool radio = true;
  uint32_t link_ms = millis() - t0;

  Touch_Init();
  lcd_lvgl_Init();
  display_up = true;
  analogWrite(PIN_BACKLIGHT, BACKLIGHT_DUTY);
  if (example_lvgl_lock(-1)) { fire_ui_build(); example_lvgl_unlock(); }
  show_state();

  Serial.printf("wake #%lu: radio=%d link=%d ch=%u in %lu ms; on=%u flame=%u fan=%u left=%lus src=%u\n",
                (unsigned long) wakes, radio, link_ok, hub_channel, (unsigned long) link_ms, state.on,
                state.flame, state.fan, (unsigned long) state.seconds_left, state.last_source);
  Serial.printf("knob MAC %s (put this in the hub's espnow peers)
", WiFi.macAddress().c_str());
  last_input_ms = millis();
  xTaskCreate(knob_task, "knob", 2048, NULL, 3, NULL);
}

void loop()
{
  static uint32_t last_ui = 0, last_poll = 0, last_touch_poll = 0, press_ms = 0;
  static bool pressed = false;
  // If a tap woke us, that finger is still down: ignore it until it lifts.
  static bool ignore_touch = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
                             (esp_sleep_get_ext1_wakeup_status() & (1ULL << PIN_TOUCH_INT));

  // Knob: change the item shown.
  int steps = knob_steps;
  if (steps && have_state) {
    knob_steps -= steps;
    const State &s = state;
    if (mode == MODE_FIRE)
      pending_on = steps > 0 ? 1 : 0;     // right = on, left = off
    else if (mode == MODE_FLAME)
      pending_flame = constrain((pending_flame >= 0 ? pending_flame : s.flame) + steps, 1, 6);
    else
      pending_fan = constrain((pending_fan >= 0 ? pending_fan : s.fan) + steps, 0, 6);
    // Drop "changes" that match what the hub already has.
    if (pending_on == s.on) pending_on = -1;
    if (pending_flame == s.flame) pending_flame = -1;
    if (pending_fan == s.fan) pending_fan = -1;
    pending_since = millis();
    send_failed = false;
    show_state();
  } else if (steps) {
    knob_steps -= steps;   // no state from the hub yet: nothing to change
  }
  if (has_pending() && millis() - pending_since > SEND_DELAY_MS) {
    bool on = pending_on >= 0 ? pending_on : state.on;
    int flame = pending_flame >= 0 ? pending_flame : state.flame;
    int fan = pending_fan >= 0 ? pending_fan : state.fan;
    bool ok = radio_on() && send_set(on, flame, fan);
    radio_off();
    send_failed = !ok;
    Serial.printf("SET on=%d flame=%d fan=%d: %s\n", on, flame, fan, ok ? "ok" : "failed");
    pending_on = pending_flame = pending_fan = -1;
    show_state();
  }

  // Screen: ask the touch chip directly every 30 ms (its INT line only pulses).
  if (millis() - last_touch_poll >= 30) {
    last_touch_poll = millis();
    uint16_t x, y;
    bool down = getTouch(&x, &y);
    if (down) last_input_ms = millis();
    if (down && !pressed) {
      pressed = true;
      press_ms = millis();
    } else if (!down && pressed) {
      pressed = false;
      if (ignore_touch) {
        ignore_touch = false;
      } else if (millis() - press_ms < TAP_MAX_MS) {
        mode = mode == MODE_FIRE ? MODE_FLAME : mode == MODE_FLAME ? MODE_FAN : MODE_FIRE;
        show_state();
      }
    }
  }

  // Refresh from the hub every 10 s while awake (shows HA changes live), and the
  // countdown every second.
  if (millis() - last_poll > POLL_MS && !has_pending()) {
    last_poll = millis();
    link_ok = poll_hub();
  }
  if (millis() - last_ui > 1000) {
    last_ui = millis();
    show_state();
  }

  if (millis() - last_input_ms > IDLE_SLEEP_MS && !has_pending())
    go_to_sleep();
  delay(5);
}

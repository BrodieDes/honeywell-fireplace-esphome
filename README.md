# Honeywell RT8220A fireplace remote for ESPHome

Control a gas fireplace that uses the **Honeywell RT8220A** remote from Home Assistant. An ESP32-S3 with a CC1101 radio copies the remote's 303.825 MHz signal.

- On/off, flame 1-6 and fan off/1-6 from Home Assistant.
- **The original remote keeps working.** The board listens to it and keeps HA in sync.
- **The remote's automatic resends don't undo HA.** See [The remote resends on its own](#the-remote-resends-on-its-own).
- **Safety runs on the board itself,** not just in HA:
  - It sends OFF every time it starts.
  - It turns the fire off after a set maximum runtime, even if Wi-Fi or HA is down.
- **Learn Remote:** the board learns your remote's ID with one button press in HA. No code changes, no radio captures.
- **Optional battery wall knob:** a Waveshare round touch-screen knob controls the fire over ESP-NOW and shows changes made in HA. See [Battery wall knob (optional)](#battery-wall-knob-optional).
- **Recovery without opening the case:**
  - A Wi-Fi setup hotspot.
  - A fallback if you type the wrong Wi-Fi password.
  - A factory reset by restarting it 5 times.

Built and tested on a Vermont Castings fireplace with an RT8220A remote and an RV8310 receiver/valve. Other fireplaces using the same remote family (Majestic, Monessen, Vermont Castings) may work. See [Compatibility](#compatibility).

> **This controls a gas appliance.** It only sends the same signals as the remote you already own; it doesn't touch the gas valve, wiring or pilot. Keep the original remote and the manual controls working, follow your fireplace's safety instructions, and never leave the fire on unattended. No warranty: use at your own risk.

## How it works

The RT8220A sends a fixed-code, Manchester-encoded OOK frame about 28 times over 1 second. Every press sends the **whole state**: on/off, flame level and fan level. It doesn't say which button was pressed. So the board doesn't need to replay recordings: it builds any of the 98 states from tables. The full decode is in [protocol-notes.md](protocol-notes.md).

The fireplace has no way to report back, which is also true of the original remote. HA shows what was last sent, or what the board last heard from the remote.

### The remote resends on its own

The RT8220A repeats its last state about every 10 minutes, and when you put batteries in, without any button being pressed. The receiver obeys it. So if the remote's display says OFF and HA turned the fire on, the next resend puts the fire out.

The board tells a resend from a real press: a resend is one burst with a single state, the same state the remote sent last. When a resend changes the fire, the board puts it back 5 seconds later, unless the remote is used again within that time. Last Command shows "Restored after remote resend". It relights at most 8 times an hour, and a relight doesn't restart Max Runtime.

To turn the fire off with the remote while its display already says OFF, cycle the mode button through ON and AUTO back to OFF. You'll see the fire go out briefly and relight if a single press only repeats OFF.

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3 dev board | Built with a Freenove ESP32-S3 WROOM (camera module removed). Any ESP32-S3 should work if you change the pins. |
| CC1101 module | Ebyte E07-M1101D. The common 433 MHz version worked fine tuned down to 303.825 MHz at room range. |
| Antenna | SMA antenna on the CC1101 |
| Perfboard | 17 × 20 holes, 2.54 mm pitch, if you use the case below |
| Power | USB-C 5 V |

### Wiring

| CC1101 | ESP32-S3 |
|---|---|
| VCC | 3V3 (**never 5 V**) |
| GND | GND |
| SCK | GPIO12 |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| CSN | GPIO10 |
| GDO0 | GPIO14 (transmit data) |
| GDO2 | GPIO21 (receive data) |

### Case

A printable snap-fit case for the perfboard build is in [case/](case/):
- an OpenSCAD source, with every dimension as a parameter
- STLs
- a Bambu Studio 3MF

[case/PRINT-NOTES.md](case/PRINT-NOTES.md) has the settings. Print it in PETG or ASA, not PLA, because it may sit near the fireplace.

## Install

1. In the `esphome/` folder, copy `secrets.example.yaml` to `secrets.yaml` and fill it in:
   - your Wi-Fi details
   - a new API encryption key (`openssl rand -base64 32`)
   - an OTA password
2. Make the knob link key, even if you have no knob. The firmware needs the file to build:

   ```bash
   python tools/make_link_key.py
   ```

   This writes `esphome/fireplace_link_key.h` and a copy for the knob sketch. Keep it private, like `secrets.yaml`.
3. Flash over USB from the ESPHome dashboard, or from the command line, inside `esphome/`:

   ```bash
   esphome run fireplace-rf.yaml
   ```

   On the Freenove board, use the port marked **USB UART**.
4. Add the device in Home Assistant (**Settings → Devices & Services → ESPHome**) and give it the API key.
5. **Teach it your remote:**
   1. Press **Learn Remote** in HA.
   2. Within 60 seconds, press **OFF** on your remote, near the board.
   3. **Last Command** should show "Learn: remote XXXXXXX saved", and **Remote ID** shows your remote's ID.

   Until a remote is learned, the board transmits nothing.

## Home Assistant entities

| Entity | What it does |
|---|---|
| Fireplace (switch) | On/off. Off always transmits, even if HA already thinks it's off. |
| Flame Level, Fan Level | Applied straight away while on. While off, they set the level for the next turn-on. |
| Max Runtime | Hours before the board turns the fire off by itself (1-8, default 4) |
| Last Command | What was sent last and why: HA, Remote, Max runtime, Boot, Resend, Restored after remote resend, or the Learn result |
| Resend State | Sends the current state again, for when the fireplace and HA disagree |
| Learn Remote, Remote ID | Teach the board a remote, and see which one it copies |
| Knob Battery, Knob Battery Level, Knob On USB, Knob Signal | Only with the [battery knob](#battery-wall-knob-optional): its battery voltage and rough %, whether it last reported from USB, and its radio signal |
| Firmware Version, WiFi Signal, Uptime, Restart | Diagnostics |

For extra safety, add HA automations on top, for example "off at bedtime" and "off when everyone leaves".

## Battery wall knob (optional)

A [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) on its 800 mAh battery makes a wall remote. It shows the fireplace with animated flames and a ring for the runtime left.
- **Tap** the screen to switch between Fire, Flame and Fan.
- **Turn** the knob to change what's shown. In Fire mode, right turns it on and left turns it off. Flame goes 1-6, Fan goes off/1-6.
- **Sending:** a change is sent 1 second after the knob stops turning. It goes through the same code as HA, so Max Runtime and every other safety rule still apply.
- **Sleep:** it sleeps 10 seconds after the last touch or turn, and wakes on a tap or a knob click. On every wake it asks the board for the current state, so changes made in HA or with the original remote show up.
- **Check-in:** every 6 hours it wakes briefly with the screen off and reports its battery to HA.

It talks to the board over **ESP-NOW**, not Wi-Fi, so it doesn't have to join your network on every wake. ESPHome's ESP-NOW has no encryption, so every message is signed with HMAC-SHA256 using the key from `tools/make_link_key.py`. A command is only accepted with a one-time token from the board that's less than 60 seconds old, so recorded messages can't be replayed. The full protocol is in `esphome/fireplace_link.h`.

### Setting up the knob

1. **Board side:** put the knob's Wi-Fi MAC in `esphome/fireplace-rf.yaml` under `espnow: peers:`. The knob prints it on USB serial at 115200 baud ("knob MAC ..."), so flash the knob once first to read it.
2. **Knob side:** put the board's Wi-Fi MAC in `HUB_MAC` at the top of `knob/fireplace_knob/fireplace_knob.ino`. HA shows it on the ESPHome device page, and it's in the board's log at start-up.
3. **Key:** check that `knob/fireplace_knob/fireplace_link_key.h` is the same file as `esphome/fireplace_link_key.h`. `make_link_key.py` writes both.
4. **Build and flash** with Arduino-ESP32 3.3 or newer:
   1. Install LVGL **8.4** and copy `knob/lv_conf.h` next to the `lvgl` library folder.
   2. Build and flash with arduino-cli:

   ```bash
   arduino-cli compile --upload -p <port>      --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc"      knob/fireplace_knob
   ```

   The USB-C port connects to a different chip depending on which way the plug goes in. Use the way round that shows up as a **USB Serial Device**, not CH340. The knob's USB port disappears while it sleeps, so tap it just before flashing.

### Knob battery life

Expect roughly 2-3 months per charge with a few uses a day. That's an estimate, not a measurement. Most of the drain is in sleep, from parts of the board that are always powered. The biggest single one is the battery-reading divider (R62/R63, 10 kΩ + 10 kΩ across the battery, about 0.2 mA). Replacing both with 1 MΩ should cut that to about 2 µA. That hasn't been tried yet, so it's untested. The firmware already does what it can:
- deep sleep with only the knob and touch pins able to wake it;
- the radio on only while talking to the board;
- the display and the vibration driver put to sleep;
- the touch chip left in its low-power mode;
- the board's second chip (an ESP32-U4WDH) kept asleep.

The second chip needs its own one-time sleep firmware: flash `knob/u4wdh_sleep` to it, with the USB-C plug turned the other way round (the CH340 port), at 115200 baud, FQBN `esp32:esp32:esp32:FlashSize=4M,UploadSpeed=115200`.

`fire_ui.cpp` draws the screen. The display, touch and LVGL glue (`lcd_bsp.*`, `cst816.*`, `lcd_config.h`) are adapted from Waveshare's demo for this board. `esp_lcd_sh8601.*` is Espressif's driver (Apache-2.0).

## Recovery

- **New Wi-Fi network or password.**
  1. If the board can't join Wi-Fi for 1 minute, it starts an open hotspot called **"Fireplace-RF Setup"**.
  2. Join it from a phone and enter the new network on the page that opens (or go to `192.168.4.1`).
  3. The network has to be 2.4 GHz.
  4. If the new details don't connect within 3 minutes, the board goes back to the last ones that worked.
- **Factory reset.** Restart the board 5 times, each within 10 seconds of the last, with the RESET button or by unplugging it. This wipes the saved Wi-Fi, the learned remote, Max Runtime and the levels.
- **Opening the USB serial port restarts the board** and counts toward the factory reset, so don't open it 5 times in a row.

## Compatibility

Only one remote has been tested. The board learns any remote's ID, but whether the **flame and fan codes** are the same on every RT8220A is unknown. If Learn Remote reports **"its codes differ"**, that's the answer for your remote. Please [open an issue](../../issues) with your fireplace and remote model. The firmware log line "sent unknown state ..." is the most useful part.

Reports of it working (or not) on other fireplaces are welcome too.

## Radio note

The board sends the same 303.825 MHz signal as the remote you already own, at low power, only when you command it. Short-range devices like this fall under rules such as FCC Part 15 and ISED RSS-210. This is a personal project, not a certified product.

## Files

- `esphome/fireplace-rf.yaml`: the ESPHome firmware.
- `esphome/fireplace_codes.h`: frame builder, decoder and learner. Generated by `tools/gen_codes.py --public` from captures, which aren't published because they hold the original remote's ID.
- `esphome/wifi_fallback.h`: goes back to the last working Wi-Fi after a bad setup-page entry.
- `esphome/remote_rx.h`: tells the remote's automatic resends from real presses.
- `esphome/fireplace_link.h`: the knob <-> board ESP-NOW protocol (shared by both firmwares).
- `knob/fireplace_knob/`: the battery knob's firmware (Arduino-ESP32 + LVGL 8.4), and `knob/lv_conf.h`.
- `tools/make_link_key.py`: makes the private knob link key.
- `tools/send.py`: send a state from a PC through the ESPHome API.
- `tools/decode.py`, `tools/gen_codes.py`: the protocol tools used to decode the remote.

## License

MIT. See [LICENSE](LICENSE).

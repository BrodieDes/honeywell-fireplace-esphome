# Honeywell RT8220A fireplace remote for ESPHome

Control a gas fireplace that uses the **Honeywell RT8220A** remote from Home Assistant. An ESP32-S3 with a CC1101 radio copies the remote's 303.825 MHz signal.

- On/off, flame 1-6 and fan off/1-6 from Home Assistant.
- **The original remote keeps working.** The board listens to it and keeps HA in sync.
- **Safety runs on the board itself,** not just in HA:
  - It sends OFF every time it starts.
  - It turns the fire off after a set maximum runtime, even if Wi-Fi or HA is down.
- **Learn Remote:** the board learns your remote's ID with one button press in HA. No code changes, no radio captures.
- **Recovery without opening the case:**
  - A Wi-Fi setup hotspot.
  - A fallback if you type the wrong Wi-Fi password.
  - A factory reset by restarting it 5 times.

Built and tested on a Vermont Castings fireplace with an RT8220A remote and an RV8310 receiver/valve. Other fireplaces using the same remote family (Majestic, Monessen, Vermont Castings) may work. See [Compatibility](#compatibility).

> **This controls a gas appliance.** It only sends the same signals as the remote you already own; it doesn't touch the gas valve, wiring or pilot. Keep the original remote and the manual controls working, follow your fireplace's safety instructions, and never leave the fire on unattended. No warranty: use at your own risk.

## How it works

The RT8220A sends a fixed-code, Manchester-encoded OOK frame about 28 times over 1 second. Every press sends the **whole state**: on/off, flame level and fan level. It doesn't say which button was pressed. So the board doesn't need to replay recordings: it builds any of the 98 states from tables. The full decode is in [protocol-notes.md](protocol-notes.md).

The fireplace has no way to report back, which is also true of the original remote. HA shows what was last sent, or what the board last heard from the remote.

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
2. Flash over USB from the ESPHome dashboard, or from the command line, inside `esphome/`:

   ```bash
   esphome run fireplace-rf.yaml
   ```

   On the Freenove board, use the port marked **USB UART**.
3. Add the device in Home Assistant (**Settings → Devices & Services → ESPHome**) and give it the API key.
4. **Teach it your remote:**
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
| Last Command | What was sent last and why: HA, Remote, Max runtime, Boot, Resend, or the Learn result |
| Resend State | Sends the current state again, for when the fireplace and HA disagree |
| Learn Remote, Remote ID | Teach the board a remote, and see which one it copies |
| Firmware Version, WiFi Signal, Uptime, Restart | Diagnostics |

For extra safety, add HA automations on top, for example "off at bedtime" and "off when everyone leaves".

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
- `tools/send.py`: send a state from a PC through the ESPHome API.
- `tools/decode.py`, `tools/gen_codes.py`: the protocol tools used to decode the remote.

## License

MIT. See [LICENSE](LICENSE).

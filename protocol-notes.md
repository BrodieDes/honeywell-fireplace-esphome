# RT8220A Protocol Notes

Decoded 2026-10-30 from captures of one RT8220A remote (Vermont Castings fireplace, RV8310 valve) with `tools/decode.py`. The captures aren't published: they contain that remote's ID.

## Physical layer
- 303.825 MHz, OOK. Received cleanly at 0 dB attenuation / 8 dB OOK threshold (12 dB attenuation is too deaf).
- Manchester, half-bit about 210 us (bit about 420 us, about 2.4 kbit/s). The receiver reads highs about 45 us short and lows about 45 us long.
- One press transmits the same 170 half-bit (85-bit) frame back to back for about 1 s.

## Frame (cyclic)
`preamble (alternating run, decodes as 1s)` + `SYNC` + `data`

- SYNC (half-bits, deliberate Manchester violations): `110010011011001100100110`
- Data, after SYNC (Manchester `10`=1, `01`=0):
  - bits 0-27: transmitter ID, fixed per remote (the board learns it with Learn Remote)
  - bits 28-47: state field (20 bits), below
  - rest (the 25-bit "tail"): constant for a given remote, then the preamble. Learn Remote stores it with the ID.

## Behaviour
- **Fixed code.** The same state always produces the same frame (Test 3 passed).
- **Every key press transmits the full current state.** The frame does not say which key was pressed. FLAME, FAN and TIMER presses resend the unchanged state.
- **Not transmitted:** the timer (the handset counts down itself) and AUTO mode. In AUTO, the handset sends ordinary flame/fan states as its thermostat decides; it sent nothing in 2 min while satisfied.
- **One-way.** No valve transmissions seen (pilot was off; recheck during replay).

## Replay (Test 4), 2026-10-30
- Passed. Frames built by `esphome/fireplace_codes.h` and sent from the ESP32: Flame 1 lit the burner, OFF turned it off. Flame 3 was sent but the level change was not confirmed. Flame 6 / fan 3, never sent by the remote and built by XOR, worked exactly, so all 49 states are usable.
- Two OFF attempts failed before any RF was sent: the ESPHome API connection dropped during the handshake (Wi-Fi at -80 dB). The production build needs a reliable link and an on-device max-runtime OFF that does not depend on HA.

## State field (bits 28-47)
| State | Bits |
|---|---|
| OFF | `00111001110110101011` |
| ON flame 0, fan 0 (also sent in AUTO when satisfied) | `00111101010110111001` |
| flame 1 / fan 0 | `10111101011110111001` |
| flame 2 / fan 0 | `00011101010100111001` |
| flame 3 / fan 0 | `10011101011100111001` |
| flame 4 / fan 0 | `01111101010011111001` |
| flame 5 / fan 0 | `11111101011011111001` |
| flame 6 / fan 0 | `01011101010001111001` |
| flame 0 / fan 1 | `00111000011110100111` |
| flame 0 / fan 2 | `00111100011110110111` |
| flame 0 / fan 3 | `00111011011110100011` |
| flame 0 / fan 4 | `00111111011110110011` |
| flame 0 / fan 5 | `00111010011110101111` |
| flame 0 / fan 6 | `00111110011110111111` |

Flame 0-6 and fan 0-6. Up and down presses give the same frame for the same state.

**The encoding is linear (XOR-separable):**
`state(f, m) = flame(f, fan 0) XOR fan(flame 0, m) XOR state(0, 0)`.
Verified against the captured "flame 6, fan 1" frame, which matches the prediction bit for bit. All 49 flame/fan combinations can be built from the 13 captured frames, without decoding the checksum.

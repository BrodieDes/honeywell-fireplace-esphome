"""Create the private key for the knob <-> hub ESP-NOW link (fireplace_link_key.h).

Writes the same random 32-byte key to every path given (default: esphome/ and every knob
sketch folder that has fireplace_link.h). Both firmwares must be built with the same key.
Never commit or publish the file. Refuses to overwrite an existing key unless --force is
given, because replacing it means re-flashing both the hub and the knob.

Usage:  python tools/make_link_key.py [--force] [path ...]
"""
import secrets
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    args = [a for a in sys.argv[1:] if a != "--force"]
    force = "--force" in sys.argv[1:]
    if args:
        targets = [Path(a) for a in args]
    else:
        targets = [ROOT / "esphome" / "fireplace_link_key.h"]
        targets += [p.parent / "fireplace_link_key.h" for p in (ROOT / "knob").rglob("fireplace_link.h")]
    existing = [t for t in targets if t.exists()]
    if existing and not force:
        sys.exit("Key already exists (use --force to replace it, then re-flash hub and knob):\n  "
                 + "\n  ".join(map(str, existing)))

    key = secrets.token_bytes(32)
    rows = ",\n".join("  " + ", ".join(f"0x{b:02X}" for b in key[i:i + 8]) for i in range(0, 32, 8))
    text = ("// PRIVATE: shared key for the knob <-> hub ESP-NOW link (fireplace_link.h).\n"
            "// Made by tools/make_link_key.py. Never publish. Keep identical copies in esphome/\n"
            "// and the knob sketch folder.\n"
            "#pragma once\n#include <cstdint>\n"
            f"static const uint8_t LINK_KEY[32] = {{\n{rows}\n}};\n")
    for t in targets:
        t.parent.mkdir(parents=True, exist_ok=True)
        t.write_text(text, encoding="utf-8")
        print(f"wrote {t}")


if __name__ == "__main__":
    main()

"""Send a fireplace state through the fireplace-rf set_fire action, retrying the API connection.

Usage:  python tools/send.py off [host]
        python tools/send.py <flame 0-6> <fan 0-6> [host]
"""
import asyncio
import inspect
import re
import sys
from pathlib import Path

from aioesphomeapi import APIClient, LogLevel

ANSI = re.compile(r"\x1b\[[0-9;]*m")
ATTEMPTS = 5
SECRETS = Path(__file__).resolve().parent.parent / "esphome" / "secrets.yaml"


def api_key() -> str:
    """The API encryption key, read from secrets.yaml at run time."""
    m = re.search(r'^api_key:\s*"?([^"\s]+)', SECRETS.read_text(), re.M)
    if not m:
        sys.exit("No api_key in esphome/secrets.yaml")
    return m.group(1)


if sys.argv[1].lower() == "off":
    STATE = {"is_off": True, "flame": 0, "fan": 0}
    HOST = sys.argv[2] if len(sys.argv) > 2 else "fireplace-rf.local"
else:
    STATE = {"is_off": False, "flame": int(sys.argv[1]), "fan": int(sys.argv[2])}
    HOST = sys.argv[3] if len(sys.argv) > 3 else "fireplace-rf.local"


async def send_once() -> bool:
    cli = APIClient(HOST, 6053, None, noise_psk=api_key())
    await cli.connect(login=True)
    try:
        _, services = await cli.list_entities_services()
        service = next(s for s in services if s.name == "set_fire")
        sent = asyncio.Event()

        def on_log(msg) -> None:
            line = ANSI.sub("", msg.message.decode(errors="replace"))
            if "TX state" in line:
                print(line, flush=True)
                sent.set()

        cli.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO)
        await asyncio.sleep(0.5)
        result = cli.execute_service(service, STATE)
        if inspect.isawaitable(result):
            await result
        await asyncio.wait_for(sent.wait(), 5)
        await asyncio.sleep(4)  # let both ~1 s transmissions (2 s apart) finish
        return True
    finally:
        await cli.disconnect()


async def main() -> int:
    for attempt in range(1, ATTEMPTS + 1):
        try:
            if await send_once():
                return 0
        except Exception as exc:  # flaky Wi-Fi: the handshake sometimes drops
            print(f"Attempt {attempt} failed: {exc}", flush=True)
            await asyncio.sleep(1)
    print("Gave up: nothing was sent.")
    return 1


sys.exit(asyncio.run(main()))

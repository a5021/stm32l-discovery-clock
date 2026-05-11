"""
NTP time synchronisation for STM32L152RB clock.

Queries an NTP server (pool.ntp.org by default) for UTC time,
then writes "seconds since midnight" to the mailbox at 0x20000000
via ST-LINK_CLI.exe. The firmware picks up the value and updates
the RTC.

Usage:
    python ntp_time.py
    python ntp_time.py --server time.google.com
"""

import argparse
import struct
import socket
import subprocess
import sys
from datetime import datetime, timezone

NTP_PORT = 123
NTP_EPOCH_OFFSET = 2208988800  # Seconds between NTP epoch (1900-01-01) and UNIX epoch (1970-01-01)
TIMEOUT = 5                     # NTP query timeout in seconds
RAM_ADDRESS = "0x20000000"
STLINK_CLI = "ST-LINK_CLI.exe"


def ntp_timestamp(server: str = "pool.ntp.org") -> float:
    """Fetch current UTC time from an NTP server, return UNIX timestamp."""
    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.settimeout(TIMEOUT)

    # NTP request packet (RFC 4330 – SNTPv4)
    data = b'\x1b' + 47 * b'\x00'

    client.sendto(data, (server, NTP_PORT))
    resp, _ = client.recvfrom(1024)
    client.close()

    if len(resp) < 48:
        raise RuntimeError(f"Short NTP response: {len(resp)} bytes")

    # Transmit timestamp is in bytes 40–43 (integer part)
    t_int = struct.unpack_from("!I", resp, 40)[0]
    # Byte 44–47 (fractional part), not used here
    return float(t_int - NTP_EPOCH_OFFSET)


def seconds_since_midnight(dt: datetime) -> int:
    """Return seconds since midnight for *dt*."""
    return dt.hour * 3600 + dt.minute * 60 + dt.second


def write_ram(addr: str, value: str) -> None:
    """Write *value* to *addr* via ST-LINK_CLI.exe.

    ST-LINK_CLI may print warnings and return a non-zero exit code
    even when the write succeeds. Both stdout and stderr are
    discarded, and the exit code is ignored.
    """
    cmd = [STLINK_CLI, "-c", "SWD", "HOTPLUG", "-w32", addr, value]
    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print(f"Error: '{STLINK_CLI}' not found. Install ST-Link tools or adjust the path.")
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Sync clock board time via NTP")
    parser.add_argument("--server", default="pool.ntp.org",
                        help="NTP server address (default: pool.ntp.org)")
    args = parser.parse_args()

    print(f"Querying NTP server {args.server} ...", end=" ", flush=True)
    try:
        ts = ntp_timestamp(args.server)
    except Exception as e:
        print(f"failed: {e}")
        sys.exit(1)

    dt = datetime.fromtimestamp(ts, tz=timezone.utc)
    secs = seconds_since_midnight(dt)
    hex_val = f"0x{secs:08X}"

    print(f"OK")
    print(f"UTC time:  {dt.hour:02d}:{dt.minute:02d}:{dt.second:02d} ({secs} sec)")
    print(f"Writing {hex_val} to {RAM_ADDRESS} ...", end=" ", flush=True)

    write_ram(RAM_ADDRESS, hex_val)
    print("done")


if __name__ == "__main__":
    main()

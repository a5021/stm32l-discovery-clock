import time
import subprocess

def get_seconds_since_midnight():
    """Return the number of seconds elapsed since midnight."""
    t = time.localtime()
    return t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec

def main():
    # 1. Get current time (seconds since midnight)
    seconds_today = get_seconds_since_midnight()
    t = time.localtime()

    # 2. Fixed RAM address
    RAM_ADDRESS = "0x20000000"

    # 3. Format the value as a 32‑bit hexadecimal string
    hex_value = f"0x{seconds_today:08X}"

    print(f"Current time: {t.tm_hour:02d}:{t.tm_min:02d}:{t.tm_sec:02d} ({seconds_today} sec)")
    print(f"Sending {hex_value} to address {RAM_ADDRESS}...")

    # 4. ST‑Link CLI command (adjust the path if necessary)
    cli_path = "ST-LINK_CLI.exe"          # Make sure it's in PATH or use full path
    cmd = [cli_path, "-c", "SWD", "HOTPLUG", "-w32", RAM_ADDRESS, hex_value]

    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("✅ Time synchronized successfully!")
    except FileNotFoundError:
        print(f"❌ Utility not found at: {cli_path}")

if __name__ == "__main__":
    main()

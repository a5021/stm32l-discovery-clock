# STM32L152RB Clock (MB963B Discovery Board)

Firmware for a real-time clock using the onboard LCD and RTC of the
STM32L152RB Discovery kit (MB963 B-0).

## Features

- **HH:MM:SS display** on the 6-digit segment LCD (glass `MB963B`)
- **Blinking colons** between hour/minute and minute/second, running
  at 0.5 Hz (500 ms on, 500 ms off)
- **RTC** driven by the 32.768 kHz LSE crystal for accurate timekeeping
- **Time setup** – long-press the USER button to enter adjustment mode;
  short-press increments the selected field, long-press moves to the
  next field (HH → MM → SS → exit)
- **PC time sync** via an ST-Link RAM mailbox – a host script writes
  "seconds since midnight" to `0x20000000`; the firmware reads it and
  updates the RTC

## Hardware

| Component | Description |
|-----------|-------------|
| **MCU**   | STM32L152RB (Cortex‑M3, 128 KB flash, 16 KB SRAM) |
| **Board** | STM32L-Discovery (MB963 B-0) |
| **LCD**   | Integrated 6‑digit segment glass, 1/4 duty, 1/3 bias |
| **Clock** | LSE 32.768 kHz for LCD and RTC, HSI 16 MHz → MSI ~2.1 MHz for CPU |
| **Button**| USER (PA0, active-high, external pull-down to GND) |
| **Pinout**| LCD segments on PA0–PA15, PB0–PB15, PC0–PC15 (AF11) |

## Project structure

```
├── main.c                  # Firmware source
├── system_stm32l1xx.c       # System init (clock, vector table)
├── startup_stm32l152xb.s    # CMSIS startup (reset vector, exceptions)
├── stm32l152xb.ld           # Linker script
├── Makefile                 # Build & flash
├── sync_time.py             # PC sync script (ST-Link mailbox)
├── inc/
│   ├── stm32l152xb.h        # MCU register definitions
│   ├── stm32l1xx.h          # STM32L1xx header
│   ├── system_stm32l1xx.h   # System clock header
│   ├── core_cm3.h           # CMSIS Cortex‑M3 core
│   └── m-profile/           # CMSIS‑ARM headers
└── .gitignore
```

## Build & flash

### Prerequisites

- **ARM GCC toolchain** (`arm-none-eabi-gcc`, `arm-none-eabi-objcopy`)
- **st-link tools** (`st-flash`) from [stlink](https://github.com/stlink-org/stlink)

### Commands

```sh
make          # Build (produces stm32l152xb.elf + stm32l152xb.bin)
make flash    # Build & flash to the board via ST-Link
make clean    # Remove build artifacts
```

## Setting the time

### Option A – Button (no PC needed)

1. Hold the USER button for ≈1 s to enter setup mode
2. **Short-press** to increment the blinking field
3. **Long-press** to confirm the current field and move to the next
4. After the last field (seconds) the RTC is updated and the clock
   resumes normal operation

### Option B – PC sync (more precise)

The firmware reserves `0x20000000` as a mailbox. When the value differs
from `0xFFFFFFFF` the firmware interprets it as seconds since midnight
and updates the RTC.

The `sync_time.py` script automates this:

```sh
python sync_time.py
```

It computes seconds since midnight on the host and writes the value to
the mailbox via `ST-LINK_CLI.exe`.

**Note:** Adjust `cli_path` in the script if `ST-LINK_CLI.exe` is not in
your `PATH`.

## Technical details

### LCD mapping

The MB963B glass uses 4 commons (COM0–COM3) and up to 28 segment lines.
Each digit position occupies 4 logical segments. The colon bars (BAR1
and BAR3) are on COM1, SEG1 and COM1, SEG7 – these share the same
physical pins as the C‑segment of the hour digits. This means the colon
dots cannot be lit independently of those digit segments; the firmware
preserves the colon bits during digit writes.

### RTC

- **Prescalers:** async 127 + sync 255 → 1 Hz from 32.768 kHz LSE
- **Format:** 24‑hour, BCD
- **Init:** defaults to `00:00:00`

### PC sync mailbox

| Address      | Width | Description |
|--------------|-------|-------------|
| `0x20000000` | 32‑bit | Seconds since midnight, or `0xFFFFFFFF` ⇔ empty |

The firmware polls this address in the main loop. After reading a valid
value it immediately marks the mailbox as empty (`0xFFFFFFFF`) to avoid
re-processing.

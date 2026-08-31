# grblHAL driver for WCH CH32V317 (PickOMatic)

grblHAL driver for the WCH CH32V317 (RISC-V QingKe V4F @ 144 MHz), targeting
the [PickOMatic V2](https://github.com/rmingon/PickOMatic-V2) pick-and-place
motion controller:

- 7 stepper channels (onboard DRV8825, STEP/DIR only; current and
  microstepping are set in hardware, not in software)
- 3 servo outputs (50 Hz PWM)
- 2 relays (vacuum pump, lighting)
- 5 endstop inputs (external interrupt capable)
- Ethernet via the CH32V317 MAC (phase 2, stubbed for now)

## Status

Work in progress. Current state: repo skeleton, the build produces an
empty-but-linking ELF. The grblHAL driver itself (driver.c, serial, NVS,
board map) comes next.

## Layout

| Path | Content |
|------|---------|
| `grbl/` | grblHAL core (git submodule, [grblHAL/core](https://github.com/grblHAL/core)) |
| `lib/Peripheral/` | WCH CH32V30x standard peripheral driver (vendored from the WCH EVT) |
| `lib/Core/` | QingKe V4 core support (core_riscv) |
| `lib/Startup/` | startup_ch32v30x_D8C.S (vector table, reset handler) |
| `lib/Debug/` | WCH debug helpers (delay, debug printf) |
| `src/` | driver sources (main, system clock, interrupt handlers, later driver.c and friends) |
| `boards/` | board pin maps (added with the driver) |
| `ld/Link.ld` | linker script, FLASH 256K / RAM 64K |

## Building

### Makefile (command line / CI)

```sh
git clone --recurse-submodules https://github.com/rmingon/grblHAL-CH32V31x
cd grblHAL-CH32V31x
make
```

Output goes to `build/` (`.elf`, `.hex`, `.bin`, map file). `make info` shows
which toolchain was picked, `make lst` produces a disassembly listing.

The Makefile looks for a RISC-V toolchain in this order:

1. `CROSS=<prefix>` on the command line
2. `riscv-none-elf-gcc` in `PATH`
3. `riscv-wch-elf-gcc` from MounRiver Studio 2 (the macOS bundle path is
   searched automatically; on other systems add the toolchain `bin/` to
   `PATH` or pass `MRS2_DARWIN_BIN=`)
4. `riscv32-wch-elf-gcc` (MRS2 GCC15) or `riscv-none-embed-gcc` (legacy)

### MounRiver Studio 2

Open the repository root as an MRS2 project (`grblHAL-CH32V31x.wvproj` /
`.project`). The managed build writes to `obj/`. Flash and debug via
WCH-Link using the bundled launch configuration.

## Flash/RAM configuration

The CH32V317 (CH32V30x_D8C family) splits its flash and SRAM via user
option bytes. This project is linked for **FLASH 256K / RAM 64K**; program
the option bytes to match (MounRiver Studio or WCH-LinkUtility, "RAM & ROM"
setting), otherwise the upper 32K of RAM used by the firmware will not
exist. Factory default on most parts is 288K/32K.

## License

GPLv3, like grblHAL itself. See [COPYING](COPYING). Files under `lib/` are
vendored from the WCH EVT and keep their original WCH license headers.

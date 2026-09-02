# grblHAL driver for WCH CH32V317

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](COPYING)
[![Target](https://img.shields.io/badge/MCU-CH32V317VCT6-2b4b8c.svg)](https://www.wch-ic.com/)
[![Core](https://img.shields.io/badge/core-grblHAL-success.svg)](https://github.com/grblHAL/core)

A [grblHAL](https://github.com/grblHAL) driver for the WCH **CH32V317**
(RISC-V QingKe V4F, 144 MHz), written for the
[PickOMatic V2](https://github.com/rmingon/PickOMatic-V2) pick-and-place
motion controller.

> **Status: work in progress.** The grblHAL core builds and links with a
> working driver (steppers, limits, relay outputs). Serial stream,
> settings storage and interrupt priority tuning are still to come.
> Not ready for production use.

---

## Target hardware

The PickOMatic V2 controller board:

| Subsystem | Details |
|-----------|---------|
| MCU | WCH CH32V317VCT6, RISC-V QingKe V4F @ 144 MHz, hardware FPU |
| Steppers | 7 channels, onboard DRV8825, STEP/DIR only |
| Servos | 3 outputs, 50 Hz PWM |
| Relays | 2 (vacuum pump, lighting) |
| Endstops | 5 inputs, external interrupt capable |
| Networking | Ethernet via the CH32V317 MAC (planned, phase 2) |

Stepper current (VREF trimpot) and microstepping (jumpers) are set in
hardware; the firmware intentionally provides no software control of either.

## Roadmap

- [x] Repository skeleton, reproducible cross-build, linking ELF
- [x] `driver.c` / `driver.h`: grblHAL driver contract (stepper, limits, control inputs, coolant, spindle stub)
- [x] Timer and PFIC priority setup, nested interrupts with the stepper ISR at highest preemption level, servo PWM (TIM4) as aux analog outputs
- [ ] `serial.c`: USART stream via `stream_connect()`, USB CDC if cheap
- [ ] `nvs.c`: settings storage in flash (grblHAL NVS API)
- [ ] `boards/pickomatic_map.h`: pin map
- [ ] `my_machine.h`: board selection and build options
- [ ] Ethernet / networking (phase 2)

## Interrupts and timers

| Resource | Use | PFIC preemption level |
|----------|-----|-----------------------|
| TIM2 | Main stepper interrupt, 16-bit, 18 MHz base, 3 prescaler banks | 0 (highest), VTF channel 0 |
| TIM3 | Step pulse off, one-shot at 144 MHz | 1, VTF channel 1 |
| EXTI0..4 | Endstops PE0..PE4, 40 ms software debounce | 1 |
| TIM4 CH1..3 | Servo PWM 50 Hz on PB6..PB8 | none (no interrupt) |
| SysTick | 1 ms tick, delays, elapsed time | 3 (lowest) |

The QingKe V4F hardware stack (HPE) is 3 levels deep, so only three
preemption levels are used and every handler runs as a WCH fast interrupt
(no software register save/restore). Interrupts sharing a level never
nest.

Servos are grblHAL aux analog outputs `E0..E2`, value = pulse width in
microseconds (`M67 E0 Q1500`, `M67 E0 Q0` releases the servo).

## Repository layout

```
├── grbl/             grblHAL core (git submodule)
├── lib/
│   ├── Core/         QingKe V4 core support (core_riscv)
│   ├── Debug/        WCH debug helpers (delay, debug printf)
│   ├── Peripheral/   WCH CH32V30x standard peripheral driver (vendored EVT)
│   └── Startup/      startup_ch32v30x_D8C.S (vector table, reset handler)
├── src/              Driver sources (main, system clock, ISRs, driver.c and friends)
├── boards/           Board pin maps (added with the driver)
├── ld/Link.ld        Linker script, FLASH 256K / RAM 64K
└── Makefile          Command-line / CI build
```

## Building

### Prerequisites

- A RISC-V bare-metal GCC toolchain. Any of the following works:
  - `riscv-none-elf-gcc` (xPack) in `PATH`
  - [MounRiver Studio 2](http://www.mounriver.com/) (its bundled toolchain
    is found automatically on macOS)
- GNU Make and Git

### Command line

```sh
git clone --recurse-submodules https://github.com/rmingon/grblHAL-CH32V31x
cd grblHAL-CH32V31x
make
```

Artifacts are written to `build/` (`.elf`, `.hex`, `.bin` and a map file).

Useful targets and options:

| Command | Effect |
|---------|--------|
| `make info` | Show the toolchain, architecture and ABI in use |
| `make lst` | Generate a disassembly listing |
| `make clean` | Remove the `build/` directory |
| `make CROSS=riscv-none-elf-` | Force a specific toolchain prefix |

The Makefile probes toolchains in this order: `CROSS=` override,
`riscv-none-elf-`, `riscv-wch-elf-` (MRS2 GCC12), `riscv32-wch-elf-`
(MRS2 GCC15), `riscv-none-embed-` (legacy). WCH toolchains build with
`-march=rv32imafcxw`, vanilla toolchains with `-march=rv32imafc_zicsr`,
both with the `ilp32f` hard-float ABI.

### MounRiver Studio 2

Open the repository root as an MRS2 project (`grblHAL-CH32V31x.wvproj`).
The managed build writes to `obj/`, independent of the Makefile output.
Flash and debug over WCH-Link with the bundled launch configuration.

## Flash / RAM configuration

The CH32V317 (CH32V30x_D8C family) splits flash and SRAM through user
option bytes. This project is linked for **FLASH 256K / RAM 64K**:

| Split | Note |
|-------|------|
| 288K / 32K | Factory default on most parts |
| **256K / 64K** | **Required by this firmware** |

Program the option bytes accordingly (MounRiver Studio or WCH-LinkUtility,
"RAM & ROM" setting) before flashing, otherwise the upper 32K of RAM used
by the firmware will not exist.

## Related projects

- [grblHAL/core](https://github.com/grblHAL/core), the grblHAL core this driver plugs into
- [PickOMatic V2](https://github.com/rmingon/PickOMatic-V2), the open-source hardware this firmware runs on
- [openwch](https://github.com/openwch), source of the vendored WCH EVT peripheral library

## License

GPLv3, like grblHAL itself. See [COPYING](COPYING).

Files under `lib/` are vendored from the WCH EVT and keep their original
WCH license headers.

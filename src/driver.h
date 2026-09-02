/*
  driver.h - grblHAL driver for WCH CH32V317 (QingKe V4F)

  Part of grblHAL

  Copyright (c) 2026 Ronan Mingon

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <stdbool.h>
#include <stdint.h>

#include "ch32v30x.h"

#ifndef OVERRIDE_MY_MACHINE
#include "my_machine.h"
#endif

#ifdef BOARD_PICKOMATIC
#include "boards/pickomatic_map.h"
#else
#error "No board defined!"
#endif

#include "grbl/driver_opts.h"

#ifndef RX_BUFFER_SIZE
#define RX_BUFFER_SIZE 1024
#endif

// Interrupt handlers rely on the QingKe hardware prologue/epilogue (HPE),
// enabled in startup_ch32v30x_D8C.S (INTSYSCR = 0x0b). The attribute is
// supported by the WCH toolchains (riscv-none-embed, riscv-wch-elf,
// riscv32-wch-elf); a vanilla riscv-none-elf-gcc build must fall back to
// the standard interrupt attribute (software save/restore, HPE-safe).
#if defined(__riscv_xw) || defined(__WCH__) || defined(WCH_TOOLCHAIN)
#define CH32_ISR __attribute__((interrupt("WCH-Interrupt-fast")))
#else
#define CH32_ISR __attribute__((interrupt))
#endif

// Timer allocation:
//   TIM2    - main stepper interrupt (16-bit, 3 prescaler banks)
//   TIM3    - step pulse off (one-shot)
//   TIM4    - servo PWM, 50 Hz (deliverable 3)
//   SysTick - 1 ms system tick
#define STEPPER_TIMER           TIM2
#define STEPPER_TIMER_IRQn      TIM2_IRQn
#define PULSE_TIMER             TIM3
#define PULSE_TIMER_IRQn        TIM3_IRQn

// TIM2/TIM3 kernel clock is 144 MHz (APB1 = HCLK/2, timer clock x2).
// Stepper timer base rate: 144 MHz / 8 = 18 MHz.
#define TIMER_CLOCK_HZ          144000000UL
#define STEPPER_TIMER_DIV       8

// Fast GPIO access; bit arguments are bit masks (1 << pin).
#define DIGITAL_OUT(port, bit, on) (port)->BSHR = (on) ? (uint32_t)(bit) : ((uint32_t)(bit) << 16)
#define DIGITAL_IN(port, bit) (!!((port)->INDR & (bit)))

#endif // _DRIVER_H_

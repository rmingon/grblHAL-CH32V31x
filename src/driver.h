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
//   TIM4    - servo PWM, 50 Hz, CH1..CH3 (aux analog outputs E0..E2)
//   SysTick - 1 ms system tick
#define STEPPER_TIMER           TIM2
#define STEPPER_TIMER_IRQn      TIM2_IRQn
#define PULSE_TIMER             TIM3
#define PULSE_TIMER_IRQn        TIM3_IRQn
#define SERVO_TIMER             TIM4

// TIM2/TIM3/TIM4 kernel clock is 144 MHz (APB1 = HCLK/2, timer clock x2).
// Stepper timer base rate: 144 MHz / 8 = 18 MHz.
#define TIMER_CLOCK_HZ          144000000UL
#define STEPPER_TIMER_DIV       8

// PFIC interrupt priorities.
//
// INTSYSCR (CSR 0x804) = 0x0b: HWSTKEN (hardware prologue/epilogue),
// INESTEN (nesting enabled), PMTCFG = 2 (4 preemption levels: IPRIOR
// bits [7:6] = preemption, bit [5] = sub-priority).
//
// The QingKe V4F hardware stack is 3 levels deep (CH32FV2x_V3xRM, PFIC
// chapter): interrupts nested deeper than that must save context in
// software. Every handler here uses "WCH-Interrupt-fast" (no software
// save/restore), so only THREE preemption levels are used and interrupts
// sharing a level never nest (sub-priority only orders pending requests).
// Worst case depth: level 3 <- level 1 <- level 0 = 3.
//
//   level 0: stepper timer (TIM2)           preempts everything
//   level 1: step pulse off (TIM3), limits  preempt everything but the stepper
//   level 2: reserved (do not use, would make depth 4)
//   level 3: SysTick, serial/USB/Ethernet   lowest
#define IRQ_PRIO(preempt, sub)  ((uint8_t)(((preempt) << 6) | ((sub) << 5)))

#define STEPPER_TIMER_IRQ_PRIO  IRQ_PRIO(0, 0)
#define PULSE_TIMER_IRQ_PRIO    IRQ_PRIO(1, 0)
#define LIMITS_IRQ_PRIO         IRQ_PRIO(1, 1)
#define SERIAL_IRQ_PRIO         IRQ_PRIO(3, 0)
#define SYSTICK_IRQ_PRIO        IRQ_PRIO(3, 1)

// VTF (vector table free) channels: direct-jump fast interrupts, 4 available.
#define VTF_CH_STEPPER          0
#define VTF_CH_PULSE            1

// Limit switch software debounce delay
#define LIMIT_DEBOUNCE_MS       40

// Servo outputs (TIM4 PWM) registered as aux analog outputs, ioports_analog.c
void ioports_init_analog (void);

// Fast GPIO access; bit arguments are bit masks (1 << pin).
#define DIGITAL_OUT(port, bit, on) (port)->BSHR = (on) ? (uint32_t)(bit) : ((uint32_t)(bit) << 16)
#define DIGITAL_IN(port, bit) (!!((port)->INDR & (bit)))

#endif // _DRIVER_H_

/*
  pickomatic_map.h - board map for the PickOMatic V2 pick-and-place controller

  Pin assignments extracted from the PickOMatic V2 KiCad design
  (https://github.com/rmingon/PickOMatic-V2, hardware/hardware.kicad_pcb,
  MCU U8, CH32V317VCT6 LQFP-100). To be confirmed against the board,
  final review is deliverable 6.

  Wiring notes (from the schematic):
  - All STEP and DIR signals are on GPIOD: single-port, single-write output.
  - DRV8825 nENBL is left floating (internal pulldown = always enabled),
    nSLEEP and nRESET are hardwired to 3V3: there is NO software stepper
    enable on this board.
  - Endstop inputs have external 10K pull-ups to 3V3, switches close to
    GND: active low. Lines PE0..PE4 map to EXTI0..EXTI4, one dedicated
    interrupt vector each.
  - Relays are driven through AO3400A N-FETs: active high.
  - Servo headers are wired to TIM4 CH1/CH2/CH3 (default mapping).

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

#if N_AXIS > 7
#error "PickOMatic V2 has 7 stepper channels, N_AXIS > 7 is not supported"
#endif

#define BOARD_NAME "PickOMatic V2"
#define BOARD_URL "https://github.com/rmingon/PickOMatic-V2"

// Motor channel to axis mapping (channel n = STEP_n/DIR_n nets):
// X = 1, Y = 2, Z = 3, A = 4, B = 5, C = 6, U = 7
// TODO: confirm which physical driver corresponds to which machine axis.

// Step outputs, all on GPIOD (PD0..PD6 = STEP_1..STEP_7).
#define STEP_PORT       GPIOD
#define X_STEP_PIN      0
#define Y_STEP_PIN      1
#define Z_STEP_PIN      2
#if N_AXIS > 3
#define A_STEP_PIN      3
#endif
#if N_AXIS > 4
#define B_STEP_PIN      4
#endif
#if N_AXIS > 5
#define C_STEP_PIN      5
#endif
#if N_AXIS > 6
#define U_STEP_PIN      6
#endif

// Direction outputs, all on GPIOD (PD7, PD9..PD14 = DIR_1..DIR_7).
#define DIR_PORT        GPIOD
#define X_DIR_PIN       7
#define Y_DIR_PIN       9
#define Z_DIR_PIN       10
#if N_AXIS > 3
#define A_DIR_PIN       11
#endif
#if N_AXIS > 4
#define B_DIR_PIN       12
#endif
#if N_AXIS > 5
#define C_DIR_PIN       13
#endif
#if N_AXIS > 6
#define U_DIR_PIN       14
#endif

// No stepper enable outputs: DRV8825 nENBL floating, nSLEEP/nRESET tied high.

// Limit inputs, GPIOE (PE0..PE4 = END_STOP_1..END_STOP_5, EXTI0..EXTI4).
// 5 endstops for 7 axes: C and U have no limit switch.
// TODO: confirm which endstop belongs to which axis.
#define LIMIT_PORT      GPIOE
#define X_LIMIT_PIN     0
#define Y_LIMIT_PIN     1
#define Z_LIMIT_PIN     2
#if N_AXIS > 3
#define A_LIMIT_PIN     3
#endif
#if N_AXIS > 4
#define B_LIMIT_PIN     4
#endif

// Relay outputs (through 680R + AO3400A, active high).
// M8 (flood) = vacuum pump, M7 (mist) = lighting.
// TODO: confirm RELAY_1 = vacuum pump and RELAY_2 = lighting.
#define COOLANT_FLOOD_PORT  GPIOE
#define COOLANT_FLOOD_PIN   5   // RELAY_1
#define COOLANT_MIST_PORT   GPIOE
#define COOLANT_MIST_PIN    6   // RELAY_2

// Servo outputs, 50 Hz PWM on TIM4, registered as aux analog outputs
// E0..E2 (M67/M68, value = pulse width in microseconds).
#define SERVO_PORT      GPIOB
#define SERVO1_PIN      6   // TIM4 CH1
#define SERVO2_PIN      7   // TIM4 CH2
#define SERVO3_PIN      8   // TIM4 CH3

// Status LEDs (active state TBC from schematic).
#define LED_BLUE_PORT   GPIOC
#define LED_BLUE_PIN    2
#define LED_RED_PORT    GPIOC
#define LED_RED_PIN     3

// USART1 on PA9 (TX) / PA10 (RX), default mapping (deliverable 4).
// USB FS device on PA11/PA12 (optional CDC stream, deliverable 4).
// Ethernet: internal 10M PHY on dedicated MDI pins (phase 2).

// No control inputs (reset/feed hold/cycle start) and no probe input
// are present on this board.

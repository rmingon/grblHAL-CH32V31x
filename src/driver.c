/*
  driver.c - grblHAL driver for WCH CH32V317 (QingKe V4F), PickOMatic board

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

#include <math.h>
#include <string.h>

#include "driver.h"

#include "grbl/hal.h"
#include "grbl/state_machine.h"
#include "grbl/stream.h"

// Step pulse timer (TIM3) runs at full timer clock.
#define PULSE_TICKS_PER_US (TIMER_CLOCK_HZ / 1000000UL)                       // 144
#define STEP_TICKS_PER_US  (TIMER_CLOCK_HZ / STEPPER_TIMER_DIV / 1000000UL)   // 18

// Interrupt latency compensation for the step pulse width, in microseconds.
// TODO: calibrate on a scope (deliverable 3, together with PFIC priorities).
#define STEP_PULSE_LATENCY_US 0.8f

// Pin bit masks

#define X_STEP_BIT (1 << X_STEP_PIN)
#define Y_STEP_BIT (1 << Y_STEP_PIN)
#define Z_STEP_BIT (1 << Z_STEP_PIN)
#define X_DIR_BIT  (1 << X_DIR_PIN)
#define Y_DIR_BIT  (1 << Y_DIR_PIN)
#define Z_DIR_BIT  (1 << Z_DIR_PIN)
#define X_LIMIT_BIT (1 << X_LIMIT_PIN)
#define Y_LIMIT_BIT (1 << Y_LIMIT_PIN)
#define Z_LIMIT_BIT (1 << Z_LIMIT_PIN)

#if N_AXIS > 3
#define A_STEP_BIT (1 << A_STEP_PIN)
#define A_DIR_BIT  (1 << A_DIR_PIN)
#define A_LIMIT_BIT (1 << A_LIMIT_PIN)
#else
#define A_STEP_BIT 0
#define A_DIR_BIT  0
#define A_LIMIT_BIT 0
#endif

#if N_AXIS > 4
#define B_STEP_BIT (1 << B_STEP_PIN)
#define B_DIR_BIT  (1 << B_DIR_PIN)
#define B_LIMIT_BIT (1 << B_LIMIT_PIN)
#else
#define B_STEP_BIT 0
#define B_DIR_BIT  0
#define B_LIMIT_BIT 0
#endif

#if N_AXIS > 5
#define C_STEP_BIT (1 << C_STEP_PIN)
#define C_DIR_BIT  (1 << C_DIR_PIN)
#else
#define C_STEP_BIT 0
#define C_DIR_BIT  0
#endif

#if N_AXIS > 6
#define U_STEP_BIT (1 << U_STEP_PIN)
#define U_DIR_BIT  (1 << U_DIR_PIN)
#else
#define U_STEP_BIT 0
#define U_DIR_BIT  0
#endif

#define STEP_MASK (X_STEP_BIT|Y_STEP_BIT|Z_STEP_BIT|A_STEP_BIT|B_STEP_BIT|C_STEP_BIT|U_STEP_BIT)
#define DIR_MASK  (X_DIR_BIT|Y_DIR_BIT|Z_DIR_BIT|A_DIR_BIT|B_DIR_BIT|C_DIR_BIT|U_DIR_BIT)

// PE0..PE4: limit port bits equal axis bits (X..B), EXTI lines 0..4.
#define LIMIT_MASK (X_LIMIT_BIT|Y_LIMIT_BIT|Z_LIMIT_BIT|A_LIMIT_BIT|B_LIMIT_BIT)

#define COOLANT_FLOOD_BIT (1 << COOLANT_FLOOD_PIN)
#define COOLANT_MIST_BIT  (1 << COOLANT_MIST_PIN)

// Interrupt handlers (weak symbols in startup_ch32v30x_D8C.S)
CH32_ISR void SysTick_Handler (void);
CH32_ISR void TIM2_IRQHandler (void);
CH32_ISR void TIM3_IRQHandler (void);
CH32_ISR void EXTI0_IRQHandler (void);
CH32_ISR void EXTI1_IRQHandler (void);
CH32_ISR void EXTI2_IRQHandler (void);
CH32_ISR void EXTI3_IRQHandler (void);
CH32_ISR void EXTI4_IRQHandler (void);

static bool IOInitDone = false;
static delay_t delay_state = { .ms = 0, .callback = NULL };
static volatile uint32_t elapsed_ticks = 0;

// Step pulse timing, computed in settings_changed()
static uint32_t pulse_width_ticks;      // TIM3 ticks (144 MHz)
static uint32_t pulse_delay_ticks;      // TIM3 ticks (144 MHz)
static uint32_t t_min_period;           // stepper timer ticks (f_step_timer)
static axes_signals_t step_pulse_pending = {0};

// Direction output lookup: BSHR word (set | clear << 16) per dir_out
// combination, dir_invert baked in. 1 << N_AXIS entries.
static uint32_t dir_outmap[1 << N_AXIS];

extern void board_ports_init (void); // weak in grbl core

//
// GPIO helpers
//

static inline __attribute__((always_inline)) void stepper_step_out (axes_signals_t step_out)
{
    // Step channels are PD0..PD6: axis bits equal port bits.
    uint32_t bits = (step_out.mask ^ settings.steppers.step_invert.mask) & AXES_BITMASK;

    STEP_PORT->BSHR = bits | ((~bits & STEP_MASK) << 16);
}

static inline __attribute__((always_inline)) void stepper_dir_out (axes_signals_t dir_out)
{
    DIR_PORT->BSHR = dir_outmap[dir_out.mask & AXES_BITMASK];
}

static void stepdirmap_init (settings_t *settings)
{
    static const uint8_t dir_pins[N_AXIS] = {
        X_DIR_PIN, Y_DIR_PIN, Z_DIR_PIN
#if N_AXIS > 3
      , A_DIR_PIN
#endif
#if N_AXIS > 4
      , B_DIR_PIN
#endif
#if N_AXIS > 5
      , C_DIR_PIN
#endif
#if N_AXIS > 6
      , U_DIR_PIN
#endif
    };

    uint32_t i, j, bits;

    for(i = 0; i < (1 << N_AXIS); i++) {
        bits = 0;
        for(j = 0; j < N_AXIS; j++) {
            if((i ^ settings->steppers.dir_invert.mask) & (1 << j))
                bits |= 1 << dir_pins[j];
        }
        dir_outmap[i] = bits | ((~bits & DIR_MASK) << 16);
    }
}

//
// 1 ms system tick (SysTick), also provides delays and elapsed time
//

static void systick_init (void)
{
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = (SystemCoreClock / 1000) - 1;
    // STE | STIE | STCLK (HCLK) | STRE (auto reload)
    SysTick->CTLR = 0x0F;

    NVIC_EnableIRQ(SysTicK_IRQn);
}

void SysTick_Handler (void)
{
    SysTick->SR = 0;

    elapsed_ticks++;

    if(delay_state.ms && !(--delay_state.ms)) {
        if(delay_state.callback) {
            delay_state.callback();
            delay_state.callback = NULL;
        }
    }
}

static void driver_delay (uint32_t ms, delay_callback_ptr callback)
{
    if((delay_state.ms = ms) > 0) {
        if(!(delay_state.callback = callback)) {
            while(delay_state.ms)
                grbl.on_execute_delay(state_get());
        }
    } else {
        if(delay_state.callback) {
            delay_state.ms = 0;
            delay_state.callback = NULL;
        }
        if(callback)
            callback();
    }
}

static uint32_t getElapsedTicks (void)
{
    return elapsed_ticks;
}

static uint64_t getMicros (void)
{
    uint32_t ticks, cnt;

    do {
        ticks = elapsed_ticks;
        cnt = (uint32_t)SysTick->CNT; // low word, always < SystemCoreClock / 1000
    } while(ticks != elapsed_ticks);

    return (uint64_t)ticks * 1000 + cnt / (SystemCoreClock / 1000000);
}

//
// Steppers
//

// No hardware support: DRV8825 nENBL is floating (enabled), nSLEEP/nRESET
// are tied high. Motors are always energized; the core still tracks the
// requested state via its wrapper around this call.
static void stepperEnable (axes_signals_t enable, bool hold)
{
    (void)enable;
    (void)hold;
}

// Set the step pulse rate for the next motion segment.
// cycles_per_tick is in f_step_timer (18 MHz) units; three prescaler banks
// extend the 16-bit timer range up to 2^20 ticks (58 ms).
static void stepperCyclesPerTick (uint32_t cycles_per_tick)
{
    if(cycles_per_tick < t_min_period)
        cycles_per_tick = t_min_period;

    if(cycles_per_tick < (1UL << 16))
        STEPPER_TIMER->PSC = STEPPER_TIMER_DIV - 1;
    else if(cycles_per_tick < (1UL << 18)) {
        STEPPER_TIMER->PSC = (STEPPER_TIMER_DIV << 2) - 1;
        cycles_per_tick >>= 2;
    } else {
        if(cycles_per_tick >= (1UL << 20))
            cycles_per_tick = (1UL << 20) - 1;
        STEPPER_TIMER->PSC = (STEPPER_TIMER_DIV << 4) - 1;
        cycles_per_tick >>= 4;
    }

    STEPPER_TIMER->ATRLR = (uint16_t)(cycles_per_tick - 1);
}

// Enable the main stepper interrupt; the first interrupt fires after a
// short delay to give the drivers time to energize.
static void stepperWakeUp (void)
{
    hal.stepper.enable((axes_signals_t){AXES_BITMASK}, false);

    stepperCyclesPerTick(hal.f_step_timer / 500); // ~2 ms
    STEPPER_TIMER->SWEVGR = TIM_UG;               // load PSC/ATRLR (URS: no UIF)
    STEPPER_TIMER->INTFR = 0;
    STEPPER_TIMER->DMAINTENR |= TIM_UIE;
    STEPPER_TIMER->CTLR1 |= TIM_CEN;
}

// Disable the main stepper interrupt. Called from interrupt context.
static void stepperGoIdle (bool clear_signals)
{
    STEPPER_TIMER->DMAINTENR &= ~TIM_UIE;
    STEPPER_TIMER->CTLR1 &= ~TIM_CEN;

    if(clear_signals) {
        stepper_dir_out((axes_signals_t){0});
        stepper_step_out((axes_signals_t){0});
    }
}

// Output direction and step signals, start the pulse-off one-shot (TIM3).
// Called from the stepper interrupt.
static void stepperPulseStart (stepper_t *stepper)
{
    if(stepper->dir_changed.bits) {
        stepper->dir_changed.bits = 0;
        stepper_dir_out(stepper->dir_out);
    }

    if(stepper->step_out.bits) {
        stepper_step_out(stepper->step_out);
        PULSE_TIMER->CTLR1 |= TIM_CEN;
    }
}

// Variant with delay between a direction change and the step pulse ($29).
static void stepperPulseStartDelayed (stepper_t *stepper)
{
    if(stepper->dir_changed.bits) {

        stepper->dir_changed.bits = 0;
        stepper_dir_out(stepper->dir_out);

        if(stepper->step_out.bits) {
            step_pulse_pending = stepper->step_out;
            PULSE_TIMER->ATRLR = pulse_delay_ticks;
            PULSE_TIMER->CTLR1 |= TIM_CEN;
        }

        return;
    }

    if(stepper->step_out.bits) {
        stepper_step_out(stepper->step_out);
        PULSE_TIMER->CTLR1 |= TIM_CEN;
    }
}

// Main stepper interrupt (TIM2)
void TIM2_IRQHandler (void)
{
    STEPPER_TIMER->INTFR = 0;
    hal.stepper.interrupt_callback();
}

// Step pulse off / delayed pulse on (TIM3, one-shot)
void TIM3_IRQHandler (void)
{
    PULSE_TIMER->INTFR = 0;

    if(step_pulse_pending.bits) {
        stepper_step_out(step_pulse_pending);
        step_pulse_pending.bits = 0;
        PULSE_TIMER->ATRLR = pulse_width_ticks;
        PULSE_TIMER->CTLR1 |= TIM_CEN;
    } else {
        stepper_step_out((axes_signals_t){0}); // back to idle levels
        PULSE_TIMER->ATRLR = pulse_width_ticks;
    }
}

//
// Limit switches (PE0..PE4 = EXTI0..EXTI4)
//

// Returns limit state, 1 = triggered. Port bits equal axis bits.
static limit_signals_t limitsGetState (void)
{
    limit_signals_t signals = {0};

    signals.min.mask = ((uint8_t)(LIMIT_PORT->INDR & LIMIT_MASK) ^ settings.limits.invert.mask) & LIMIT_MASK;

    return signals;
}

// Enable/disable limit switch interrupts. Switches used as homing source
// are polled by the homing cycle and get their interrupt disabled.
static void limitsEnable (bool on, axes_signals_t homing_cycle)
{
    uint32_t enable = 0;

    if(on) {
        enable = LIMIT_MASK;
        if(homing_cycle.mask) {
            limit_signals_t homing_source = xbar_get_homing_source_from_cycle(homing_cycle);
            enable &= ~(uint32_t)homing_source.min.mask;
        }
    }

    __disable_irq();
    EXTI->INTENR = (EXTI->INTENR & ~(uint32_t)LIMIT_MASK) | enable;
    __enable_irq();

    if(enable)
        EXTI->INTFR = enable; // clear stale pending flags (write 1 to clear)
}

static void limit_isr (uint32_t line_bit)
{
    EXTI->INTFR = line_bit;
    hal.limits.interrupt_callback(limitsGetState());
}

void EXTI0_IRQHandler (void) { limit_isr(1 << 0); }
void EXTI1_IRQHandler (void) { limit_isr(1 << 1); }
void EXTI2_IRQHandler (void) { limit_isr(1 << 2); }
void EXTI3_IRQHandler (void) { limit_isr(1 << 3); }
void EXTI4_IRQHandler (void) { limit_isr(1 << 4); }

//
// Control signals: none on this board
//

static control_signals_t systemGetState (void)
{
    return (control_signals_t){0};
}

//
// Coolant: flood = vacuum pump relay, mist = lighting relay
//

static void coolantSetState (coolant_state_t mode)
{
    mode.value ^= settings.coolant.invert.mask;
    DIGITAL_OUT(COOLANT_FLOOD_PORT, COOLANT_FLOOD_BIT, mode.flood);
    DIGITAL_OUT(COOLANT_MIST_PORT, COOLANT_MIST_BIT, mode.mist);
}

static coolant_state_t coolantGetState (void)
{
    coolant_state_t state = {0};

    state.flood = !!(COOLANT_FLOOD_PORT->OUTDR & COOLANT_FLOOD_BIT);
    state.mist = !!(COOLANT_MIST_PORT->OUTDR & COOLANT_MIST_BIT);
    state.value ^= settings.coolant.invert.mask;

    return state;
}

//
// Atomics and IRQ control
//

static void irqEnable (void)
{
    __enable_irq();
}

static void irqDisable (void)
{
    __disable_irq();
}

static void bitsSetAtomic (volatile uint_fast16_t *ptr, uint_fast16_t bits)
{
    __disable_irq();
    *ptr |= bits;
    __enable_irq();
}

static uint_fast16_t bitsClearAtomic (volatile uint_fast16_t *ptr, uint_fast16_t bits)
{
    __disable_irq();
    uint_fast16_t prev = *ptr;
    *ptr &= ~bits;
    __enable_irq();

    return prev;
}

static uint_fast16_t valueSetAtomic (volatile uint_fast16_t *ptr, uint_fast16_t value)
{
    __disable_irq();
    uint_fast16_t prev = *ptr;
    *ptr = value;
    __enable_irq();

    return prev;
}

//
// Settings changed: recompute pulse timing and input signal modes
//

static void settings_changed (settings_t *settings, settings_changed_flags_t changed)
{
    (void)changed;

    stepdirmap_init(settings);

    if(IOInitDone) {

        hal.stepper.go_idle(true);

        // Step pulse width and optional dir-to-step delay, TIM3 ticks.
        float pulse_us = settings->steppers.pulse_microseconds;

        pulse_width_ticks = max((uint32_t)((pulse_us - STEP_PULSE_LATENCY_US) * (float)PULSE_TICKS_PER_US), 2);

        if(hal.driver_cap.step_pulse_delay && settings->steppers.pulse_delay_microseconds > 0.0f) {
            pulse_delay_ticks = max((uint32_t)((settings->steppers.pulse_delay_microseconds - STEP_PULSE_LATENCY_US) * (float)PULSE_TICKS_PER_US), 2);
            hal.stepper.pulse_start = stepperPulseStartDelayed;
        } else {
            pulse_delay_ticks = 0;
            hal.stepper.pulse_start = stepperPulseStart;
        }

        PULSE_TIMER->ATRLR = pulse_width_ticks;

        // Lower bound for the stepper timer period: pulse (+ delay) + margin.
        t_min_period = (uint32_t)ceilf((pulse_us + settings->steppers.pulse_delay_microseconds + 3.0f) * (float)STEP_TICKS_PER_US);

        // Limit inputs: internal pull (external 10K pull-ups are always on)
        // and interrupt edge. Trigger edge = transition to active state.
        GPIO_InitTypeDef gpio = {
            .GPIO_Speed = GPIO_Speed_50MHz,
            .GPIO_Pin = LIMIT_MASK,
            .GPIO_Mode = settings->limits.disable_pullup.mask ? GPIO_Mode_IN_FLOATING : GPIO_Mode_IPU
        };
        GPIO_Init(LIMIT_PORT, &gpio);

        uint32_t falling = (uint32_t)(settings->limits.disable_pullup.mask ^ settings->limits.invert.mask) & LIMIT_MASK;

        __disable_irq();
        EXTI->FTENR = (EXTI->FTENR & ~(uint32_t)LIMIT_MASK) | falling;
        EXTI->RTENR = (EXTI->RTENR & ~(uint32_t)LIMIT_MASK) | (~falling & LIMIT_MASK);
        __enable_irq();
    }
}

//
// driver_setup: configure MCU peripherals, called once after settings load
//

static bool driver_setup (settings_t *settings)
{
    // Clocks
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC
                           | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);

    // Step + dir outputs (all on GPIOD)
    GPIO_InitTypeDef gpio = {
        .GPIO_Speed = GPIO_Speed_50MHz,
        .GPIO_Mode = GPIO_Mode_Out_PP,
        .GPIO_Pin = STEP_MASK | DIR_MASK
    };
    GPIO_Init(GPIOD, &gpio);

    // Relay outputs (off) and LEDs
    DIGITAL_OUT(COOLANT_FLOOD_PORT, COOLANT_FLOOD_BIT, 0);
    DIGITAL_OUT(COOLANT_MIST_PORT, COOLANT_MIST_BIT, 0);
    gpio.GPIO_Pin = COOLANT_FLOOD_BIT | COOLANT_MIST_BIT;
    GPIO_Init(GPIOE, &gpio);

    gpio.GPIO_Pin = (1 << LED_BLUE_PIN) | (1 << LED_RED_PIN);
    GPIO_Init(LED_BLUE_PORT, &gpio);

    // Stepper timer (TIM2): up-counting, update IRQ on overflow only
    STEPPER_TIMER->CTLR1 = TIM_URS;
    STEPPER_TIMER->PSC = STEPPER_TIMER_DIV - 1;

    // Pulse timer (TIM3): one-shot at full timer clock
    PULSE_TIMER->CTLR1 = TIM_OPM | TIM_URS;
    PULSE_TIMER->PSC = 0;
    PULSE_TIMER->INTFR = 0;
    PULSE_TIMER->DMAINTENR = TIM_UIE;

    // Limit inputs on EXTI lines 0..4 (one vector each).
    // NOTE: interrupt priorities and nesting are set up in deliverable 3;
    // the stepper ISR (TIM2) must preempt everything else.
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOE, GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOE, GPIO_PinSource1);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOE, GPIO_PinSource2);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOE, GPIO_PinSource3);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOE, GPIO_PinSource4);

    NVIC_EnableIRQ(STEPPER_TIMER_IRQn);
    NVIC_EnableIRQ(PULSE_TIMER_IRQn);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI3_IRQn);
    NVIC_EnableIRQ(EXTI4_IRQn);

    IOInitDone = settings->version.id == SETTINGS_VERSION;

    hal.stepper.go_idle(true);
    hal.coolant.set_state((coolant_state_t){0});

    settings_changed(settings, (settings_changed_flags_t){0});

    return IOInitDone;
}

//
// driver_init: entry point, called once by the core. Populates the HAL.
//

bool driver_init (void)
{
    SystemCoreClockUpdate();
    systick_init();

    hal.info = "CH32V317";
    hal.driver_version = "260901";
    hal.driver_url = "https://github.com/rmingon/grblHAL-CH32V31x";
    hal.board = BOARD_NAME;
    hal.board_url = BOARD_URL;

    hal.driver_setup = driver_setup;
    hal.f_mcu = SystemCoreClock / 1000000UL;
    hal.f_step_timer = TIMER_CLOCK_HZ / STEPPER_TIMER_DIV;
    hal.step_us_min = 2.0f;
    hal.rx_buffer_size = RX_BUFFER_SIZE;
    hal.delay_ms = driver_delay;

    hal.stepper.wake_up = stepperWakeUp;
    hal.stepper.go_idle = stepperGoIdle;
    hal.stepper.enable = stepperEnable;
    hal.stepper.cycles_per_tick = stepperCyclesPerTick;
    hal.stepper.pulse_start = stepperPulseStart;

    hal.limits.enable = limitsEnable;
    hal.limits.get_state = limitsGetState;

    hal.control.get_state = systemGetState;

    hal.coolant.set_state = coolantSetState;
    hal.coolant.get_state = coolantGetState;

    hal.irq_enable = irqEnable;
    hal.irq_disable = irqDisable;
    hal.set_bits_atomic = bitsSetAtomic;
    hal.clear_bits_atomic = bitsClearAtomic;
    hal.set_value_atomic = valueSetAtomic;
    hal.get_elapsed_ticks = getElapsedTicks;
    hal.get_micros = getMicros;

    grbl.on_settings_changed = settings_changed;

    // No spindle is registered: the core falls back to its null spindle
    // (M3/M4/M5 accepted, no outputs). Servos are driven separately (TIM4).

    // Stream: null stream placeholder until serial.c lands (deliverable 4).
    stream_connect(stream_null_init(115200));

    // Settings storage: none yet, defaults are used and changes are lost
    // on reset. Flash emulation NVS is deliverable 5.
    hal.nvs.type = NVS_None;

    // Capabilities
    hal.limits_cap.min.mask = LIMIT_MASK;                   // X..B, no C/U switches
    hal.coolant_cap.flood = On;                             // vacuum pump relay
    hal.coolant_cap.mist = On;                              // lighting relay
    hal.driver_cap.step_pulse_delay = On;
    hal.driver_cap.amass_level = 3;
    hal.driver_cap.limits_pull_up = On;
    hal.driver_cap.software_debounce = Off;                 // TODO: revisit in deliverable 3

    return hal.version == HAL_VERSION;
}

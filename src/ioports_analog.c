/*
  ioports_analog.c - servo PWM outputs as grblHAL aux analog outputs (CH32V317)

  The three PickOMatic servo headers are driven by TIM4 CH1..CH3 and
  registered with the core as analog auxiliary outputs E0..E2, so they
  are reachable from G-code with M67/M68:

      M67 E0 Q1500    ; servo 1 pulse width 1500 us (synchronous)
      M68 E1 Q2000    ; servo 2 pulse width 2000 us (immediate)
      M67 E0 Q0       ; no pulse, servo released

  Default configuration: 50 Hz, value = pulse width in microseconds,
  500..2500 us. All three channels share the TIM4 time base, so a
  frequency change (ioport_analog_out_config) applies to all servos.

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

#include <string.h>

#include "driver.h"

#include "grbl/ioports.h"

#define SERVO_PWM_HZ        50.0f
#define SERVO_MIN_US        500.0f
#define SERVO_MAX_US        2500.0f
#define SERVO_PERIOD_MAX    65530

typedef struct {
    pin_function_t id;
    GPIO_TypeDef *port;
    uint8_t pin;
    __IO uint16_t *ccr;         // TIM4 capture/compare register of the channel
    pin_mode_t mode;
    const char *description;
    float value;
    ioports_pwm_t pwm;
} servo_out_t;

static servo_out_t servo_out[] = {
    { .id = Output_Analog_Aux0, .port = SERVO_PORT, .pin = SERVO1_PIN, .ccr = &SERVO_TIMER->CH1CVR, .description = "Servo 1" },
    { .id = Output_Analog_Aux1, .port = SERVO_PORT, .pin = SERVO2_PIN, .ccr = &SERVO_TIMER->CH2CVR, .description = "Servo 2" },
    { .id = Output_Analog_Aux2, .port = SERVO_PORT, .pin = SERVO3_PIN, .ccr = &SERVO_TIMER->CH3CVR, .description = "Servo 3" }
};

#define N_SERVO (sizeof(servo_out) / sizeof(servo_out_t))

static io_ports_data_t analog;

static void servo_timer_config (uint32_t prescaler, uint32_t period)
{
    TIM_TimeBaseInitTypeDef base = {
        .TIM_Prescaler = prescaler - 1,
        .TIM_CounterMode = TIM_CounterMode_Up,
        .TIM_Period = period - 1,
        .TIM_ClockDivision = TIM_CKD_DIV1,
        .TIM_RepetitionCounter = 0
    };

    TIM_Cmd(SERVO_TIMER, DISABLE);
    TIM_TimeBaseInit(SERVO_TIMER, &base);
    TIM_ARRPreloadConfig(SERVO_TIMER, ENABLE);
    TIM_Cmd(SERVO_TIMER, ENABLE);
}

static void servo_pwm_out (uint8_t port, float value)
{
    servo_out_t *servo = &servo_out[port];

    servo->value = value;
    *servo->ccr = ioports_compute_pwm_value(&servo->pwm, value);
}

static bool analog_out (uint8_t port, float value)
{
    if(port < analog.out.n_ports)
        servo_pwm_out(port, value);

    return port < analog.out.n_ports;
}

static float servo_get_value (xbar_t *output)
{
    return output->id < analog.out.n_ports ? servo_out[output->id].value : -1.0f;
}

// Configure PWM for a servo output. The time base is shared by all
// channels: the prescaler/period found for the requested frequency is
// applied to TIM4 and the per-channel precomputed values are refreshed.
static bool servo_config (xbar_t *output, pwm_config_t *config, bool persistent)
{
    (void)persistent;

    bool ok;
    uint32_t prescaler = 0;
    servo_out_t *servo = &servo_out[output->id];

    do {
        prescaler++;
        ok = ioports_precompute_pwm_values(config, &servo->pwm, TIMER_CLOCK_HZ / prescaler);
    } while(ok && servo->pwm.period > SERVO_PERIOD_MAX);

    if(ok) {

        servo_timer_config(prescaler, servo->pwm.period);

        servo->mode.pwm = !config->servo_mode;
        servo->mode.servo_pwm = config->servo_mode;

        // Channels sharing the time base see the new period: refresh them.
        uint_fast8_t i;
        for(i = 0; i < analog.out.n_ports; i++) {
            if(i != output->id) {
                servo_out[i].pwm.period = servo->pwm.period;
                servo_out[i].pwm.f_clock = servo->pwm.f_clock;
            }
            servo_pwm_out(i, servo_out[i].value);
        }
    }

    return ok;
}

static bool set_function (xbar_t *port, pin_function_t function)
{
    if(!port->mode.input)
        servo_out[port->id].id = function;

    return true;
}

static xbar_t *get_pin_info (io_port_direction_t dir, uint8_t port)
{
    static xbar_t pin;
    xbar_t *info = NULL;

    memset(&pin, 0, sizeof(xbar_t));

    if(dir == Port_Output && port < analog.out.n_ports) {
        pin.id = port;
        pin.port = (void *)servo_out[port].port;
        pin.pin = servo_out[port].pin;
        pin.mode = servo_out[port].mode;
        pin.mode.pwm &= !pin.mode.servo_pwm;
        XBAR_SET_CAP(pin.cap, pin.mode);
        pin.function = servo_out[port].id;
        pin.group = PinGroup_AuxOutputAnalog;
        pin.description = servo_out[port].description;
        pin.get_value = servo_get_value;
        pin.config = servo_config;
        pin.set_function = set_function;
        info = &pin;
    }

    return info;
}

static void set_pin_description (io_port_direction_t dir, uint8_t port, const char *description)
{
    if(dir == Port_Output && port < analog.out.n_ports)
        servo_out[port].description = description;
}

void ioports_init_analog (void)
{
    io_analog_t ports = {
        .ports = &analog,
        .analog_out = analog_out,
        .get_pin_info = get_pin_info,
        .set_pin_description = set_pin_description
    };

    analog.out.n_ports = N_SERVO;

    if(!ioports_add_analog(&ports))
        return;

    // TIM4 CH1..CH3 on PB6..PB8, default mapping, alternate function push-pull.
    GPIO_InitTypeDef gpio = {
        .GPIO_Pin = (1 << SERVO1_PIN) | (1 << SERVO2_PIN) | (1 << SERVO3_PIN),
        .GPIO_Speed = GPIO_Speed_50MHz,
        .GPIO_Mode = GPIO_Mode_AF_PP
    };
    GPIO_Init(SERVO_PORT, &gpio);

    TIM_OCInitTypeDef oc = {
        .TIM_OCMode = TIM_OCMode_PWM1,
        .TIM_OutputState = TIM_OutputState_Enable,
        .TIM_Pulse = 0,
        .TIM_OCPolarity = TIM_OCPolarity_High
    };
    TIM_OC1Init(SERVO_TIMER, &oc);
    TIM_OC2Init(SERVO_TIMER, &oc);
    TIM_OC3Init(SERVO_TIMER, &oc);
    TIM_OC1PreloadConfig(SERVO_TIMER, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(SERVO_TIMER, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(SERVO_TIMER, TIM_OCPreload_Enable);

    // 50 Hz servo pulses, value in microseconds, no pulse when off.
    pwm_config_t config = {
        .freq_hz = SERVO_PWM_HZ,
        .min = SERVO_MIN_US,
        .max = SERVO_MAX_US,
        .off_value = 0.0f,
        .min_value = SERVO_MIN_US / 10000.0f * SERVO_PWM_HZ,  // percent of period
        .max_value = SERVO_MAX_US / 10000.0f * SERVO_PWM_HZ,
        .invert = Off,
        .servo_mode = On
    };

    xbar_t *pin;
    uint_fast8_t i;

    for(i = 0; i < N_SERVO; i++) {
        servo_out[i].mode.output = servo_out[i].mode.analog = servo_out[i].mode.servo_pwm = On;
        if((pin = get_pin_info(Port_Output, i)))
            pin->config(pin, &config, false);
    }
}

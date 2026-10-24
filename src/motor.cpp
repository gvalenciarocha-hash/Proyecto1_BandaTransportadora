#include "motor.h"
#include "driver/ledc.h"

namespace {
    constexpr gpio_num_t PIN_IA = GPIO_NUM_12;   // L9110S entrada A
    constexpr gpio_num_t PIN_IB = GPIO_NUM_14;   // L9110S entrada B

    constexpr ledc_mode_t     MODE    = LEDC_HIGH_SPEED_MODE;
    constexpr ledc_timer_t    TIMER   = LEDC_TIMER_0;
    constexpr ledc_channel_t  CH_IA   = LEDC_CHANNEL_0;
    constexpr ledc_channel_t  CH_IB   = LEDC_CHANNEL_1;
    constexpr uint32_t        FREQ_HZ = 1000;

    void setDuty(ledc_channel_t ch, uint32_t duty) {
        ledc_set_duty(MODE, ch, duty);
        ledc_update_duty(MODE, ch);
    }

    void configChannel(ledc_channel_t ch, gpio_num_t pin) {
        ledc_channel_config_t cfg = {};
        cfg.gpio_num   = pin;
        cfg.speed_mode = MODE;
        cfg.channel    = ch;
        cfg.intr_type  = LEDC_INTR_DISABLE;
        cfg.timer_sel  = TIMER;
        cfg.duty       = 0;
        cfg.hpoint     = 0;
        ledc_channel_config(&cfg);
    }
}

namespace Motor {

void init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode      = MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num       = TIMER;
    timer.freq_hz         = FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    configChannel(CH_IA, PIN_IA);
    configChannel(CH_IB, PIN_IB);

    coast();   // Estado seguro al arrancar
}

void forward(uint16_t duty) {
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    setDuty(CH_IA, duty);
    setDuty(CH_IB, 0);
}

void brake() {
    setDuty(CH_IA, DUTY_MAX);
    setDuty(CH_IB, DUTY_MAX);
}

void coast() {
    setDuty(CH_IA, 0);
    setDuty(CH_IB, 0);
}

}  // namespace Motor
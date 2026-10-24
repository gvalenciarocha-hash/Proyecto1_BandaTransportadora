#pragma once
#include <Arduino.h>

// Capa de abstracción de hardware (HAL) del motor DC con puente H L9110S
namespace Motor {
    constexpr uint16_t DUTY_MAX = 1023;   // 10 bits: 2^10 - 1

    void init();                  // Configura timer y canales ledc
    void forward(uint16_t duty);  // Avance con PWM (0 a 1023)
    void brake();                 // Frenado dinámico
    void coast();                 // Giro por inercia
}
#pragma once
#include <Arduino.h>

// Paro de emergencia: microinterruptor entre IO33 y GND (INPUT_PULLUP)
namespace Boton {
    constexpr uint8_t  PIN = 33;
    constexpr uint32_t DEBOUNCE_CYCLES = 4000000UL;  // 25 ms a 160 MHz

    void init();             // Configura el pin y adjunta la ISR (FALLING)
    bool takeEvent();        // true si hubo pulsación pendiente; limpia la bandera
    uint32_t pressCount();   // Total de pulsaciones válidas
}
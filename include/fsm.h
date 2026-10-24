#pragma once
#include <Arduino.h>

// Máquina de estados finitos orientada a eventos (no bloqueante, O(1))
namespace Fsm {
    enum class State : uint8_t { IDLE, RUNNING, FAULT, COUNT };
    enum class Event : uint8_t { NONE, BTN_PRESS, TIMEOUT, COUNT };

    constexpr uint32_t IDLE_TIMEOUT_MS = 3000;   // Espera en IDLE antes de arrancar

    void  init();      // Estado inicial seguro (motor en coast)
    void  update();    // Llamar en cada loop(): genera el evento y lo despacha
    State current();   // Estado actual
}
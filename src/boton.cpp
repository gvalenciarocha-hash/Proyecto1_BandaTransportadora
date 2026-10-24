#include "boton.h"
#include "xtensa/core-macros.h"   // Declara XTHAL_GET_CCOUNT()

namespace {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;   // Spinlock SMP

    volatile bool     eventFlag     = false;   // Bandera para el loop()
    volatile uint32_t pressCounter  = 0;       // Pulsaciones válidas
    volatile uint32_t isrLastCycles = 0;       // CCOUNT del último disparo

    // ISR en IRAM: no puede fallar por caché de flash ni llamar funciones bloqueantes
    void IRAM_ATTR isrBoton() {
        uint32_t now = XTHAL_GET_CCOUNT();

        // Resta sin signo de 32 bits: correcta aun con desbordamiento del contador
        if ((uint32_t)(now - isrLastCycles) >= Boton::DEBOUNCE_CYCLES) {
            isrLastCycles = now;

            portENTER_CRITICAL_ISR(&mux);
            eventFlag = true;
            pressCounter++;
            portEXIT_CRITICAL_ISR(&mux);
        }
    }
}

namespace Boton {

void init() {
    pinMode(PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN), isrBoton, FALLING);
}

bool takeEvent() {
    bool ev;
    portENTER_CRITICAL(&mux);
    ev = eventFlag;
    eventFlag = false;
    portEXIT_CRITICAL(&mux);
    return ev;
}

uint32_t pressCount() {
    uint32_t c;
    portENTER_CRITICAL(&mux);
    c = pressCounter;
    portEXIT_CRITICAL(&mux);
    return c;
}

}  // namespace Boton
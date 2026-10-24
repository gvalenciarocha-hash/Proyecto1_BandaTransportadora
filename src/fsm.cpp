#include "fsm.h"
#include "motor.h"
#include "boton.h"

namespace Fsm {
namespace {

    // Cada handler recibe el estado actual y devuelve el siguiente
    using Transition = State (*)(State);

    State    current_  = State::IDLE;
    uint32_t enteredMs = 0;   // Instante en que se entró al estado actual

    // ---- Handlers ----
    State stay(State s) {
        return s;
    }

    State startMotor(State) {
        Motor::forward(800);                       // ~78% de duty (de 1023)
        Serial.println("IDLE -> RUNNING");
        return State::RUNNING;
    }

    State emergencyStop(State) {
        Motor::brake();                            // Frenado dinámico
        Serial.println("RUNNING -> FAULT (paro de emergencia)");
        return State::FAULT;
    }

    State rearm(State) {
        Motor::coast();                            // Giro por inercia
        Serial.println("FAULT -> IDLE (rearme)");
        return State::IDLE;
    }

    // ---- Matriz de transiciones: filas = Estado, columnas = Evento ----
    constexpr size_t N_STATES = static_cast<size_t>(State::COUNT);
    constexpr size_t N_EVENTS = static_cast<size_t>(Event::COUNT);

    //                                    NONE   BTN_PRESS       TIMEOUT
    constexpr Transition TABLE[N_STATES][N_EVENTS] = {
        /* IDLE    */ {                   stay,  stay,           startMotor },
        /* RUNNING */ {                   stay,  emergencyStop,  stay       },
        /* FAULT   */ {                   stay,  rearm,          stay       },
    };

    // ---- Generación de eventos (sin delay, sin bloqueos) ----
    Event readEvent() {
        if (Boton::takeEvent()) {                              // Lee la bandera de la ISR con spinlock
            return Event::BTN_PRESS;
        }
        if (millis() - enteredMs >= IDLE_TIMEOUT_MS) {         // Resta sin signo: segura ante rollover
            return Event::TIMEOUT;
        }
        return Event::NONE;
    }

}  // namespace

void init() {
    Motor::coast();
    current_  = State::IDLE;
    enteredMs = millis();
}

void update() {
    Event e = readEvent();

    // Despacho en O(1): acceso directo a la matriz, sin if/else ni switch
    State next = TABLE[static_cast<size_t>(current_)][static_cast<size_t>(e)](current_);

    if (next != current_) {
        current_  = next;
        enteredMs = millis();
    }
}

State current() {
    return current_;
}

}  // namespace Fsm
#include <Arduino.h>
#include "motor.h"
#include "boton.h"
#include "fsm.h"

void setup() {
    Serial.begin(115200);
    Motor::init();
    Boton::init();
    Fsm::init();
    Serial.println("FSM lista: IDLE");
}

void loop() {
    Fsm::update();   // Sin delay(): el lazo nunca se bloquea
}
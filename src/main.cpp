#include <Arduino.h>
#include "motor.h"
#include "boton.h"

void setup() {
    Serial.begin(115200);
    Motor::init();
    Boton::init();
    Serial.println("Listo. Presiona el paro de emergencia (IO33).");
}

void loop() {
    if (Boton::takeEvent()) {
        Serial.print("Pulsacion valida #");
        Serial.println(Boton::pressCount());
    }
}
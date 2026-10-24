# Controlador de Banda Transportadora con ESP32

**Proyecto 1 · Programación II · Ingeniería Electrónica**
Universidad Técnica Nacional

Autor: Gabriel Moisés Valencia Rocha

---

## Descripción general

Este proyecto es el firmware de un controlador para una banda transportadora. Está construido sobre un ESP32 de Espressif, con PlatformIO y el framework Arduino. Un motorreductor DC mueve la banda y un microinterruptor hace de paro de emergencia.

La idea central es que el programa nunca se queda esperando. No usa `delay()`: todo el comportamiento se organiza en una máquina de estados finitos (FSM) guiada por eventos. El motor se controla con PWM generado por hardware (`ledc`), el paro de emergencia se atiende por interrupción con un filtro de rebotes basado en ciclos de CPU, y las variables compartidas entre la interrupción y el programa principal se protegen con spinlocks.

## Cómo funciona

1. Al encender, el sistema queda en **IDLE** con el motor libre (coast).
2. Tras 3 segundos sin novedades, la FSM recibe un evento `TIMEOUT` y pasa a **RUNNING**: el motor arranca.
3. Si se presiona el paro de emergencia, la interrupción registra la pulsación y la FSM pasa a **FAULT**: el motor frena de forma dinámica.
4. Una nueva pulsación rearma el sistema y lo devuelve a **IDLE**, desde donde el ciclo se repite.

## Hardware

| Elemento | Conexión |
|---|---|
| Motorreductor DC (DG01D) con puente H L9110S | IO12 (entrada A) e IO14 (entrada B) |
| Paro de emergencia (microinterruptor) | IO33 a GND, con `INPUT_PULLUP` interno |
| Alimentación | Fuente de 5 V / 3 A por conector de barril, con el jumper en SELECT-Vin |

La fuente de 5 V / 3 A evita caídas de tensión (brownouts) en el ESP32 durante el arranque o cuando el motor se atasca.

## Estructura del proyecto

```
include/
  motor.h     Interfaz del motor (ledc, 10 bits, 1000 Hz)
  boton.h     Interfaz del paro de emergencia
  fsm.h       Estados, eventos e interfaz de la FSM
src/
  motor.cpp   forward(), brake() y coast()
  boton.cpp   ISR con IRAM_ATTR, CCOUNT y spinlock
  fsm.cpp     Matriz de transiciones y despacho en O(1)
  main.cpp    setup() y loop() sin delay()
platformio.ini
```

Cada módulo tiene una sola responsabilidad: `motor` habla con el hardware del motor, `boton` encapsula la interrupción y `fsm` decide qué hacer. `main.cpp` solo los conecta.

## Máquina de estados

| Estado \ Evento | NONE | BTN_PRESS | TIMEOUT |
|---|---|---|---|
| **IDLE** | IDLE | IDLE | RUNNING (arranca el motor) |
| **RUNNING** | RUNNING | FAULT (frenado dinámico) | RUNNING |
| **FAULT** | FAULT | IDLE (coast, rearme) | FAULT |

El despacho es un acceso directo a un arreglo `constexpr` de punteros a funciones, indexado por `[estado][evento]`. No hay `if-else` ni `switch-case` anidados, y el tiempo de decisión es el mismo sin importar cuántos estados o eventos existan.

## Decisiones de diseño

- **PWM por hardware:** `LEDC_TIMER_0` a 10 bits y 1000 Hz; el motor avanza con un duty de 800 sobre 1023 (aprox. 78 %).
- **Antirrebote por ciclos:** la ISR lee `XTHAL_GET_CCOUNT()` y descarta disparos a menos de 4 000 000 ciclos del anterior (25 ms a 160 MHz), sin usar `millis()` ni `delay()`.
- **ISR segura:** declarada con `IRAM_ATTR`, sin llamadas bloqueantes, y con una sección crítica mínima protegida por `portMUX_TYPE`.
- **Variables compartidas:** la bandera y el contador son `volatile`, y `loop()` los lee dentro de una sección crítica.
- **Estado inicial seguro:** al arrancar, el motor queda en coast hasta que la FSM decide lo contrario.

## Compilación y carga

El proyecto usa PlatformIO con la placa `esp32dev` y el framework Arduino. Desde la terminal de PlatformIO:

```
pio run                      # compilar
pio run --target upload      # cargar a la placa
pio device monitor           # monitor serial (115200 baudios)
```

## Prueba en hardware

El comportamiento se verificó en la placa con el monitor serial. Una secuencia típica de arranque, paro de emergencia y rearme se ve así:

```
FSM lista: IDLE
IDLE -> RUNNING
RUNNING -> FAULT (paro de emergencia)
FAULT -> IDLE (rearme)
IDLE -> RUNNING
```

Cada pulsación del microinterruptor produce una sola transición, lo que confirma que el filtro de rebotes funciona.

---

## Marco teórico

### 1. Concurrencia en FreeRTOS: Mutex, Spinlocks y Deadlocks

**1.1 Contexto de tarea vs. contexto de excepción (ISR)**

Una tarea de FreeRTOS tiene su propio stack, su bloque de control (TCB) y un estado (Running, Ready, Blocked), de modo que el planificador puede suspenderla, dormirla y reanudarla. Una ISR, en cambio, se ejecuta como respuesta a una excepción de hardware y usa el stack de interrupción. No es una tarea ni tiene TCB propio, así que el planificador no puede bloquearla. Por eso solo puede usar las funciones con sufijo `FromISR` o `_ISR`.

Una función como `vTaskDelay` coloca la tarea actual en la lista de tareas bloqueadas y fuerza un cambio de contexto. Dentro de una ISR no existe una tarea propia que bloquear, y se estarían manipulando las listas del kernel de la tarea interrumpida desde un contexto no protegido. El kernel detecta la llamada bloqueante en contexto de interrupción y aborta con un pánico fatal.

**1.2 Spinlock SMP vs. Mutex de tarea**

Un Mutex (`SemaphoreHandle_t`) es un objeto del kernel: si está ocupado, la tarea que lo solicita pasa al estado Blocked y el planificador cede la CPU a otra. Solo puede tomarse desde una tarea.

Un spinlock (`portMUX_TYPE`) es una variable en memoria que se adquiere con una operación atómica de hardware y espera de forma activa (busy-wait). Además, `portENTER_CRITICAL` enmascara las interrupciones del núcleo local y excluye al otro núcleo, lo que lo hace apto para sistemas SMP.

Entre `loop()` y una ISR debe usarse un spinlock por dos razones. La ISR no puede bloquearse esperando un Mutex, y la sección crítica debe impedir que la ISR interrumpa a `loop()` a mitad de la lectura de las variables compartidas.

**1.3 Inversión de prioridad y herencia de prioridad**

La inversión de prioridad ocurre cuando una tarea de alta prioridad (H) espera un recurso que retiene una de baja prioridad (L), y una tarea de prioridad media (M), que no usa ese recurso, expropia a L. Así, H queda bloqueada indirectamente por M durante un tiempo no acotado. Un caso conocido es el del Mars Pathfinder, en 1997.

Con herencia de prioridad, cuando H solicita el Mutex que posee L, el kernel eleva temporalmente la prioridad de L hasta la de H. De esta forma M ya no puede expropiar a L, que termina su sección crítica, libera el Mutex y recupera su prioridad original. El bloqueo de H queda acotado a la duración de esa sección crítica.

**1.4 Condiciones de Coffman y secuencia del deadlock**

Las cuatro condiciones son:

1. Exclusión mutua.
2. Retención y espera (hold and wait).
3. No apropiación.
4. Espera circular.

La secuencia con un Mutex compartido sería esta:

1. `loop()` toma el Mutex y entra a su sección crítica.
2. Llega la interrupción de IO33 y la ISR intenta tomar el mismo Mutex, que está ocupado.
3. La ISR no puede bloquearse ni ceder la CPU, así que queda esperando o falla.
4. `loop()`, que es el dueño del Mutex, no puede continuar porque la ISR ocupa la CPU y solo se reanuda cuando la ISR retorne.

Cada una espera a la otra, sin apropiación posible, y el sistema se congela hasta que el watchdog lo reinicia. Por eso este proyecto usa un spinlock.

### 2. Arquitectura de memoria y restricciones del silicio

**2.1 Caché de flash e `IRAM_ATTR`**

El ESP32 ejecuta el código desde una flash SPI externa a través de una caché. Durante una escritura o un borrado de flash (por ejemplo en NVS o en una actualización OTA), la caché se deshabilita temporalmente porque el controlador SPI está ocupado, y ninguna instrucción puede leerse de la flash.

Si en ese intervalo llega una interrupción cuyo manejador reside en flash (una ISR sin `IRAM_ATTR`), la CPU intenta buscar la instrucción con la caché deshabilitada. Esto provoca una excepción (Guru Meditation Error) y el reinicio. `IRAM_ATTR` ubica la ISR, y lo que ella llama, en la IRAM interna, que siempre está accesible.

**2.2 Lecturas fragmentadas (torn reads)**

El ESP32 (Xtensa LX6) trabaja con registros y bus de datos de 32 bits, así que un `uint64_t` se lee con dos instrucciones: una para la palabra baja y otra para la alta. Si la ISR interrumpe justo entre ambas y actualiza el contador, por ejemplo de `0x00000000_FFFFFFFF` a `0x00000001_00000000`, `loop()` habrá leído la parte baja antigua (`0xFFFFFFFF`) y la parte alta nueva (`0x00000001`). El valor obtenido, `0x00000001_FFFFFFFF`, nunca existió realmente.

La solución es leer dentro de una sección crítica con spinlock. En este proyecto el contador es de 32 bits, que se lee con una sola instrucción, pero igual se lee bajo spinlock para mantener coherentes la bandera y el contador entre sí.

**2.3 La palabra clave `volatile`**

Con optimización `-O2`, el compilador aplica asignación de registros: carga una variable de memoria en un registro de la CPU (por ejemplo `a2`) y lo reutiliza en lugar de volver a leer la RAM, porque desde el flujo de `loop()` nadie la modifica. Si la bandera que escribe la ISR no es `volatile`, `loop()` puede evaluar siempre el valor obsoleto del registro e ignorar los cambios reales en la RAM.

`volatile` obliga al compilador a leer y escribir la memoria en cada acceso y le prohíbe eliminar o reordenar esos accesos. No garantiza atomicidad ni sincroniza entre núcleos, por lo que se complementa con el spinlock.

### 3. Física del rebote mecánico y depuración

**3.1 Física del contacto**

Las superficies metálicas no son perfectamente lisas, y los contactos tienen masa y elasticidad. Al cerrarse, chocan, rebotan y vuelven a chocar, mientras las microasperezas y los pequeños arcos abren y cierran el circuito, hasta que la oscilación se amortigua. Todo esto dura del orden de milisegundos.

Un microcontrolador a 160 MHz (un ciclo cada 6,25 ns) muestrea la entrada en tiempos muy inferiores a esa duración. Por eso ve cada transición como un flanco independiente, y una sola pulsación aparece como una ráfaga de 20 a 50 cambios de nivel lógico.

**3.2 Depuración por ciclos (`CCOUNT`)**

`delay()` bloquea la ejecución y recurre a `vTaskDelay`, lo que provoca un pánico dentro de una ISR. `millis()` depende de servicios del sistema y del temporizador del SO, y no es la herramienta adecuada para una ISR que debe ser mínima y determinista.

`XTHAL_GET_CCOUNT()`, en cambio, lee el registro especial CCOUNT de Xtensa con una sola instrucción. Es un contador de 32 bits que se incrementa en cada ciclo de CPU, así que entrega una marca de tiempo sin llamar al sistema operativo y sin bloquear. A 160 MHz, 25 ms equivalen a 4 000 000 ciclos.

**3.3 Aritmética modular**

Sea N = 2^32. El registro CCOUNT toma valores en Z_N. Si t0 es la lectura anterior, t1 la actual y D los ciclos realmente transcurridos, con 0 ≤ D < N, entonces:

```
t1 = (t0 + D) mod N
```

La resta sin signo de 32 bits calcula:

```
(t1 - t0) mod N = (t0 + D - t0) mod N = D mod N = D
```

El resultado es D porque D < N. Si no hay desbordamiento, es una resta normal. Si lo hay, t1 = t0 + D - N, de modo que t1 - t0 = D - N, que en aritmética sin signo de 32 bits equivale a D - N + N = D.

Ejemplo: con t0 = `0xFFFFFF00` y D = `0x200`, resulta t1 = `0x00000100`, y `0x00000100 - 0xFFFFFF00 = 0x00000200` (mod 2^32). La única condición es que D sea menor que 2^32 ciclos, es decir, 26,84 s a 160 MHz.

### 4. Periféricos y determinismo algorítmico

**4.1 Hardware PWM (LEDC)**

Cada canal LEDC solo define el pin y el ciclo de trabajo. La base de tiempo (frecuencia y resolución del contador) la define el temporizador al que el canal está asociado. Sin configurar `LEDC_TIMER_0`, los canales no tendrían referencia de tiempo.

Con 10 bits, el duty tiene 1024 niveles (0 a 1023). A 1000 Hz se cumple que frecuencia × 2^resolución = 1000 × 1024 ≈ 1,024 MHz, muy inferior al reloj de origen de 80 MHz, por lo que el divisor es realizable. Los dos canales (IO12 e IO14) comparten el temporizador y, por tanto, la frecuencia. El PWM lo genera el hardware sin intervención de la CPU, sin el jitter de una solución por software.

**4.2 Resistencias pull-up en IO34**

Los GPIO 34 a 39 del ESP32 son solo de entrada y no tienen resistencias pull-up ni pull-down internas, por lo que `INPUT_PULLUP` no tendría efecto. Con el interruptor abierto, el pin quedaría flotante (alta impedancia), captaría ruido y produciría lecturas aleatorias y falsas interrupciones. Se necesita una resistencia externa, típicamente de 10 kΩ a 3,3 V, que fije el nivel alto en reposo.

**4.3 Matriz de transiciones, complejidad O(1) y predicción de bifurcaciones**

Acceder a `TABLE[estado][evento]` equivale a calcular una dirección (base + (estado × N_EVENTOS + evento) × tamaño del puntero), hacer una carga y una llamada indirecta. La cantidad de operaciones no depende del número de estados ni de eventos, por lo que la complejidad es O(1). Además, al ser `constexpr`, la tabla queda en memoria de solo lectura y se resuelve en compilación.

Un `switch` anidado genera una cadena de comparaciones y saltos condicionales que dependen de los datos. Cuando el predictor de bifurcaciones falla, se vacía el pipeline y el tiempo de despacho varía. La matriz usa un único salto indirecto y un tiempo uniforme.

En el Xtensa LX6, que tiene un pipeline corto, la penalización por un fallo de predicción es modesta. La ventaja principal en este microcontrolador es, entonces, el determinismo, la legibilidad y la facilidad para extender la FSM, más que la velocidad pura.
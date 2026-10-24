# Proyecto 1: Firmware Controlador Banda Transportadora

Ingeniería Electrónica – Programación II
Autor: Moisés Valencia

Firmware para la IdeaBoard de CRCibernetica (ESP32) que controla un motorreductor DC mediante una máquina de estados finitos (FSM) orientada a eventos, con PWM por hardware (`ledc`), paro de emergencia por interrupción con filtro de rebotes por ciclos de CPU y acceso concurrente protegido con spinlocks.

## Hardware

| Elemento | Conexión |
|---|---|
| Motor DC (DG01D) con L9110S integrado | IO12 (entrada A) e IO14 (entrada B) |
| Paro de emergencia (microinterruptor) | IO33 a GND, con `INPUT_PULLUP` interno |
| Alimentación | Fuente de 5 V / 3 A por conector de barril, jumper en SELECT-Vin |

## Estructura del proyecto

```
include/
  motor.h     HAL del motor (ledc, 10 bits, 1000 Hz)
  boton.h     Interfaz del paro de emergencia
  fsm.h       Estados, eventos e interfaz de la FSM
src/
  motor.cpp   forward(), brake(), coast()
  boton.cpp   ISR con IRAM_ATTR, CCOUNT y spinlock
  fsm.cpp     Matriz de transiciones y despacho O(1)
  main.cpp    setup() y loop() sin delay()
platformio.ini
```

## Máquina de estados

| Estado \ Evento | NONE | BTN_PRESS | TIMEOUT |
|---|---|---|---|
| IDLE | IDLE | IDLE | RUNNING (arranca el motor) |
| RUNNING | RUNNING | FAULT (frenado dinámico) | RUNNING |
| FAULT | FAULT | IDLE (coast, rearme) | FAULT |

El despacho es un acceso directo a un arreglo `constexpr` de punteros a funciones, indexado por `[estado][evento]`, sin `if-else` ni `switch-case` anidados.

## Compilación

Proyecto de PlatformIO para `esp32dev` con framework Arduino. Se compila con `pio run` o con el botón Build de PlatformIO en VS Code.

## Marco teórico

### 1. Concurrencia en FreeRTOS: Mutex, Spinlocks y Deadlocks

**1.1 Contexto de tarea vs. contexto de excepción (ISR).**
Una tarea de FreeRTOS tiene su propio stack, su bloque de control (TCB) y un estado (Running, Ready, Blocked); el planificador puede suspenderla, dormirla y reanudarla. Una ISR se ejecuta como respuesta a una excepción de hardware, sobre el stack de interrupción, sin TCB propio: no es una tarea y el planificador no puede bloquearla. Por eso solo puede usar las API con sufijo `FromISR`/`_ISR`. Una función como `vTaskDelay` coloca la tarea actual en la lista de bloqueadas y fuerza un cambio de contexto. Dentro de una ISR no hay una tarea propia que bloquear: se manipularían las listas del kernel de la tarea interrumpida desde un contexto no protegido. El kernel detecta la llamada bloqueante en contexto de interrupción (aserción) y aborta con un pánico fatal.

**1.2 Spinlock SMP vs. Mutex de tarea.**
Un Mutex (`SemaphoreHandle_t`) es un objeto del kernel: si está ocupado, la tarea que lo pide pasa a Blocked y el planificador cede la CPU a otra. Solo puede tomarse desde una tarea. Un spinlock (`portMUX_TYPE`) es una variable en memoria que se adquiere con una operación atómica de hardware (compare-and-set en el ESP32) y espera de forma activa (busy-wait); además, `portENTER_CRITICAL` enmascara las interrupciones del núcleo local y excluye al otro núcleo (SMP). Entre `loop()` y una ISR se debe usar un spinlock porque la ISR no puede bloquearse esperando un Mutex, y porque la sección crítica debe impedir que la ISR interrumpa a `loop()` a mitad de la lectura de las variables compartidas.

**1.3 Inversión de prioridad y herencia de prioridad.**
Ocurre cuando una tarea de alta prioridad (H) espera un recurso retenido por una de baja prioridad (L), y una tarea de prioridad media (M), que no usa el recurso, expropia a L. H queda bloqueada indirectamente por M durante un tiempo no acotado (caso conocido: Mars Pathfinder, 1997). Con herencia de prioridad, cuando H solicita el Mutex que posee L, el kernel eleva temporalmente la prioridad de L a la de H. Así M ya no puede expropiar a L, que termina su sección crítica, libera el Mutex y vuelve a su prioridad original; el bloqueo de H queda acotado a la duración de esa sección crítica.

**1.4 Condiciones de Coffman y secuencia del deadlock.**
Las cuatro condiciones son: (1) exclusión mutua, (2) retención y espera (hold and wait), (3) no apropiación y (4) espera circular. Secuencia con un Mutex compartido: `loop()` toma el Mutex y entra a su sección crítica; llega la interrupción de IO33; la ISR intenta tomar el mismo Mutex y está ocupado. La ISR no puede bloquearse ni ceder la CPU, así que queda esperando (o falla). `loop()`, que es el dueño, no puede continuar porque la ISR ocupa la CPU y solo se reanuda cuando la ISR retorne. La ISR espera a `loop()` y `loop()` espera a la ISR: espera circular sin apropiación posible. El sistema se congela hasta que el watchdog lo reinicia. Por eso este proyecto usa un spinlock.

### 2. Arquitectura de memoria y restricciones del silicio

**2.1 Caché de flash e `IRAM_ATTR`.**
El ESP32 ejecuta el código desde flash SPI externa a través de la caché. Durante una escritura o borrado de flash (NVS, actualización OTA) la caché se deshabilita temporalmente, porque el controlador SPI está ocupado, y ninguna instrucción puede leerse de flash. Si en ese intervalo llega una interrupción cuyo manejador está en flash (ISR sin `IRAM_ATTR`), la CPU intenta buscar la instrucción y accede a una región cacheada con la caché deshabilitada, lo que produce una excepción (Guru Meditation Error) y el reinicio. `IRAM_ATTR` coloca la ISR, y lo que ella llama, en IRAM interna, que siempre es accesible.

**2.2 Lecturas fragmentadas (torn reads).**
El ESP32 (Xtensa LX6) tiene registros y bus de datos de 32 bits, así que un `uint64_t` se lee con dos instrucciones (palabra baja y palabra alta). Si la ISR interrumpe entre ambas y actualiza el contador, por ejemplo de 0x00000000_FFFFFFFF a 0x00000001_00000000, `loop()` habrá leído la parte baja antigua (0xFFFFFFFF) y la parte alta nueva (0x00000001), obteniendo 0x00000001_FFFFFFFF, un valor que nunca existió. La solución es leer dentro de una sección crítica con spinlock. En este proyecto el contador es `uint32_t` (lectura de una sola instrucción), pero también se lee bajo spinlock para mantener coherentes la bandera y el contador.

**2.3 La palabra clave `volatile`.**
Con `-O2` el compilador aplica asignación de registros: carga una variable de memoria a un registro de la CPU (por ejemplo `a2`) y reutiliza ese registro en lugar de volver a leer la RAM, porque desde el flujo de `loop()` nadie la modifica. Si la bandera que escribe la ISR no es `volatile`, `loop()` puede evaluar siempre el valor obsoleto del registro e ignorar los cambios reales en RAM. `volatile` obliga a leer y escribir la memoria en cada acceso y prohíbe eliminar o reordenar esos accesos. No da atomicidad ni sincroniza entre núcleos; por eso se complementa con el spinlock.

### 3. Física del rebote mecánico y depuración

**3.1 Física del contacto.**
Las superficies metálicas no son perfectamente lisas y los contactos tienen masa y elasticidad: al cerrarse chocan, rebotan y vuelven a chocar, mientras las microasperezas y los pequeños arcos abren y cierran el circuito, hasta que la oscilación se amortigua (del orden de milisegundos). Un microcontrolador a 160 MHz (ciclo de 6,25 ns) lee la entrada en tiempos muy inferiores a esa duración, así que ve cada transición como un flanco y una sola pulsación aparece como una ráfaga de 20 a 50 cambios lógicos.

**3.2 Depuración por ciclos (`CCOUNT`).**
`delay()` bloquea la ejecución y llama a `vTaskDelay`, lo que provoca un pánico dentro de una ISR; `millis()` depende del temporizador del sistema y es una función de flash, no segura dentro de una ISR. `XTHAL_GET_CCOUNT()` lee el registro especial CCOUNT de Xtensa (una instrucción `rsr.ccount`), un contador de 32 bits que se incrementa cada ciclo de CPU: entrega una marca de tiempo en un ciclo, sin llamar al sistema operativo y sin bloquear. A 160 MHz, 25 ms equivalen a 4 000 000 ciclos.

**3.3 Aritmética modular.**
Sea N = 2^32. CCOUNT toma valores en Z_N. Si t0 es la lectura anterior, t1 la actual y D los ciclos realmente transcurridos con 0 <= D < N, entonces t1 = (t0 + D) mod N. La resta sin signo de 32 bits calcula (t1 - t0) mod N = (t0 + D - t0) mod N = D mod N = D, porque D < N. Si no hay desbordamiento, es una resta normal; si lo hay, t1 = t0 + D - N, y t1 - t0 = D - N, que en aritmética sin signo de 32 bits equivale a D - N + N = D. Ejemplo: t0 = 0xFFFFFF00 y D = 0x200 dan t1 = 0x00000100, y 0x00000100 - 0xFFFFFF00 = 0x00000200 (mod 2^32). La condición es que D sea menor que 2^32 ciclos, es decir, 26,84 s a 160 MHz.

### 4. Periféricos de la IdeaBoard y determinismo algorítmico

**4.1 Hardware PWM (LEDC).**
Cada canal LEDC solo define el pin y el ciclo de trabajo; la base de tiempo (frecuencia y resolución del contador) la define el temporizador al que está asociado. Sin configurar `LEDC_TIMER_0` no hay referencia de tiempo para los canales. Con 10 bits el duty tiene 1024 niveles (0 a 1023) y a 1000 Hz se cumple que frecuencia x 2^resolución (1000 x 1024 = 1,024 MHz) es mucho menor que el reloj de origen de 80 MHz, por lo que el divisor es realizable. Los dos canales (IO12 e IO14) comparten el temporizador y, por tanto, la frecuencia. El PWM lo genera el hardware sin intervención de la CPU, sin jitter de software.

**4.2 Resistencias pull-up en IO34.**
Los GPIO 34 a 39 del ESP32 son solo de entrada y no tienen resistencias pull-up ni pull-down internas, por lo que `INPUT_PULLUP` no tendría efecto. Con el interruptor abierto el pin quedaría flotante (alta impedancia), captaría ruido y produciría lecturas aleatorias y falsas interrupciones. Se necesita una resistencia externa (típicamente 10 kΩ a 3,3 V) que fije el nivel alto en reposo.

**4.3 Matriz de transiciones, complejidad O(1) y predicción de bifurcaciones.**
Acceder a `TABLE[estado][evento]` equivale a calcular una dirección (base + (estado x N_EVENTOS + evento) x tamaño del puntero), una carga y una llamada indirecta: la cantidad de operaciones no depende del número de estados ni de eventos, por lo que es O(1). Al ser `constexpr`, la tabla queda en memoria de solo lectura y se resuelve en compilación. Un `switch` anidado genera una cadena de comparaciones y saltos condicionales que dependen de los datos; cuando el predictor de bifurcaciones falla se vacía el pipeline y el tiempo de despacho varía. La matriz usa un único salto indirecto y un tiempo de despacho uniforme. En el Xtensa LX6, de pipeline corto, la penalización por fallo es modesta, así que la ventaja principal en este microcontrolador es el determinismo, la legibilidad y la facilidad de extender la FSM, más que la velocidad pura.
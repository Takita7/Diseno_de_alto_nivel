# Fase 5 — Guión del Video Explicativo
## Equipo 5: Bluetooth BR/EDR con Interferencia WLAN | MP-6159 ITCR

**Duración objetivo:** 17 minutos (rango permitido: 15-20 min)
**Formato:** grabación de pantalla con narración
**Entrega:** 20/08/2026, 11:45 pm

---

## Estructura del video

| Sección | Tiempo | Contenido |
|---|---|---|
| 1. Historia y estandarización | 2:30 min | Origen, Bluetooth SIG, versiones |
| 2. Arquitectura y stack de capas | 3:30 min | Modelo de capas, piconet, FHSS |
| 3. Formato de trama | 3:00 min | Estructura del paquete BR/EDR, modulación |
| 4. Aplicaciones reales | 2:00 min | Casos de uso actuales |
| 5. Tendencias | 2:00 min | Bluetooth 6.0, LE Audio, Auracast |
| 6. Evidencia de simulación | 4:00 min | Scripts MATLAB en vivo + figuras |
| **Total** | **17:00 min** | |

---

## SECCIÓN 1 — Historia y Estandarización
*Duración: 2:30 min | Pantalla: diapositiva o imagen de línea de tiempo*

---

"Bluetooth nació en 1994 dentro de Ericsson, cuando el ingeniero Jaap Haartsen
diseñó un protocolo de radio de corto alcance para reemplazar los cables entre
teléfonos móviles y accesorios. El nombre viene del rey vikingo Harald Bluetooth,
quien unificó tribus danesas en el siglo X — una metáfora directa de la idea de
unificar protocolos de comunicación dispares.

En 1998, cinco empresas — Ericsson, Nokia, IBM, Toshiba e Intel — fundaron el
Bluetooth Special Interest Group, el organismo que hoy mantiene y publica el
estándar. En 2025 el SIG tiene más de 35,000 empresas miembro.

La evolución del estándar refleja cómo cambió el uso de la tecnología:

La versión 1.0 en 1999 estableció los fundamentos: GFSK, 1 Mbps, 79 canales
de 1 MHz en la banda de 2.4 GHz. Era suficiente para auriculares y teclados.

En 2004, la versión 2.0 introdujo Enhanced Data Rate — lo que estamos simulando
en este proyecto. EDR agregó π/4-DQPSK y 8DPSK para llevar el throughput de
1 a 3 Mbps sin cambiar el ancho de banda de canal.

En 2010, la versión 4.0 fue el cambio más importante: agregó Bluetooth Low
Energy como un modo completamente separado, diseñado para dispositivos IoT con
baterías que duran años.

La versión 5.0 en 2016 duplicó el rango y la velocidad de BLE, y agregó
Bluetooth Mesh para redes de miles de nodos.

Y la versión 5.4 en 2023 introdujo Periodic Advertising with Responses, la
base técnica de Auracast — el nuevo sistema de broadcast de audio que veremos
en la sección de tendencias."

---

## SECCIÓN 2 — Arquitectura y Stack de Capas
*Duración: 3:30 min | Pantalla: diagrama de capas*

---

"El estándar Bluetooth define un stack completo desde la capa de radio hasta
los perfiles de aplicación. Vamos capa por capa.

La capa más baja es la **capa RF**, que opera en la banda ISM de 2.4 GHz,
compartida con Wi-Fi, microondas y otros dispositivos. Para BR/EDR, el espectro
se divide en 79 canales de 1 MHz entre 2402 y 2480 MHz. Para LE, son 40 canales
de 2 MHz. Esta diferencia de ancho de canal es una de las razones por las que
LE tiene menor eficiencia espectral — 0.5 bps/Hz para LE1M contra 1.0 bps/Hz
para BR, como vemos en nuestras simulaciones.

Sobre la capa RF está el **Baseband**, el corazón del protocolo BR/EDR. Aquí
vive el mecanismo de salto de frecuencia — el enlace cambia de canal 1600 veces
por segundo, siguiendo una secuencia pseudoaleatoria determinada por la dirección
del dispositivo maestro. Este mecanismo es lo que hace a Bluetooth naturalmente
robusto ante interferencias estrechas. Y es también la base del AFH que
analizamos en este proyecto.

El Baseband también gestiona los slots de tiempo: cada slot dura 625 microsegundos.
Un paquete DH1, el que usamos en nuestra simulación, ocupa exactamente un slot de
transmisión más uno de recepción — un ciclo completo de 1.25 ms.

Encima del Baseband está el **LMP**, el Link Manager Protocol. Es responsable
de establecer la conexión, negociar los parámetros de QoS, gestionar el cifrado
y — importante para este proyecto — coordinar el intercambio del mapa de canales
AFH entre dispositivos.

Luego viene **L2CAP**, el Logical Link Control and Adaptation Protocol. Su función
principal es multiplexar los protocolos superiores sobre el mismo enlace físico.
Un solo enlace Bluetooth puede llevar simultáneamente audio HFP, datos RFCOMM
y señalización SDP.

Finalmente están los **perfiles**, que son las especificaciones de uso final:
A2DP para audio estéreo, HID para teclados y ratones, HFP para llamadas de
manos libres, y GATT para todos los servicios BLE.

La topología de red en BR/EDR se llama **piconet**: un dispositivo actúa como
Central y puede conectarse con hasta siete Periféricos activos simultáneamente.
Los periféricos en modo parked pueden ser más, hasta 255. En nuestra simulación
tenemos exactamente esta topología: un nodo Central y un Periférico."

---

## SECCIÓN 3 — Formato de Trama y Modulación PHY
*Duración: 3:00 min | Pantalla: bt_waveforms.m corriendo*

---

"Ahora vamos a ver la capa física directamente en MATLAB. Voy a correr el script
bt_waveforms que generamos en la Fase 1 del proyecto.

[CORRER bt_waveforms.m — esperar las 5 figuras]

Lo primero que aparece es el dominio del tiempo. Observen los cinco modos del
estándar Bluetooth. En BR — la fila verde — la envolvente de la señal es
perfectamente constante. Eso es GFSK, modulación de frecuencia pura: la
información está en los cambios de frecuencia, no de amplitud.

En EDR2M y EDR3M empiezan a aparecer pequeñas variaciones en la envolvente.
Eso ocurre porque estos modos usan modulación de fase — π/4-DQPSK y 8DPSK —
en la zona del payload, mientras el Access Code y el Header siguen siendo GFSK
para mantener compatibilidad con receptores BR.

LE1M se ve muy similar a BR porque también es GFSK. La diferencia está en el
índice de modulación: BR usa h=0.32, LE usa h=0.50. Eso significa que LE tiene
una desviación de frecuencia mayor — ±250 kHz en lugar de ±160 kHz en BR.
Lo podemos ver exactamente en la Figura 3.

[MOSTRAR Figura 3 — frecuencia instantánea]

Esta figura muestra la frecuencia instantánea calculada a partir de la fase
de la señal. La línea verde oscila entre ±160 kHz — ese es el BR. La naranja
oscila entre ±250 kHz — ese es LE1M. El índice de modulación mayor de LE da
más separación entre los estados de frecuencia, lo que hace el receptor más
robusto ante ruido, a costa de ocupar algo más de ancho de banda.

[MOSTRAR Figura 4 — constelaciones EDR]

Para EDR2M vemos cuatro nubes de puntos en el plano I-Q — los cuatro estados
de fase de π/4-DQPSK. Para EDR3M vemos ocho grupos distribuidos alrededor del
círculo unitario, correspondientes a los ocho estados de 8DPSK. Con ocho estados
podemos transmitir 3 bits por símbolo en lugar de 1, triplicando el throughput
sin cambiar el ancho de banda.

[MOSTRAR Figura 5 — eficiencia espectral]

Esta figura resume el trade-off central del estándar. EDR3M es tres veces más
eficiente que BR dentro del mismo canal de 1 MHz. LE1M es el menos eficiente
porque usa un canal de 2 MHz para una señal de 1 Mbps — la prioridad de LE
es robustez y bajo consumo, no eficiencia espectral.

El paquete BR/EDR tiene tres partes: el Access Code de 72 bits que identifica
la piconet y se usa para sincronización, el Header de 54 bits con tipo de
paquete y control de flujo, y el Payload que varía según el tipo. Un paquete
DH1 — el que usamos — tiene hasta 27 bytes de payload sin FEC. Un DH5 puede
llevar hasta 339 bytes pero ocupa cinco slots."

---

## SECCIÓN 4 — Aplicaciones Reales
*Duración: 2:00 min | Pantalla: imágenes de productos o diapositiva*

---

"Bluetooth BR/EDR sigue siendo la tecnología de referencia para tres categorías
de aplicaciones donde se necesita throughput sostenido en distancias cortas.

La primera es el **audio de alta calidad**. El perfil A2DP sobre BR/EDR es la
base de todos los auriculares inalámbricos modernos — AirPods, Sony WH-1000XM,
Bose QuietComfort. El codec SBC opera sobre BR a 1 Mbps y puede entregar audio
estéreo a 328 kbps. Los codecs propietarios como aptX y LDAC usan EDR para
entregar calidad de audio de alta definición.

La segunda categoría es **periféricos de computadora**. Teclados, ratones y
gamepads usan HID sobre BR/EDR porque necesitan latencia baja y throughput
moderado. Los gamepads de alto rendimiento como el DualSense de PlayStation 5
usan BR/EDR específicamente para minimizar la latencia de entrada.

La tercera es **comunicaciones de voz**. El perfil HFP permite llamadas de
manos libres en automóviles y auriculares. La especificación define canales SCO
sincrónicos de 64 kbps para audio bidireccional con latencia garantizada.
En nuestra simulación incluimos paquetes HV2 de SCO junto con los DH1 de ACL,
replicando exactamente este caso de uso.

Bluetooth BLE está reemplazando a BR/EDR en nuevas aplicaciones donde el consumo
energético importa más que el throughput: monitores de salud, sensores IoT,
beacons de localización y dispositivos médicos. Pero BR/EDR se mantiene vigente
donde la calidad de audio y la latencia son críticas."

---

## SECCIÓN 5 — Tendencias
*Duración: 2:00 min | Pantalla: diapositiva de tendencias*

---

"El estándar Bluetooth está evolucionando en tres direcciones simultáneas.

La primera es **LE Audio**, introducida en Bluetooth 5.2 y consolidada en 5.4.
LE Audio reemplaza A2DP con el codec LC3, que entrega mejor calidad de audio
que SBC a menor bitrate — 160 kbps contra 328 kbps para calidad equivalente.
Más importante, LE Audio permite que un solo transmisor envíe audio a múltiples
receptores simultáneamente sin pareamiento individual. Eso es la base de
Auracast, que ya está siendo desplegado en estadios, aeropuertos y cines para
distribución de audio en múltiples idiomas.

La segunda dirección es **Channel Sounding**, introducido en Bluetooth 6.0 en
2024. Channel Sounding usa la diferencia de fase medida en múltiples canales
para calcular la distancia entre dos dispositivos con precisión de centímetros.
Compite directamente con UWB para localización indoor — con la ventaja de que
Bluetooth está en prácticamente todos los teléfonos del mundo.

La tercera es **Bluetooth Mesh 2.0**, que mejora la escalabilidad de las redes
de sensores para soportar miles de nodos con mejor eficiencia energética.
Las aplicaciones van desde iluminación inteligente en edificios corporativos
hasta redes de sensores industriales.

En el contexto de nuestro proyecto, la coexistencia con WLAN sigue siendo un
desafío relevante en todas estas versiones — el espectro de 2.4 GHz está cada
vez más congestionado. El AFH que analizamos en este proyecto es la herramienta
fundamental que el estándar provee para mantener la robustez del enlace en ese
entorno."

---

## SECCIÓN 6 — Evidencia de Simulación
*Duración: 4:00 min | Pantalla: MATLAB en vivo*

---

### 6.1 — Caracterización del canal (bt_channel.m)
*~45 segundos*

"Antes de ver los resultados de la simulación de red, quiero mostrar la
justificación del canal. Voy a correr bt_channel.m.

[CORRER bt_channel.m]

[MOSTRAR consola — señalar SIR = −18.8 dB]

El resultado más importante es el SIR total: −18.8 dB. Esto significa que en
los canales afectados por la interferencia WLAN, la señal de interferencia es
aproximadamente 75 veces más potente que la señal Bluetooth útil. Ese número
es la razón cuantitativa por la que esperamos un PER elevado sin mitigation.

[MOSTRAR Figura 1 — plan de frecuencias]

Esta figura muestra el solapamiento: los dos nodos WLAN en 2.442 y 2.447 GHz
afectan exactamente los canales Bluetooth 30 a 55 — alrededor del 32% del
espectro disponible."

---

### 6.2 — Simulación interactiva (main_simulation.m)
*~45 segundos*

"Ahora voy a correr la simulación interactiva para ver el AFH clasificando
canales en tiempo real.

[CORRER main_simulation.m con enableWLANInterference=true, enableChannelClassification=true]

[MOSTRAR figura de coexistencia mientras corre]

Pueden ver cómo los canales van cambiando de verde a rojo a medida que el
algoritmo AFH los clasifica como malos. Después de tres intervalos de 250ms,
los canales 30-55 quedan marcados en rojo y el tráfico se concentra en los
canales verdes donde no hay interferencia.

[MOSTRAR tabla de clasificación en consola al terminar]

La tabla confirma: 54 canales buenos, 25 malos."

---

### 6.3 — Resultados consolidados (bt_main.m)
*~2:30 minutos*

"Ahora el script maestro que integra todo el proyecto.

[CORRER bt_main.m — mostrar consola mientras corre los 3 escenarios]

[MOSTRAR consola con tabla de métricas]

Los resultados centrales del proyecto: sin interferencia, PER es 0% y throughput
es 86 Kbps. Con interferencia WLAN sin mitigación, PER sube a 30% y el throughput
cae a 40 Kbps — una reducción del 53%. Con AFH activado, el PER baja a 17% y
el throughput se recupera a 59 Kbps.

[MOSTRAR Figura 2 — figura central del video]

Esta es la figura que cuenta la historia completa. La línea punteada horizontal
es el PER equivalente al límite de BER del Bluetooth Core Spec v5.4. Sin AFH
el enlace viola ese límite. Con AFH lo recupera.

[MOSTRAR Figura 1 — panel consolidado, señalar el mapa de canales]

El panel inferior izquierdo muestra el mapa de canales después de la simulación.
Los 25 canales rojos son exactamente los que el AFH identificó y excluyó.
Los 54 canales verdes son donde opera el enlace después de la clasificación.

[SEÑALAR panel de BER vs Core Spec]

Y aquí la conformidad: baseline cumple, sin AFH viola el límite en un 66%,
con AFH baja a 0.0885% — por debajo del límite de 0.1%.

El dato para resumir: el AFH reduce el PER en 12.8 puntos porcentuales y
recupera 19 Kbps de throughput, simplemente evitando los canales donde vive
la interferencia — sin aumentar potencia, sin cambiar modulación."

---

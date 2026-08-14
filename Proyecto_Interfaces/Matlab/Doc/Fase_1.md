# Fase 1 — Transmisor: Formas de Onda PHY
## Equipo 5: Bluetooth BR/EDR con Interferencia WLAN | MP-6159 ITCR

---

## Índice
1. [Objetivo de la fase](#1-objetivo-de-la-fase)
2. [Script implementado](#2-script-implementado)
3. [Cómo ejecutar](#3-cómo-ejecutar)
4. [Modos PHY configurados](#4-modos-phy-configurados)
5. [Verificación de figuras](#5-verificación-de-figuras)
6. [Puntos clave para el video](#6-puntos-clave-para-el-video)

---

## 1. Objetivo de la fase

Implementar el **Rol A (Transmisor)**: generar y visualizar las formas de onda
a nivel PHY de los modos Bluetooth BR/EDR y LE, configurando los parámetros de
modulación y capa física según la especificación Bluetooth Core Spec v5.4.

Esta fase no incluye canal ni receptor. Su propósito es demostrar que el transmisor
genera señales conformes al estándar antes de pasarlas al canal.

---

## 2. Script implementado

```
BT_MP6159/
└── bt_waveforms.m    Genera y visualiza formas de onda de 5 modos PHY
```

**API utilizado:**
- `bluetoothWaveformConfig` + `bluetoothWaveformGenerator` — modos BR/EDR
- `bleWaveformGenerator` con Name-Value arguments — modos LE

**Nota R2026a:** `bluetoothWaveformGenerator` retorna un único output en esta
versión. Los modos LE no son soportados por `bluetoothWaveformConfig` — se usa
`bleWaveformGenerator` como función separada.

---

## 3. Cómo ejecutar

```matlab
% Desde la carpeta del proyecto, con Bluetooth Toolbox instalado:
bt_waveforms
```

No requiere helpers ni archivo `.bb`. Solo Bluetooth Toolbox.
Tiempo de ejecución: menos de 5 segundos.

**Output en consola:**
```
=== CONFIGURACIÓN DE MODOS PHY ===
Modo     Modulación      BitRate    BW canal   ModIndex
BR       GFSK            1 Mbps     1 MHz      0.32
EDR2M    pi/4-DQPSK      2 Mbps     1 MHz      N/A
EDR3M    8DPSK           3 Mbps     1 MHz      N/A
LE1M     GFSK            1 Mbps     2 MHz      0.50
LE2M     GFSK            2 Mbps     2 MHz      0.50

Waveforms generados: BR(5000), EDR2M(5000), EDR3M(5000), LE1M(1600), LE2M(1664) muestras
```

---

## 4. Modos PHY configurados

### BR/EDR — `bluetoothWaveformConfig`

| Parámetro | BR | EDR2M | EDR3M |
|---|---|---|---|
| Modulación | GFSK | π/4-DQPSK | 8DPSK |
| Tipo de paquete | DH1 | 2-DH1 | 3-DH1 |
| Payload máximo | 27 bytes | 54 bytes | 83 bytes |
| Symbol rate | 1 Msym/s | 1 Msym/s | 1 Msym/s |
| Bit rate efectivo | 1 Mbps | 2 Mbps | 3 Mbps |
| BW de canal | 1 MHz | 1 MHz | 1 MHz |
| Eficiencia espectral | 1.0 bps/Hz | 2.0 bps/Hz | 3.0 bps/Hz |
| SamplesPerSymbol | 8 | 8 | 8 |
| Sample rate | 8 MHz | 8 MHz | 8 MHz |

**Estructura del paquete BR/EDR:**
```
| Access Code (72 bits) | Header (54 bits) | Payload (variable) |
```
- Access Code: identifica la piconet, usado para sincronización
- Header: tipo de paquete (4 bits), ARQN, SEQN, HEC (3 bits de paridad)
- En EDR, el payload usa modulación diferente al header. El receptor
  detecta el cambio de modo por el patrón de sincronización EDR (11 símbolos)

### LE — `bleWaveformGenerator`

| Parámetro | LE1M | LE2M |
|---|---|---|
| Modulación | GFSK | GFSK |
| Índice de modulación | 0.50 | 0.50 |
| Tipo de paquete | ADV_IND | ADV_IND |
| Symbol rate | 1 Msym/s | 2 Msym/s |
| Bit rate efectivo | 1 Mbps | 2 Mbps |
| BW de canal | 2 MHz | 2 MHz |
| Eficiencia espectral | 0.5 bps/Hz | 1.0 bps/Hz |
| ChannelIndex | 37 | 37 |
| Sample rate | 8 MHz | 16 MHz |

**Diferencia clave BR vs LE en GFSK:**
Ambos usan GFSK pero con índice de modulación distinto:
- BR: h = 0.32 → desviación de frecuencia = ±160 kHz
- LE: h = 0.50 → desviación de frecuencia = ±250 kHz

LE usa un índice mayor para mayor robustez ante interferencia,
a costa de ocupar algo más de ancho de banda.

---

## 5. Verificación de figuras

El script genera 5 figuras. Esta sección explica qué observar en cada una
para confirmar que el resultado es físicamente correcto.

---

### Figura 1 — Dominio del tiempo (5 subplots)

Muestra los primeros 60 µs de la componente I (sólida) y Q (punteada) de cada modo.

| Modo | Qué observar | Por qué |
|---|---|---|
| BR | Envolvente perfectamente constante, oscilación suave | GFSK es FM pura — la amplitud nunca varía |
| EDR2M | Pequeñas variaciones de envolvente en la zona del payload | Transición de GFSK (header) a π/4-DQPSK (payload) |
| EDR3M | Similar a EDR2M pero variaciones más frecuentes | 8DPSK tiene 8 estados de fase → más cambios visibles |
| LE1M | Visualmente similar a BR | También GFSK, misma symbol rate |
| LE2M | El doble de ciclos que LE1M en el mismo tiempo | Symbol rate 2 Msym/s — los símbolos duran la mitad |

**Señal de alerta:** si algún modo muestra variaciones de amplitud grandes y erráticas
(no suaves), puede indicar un error en la configuración del payload o del tipo de paquete.

---

### Figura 2 — PSD por modo (grid 3×2)

5 subplots individuales + 1 panel de overlay.

**Qué verificar en cada subplot:**
- BR, EDR2M, EDR3M: la mayor parte de la potencia debe estar dentro de ±0.5 MHz
  (marcado con líneas punteadas verticales). Las colas fuera de ±0.5 MHz son normales
  por el filtro gaussiano de GFSK y la naturaleza de las modulaciones de fase.
- LE1M, LE2M: la mayor parte de la potencia debe estar dentro de ±1.0 MHz
  (canal de 2 MHz). LE2M aparece notablemente más ancho que LE1M en el overlay.

**Panel overlay (posición 6):** confirma visualmente que LE2M (violeta) tiene el
espectro más ancho de los 5 modos, y que BR/EDR están contenidos dentro de 1 MHz.

---

### Figura 3 — Frecuencia instantánea GFSK (BR vs LE1M)

Muestra la frecuencia instantánea calculada como:
```
f_inst(t) = diff(unwrap(angle(waveform))) / (2π) × sample_rate
```

**Qué verificar:**
- La línea verde (BR) debe oscilar entre aproximadamente **±160 kHz**
- La línea naranja (LE1M) debe oscilar entre aproximadamente **±250 kHz**
- Ambas señales deben ser oscilaciones limpias y regulares (no ruido)

**Por qué los valores son ±160 y ±250 kHz:**
```
Desviación = h × symbol_rate / 2
BR:   0.32 × 1,000,000 / 2 = 160,000 Hz = 160 kHz
LE1M: 0.50 × 1,000,000 / 2 = 250,000 Hz = 250 kHz
```

---

### Figura 4 — Constelaciones EDR (1×2)

Muestra los puntos I/Q muestreados al símbolo del payload EDR.

**Qué verificar:**
- EDR2M (π/4-DQPSK): deben verse **4 nubes de puntos** distribuidas. Los puntos
  aparecen dispersos porque π/4-DQPSK es diferencial — hay rotación acumulativa
  de π/4 por símbolo, así que no hay 4 posiciones fijas absolutas sino transiciones.
  El círculo unitario (punteado) confirma que la potencia es aproximadamente constante.
- EDR3M (8DPSK): deben verse puntos distribuidos en **8 grupos** alrededor del
  círculo unitario. Con el payload corto de un paquete DH1, puede que no todos
  los 8 estados aparezcan con igual densidad — eso es normal con datos aleatorios.

**Señal de alerta:** si todos los puntos se concentran en un solo lugar, indica
que se están muestreando muestras fuera del payload EDR (zona del Access Code o header,
que usa GFSK y tiene comportamiento diferente).

---

### Figura 5 — Eficiencia espectral por modo

Barras con los valores teóricos: BR=1.0, EDR2M=2.0, EDR3M=3.0, LE1M=0.5, LE2M=1.0 bps/Hz.

**Interpretación:** EDR3M triplica la eficiencia de BR dentro del mismo canal de 1 MHz
gracias a 8DPSK (3 bits/símbolo vs 1 bit/símbolo en GFSK). LE1M tiene la eficiencia
más baja (0.5 bps/Hz) porque usa un canal de 2 MHz para una señal de 1 Mbps — la
prioridad de LE es robustez y bajo consumo, no eficiencia espectral.

---

## 6. Puntos clave para el video

Esta sección cubre el contenido conceptual que el integrante del Rol A debe
narrar en el video al presentar los resultados de `bt_waveforms.m`.

### Qué mostrar en pantalla
1. Correr `bt_waveforms.m` en vivo y esperar las 5 figuras
2. Señalar en Figura 1 la envolvente constante de BR/LE vs las variaciones de EDR
3. En Figura 2, mostrar el panel overlay y señalar que LE2M es más ancho
4. En Figura 3, señalar la diferencia de ±160 kHz (BR) vs ±250 kHz (LE)
5. En Figura 4, mostrar los 4 grupos de EDR2M y los 8 de EDR3M
6. En Figura 5, destacar que EDR3M triplica la eficiencia de BR

### Qué explicar sobre el estándar
- BR usa GFSK porque es simple, robusto y de bajo consumo — ideal para el diseño
  original de 1994 donde el hardware era limitado
- EDR (2004) agrega π/4-DQPSK y 8DPSK para multiplicar el throughput sin cambiar
  el ancho de banda de canal — retrocompatible con BR porque el header sigue siendo GFSK
- LE (2010) usa un canal más ancho (2 MHz) porque prioriza la coexistencia con otras
  tecnologías y la robustez en entornos con interferencia sobre la eficiencia espectral
- El índice de modulación mayor de LE (h=0.50 vs h=0.32) da más separación entre
  frecuencias → más fácil de detectar con receptores simples de bajo consumo
# Fase 2 — Canal: Interferencia WLAN en 2.4 GHz
## Equipo 5: Bluetooth BR/EDR con Interferencia WLAN | MP-6159 ITCR

---

## Índice
1. [Objetivo de la fase](#1-objetivo-de-la-fase)
2. [Scripts implementados](#2-scripts-implementados)
3. [Cómo ejecutar](#3-cómo-ejecutar)
4. [Justificación del escenario de canal](#4-justificación-del-escenario-de-canal)
5. [Modelo de canal por paso](#5-modelo-de-canal-por-paso)
6. [Verificación de figuras](#6-verificación-de-figuras)
7. [Conexión canal → métricas](#7-conexión-canal--métricas)
8. [Puntos clave para el video](#8-puntos-clave-para-el-video)

---

## 1. Objetivo de la fase

Implementar el **Rol B (Canal)**: modelar y justificar cuantitativamente el
escenario de interferencia WLAN sobre el enlace Bluetooth BR/EDR en la banda
de 2.4 GHz, siguiendo el estándar IEEE 802.15.2-2003.

Esta fase tiene dos capas:
- **Analítica** (`bt_channel.m`): caracteriza el canal con cálculos de path loss
  y balance de potencias. Justifica los parámetros elegidos.
- **Simulación** (`bt_ber_comparison.m`): ejecuta los tres escenarios del canal
  (baseline, con interferencia, con AFH) y mide el impacto en métricas PHY.

---

## 2. Scripts implementados

```
Matlab/
├── bt_channel.m          Caracterización analítica del canal (Fase 2)
└── bt_ber_comparison.m   Simulación de los 3 escenarios de canal (Fases 2-3)
```

**Helpers requeridos por bt_ber_comparison.m:**
```
├── helperBluetoothChannelClassification.m   Algoritmo AFH (Paso 2.4)
├── helperInterferingWLANNode.m              Nodos WLAN interfirientes (Paso 2.3)
├── helperVisualizeCoexistence.m             Visualización de canales
└── WLANHESUBandwidth20.bb                   Señal WLAN pregenerada
```

---

## 3. Cómo ejecutar

### Caracterización analítica del canal

```matlab
bt_channel
```

No requiere helpers. Tiempo de ejecución: menos de 10 segundos.
Produce 4 figuras + balance de potencias en consola.

### Simulación de los tres escenarios

```matlab
bt_ber_comparison
```

Requiere todos los helpers y el archivo `.bb` en la carpeta del proyecto.
Tiempo de ejecución: 2-3 minutos (tres simulaciones completas en secuencia).

---

## 4. Justificación del escenario de canal

### Referencia principal
**IEEE 802.15.2-2003**, *"Coexistence of Wireless Personal Area Networks with
Other Wireless Devices Operating in Unlicensed Frequency Bands"*, Sección 5:
modelo de coexistencia indoor entre Bluetooth y 802.11b/g.

### Por qué este escenario
Se eligió un escenario de coexistencia doméstica/oficina con dos puntos de
acceso WLAN operando en canales adyacentes del espectro 2.4 GHz. Este escenario
es representativo porque:

1. El solapamiento es **parcial** (no total): los canales WLAN cubren ~32% de
   los 79 canales Bluetooth (≈25 canales afectados), lo que permite observar
   simultáneamente canales buenos y malos, haciendo visible el efecto del AFH.

2. Los dos nodos WLAN en canales adyacentes (7 y 8) son el caso típico de
   entornos con múltiples APs sin coordinación de frecuencias, documentado
   en IEEE 802.15.2-2003 como el escenario de mayor impacto en BR/EDR.

3. Los parámetros de potencia y distancia son representativos de un AP
   doméstico 802.11g, con potencia estándar de 20 dBm y distancias de 4-10 m.

### Parámetros del escenario

| Parámetro | Valor | Fuente |
|---|---|---|
| Frecuencia WLAN nodo 1 | 2.442 GHz (canal WLAN 7) | IEEE 802.11, plan de canales 2.4 GHz |
| Frecuencia WLAN nodo 2 | 2.447 GHz (canal WLAN 8) | IEEE 802.11, plan de canales 2.4 GHz |
| Potencia TX WLAN | 20 dBm | Valor típico AP doméstico 802.11g |
| Ancho de banda WLAN | 20 MHz | Configuración estándar 802.11g/n |
| Periodicidad señal WLAN | 2 ms | Intervalo típico de tráfico/beacon |
| Posición Central BT | [0, 0, 0] m | Origen del escenario |
| Posición Periférico BT | [5, 0, 0] m | Separación 5 m (Clase 2 típico) |
| Posición nodo WLAN 1 | [0, 7, 5] m | AP en habitación adyacente |
| Posición nodo WLAN 2 | [0, 3, 0] m | AP en la misma habitación |
| Potencia TX Bluetooth | 0 dBm | Clase 2, configurado en simulación |
| Modelo de canal | Log-distance indoor, n=3 | IEEE 802.15.2-2003, Sección 5 |

### Canales Bluetooth afectados
Frecuencias de los nodos WLAN con BW de 20 MHz:
```
WLAN1 (2.442 GHz): ocupa 2432 – 2452 MHz → canales BT 30 – 50
WLAN2 (2.447 GHz): ocupa 2437 – 2457 MHz → canales BT 35 – 55

Unión: canales BT 30 – 55 afectados (~26 de 79 = 33%)
```

---

## 5. Modelo de canal por paso

### Paso 2.1 — Justificación analítica (`bt_channel.m`)

Modelo de pérdida de trayecto log-distance (IEEE 802.15.2-2003):

```
PL(d) = PL₀ + 10 × n × log₁₀(d / d₀)

Donde:
  PL₀ = 20×log₁₀(4π×2.4GHz×1m / c) ≈ 40.0 dB  (espacio libre a 1 m)
  n   = 3.0  (exponente indoor, IEEE 802.15.2)
  d₀  = 1 m  (distancia de referencia)
```

**Resultados del balance de potencias:**

| Señal | Distancia al periférico | Potencia recibida |
|---|---|---|
| Bluetooth útil | 5.0 m | **-61.0 dBm** |
| Interferencia WLAN1 | 9.9 m | **-50.0 dBm** |
| Interferencia WLAN2 | 5.8 m | **-43.0 dBm** |
| Ruido térmico (1 MHz) | — | **-114.0 dBm** |

```
SIR vs WLAN1 = -61.0 - (-50.0) = -11.0 dB
SIR vs WLAN2 = -61.0 - (-43.0) = -18.0 dB
SIR total    = -18.8 dB  ← interferencia domina sobre la señal útil
SNR          = +53.0 dB  ← sin interferencia, el enlace sería perfecto
```

El SIR negativo (-18.8 dB) significa que en los canales afectados la
interferencia WLAN es ~75 veces más potente que la señal Bluetooth.
Esto hace que el receptor no pueda decodificar correctamente, produciendo
el PER de ~30% observado en la simulación sin AFH.

---

### Paso 2.2 — Baseline sin interferencia (`bt_ber_comparison.m`, Escenario 1)

```matlab
runScenario(false, false, bbFilePath, simulationTime)
% enableWLANInterference = false
% enableChannelClassification = false
```

Solo hay AWGN (ruido térmico). Con SNR = +53 dB el enlace opera en condición
ideal. Resultado esperado: PER ≈ 0%, throughput máximo (~86 Kbps).

---

### Paso 2.3 — Canal con interferencia WLAN (`bt_ber_comparison.m`, Escenario 2)

```matlab
runScenario(true, false, bbFilePath, simulationTime)
% enableWLANInterference = true
% enableChannelClassification = false
```

Se agregan dos nodos `helperInterferingWLANNode` con la señal del archivo
`WLANHESUBandwidth20.bb` escalada a 20 dBm. El simulador aplica pérdida de
trayecto automáticamente según las posiciones configuradas.

La señal WLAN ocupa 20 MHz de ancho de banda y se transmite con periodicidad
de 2 ms, creando interferencia continua sobre los canales BT 30-55.

Resultado esperado: PER ≈ 30%, throughput cae ~53%.

---

### Paso 2.4 — Canal con interferencia WLAN + AFH (`bt_ber_comparison.m`, Escenario 3)

```matlab
runScenario(true, true, bbFilePath, simulationTime)
% enableWLANInterference = true
% enableChannelClassification = true
```

Se agrega `helperBluetoothChannelClassification` con PERThreshold = 40%.
El algoritmo AFH clasifica los 79 canales cada 250 ms y excluye los que
superan el umbral de PER. Después de 3 intervalos de clasificación (750 ms),
los canales 30-55 quedan marcados como "bad" y el tráfico se concentra en
los 54 canales restantes.

**Parámetros del algoritmo AFH:**

| Parámetro | Valor | Descripción |
|---|---|---|
| PERThreshold | 40% | Canal marcado como "bad" si PER > 40% |
| Periodicity | 250 ms | Intervalo de reclasificación |
| PreferredMinimumGoodChannels | 20 | Mínimo de canales buenos antes de revertir |
| MinReceptionsToClassify | 4 | Mínimo de paquetes para clasificar un canal |

Resultado esperado: PER baja de 30% a ~17%, throughput sube ~48% vs sin AFH.

---

## 6. Verificación de figuras (`bt_channel.m`)

### Figura 1 — Plan de frecuencias 2.4 GHz

**Qué verificar:**
- Canales verdes (libres): 0-29 y 56-78
- Canales rojos (interferidos): 30-55 aproximadamente
- Los rectángulos WLAN deben solaparse con exactamente esa zona roja

**Señal de alerta:** si la zona roja no está centrada alrededor de 2442-2447 MHz,
verificar las frecuencias `f_WLAN1_GHz` y `f_WLAN2_GHz` en la Sección 1.

---

### Figura 2 — Pérdida de trayecto indoor

**Qué verificar:**
- La curva verde (Bluetooth, 0 dBm) siempre está por debajo de la roja (WLAN, 20 dBm)
  a cualquier distancia — la WLAN siempre llega más fuerte al receptor BT
- Los tres marcadores corresponden a las distancias reales del escenario:
  BT @ 5m, WLAN2 @ 5.8m, WLAN1 @ 9.9m
- El ruido térmico (-114 dBm) queda muy por debajo de todas las señales —
  confirma que el ruido no es el factor limitante, sino la interferencia

---

### Figura 3 — Espectro WLAN sobre canales BT

**Qué verificar:**
- La PSD WLAN (roja) debe ser aproximadamente plana sobre 20 MHz — característica
  de señales OFDM como 802.11g/n
- Las dos caídas profundas en la PSD son normales: corresponden a los bordes de
  las subportadoras OFDM (subcarrier nulls)
- Las barras rojas en la parte inferior deben alinearse exactamente con la zona
  de alta potencia de la PSD WLAN

---

### Figura 4 — Balance de potencias

**Qué verificar:**
- La barra verde (BT útil) debe ser la más baja de las tres señales activas
- SIR vs WLAN2 ≈ -18 dB (la más dominante por estar más cerca)
- SIR vs WLAN1 ≈ -11 dB
- El ruido térmico debe estar ~53 dB por debajo de la señal BT útil

**Interpretación del SIR total = -18.8 dB:**
En los canales afectados, por cada 1 mW de señal Bluetooth, llegan ~75 mW de
interferencia WLAN. El receptor no puede separar la señal útil del ruido de
interferencia, produciendo el PER de ~30% observado sin AFH.

---

## 7. Conexión canal → métricas

Esta es la conexión central que la rúbrica evalúa (15% justificación del canal):

```
bt_channel.m calcula:        bt_ber_comparison.m mide:
─────────────────────────    ──────────────────────────────
SIR total = -18.8 dB    →   PER escenario 2 = 30.22%
SNR       = +53.0 dB    →   PER escenario 1 =  0.00%  (baseline)
AFH excluye ~26 canales →   PER escenario 3 = 17.40%  (con AFH)
```

El SIR negativo explica por qué el PER no es 0% ni 100%:
- No es 0% porque la interferencia WLAN es discontinua (2 ms de periodicidad)
  y el frequency hopping lleva la señal a canales libres parte del tiempo
- No es 100% porque ~67% de los canales (53 de 79) están libres de interferencia

Con AFH el PER no llega a 0% porque los primeros 250 ms de simulación
(antes del primer intervalo de clasificación) no tienen protección.

---

## 8. Puntos clave para el video

Esta sección cubre el contenido que el integrante del Rol B debe narrar
al presentar los resultados de `bt_channel.m`.

### Qué mostrar en pantalla
1. Correr `bt_channel.m` y mostrar el output de consola con los valores de SIR
2. Figura 1: señalar la zona roja y explicar cuántos canales quedan afectados
3. Figura 2: mostrar que la curva WLAN siempre está arriba de la BT → SIR negativo
4. Figura 3: señalar el espectro plano OFDM sobre los canales rojos
5. Figura 4: leer el SIR total = -18.8 dB y conectarlo con el PER de 30%

### Qué explicar sobre el modelo de canal
- El modelo log-distance (IEEE 802.15.2) es el estándar de referencia para
  coexistencia en 2.4 GHz — no se eligió arbitrariamente
- El exponente n=3 representa un entorno indoor con paredes y obstáculos;
  n=2 sería espacio libre, n=4 sería un entorno muy obstruido
- El escenario de dos APs sin coordinación de frecuencias es el más común
  en hogares y oficinas — y el más dañino para Bluetooth BR/EDR
- El AFH fue diseñado específicamente para este problema: la especificación
  Bluetooth Core Spec define el algoritmo de clasificación de canales como
  obligatorio para todos los dispositivos BR/EDR desde la versión 1.2 (2003)

### Dato para el video
Con SIR = -18.8 dB, la interferencia WLAN es aproximadamente **75 veces más
potente** que la señal Bluetooth útil en los canales afectados. Sin embargo,
el AFH reduce el PER de 30% a 17% simplemente evitando esos canales — sin
aumentar la potencia de transmisión ni cambiar la modulación.
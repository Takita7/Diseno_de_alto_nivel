# Fase 4 — Integración del Flujo Completo
## Equipo 5: Bluetooth BR/EDR con Interferencia WLAN | MP-6159 ITCR

---

## Índice
1. [Objetivo de la fase](#1-objetivo-de-la-fase)
2. [Scripts del proyecto — visión completa](#2-scripts-del-proyecto--visión-completa)
3. [Cómo ejecutar bt_main.m](#3-cómo-ejecutar-bt_mainm)
4. [Parámetros centralizados](#4-parámetros-centralizados)
5. [Resultados integrados](#5-resultados-integrados)
6. [Verificación de coherencia entre fases](#6-verificación-de-coherencia-entre-fases)
7. [Checklist final antes del video](#7-checklist-final-antes-del-video)

---

## 1. Objetivo de la fase

Ensamblar el flujo completo **Transmisor → Canal → Receptor** en un único
script maestro (`bt_main.m`) que consolida los resultados de las tres fases
anteriores, genera las figuras finales del proyecto y produce el resumen
completo de métricas normalizadas.

---

## 2. Scripts del proyecto — visión completa

```
Matlab/
│
│  SCRIPTS DE ANÁLISIS POR FASE (para análisis detallado)
├── bt_waveforms.m              Fase 1: formas de onda PHY BR/EDR y LE
├── bt_channel.m                Fase 2: caracterización analítica del canal
├── bt_ber_comparison.m         Fases 2-3: comparación de 3 escenarios
├── bt_receiver.m               Fase 3: validación vs. estándar + barrido INR
│
│  SCRIPTS DE SIMULACIÓN CON VISUALIZACIÓN
├── main_simulation.m           Simulación interactiva (canales en tiempo real)
│
│  SCRIPT MAESTRO DE INTEGRACIÓN
├── bt_main.m                   Fase 4: flujo completo + figuras finales
│
│  HELPERS (no modificar, excepto los fixes ya aplicados)
├── helperBluetoothChannelClassification.m   (fix NumConnections aplicado)
├── helperVisualizeCoexistence.m             (fix NumConnections aplicado)
├── helperInterferingWLANNode.m
└── WLANHESUBandwidth20.bb
```

### Rol de cada script en el video

| Script | Cuándo mostrarlo en el video | Qué muestra |
|---|---|---|
| `bt_waveforms.m` | Sección "Formato de trama / modulación" | Señales PHY de los 5 modos |
| `bt_channel.m` | Sección "Justificación del canal" | Plan de frecuencias y SIR |
| `main_simulation.m` | Sección "Evidencia de simulación" | Canales clasificándose en tiempo real |
| `bt_main.m` | Sección "Resultados finales" | Panel consolidado + figura central |

---

## 3. Cómo ejecutar bt_main.m

```matlab
bt_main
```

**Prerequisitos:** todos los helpers y `WLANHESUBandwidth20.bb` en la misma
carpeta. Bluetooth Toolbox + Wireless Network Toolbox instalados.

**Tiempo de ejecución:** ~3 minutos (3 simulaciones de 1.5s cada una).

**Output en consola:**
```
=== BLUETOOTH BR/EDR — SIMULACIÓN INTEGRADA (EQUIPO 5) ===

Corriendo escenarios...
  Escenario 1: baseline sin interferencia
  Escenario 2: con interferencia WLAN, sin AFH
  Escenario 3: con interferencia WLAN + AFH

============================================================
  MÉTRICAS CONSOLIDADAS — EQUIPO 5 BLUETOOTH BR/EDR
============================================================

                        Sin WLAN   Con WLAN sin AFH   Con WLAN+AFH
-----------------------------------------------------------------------
1. PER PHY:               0.00%          30.22%          17.40%
2. BER estimado:         0.0000%         0.1665%         0.0885%
3. Throughput:           86.40 Kbps     40.18 Kbps      59.47 Kbps
4. PLR app:               7.69%          15.20%          10.80%

Canales AFH: 54 buenos / 25 malos (32% excluidos)
Ganancia AFH: −12.82 pp PER | +19.30 Kbps throughput
```

---

## 4. Parámetros centralizados

Todos los parámetros del proyecto están en la Sección 1 de `bt_main.m`.
Para reproducir resultados exactos, mantener los valores por defecto.
Para experimentar, modificar aquí y los tres escenarios se actualizan.

| Parámetro | Valor | Descripción |
|---|---|---|
| `simulationTime` | 1.5 s | Duración de cada simulación de red |
| `BT_txPower_dBm` | 0 dBm | Potencia TX Bluetooth (Clase 2) |
| `BT_packetType` | "DH1" | Tipo de paquete ACL |
| `BT_dataRate_kbps` | 200 Kbps | Tasa de tráfico de aplicación |
| `N_payload_bits` | 216 bits | Payload DH1 para BER←PER |
| `WLAN_txPower_dBm` | 20 dBm | Potencia TX nodos WLAN |
| `WLAN_freq1_GHz` | 2.442 GHz | Frecuencia nodo WLAN 1 (canal 7) |
| `WLAN_freq2_GHz` | 2.447 GHz | Frecuencia nodo WLAN 2 (canal 8) |
| `AFH_perThreshold` | 40% | Umbral PER para clasificar canal como malo |
| `AFH_periodicity_s` | 250 ms | Intervalo de reclasificación de canales |
| `BER_limit_pct` | 0.1% | Límite BER del Core Spec v5.4 |

---

## 5. Resultados integrados

### Métricas normalizadas finales

| Métrica | Baseline | Con WLAN sin AFH | Con WLAN + AFH |
|---|---|---|---|
| **1. PER PHY** | 0.00% | 30.22% | 17.40% |
| **2. BER estimado** | 0.0000% | 0.1665% | 0.0885% |
| **3. Throughput** | 86.4 Kbps | 40.2 Kbps | 59.5 Kbps |
| **4. PLR aplicación** | 7.69% | 15.20% | 10.80% |
| **Core Spec v5.4** | ✓ OK | ✗ Viola | ✓ OK |

### Clasificación de canales AFH

| | Canales | % del espectro |
|---|---|---|
| Buenos (usados por AFH) | 54 / 79 | 68% |
| Malos (excluidos por AFH) | 25 / 79 | 32% |

Los 25 canales malos corresponden al rango 2432–2457 MHz, que es exactamente
la unión de las bandas de los dos nodos WLAN (canales 7 y 8 de 802.11).

### Ganancia del AFH

```
Reducción de PER:       12.82 puntos porcentuales (de 30.22% a 17.40%)
Ganancia de throughput: 19.30 Kbps               (de 40.18 a 59.47 Kbps)
Mejora relativa de PER: 42.4%
Mejora relativa de TP:  48.1%
Resultado de conformidad: el AFH baja el BER de 0.1665% a 0.0885%,
cruzando por debajo del límite de 0.1% del Core Spec v5.4
```

---

## 6. Verificación de coherencia entre fases

Esta sección conecta los resultados de los cuatro scripts para confirmar
que el sistema integrado es consistente.

### bt_channel.m → bt_main.m

| Cálculo en bt_channel.m | Resultado en bt_main.m | Coherente |
|---|---|---|
| SIR total = −18.8 dB (interferencia domina) | PER sin AFH = 30.22% (enlace degradado) | ✓ |
| SNR = +53 dB (sin interferencia, canal limpio) | PER baseline = 0.00% | ✓ |
| 26 canales BT afectados (~33%) | AFH excluyó 25 canales (32%) | ✓ |

### bt_waveforms.m → bt_main.m

| Parámetro PHY en bt_waveforms.m | Configuración en bt_main.m | Coherente |
|---|---|---|
| Modo BR, paquete DH1, 216 bits payload | `N_payload_bits = 216` | ✓ |
| Symbol rate 1 Msym/s, canal 1 MHz | Configuración `bluetoothConnectionConfig` | ✓ |

### bt_receiver.m → bt_main.m

| Resultado en bt_receiver.m | Resultado en bt_main.m | Coherente |
|---|---|---|
| BER baseline = 0.0000% → ✓ Core Spec | BER = 0.0000% → ✓ OK | ✓ |
| BER sin AFH = 0.1664% → ✗ viola | BER = 0.1665% → ✗ viola | ✓ |
| BER con AFH = 0.0885% → ✓ OK | BER = 0.0885% → ✓ OK | ✓ |

Los valores son idénticos porque todos los scripts usan `rng(1,'twister')`
y los mismos parámetros de canal y simulación.

---

## 7. Checklist final antes del video

Verificar cada punto antes de grabar:

### Simulaciones
- [ ] `bt_waveforms.m` corre sin errores y genera 5 figuras
- [ ] `bt_channel.m` corre sin errores y genera 4 figuras + consola con SIR
- [ ] `bt_ber_comparison.m` corre sin errores y genera 3 figuras
- [ ] `bt_receiver.m` corre sin errores y genera 3 figuras
- [ ] `main_simulation.m` corre sin errores y muestra figura de canales en tiempo real
- [ ] `bt_main.m` corre sin errores y genera 2 figuras + consola completa

### Coherencia de resultados
- [ ] PER baseline ≈ 0% (canal limpio sin interferencia)
- [ ] PER con WLAN sin AFH ≈ 30% (consistente con SIR = −18.8 dB)
- [ ] PER con WLAN + AFH ≈ 17% (AFH excluye los canales malos)
- [ ] BER con AFH < 0.1% (recupera conformidad Core Spec v5.4)
- [ ] Canales malos AFH ≈ 25 de 79 (≈32%, zona WLAN 2432–2457 MHz)

### Documentación
- [ ] README_fase1.md — Transmisor y formas de onda PHY
- [ ] README_fase2.md — Canal, justificación IEEE 802.15.2 y SIR
- [ ] README_fase3.md — Receptor, conformidad y barrido INR
- [ ] README_fase4.md — Integración y coherencia entre fases
- [ ] Guión del video preparado (Fase 5)

### Contenido del video (secciones obligatorias)
- [ ] Historia y estandarización: 2-3 min
- [ ] Arquitectura y stack de capas: 3-4 min
- [ ] Formato de trama / estructura de datos: 3-4 min
- [ ] Aplicaciones reales: 2 min
- [ ] Tendencias: 2 min
- [ ] Evidencia de la simulación: 4-5 min
- [ ] Duración total: 15-20 min
- [ ] Todos los integrantes hablan en el video
- [ ] Segmento de preguntas anticipadas incluido
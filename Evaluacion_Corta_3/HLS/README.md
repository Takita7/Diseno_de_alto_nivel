# HLS — Documentación técnica

## 1. Descripción general

Se implementó un acelerador de hardware utilizando **AMD Vitis HLS 2024.1**
para convertir una imagen RGB de **1920×1080** a escala de grises.

El acelerador recibe la imagen desde memoria mediante una interfaz
**AXI4 Memory Mapped**, realiza la conversión utilizando una arquitectura
pipeline y escribe la imagen resultante nuevamente en memoria.

La implementación fue verificada mediante:

- Simulación en C (C Simulation)
- Co-simulación C/RTL
- Comparación del resultado contra un modelo de referencia por software

Ver `SETUP_HLS.md` para instrucciones completas de compilación y ejecución.

---

# 2. Arquitectura

```
                     AXI4 Memory Mapped

        +--------------------------------------+
        |                                      |
        |         External DDR Memory          |
        |                                      |
        +---------------+----------------------+
                        |
                        |
                 AXI4 Master Read
                        |
                        v

        +--------------------------------------+
        |                                      |
        |      RGB to Grayscale Accelerator    |
        |                                      |
        |  +-----------+                       |
        |  | Read RGB  |                       |
        |  +-----------+                       |
        |         |                            |
        |         v                            |
        |  +--------------------+              |
        |  | Grayscale Pipeline |              |
        |  +--------------------+              |
        |         |                            |
        |         v                            |
        |  +-------------+                     |
        |  | Write Gray  |                     |
        |  +-------------+                     |
        |                                      |
        +---------------+----------------------+
                        |
                 AXI4 Master Write
                        |
                        v

        +--------------------------------------+
        |                                      |
        |         External DDR Memory          |
        |                                      |
        +--------------------------------------+
```

El acelerador se divide en tres etapas independientes:

- Lectura de la imagen RGB
- Conversión RGB → Escala de grises
- Escritura de la imagen procesada

Las tres etapas funcionan de manera concurrente utilizando la directiva
`#pragma HLS DATAFLOW`.

---

# 3. Organización del repositorio

```
Evaluacion_3/
└── HLS/
    ├── src/
    │   ├── grayscale_accel.cpp
    │   └── grayscale_accel.h
    │
    ├── tb/
    │   └── grayscale_tb.cpp
    │
    ├── data/
    │   ├── input.raw
    │   ├── output_hls_csim.raw
    │   └── output_hls_cosim.raw
    │
    ├── images/
    │   ├── input.png
    │   ├── output_hls_csim.png
    │   └── output_hls_cosim.png
    │
    ├── scripts/
    │   ├── gen_raw.py
    │   ├── raw_to_png.py
    │   └── run_hls.tcl
    │
    └── README.md
```

---

# 4. Interfaces implementadas

## AXI4 Memory Mapped

Utilizada para acceder directamente a la memoria externa.

| Puerto | Función |
|---------|----------|
| input | Lectura de la imagen RGB |
| output | Escritura de la imagen en escala de grises |

## AXI4-Lite

Utilizada para controlar el acelerador.

| Registro | Descripción |
|----------|-------------|
| input | Dirección de entrada |
| output | Dirección de salida |
| num_pixels | Cantidad de píxeles |
| return | Inicio y estado del acelerador |

---

# 5. Algoritmo de conversión

Cada píxel RGB se convierte utilizando la aproximación entera:

```
Gray = (77 × R + 150 × G + 29 × B) >> 8
```

Esta aproximación implementa:

```
Gray = 0.299R + 0.587G + 0.114B
```

sin utilizar operaciones de punto flotante, reduciendo el costo del hardware.

---

# 6. Directivas HLS utilizadas

La implementación utiliza las siguientes directivas:

- `#pragma HLS DATAFLOW`
- `#pragma HLS PIPELINE II=1`
- `#pragma HLS STREAM`
- `#pragma HLS INTERFACE m_axi`
- `#pragma HLS INTERFACE s_axilite`

Estas permiten aumentar el paralelismo y mejorar el rendimiento del acelerador.

---

# 7. Compilación y ejecución

Ver `SETUP_HLS.md`.

---

# 8. Resultados obtenidos

La simulación procesa una imagen RGB de:

- Resolución: **1920 × 1080**
- Total de píxeles: **2,073,600**

El testbench realiza las siguientes verificaciones:

- Lectura del archivo RAW
- Ejecución del acelerador
- Comparación contra un modelo de referencia
- Escritura del resultado

La simulación debe finalizar con:

```
TEST PASSED
```

Posteriormente, Vitis HLS genera:

- RTL Verilog
- Reporte de utilización
- Reporte de latencia
- Reporte de temporización

---

# 9. Archivos generados

Durante la ejecución se generan:

```
data/output_hls_csim.raw
data/output_hls_cosim.raw
images/output_hls_csim.png
images/output_hls_cosim.png
```

Los archivos PNG son generados utilizando `raw_to_png.py`
para facilitar la inspección visual del resultado.

---

# 10. Declaración de uso de IA

Se utilizó IA (ChatGPT, OpenAI) como herramienta de apoyo durante el desarrollo
del acelerador HLS.

El uso de IA se limitó a:

- Revisión de la arquitectura HLS.
- Explicación de directivas (`PIPELINE`, `DATAFLOW`, `STREAM`,
  `m_axi`, `s_axilite`).
- Revisión del código C++ para Vitis HLS.
- Apoyo en la creación del testbench.
- Elaboración de scripts auxiliares (`gen_raw.py`,
  `raw_to_png.py` y `run_hls.tcl`).
- Revisión de la documentación técnica.

Todas las decisiones de diseño, integración, validación y verificación fueron
realizadas y comprendidas antes de la entrega final.
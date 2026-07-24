# HLS — Guía de instalación y ejecución

> Pasos necesarios para reproducir la implementación HLS del acelerador
> RGB→Escala de Grises utilizando **AMD Vitis HLS 2024.1** sobre la
> plataforma **AMD Kria KV260**.

---

# 1. Dependencias

## Software requerido

- AMD Vitis Unified 2024.1
- AMD Vivado 2024.1
- Python 3.10 o superior
- NumPy
- Matplotlib

Instalar las librerías de Python:

```bash
python -m pip install numpy matplotlib
```

---

# 2. Estructura del proyecto

El proyecto debe tener la siguiente organización:

```text
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
    │
    ├── images/
    │
    ├── scripts/
    │   ├── png_to_raw.py
    │   ├── raw_to_png.py
    │   └── run_hls.tcl
    │
    └── README.md
```

---

# 3. Generar la imagen de entrada

El acelerador utiliza una imagen RGB en formato RAW.

Para generar automáticamente la imagen de prueba:

```bash
python scripts/png_to_raw.py
```

El script genera:

```text
data/input.raw
```

Características de la imagen:

- Resolución: **1920 × 1080**
- Formato: **RGB888**
- Tamaño esperado:

```
6220800 bytes
```

---

# 4. Visualizar la imagen de entrada (Opcional)

Para convertir la imagen RAW a PNG:

```bash
python scripts/raw_to_png.py data/input.raw
```

Se genera:

```text
images/input.png
```

Este paso es únicamente para inspección visual.

---

# 5. Ejecutar Vitis HLS

Abrir una terminal de **AMD Vitis HLS 2024.1**.

Moverse al directorio del proyecto:

```bash
cd Evaluacion_3/HLS
```

Ejecutar:

```bash
vitis_hls -f scripts/run_hls.tcl
```

El script realiza automáticamente:

- Creación del proyecto HLS
- Adición del acelerador
- Adición del testbench
- Configuración del dispositivo KV260
- Configuración del reloj a 250 MHz
- Simulación en C
- Síntesis HLS
- Co-simulación C/RTL
- Exportación del IP

---

# 6. Resultados generados

La simulación produce:

```text
data/output_hls_csim.raw
```

La co-simulación produce:

```text
data/output_hls_cosim.raw
```

Ambos archivos deben tener un tamaño de:

```
2073600 bytes
```

---

# 7. Visualizar los resultados

Convertir la salida de la simulación en C:

```bash
python scripts/raw_to_png.py data/output_hls_csim.raw
```

Genera:

```text
images/output_hls_csim.png
```

Convertir la salida de la co-simulación:

```bash
python scripts/raw_to_png.py data/output_hls_cosim.raw
```

Genera:

```text
images/output_hls_cosim.png
```

---

# 8. Reportes generados por Vitis HLS

Después de la síntesis se generan los reportes dentro del proyecto:

```text
grayscale_hls_project/
└── solution1/
    ├── csim/
    ├── syn/
    │   └── report/
    │       ├── grayscale_accel_csynth.rpt
    │       ├── grayscale_accel_csynth.xml
    │       ├── utilization.rpt
    │       └── timing.rpt
    │
    ├── sim/
    └── impl/
```

Los reportes incluyen:

- Latencia
- Initiation Interval (II)
- Utilización de LUT
- Utilización de FF
- Utilización de DSP
- Utilización de BRAM
- Frecuencia alcanzada

---

# 9. Exportación del IP

Al finalizar la ejecución se genera automáticamente un IP compatible con
Vivado.

Ubicación:

```text
grayscale_hls_project/
└── solution1/
    └── impl/
        └── ip/
```

Este IP puede integrarse posteriormente dentro de un diseño Vivado.

---

# 10. Validación

La implementación se considera correcta cuando:

- La simulación en C finaliza con:

```
TEST PASSED
```

- La co-simulación finaliza sin errores.

- Los archivos:

```text
output_hls_csim.raw
```

y

```text
output_hls_cosim.raw
```

son idénticos.

- Las imágenes PNG obtenidas corresponden a la conversión correcta de la
imagen RGB original a escala de grises.
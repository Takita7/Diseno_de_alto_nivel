# MP6160 – EC2: Sistema de Procesamiento de Imagen con SystemC TLM 2.0

> **Diseño de Alto Nivel | II Cuatrimestre 2026**
> Profesor: Luis G. León-Vega, Ph.D

---

## Tabla de Contenidos
1. [Descripción del Sistema](#descripción-del-sistema)
2. [Requisitos e Instalación](#requisitos-e-instalación)
3. [Compilación y Ejecución](#compilación-y-ejecución)
4. [Organización del Repositorio](#organización-del-repositorio)
5. [Organización de los Módulos](#organización-de-los-módulos)
6. [Diagrama de Bloques](#diagrama-de-bloques)
7. [Diagrama de Secuencias](#diagrama-de-secuencias)
8. [Formato de Transacciones TLM](#formato-de-transacciones-tlm)
9. [Mapa de Memoria](#mapa-de-memoria)
10. [Resultados Obtenidos](#resultados-obtenidos)
11. [Referencias](#referencias)
12. [Declaración de Uso de IA](#declaración-de-uso-de-ia)

---

## Descripción del Sistema

Este proyecto modela a nivel de transacciones (TLM 2.0) un sistema embebido para
procesamiento de imagen 1080p. El sistema implementa el siguiente flujo:

```
Disco → CPU → RAM → Acelerador → RAM → CPU → Disco
```

El acelerador convierte una imagen RAW RGB (1920×1080×3 bytes) a escala de grises
usando una aproximación entera de la fórmula **ITU-R BT.601**:

```
Y = 0.299·R + 0.587·G + 0.114·B
```

Implementada en el acelerador como aritmética entera para evitar punto flotante:

```cpp
gray = (77·R + 150·G + 29·B) >> 8
```

Los coeficientes (77, 150, 29) son las aproximaciones enteras de los pesos BT.601
escalados a 256.

---

## Requisitos e Instalación

### Dependencias

| Herramienta | Versión mínima | Instalación (Ubuntu/Debian)        |
|-------------|----------------|------------------------------------|
| GCC / G++   | 9.0            | `sudo apt install build-essential` |
| CMake       | 3.10           | `sudo apt install cmake`           |
| SystemC     | 2.3.3          | Ver instrucciones abajo            |
| Python 3    | 3.8            | `sudo apt install python3 python3-pip` |
| NumPy       | 1.20           | `pip install numpy`                |
| Pillow      | 8.0            | `pip install Pillow`               |

### Instalación de SystemC

SystemC no viene en los repositorios estándar de Ubuntu, hay que compilarlo desde
la fuente oficial de Accellera.

```bash
# 1. Descargar el código fuente desde:
#    https://www.accellera.org/downloads/standards/systemc
#    (requiere registro gratuito en el sitio)
tar -xzf systemc-2.3.4.tar.gz
cd systemc-2.3.4

# 2. Compilar e instalar
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/systemc
make -j$(nproc)
sudo make install

# 3. Verificar instalación
ls /usr/local/systemc/include/systemc.h
```

> Alternativa: en algunas distribuciones está disponible como `libsystemc-dev`
> vía `apt`, pero suele ser una versión más antigua. Se recomienda compilar
> desde la fuente para tener TLM 2.0 completo.

---

## Compilación y Ejecución

```bash
# 1. Clonar el repositorio
git clone https://github.com/Takita7/Diseno_de_alto_nivel/tree/main/Evaluacion_Corta_2
cd Evaluacion_Corta_2/test

# 2. Compilar (ajustar la ruta si SystemC está en otro lugar)
make all SYSTEMC_HOME=/usr/local/systemc

# 3. Generar la imagen de prueba (patrón sintético 1920×1080 RGB)
make gen

# 4. Ejecutar la simulación completa (compila + corre + genera comparación)
make run
```

También se puede ejecutar paso a paso:

```bash
make exec      # Ejecutar con imagen de entrada ya existente
make view      # Ver salida en escala de grises
make compare   # Generar figura comparativa input vs output
make view-show # Abrir la figura de comparación automáticamente
```

Salida esperada en consola:

```
[CPU] PASO 1: Cargando imagen desde disco
[STORAGE] Cargado: images/input.raw (6220800 bytes)
[CPU] PASO 2: Escribiendo imagen en RAM
[CPU] PASO 3: Configurando acelerador
[CPU] PASO 4: Iniciando acelerador
[ACCEL] Iniciando conversion RGB->Gray
[ACCEL] Progreso: 204800 / 2073600 px (9%)
...
[ACCEL] Conversion completada
[CPU] PASO 5: Leyendo imagen procesada
[CPU] PASO 6: Guardando imagen en disco
[STORAGE] Guardado: images/output.raw (2073600 bytes)
[CPU] Simulacion completada en X ns
```

### Limpiar artefactos de compilación

```bash
make clean        # Elimina objetos y binario
make clean-all    # Elimina también las imágenes generadas
```

---

## Organización del Repositorio

```
Evaluacion_Corta_2/
├── src/
│   ├── sc_main.cpp          # Punto de entrada: instancia módulos y binding
│   ├── storage.h / .cpp     # Almacenamiento persistente (E/S de archivos)
│   ├── ram.h                # Memoria RAM de 64 MB (target TLM)
│   ├── bus.h                # Bus TLM 2.0 (router de transacciones)
│   ├── accelerator.h / .cpp # Acelerador RGB→Grayscale (target + initiator)
│   └── cpu.h                # CPU (iniciador, controla el flujo)
├── test/
│   ├── Makefile             # Sistema de compilación
│   ├── images/              # Imágenes de entrada y salida (.raw y previews .png)
│   ├── scripts/
│   │   ├── gen_raw.py       # Genera imagen de prueba 1080p RAW RGB
│   │   ├── view_raw.py      # Visualiza un archivo .raw como imagen
│   │   └── compare_raw.py   # Genera comparación entrada vs salida
│   └── archived/            # Versiones previas del sc_main durante desarrollo
└── README.md
```

> **Nota:** El `Makefile` se encuentra dentro de `test/` y referencia las fuentes
> en `../src`. Todos los comandos `make` deben ejecutarse desde `test/`.

---

## Organización de los Módulos

### `Storage` (`src/storage.h`)
- **Tipo:** Módulo SC sin sockets TLM.
- **Responsabilidad:** Leer `images/input.raw` desde disco y escribir `images/output.raw`.
- **Métodos:** `load_image()` devuelve `std::vector<uint8_t>`; `save_image()`
  recibe el vector y lo escribe.
- **Nota:** Usa `std::ios::binary` obligatoriamente para evitar corrupción de
  datos binarios.

### `RAM` (`src/ram.h`)
- **Tipo:** Target TLM 2.0 con `simple_target_socket`.
- **Capacidad:** 64 MB implementados como `std::vector<uint8_t>`.
- **Interfaces:** `b_transport` para transacciones normales; `transport_dbg`
  para inspección sin avance de tiempo.
- **Latencia modelada:** 1 ns por byte transferido.

### `Bus` (`src/bus.h`)
- **Tipo:** Router TLM 2.0 con dos target sockets y dos initiator sockets.
- **Lógica de ruteo:**
  - `addr ≤ 0x03FFFFFF` → RAM
  - `0x04000000 ≤ addr ≤ 0x040000FF` → Acelerador (offset = addr − 0x04000000)
- **Importante:** Las transacciones originadas en el acelerador van siempre
  directo a RAM, sin pasar por la lógica de decodificación.

### `Accelerator` (`src/accelerator.h`)
- **Tipo:** Target TLM 2.0 (registros de control) + Initiator TLM 2.0 (acceso
  a RAM).
- **Registros:** SRC, DST, CNT, CTRL, STATUS (ver mapa de memoria).
- **Hilo `SC_THREAD process_thread`:** espera un `sc_event` que se dispara
  cuando el CPU escribe `1` en `REG_CTRL`. Procesa en bloques de 1 024 píxeles.
- **Fórmula:** ITU-R BT.601 — aproximación entera `(77·R + 150·G + 29·B) >> 8`.
- **Latencia modelada:** 100 ns por bloque de 1 024 píxeles procesados.

### `CPU` (`src/cpu.h`)
- **Tipo:** Initiator TLM 2.0 con `simple_initiator_socket`.
- **Hilo `SC_THREAD run`:** implementa los 6 pasos del flujo del sistema.
- **Helpers privados:** `tlm_write`, `tlm_read`, `write_reg`, `read_reg`
  encapsulan la construcción del `tlm_generic_payload`.
- **Transferencias:** burst de 64 KB para DMA de imagen; escrituras de 4
  bytes para registros.
- **Polling:** lee `REG_STATUS` cada 500 ns hasta que el acelerador reporta
  `STATUS_DONE = 2`.

---

## Diagrama de Bloques

```
┌─────────────────────────────────────────────────────────────────┐
│                      Sistema TLM 2.0                            │
│                                                                 │
│  ┌─────────┐  TLM   ┌──────────────────────────────────────┐    │
│  │   CPU   │◄──────►│              BUS                     │    │
│  │(Master) │        │   (Router de Transacciones TLM 2.0)  │    │
│  └────┬────┘        └──────────┬───────────────┬───────────┘    │
│       │                        │               │                │
│       │ set_storage()          │ ram_socket    │ accel_out      │
│       ▼                        ▼               ▼                │
│  ┌─────────┐           ┌────────────┐  ┌──────────────────┐     │
│  │ Storage │           │    RAM     │  │   Accelerator    │     │
│  │(Archivo)│           │  (64 MB)   │  │  (RGB→Grayscale) │     │
│  └─────────┘           │  Target    │  │  Target + Init   │     │
│   load/save            └────────────┘  └────────┬─────────┘     │
│   archivos RAW               ▲                   │              │
│                              │    init_socket    │              │
│                              └───────────────────┘              │
│                          (Accel accede RAM via Bus)             │
└─────────────────────────────────────────────────────────────────┘
```

### Roles TLM de cada módulo

| Módulo      | Rol TLM                           | Socket(s)                                          |
|-------------|-----------------------------------|----------------------------------------------------|
| CPU         | Initiator                         | `simple_initiator_socket`                          |
| RAM         | Target                            | `simple_target_socket`                             |
| Bus         | Target + Initiator (router)       | 2 target sockets, 2 initiator sockets              |
| Accelerator | Target (regs) + Initiator (datos) | `simple_target_socket` + `simple_initiator_socket` |
| Storage     | Sin TLM                           | E/S directa de archivos                            |

---

## Diagrama de Secuencias

```
CPU              Storage        Bus            RAM         Accelerator
 │                  │             │              │               │
 │──load_image()───►│             │              │               │
 │◄──vector<uint8>──┘             │              │               │
 │                                │              │               │
 │──TLM_WRITE(0x0000, 64KB)──────►│              │               │
 │                                │─TLM_WRITE───►│               │
 │                                │◄──OK─────────┤               │
 │◄───────────────────────────────┤              │               │
 │        (x N chunks 64KB)       │              │               │
 │                                │              │               │
 │──TLM_WRITE(REG_SRC, 0x0000)───►│──────────────────TLM_WRITE──►│
 │──TLM_WRITE(REG_DST, 0x600000)─►│──────────────────TLM_WRITE──►│
 │──TLM_WRITE(REG_CNT, 2073600)──►│──────────────────TLM_WRITE──►│
 │──TLM_WRITE(REG_CTRL, 1)───────►│──────────────────TLM_WRITE──►│
 │                                │              │          start_ev_.notify()
 │                                │              │               │──[SC_THREAD]─┐
 │──poll REG_STATUS (cada 500ns)  │              │               │  mem_read()  │
 │                                │              │◄──TLM_READ────┤              │
 │                                │              │───data────────►              │
 │                                │              │               │  BT.709      │
 │                                │              │               │  convert     │
 │                                │              │◄──TLM_WRITE───┤              │
 │                                │              │               │◄─────────────┘
 │                                │              │          STATUS_DONE
 │◄──REG_STATUS == 2─────────────────────────────┤               │
 │                                │              │               │
 │──TLM_READ(0x800000, 64KB)─────►│              │               │
 │                                │─TLM_READ────►│               │
 │                                │◄──data───────┤               │
 │◄───────────────────────────────┤              │               │
 │        (x N chunks 64KB)       │              │               │
 │                                │              │               │
 │──save_image()──────────────────►              │               │
 │◄───────────────────────────────┘              │               │
 │                                               │               │
[sc_stop()]
```

---

## Formato de Transacciones TLM

Todas las transacciones usan `tlm::tlm_generic_payload` con los siguientes
campos:

| Campo              | Tipo                                        | Valor usado                     |
|--------------------|---------------------------------------------|---------------------------------|
| `command`          | `TLM_READ_COMMAND` / `TLM_WRITE_COMMAND`    | Según la operación              |
| `address`          | `uint64_t`                                  | Dirección en el espacio de bus  |
| `data_ptr`         | `uint8_t*`                                  | Puntero al buffer de datos      |
| `data_length`      | `unsigned int`                              | Bytes a transferir              |
| `streaming_width`  | `unsigned int`                              | Igual a `data_length`           |
| `byte_enable_ptr`  | `nullptr`                                   | No se usa                       |
| `dmi_allowed`      | `false`                                     | DMI deshabilitado               |
| `response_status`  | `TLM_INCOMPLETE_RESPONSE` → `TLM_OK_RESPONSE` | Se actualiza en el target     |

### Tipos de transacciones por módulo

| Origen → Destino  | Tipo        | Dirección (bus)           | Tamaño            |
|-------------------|-------------|---------------------------|-------------------|
| CPU → RAM         | WRITE burst | `0x00000000`              | 64 KB chunks      |
| CPU → Accel regs  | WRITE       | `0x04000000 – 0x0400000C` | 4 bytes           |
| CPU → Accel regs  | READ        | `0x04000010`              | 4 bytes           |
| CPU → RAM         | READ burst  | `0x00600000`              | 64 KB chunks      |
| Accel → RAM       | READ burst  | `0x00000000`              | 3 KB (1K px × 3)  |
| Accel → RAM       | WRITE burst | `0x00600000`              | 1 KB (1K px)      |

---

## Mapa de Memoria

```
Espacio de direcciones del Bus (32 bits)
═══════════════════════════════════════════════════════

  0x00000000  ┌─────────────────────────────────────┐
              │           RAM (64 MB)               │
              │                                     │
  0x00000000  │  Imagen RGB de entrada              │  6 220 800 bytes
  0x005EEFFF  │  (1920 × 1080 × 3 bytes)            │
              │                                     │
  0x005EF000  │  [sin usar]                         │
  0x005FFFFF  │                                     │
              │                                     │
  0x00600000  │  Imagen Grayscale de salida         │  2 073 600 bytes
  0x007F3FFF  │  (1920 × 1080 × 1 byte)             │
              │                                     │
  0x007F4000  │  [libre]                            │
  0x03FFFFFF  │                                     │
              └─────────────────────────────────────┘

  0x04000000  ┌─────────────────────────────────────┐
              │     Registros del Acelerador        │
  +0x00       │  REG_SRC    (R/W)  dir. imagen RGB  │
  +0x04       │  REG_DST    (R/W)  dir. imagen gris │
  +0x08       │  REG_CNT    (R/W)  total píxeles    │
  +0x0C       │  REG_CTRL   (R/W)  escribir 1=start │
  +0x10       │  REG_STATUS (R)    0=IDLE 1=BUSY    │
              │                    2=DONE           │
  0x04000014  └─────────────────────────────────────┘
```

---

## Resultados Obtenidos

- La imagen de entrada (`test/images/input.raw`) es un patrón sintético 1920×1080 RGB
  generado por `test/scripts/gen_raw.py`.
- La imagen de salida (`test/images/output.raw`) contiene los valores de luminancia
  calculados con BT.709.
- La verificación visual se genera con `make compare`, produciendo
  `test/images/comparison.png` con ambas imágenes lado a lado.

### Separación de capas

| Capa                    | Módulo(s)    | Mecanismo                              |
|-------------------------|--------------|----------------------------------------|
| Procesamiento funcional | `Accelerator`| `SC_THREAD`, aritmética BT.709         |
| Comunicación TLM        | `Bus`, sockets | `tlm_generic_payload`, `b_transport` |
| Almacenamiento temporal | `RAM`        | `std::vector<uint8_t>` de 64 MB        |
| E/S persistente         | `Storage`    | `std::ifstream` / `std::ofstream`      |

---

## Referencias

- ITU-R BT.601-7, *"Studio encoding parameters of digital television for
  standard 4:3 and wide-screen 16:9 aspect ratios"*, International
  Telecommunication Union, 2011.
- IEEE Std 1666-2023, *"IEEE Standard for Standard SystemC Language
  Reference Manual"*, IEEE, 2023.
- Accellera Systems Initiative, *SystemC Reference Implementation*,
  https://github.com/accellera-official/systemc

---

## Declaración de Uso de IA

De acuerdo con la política de uso de IA del curso MP6160, se declara el siguiente
uso de inteligencia artificial en esta evaluación:

**Herramienta utilizada:** Claude (Anthropic) — interfaz de chat web (claude.ai)

### Resumen del uso

Se utilizó Claude como apoyo conceptual durante el diseño del sistema, mediante
una conversación guiada paso a paso en lugar de generación masiva de código.

### Detalle por categoría

| Categoría               | ¿Se usó? | Descripción                                                                 |
|-------------------------|----------|-----------------------------------------------------------------------------|
| Consulta de conceptos   | Sí       | TLM 2.0 (sockets, `b_transport`, `tlm_generic_payload`, `sc_event`); fórmula de conversión ITU-R BT.601 e implementación entera |
| Revisión de código      | Sí       | Corrección de errores de síntaxis en los módulos                            |
| Depuración              | No       | —                                                                           |
| Generación de diagramas | Sí       | Diagrama de bloques (ASCII) y diagrama de secuencias                        |
| Mejora de redacción     | Sí       | Estructuración del README técnico                                           |
| Revisión de documentación | Sí     | Verificación de consistencia entre README y estructura real del repositorio |

### Descripción detallada

- **Consulta de conceptos:** se consultaron los fundamentos de TLM 2.0 antes de
  escribir cada módulo, así como la fórmula de conversión RGB→escala de grises
  BT.601 y su implementación como aproximación entera sin punto flotante.

- **Consulta de documentación oficial:** se verificó que los patrones usados
  (`simple_initiator_socket`/`simple_target_socket`, modelo `b_transport`) estuvieran
  alineados con los ejemplos del repositorio oficial de Accellera
  (`accellera-official/systemc`) y el estándar IEEE 1666-2023.

- **Implementación guiada:** el equipo escribió el código de los cinco módulos
  (`Storage`, `RAM`, `Bus`, `Accelerator`, `CPU`) guiándose por explicaciones
  conceptuales y fragmentos de referencia generados con apoyo de Claude.
  Durante la implementación se cometieron y corrigieron errores propios.

- **Generación de diagramas:** el diagrama de bloques y el diagrama de secuencias
  se generaron con apoyo de IA.

- **Mejora de redacción y revisión:** se usó para estructurar el README técnico
  y verificar la consistencia entre la documentación y la estructura real del repositorio.

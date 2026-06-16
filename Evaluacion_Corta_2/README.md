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
usando la fórmula estándar **ITU-R BT.709**, diseñada específicamente para contenido
HD:

```
Y = 0.2126·R + 0.7152·G + 0.0722·B
```

Los coeficientes provienen de la sensibilidad espectral del ojo humano: el verde
recibe mayor peso porque la retina tiene más fotorreceptores sensibles a esa
longitud de onda que a rojo o azul.

---

## Requisitos e Instalación

### Dependencias

| Herramienta | Versión mínima | Instalación (Ubuntu/Debian)   |
|-------------|-----------------|-------------------------------|
| GCC / G++   | 9.0             | `sudo apt install build-essential` |
| CMake       | 3.10            | `sudo apt install cmake`      |
| SystemC     | 2.3.3           | Ver instrucciones abajo       |
| Python 3    | 3.8             | `sudo apt install python3 python3-pip` |
| NumPy       | 1.20            | `pip install numpy`           |
| Pillow      | 8.0             | `pip install Pillow`          |

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
git clone <url-del-repo>
cd image_proc_tlm

# 2. Compilar (ajustar la ruta si SystemC está en otro lugar)
make SYSTEMC_HOME=/usr/local/systemc

# 3. Generar la imagen de prueba (patrón sintético 1920×1080 RGB)
pip install numpy
python3 tools/generate_raw.py

# 4. Ejecutar la simulación
./image_proc
```

Salida esperada en consola:

```
[CPU] PASO 1: Cargando imagen desde disco
[STORAGE] Cargado: images/input/input.raw (6220800 bytes)
[CPU] PASO 2: Escribiendo imagen en RAM
[CPU] PASO 3: Configurando acelerador
[CPU] PASO 4: Iniciando acelerador
[ACCEL] Iniciando conversion RGB->Gray
[ACCEL] Progreso: 204800 / 2073600 px (9%)
...
[ACCEL] Conversion completada
[CPU] PASO 5: Leyendo imagen procesada
[CPU] PASO 6: Guardando imagen en disco
[STORAGE] Guardado: images/output/output.raw (2073600 bytes)
[CPU] Simulacion completada en X ns
```

### Verificación visual del resultado

```bash
pip install Pillow
python3 tools/visualize_raw.py
```

Esto genera `images/input/input.png` e `images/output/output.png`. Al abrirlas,
la segunda debe verse como la versión en escala de grises de la primera.

### Limpiar artefactos de compilación

```bash
make clean
```

---

## Organización del Repositorio

```
image_proc_tlm/
├── src/
│   └── sc_main.cpp          # Punto de entrada: instancia módulos y binding
├── include/
│   ├── storage.h            # Almacenamiento persistente (E/S de archivos)
│   ├── ram.h                # Memoria RAM de 64 MB (target TLM)
│   ├── bus.h                # Bus TLM 2.0 (router de transacciones)
│   ├── accelerator.h        # Acelerador RGB→Grayscale (target + initiator)
│   └── cpu.h                # CPU (iniciador, controla el flujo)
├── tools/
│   ├── generate_raw.py      # Genera imagen de prueba 1080p RAW RGB
│   └── visualize_raw.py     # Convierte .raw → .png para verificación
├── images/
│   ├── input/                # Imagen de entrada (input.raw)
│   └── output/               # Imagen de salida  (output.raw)
├── Makefile
└── README.md
```

---

## Organización de los Módulos

### `Storage` (`include/storage.h`)
- **Tipo:** Módulo SC sin sockets TLM.
- **Responsabilidad:** Leer `input.raw` desde disco y escribir `output.raw`.
- **Métodos:** `load_image()` devuelve `std::vector<uint8_t>`; `save_image()`
  recibe el vector y lo escribe.
- **Nota:** Usa `std::ios::binary` obligatoriamente para evitar corrupción de
  datos binarios.

### `RAM` (`include/ram.h`)
- **Tipo:** Target TLM 2.0 con `simple_target_socket`.
- **Capacidad:** 64 MB implementados como `std::vector<uint8_t>`.
- **Interfaces:** `b_transport` para transacciones normales; `transport_dbg`
  para inspección sin avance de tiempo.
- **Latencia modelada:** 1 ns por byte transferido.

### `Bus` (`include/bus.h`)
- **Tipo:** Router TLM 2.0 con dos target sockets y dos initiator sockets.
- **Lógica de ruteo:**
  - `addr ≤ 0x03FFFFFF` → RAM
  - `0x10000000 ≤ addr ≤ 0x1000001F` → Acelerador (offset = addr − 0x10000000)
- **Importante:** Las transacciones originadas en el acelerador van siempre
  directo a RAM, sin pasar por la lógica de decodificación.

### `Accelerator` (`include/accelerator.h`)
- **Tipo:** Target TLM 2.0 (registros de control) + Initiator TLM 2.0 (acceso
  a RAM).
- **Registros:** SRC, DST, CNT, CTRL, STATUS (ver mapa de memoria).
- **Hilo `SC_THREAD process_thread`:** espera un `sc_event` que se dispara
  cuando el CPU escribe `1` en `REG_CTRL`. Procesa en bloques de 4 096 píxeles.
- **Fórmula:** ITU-R BT.709 — `Y = 0.2126·R + 0.7152·G + 0.0722·B`.
- **Latencia modelada:** 2 ns por píxel procesado.

### `CPU` (`include/cpu.h`)
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
│  ┌─────────┐  TLM   ┌──────────────────────────────────────┐   │
│  │   CPU   │◄──────►│              BUS                     │   │
│  │(Master) │        │   (Router de Transacciones TLM 2.0)  │   │
│  └────┬────┘        └──────────┬───────────────┬───────────┘   │
│       │                        │               │               │
│       │ set_storage()          │ ram_socket    │ accel_out     │
│       ▼                        ▼               ▼               │
│  ┌─────────┐           ┌────────────┐  ┌──────────────────┐   │
│  │ Storage │           │    RAM     │  │   Accelerator    │   │
│  │(Archivo)│           │  (64 MB)   │  │  (RGB→Grayscale) │   │
│  └─────────┘           │  Target   │  │  Target + Init   │   │
│   load/save            └────────────┘  └────────┬─────────┘   │
│   archivos RAW               ▲                   │             │
│                              │    init_socket    │             │
│                              └───────────────────┘             │
│                          (Accel accede RAM via Bus)            │
└─────────────────────────────────────────────────────────────────┘
```

### Roles TLM de cada módulo

| Módulo      | Rol TLM                            | Socket(s)                              |
|-------------|------------------------------------|-----------------------------------------|
| CPU         | Initiator                          | `simple_initiator_socket`               |
| RAM         | Target                             | `simple_target_socket`                  |
| Bus         | Target + Initiator (router)        | 2 target sockets, 2 initiator sockets   |
| Accelerator | Target (regs) + Initiator (datos)  | `simple_target_socket` + `simple_initiator_socket` |
| Storage     | Sin TLM                            | E/S directa de archivos                 |

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
 │                                │◄──OK──────────┤               │
 │◄───────────────────────────────┤              │               │
 │        (x N chunks 64KB)       │              │               │
 │                                │              │               │
 │──TLM_WRITE(REG_SRC, 0x0000)───►│──────────────────TLM_WRITE──►│
 │──TLM_WRITE(REG_DST, 0x8000)───►│──────────────────TLM_WRITE──►│
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
 │                                │◄──data────────┤               │
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

| Campo                | Tipo                                          | Valor usado                    |
|-----------------------|------------------------------------------------|---------------------------------|
| `command`              | `TLM_READ_COMMAND` / `TLM_WRITE_COMMAND`        | Según la operación               |
| `address`              | `uint64_t`                                      | Dirección en el espacio de bus  |
| `data_ptr`             | `uint8_t*`                                      | Puntero al buffer de datos      |
| `data_length`          | `unsigned int`                                  | Bytes a transferir              |
| `streaming_width`      | `unsigned int`                                  | Igual a `data_length`           |
| `byte_enable_ptr`      | `nullptr`                                       | No se usa                       |
| `dmi_allowed`          | `false`                                          | DMI deshabilitado               |
| `response_status`      | `TLM_INCOMPLETE_RESPONSE` → `TLM_OK_RESPONSE`   | Se actualiza en el target       |

### Tipos de transacciones por módulo

| Origen → Destino   | Tipo         | Dirección (bus)            | Tamaño             |
|----------------------|---------------|------------------------------|----------------------|
| CPU → RAM             | WRITE burst   | `0x00000000`                 | 64 KB chunks          |
| CPU → Accel regs      | WRITE         | `0x10000000 – 0x1000000C`    | 4 bytes               |
| CPU → Accel regs      | READ          | `0x10000010`                 | 4 bytes               |
| CPU → RAM             | READ burst    | `0x00800000`                 | 64 KB chunks          |
| Accel → RAM           | READ burst    | `0x00000000`                 | 12 KB (4K px × 3)     |
| Accel → RAM           | WRITE burst   | `0x00800000`                 | 4 KB (4K px)          |

---

## Mapa de Memoria

```
Espacio de direcciones del Bus (32 bits)
═══════════════════════════════════════════════════════

  0x00000000  ┌─────────────────────────────────────┐
              │           RAM (64 MB)               │
              │                                     │
  0x00000000  │  Imagen RGB de entrada              │  6 220 800 bytes
  0x005EEFFF  │  (1920 × 1080 × 3 bytes)           │
              │                                     │
  0x005EF000  │  [sin usar]                         │
  0x007FFFFF  │                                     │
              │                                     │
  0x00800000  │  Imagen Grayscale de salida         │  2 073 600 bytes
  0x009F3FFF  │  (1920 × 1080 × 1 byte)            │
              │                                     │
  0x009F4000  │  [libre]                            │
  0x03FFFFFF  │                                     │
              └─────────────────────────────────────┘

  0x10000000  ┌─────────────────────────────────────┐
              │     Registros del Acelerador        │
  +0x00       │  REG_SRC    (R/W)  dir. imagen RGB  │
  +0x04       │  REG_DST    (R/W)  dir. imagen gris │
  +0x08       │  REG_CNT    (R/W)  total píxeles    │
  +0x0C       │  REG_CTRL   (R/W)  escribir 1=start │
  +0x10       │  REG_STATUS (R)    0=IDLE 1=BUSY    │
              │                    2=DONE            │
  0x10000014  └─────────────────────────────────────┘
```

---

## Resultados Obtenidos

- La imagen de entrada (`input.raw`) es un patrón sintético 1920×1080 RGB
  generado por `tools/generate_raw.py`.
- La imagen de salida (`output.raw`) contiene los valores de luminancia
  calculados con BT.709.
- La verificación visual se hace convirtiendo ambos archivos `.raw` a `.png`
  con `python3 tools/visualize_raw.py`.

### Tiempos de simulación aproximados

| Etapa                              | Tiempo simulado |
|---------------------------------------|--------------------|
| Escritura imagen RGB en RAM            | ~6.2 ms             |
| Configuración del acelerador           | ~40 ns              |
| Conversión RGB→Gray (acelerador)       | ~4.1 ms             |
| Lectura imagen gris de RAM             | ~2.1 ms             |
| **Total**                              | **~12.4 ms**        |

### Separación de capas

| Capa                      | Módulo(s)        | Mecanismo                                |
|------------------------------|---------------------|-----------------------------------------------|
| Procesamiento funcional       | `Accelerator`        | `SC_THREAD`, aritmética BT.709                 |
| Comunicación TLM              | `Bus`, sockets       | `tlm_generic_payload`, `b_transport`           |
| Almacenamiento temporal       | `RAM`                | `std::vector<uint8_t>` de 64 MB                |
| E/S persistente               | `Storage`            | `std::ifstream` / `std::ofstream`              |

---

## Referencias

- ITU-R BT.709-6, *"Parameter values for the HDTV standards for production
  and international programme exchange"*, International Telecommunication
  Union, 2015.
- IEEE Std 1666-2023, *"IEEE Standard for Standard SystemC Language
  Reference Manual"*, IEEE, 2023.
- Accellera Systems Initiative, *SystemC Reference Implementation*,
  https://github.com/accellera-official/systemc

# EC4 — Diseño de Alto Nivel
## Sistema de Procesamiento de Imágenes: TLM + RTL Verilog + UVM

---

## Descripción

Sistema de procesamiento de imágenes 1080p RGB -> escala de grises, implementado en múltiples niveles de abstracción:

- **SystemC TLM 2.0** — modelo behavioral del sistema completo (CPU, Bus, RAM, Acelerador)
- **RTL SystemVerilog** — módulo RAM con interfaz AXI4 Full
- **UVM** — ambiente de verificación del módulo RAM RTL
- **Co-simulación Verilator** — integración SystemC + RTL Verilog

---

## Requisitos

### Software

| Herramienta          | Versión mínima | Propósito                |
|----------------------|----------------|--------------------------|
| g++                  | 11+            | Compilación C++17        |
| SystemC              | 2.3.4          | Simulación TLM           |
| Verilator            | 5.020+         | Transpilación RTL -> C++ |
| Python 3             | 3.8+           | Scripts de imagen        |
| VCS (EDA playground) | -              | UVM testbench            |
| Pillow (PIL)         | -              | Visualización imágenes   |


## Organización del Repositorio

```
Evaluacion_Corta_4/
├── src/                        # Módulos SystemC 
│   ├── accelerator.cpp/h       # Acelerador grayscale con registros AXI-like
│   ├── bus.h                   # Router TLM (RAM 64MB + Acelerador)
│   ├── cpu.h                   # CPU
│   ├── ram.h                   # RAM original 
│   ├── ram_axi4_model.h        # RAM behavioral AXI4 
│   ├── tlm_to_axi4_bridge.h    # Puente TLM -> AXI4 
│   ├── axi4_if.h               # Structs de los 5 canales AXI4 Full
│   ├── ram_rtl_sc.h            # Wrapper SystemC sobre Vram_axi4
│   └── storage.cpp/h           # Almacenamiento persistente
│
├── rtl/                        # RTL SystemVerilog
│   └── ram_axi4.sv             # RAM 64MB con puerto AXI4 Full
│
├── UVM/                        # Ambiente UVM 
│   ├── axi4_if.sv              # Interface 
│   ├── axi4_seq_item.sv        # Transaction object
│   ├── axi4_driver.sv          # Driver AXI4
│   ├── axi4_monitor.sv         # Monitor 
│   ├── axi4_agent.sv           # Agente (driver + sequencer + monitor)
│   ├── axi4_scoreboard.sv      # Shadow memory
│   ├── axi4_coverage.sv        # Cobertura funcional
│   ├── axi4_sequences.sv       # Secuencias (wr_rd, burst, strb, stress)
│   ├── axi4_env.sv             # Environment completo
│   ├── axi4_test.sv            # Tests (smoke, burst, stress)
│   ├── axi4_pkg.sv             # Package wrapper
│   └── tb_top.sv               # Top-level UVM
│
├── test/                       # Testbenches y scripts
│   ├── top_test.cpp            # SystemC + AXI4 behavioral
│   ├── top_test_DPI.cpp        # SystemC + RTL Verilog
│   ├── sim_main.cpp            # Testbench C++ standalone (Verilator)
│   ├── images/
│   │   ├── input.raw           # Imagen de entrada 1080p RAW RGB
│   │   ├── output.raw          # Imagen de salida grayscale
│   │   ├── output_golden.raw   # Referencia para verificación formal
│   │   └── comparison.png      # Figura comparativa generada
│   └── scripts/
│       ├── gen_raw.py          # Genera imagen de prueba RAW
│       ├── compare_raw.py      # Figura comparativa input vs output
│       ├── view_raw.py         # Visualizar imagen RAW
│       └── verify_golden.py    # Verificación formal vs golden
│
├── Makefile                    # Build system completo
└── README.md                   # Este archivo
```

---

## Organización de los Módulos

### Mapa de Memoria (Bus)

| Región            | Base         | Fin          | Tamaño  |
|-------------------|--------------|--------------|---------|
| RAM (entrada RGB) | `0x00000000` | `0x005EC3FF` | ~6.2 MB |
| RAM (salida gray) | `0x00600000` | `0x007FABFF` | ~2.0 MB |
| RAM total         | `0x00000000` | `0x03FFFFFF` | 64 MB   |
| Acelerador (regs) | `0x04000000` | `0x040000FF` | 256 B   |

### Registros del Acelerador

| Offset | Nombre            | Función                                |
|--------|-------------------|----------------------------------------|
| `0x00` | `REG_INPUT_ADDR`  | Dirección base imagen RGB entrada      |
| `0x04` | `REG_OUTPUT_ADDR` | Dirección base imagen grayscale salida |
| `0x08` | `REG_NUM_PIXELS`  | Cantidad total de píxeles              |
| `0x0C` | `REG_CONTROL`     | Bit 0 = START                          |
| `0x10` | `REG_STATUS`      | 0=IDLE, 1=BUSY, 2=DONE, 3=ERROR        |


## Diagrama de Bloques

### SystemC TLM

```
┌─────────────┐      TLM       ┌─────────────┐      TLM       ┌──────────────────┐
│             │ ─────────────> │             │ ─────────────> │      RAM         │
│     CPU     │                │     Bus     │                │ directo/AXI4     │ 
│             │ <───────────── │             │ <───────────── │                  │
└─────────────┘                │             │                │                  │
      │ TLM                    │             │      TLM       └──────────────────┘
      │ write/read             │             │ ─────────────>
      ▼                        │             │ <─────────────  ┌──────────────────┐
┌──────────────────┐           └─────────────┘                 │  Acelerador      │
│PersistentStorage │                    ▲                      │  (grayscale)     │
│  (disco)         │                    │ TLM                  └──────────────────┘
└──────────────────┘                    │
                                ┌───────┴──────┐
                                │  Acelerador  │
                                │  mem_socket  │
                                └──────────────┘
```

### Co-simulación SystemC + RTL Verilog

```
┌─────────┐   TLM   ┌─────────┐   TLM   ┌──────────────────────────────────┐
│   CPU   │────────►│   Bus   │────────►│  RAM_RTL_SC (wrapper SystemC)    │
│(SC pure)│◄────────│(SC pure)│◄────────│  ┌────────────────────────────┐  │
└─────────┘         └─────────┘         │  │  Vram_axi4                 │  │
                         │              │  │  (C++ generado por         │  │
                         │ TLM          │  │   Verilator desde          │  │
                         ▼              │  │   ram_axi4.sv)             │  │
                    ┌──────────┐        │  └────────────────────────────┘  │
                    │Acelerador│        └──────────────────────────────────┘
                    │(SC pure) │
                    └──────────┘
```

### Ambiente UVM 

```
┌─────────────────────────────────────────────────────┐
│  axi4_env                                           │
│  ┌────────────────────────────────┐                 │
│  │  axi4_agent                    │                 │
│  │  ┌────────────┐ ┌──────────┐   │ analysis_port   │
│  │  │ Sequencer  │ │ Monitor  │ ──┼────────────────>│axi4_scoreboard
│  │  └─────┬──────┘ └──────────┘   │                 │(shadow memory)
│  │        │                       │ analysis_port   │
│  │  ┌─────▼──────┐                │────────────────>│axi4_coverage
│  │  │   Driver   │                │                 │(covergroup)
│  │  └─────┬──────┘                │                 │
│  └─────────┼──────────────────────┘                 │
└────────────┼────────────────────────────────────────┘
             │ AXI4 signals
             ▼
        ┌──────────┐
        │ram_axi4  │  (DUT)
        │  .sv     │
        └──────────┘
```

---

## Diagrama de Secuencias

### Flujo del CPU 

```
CPU              Storage          Bus              RAM            Acelerador
 │                  │              │                │                 │
 │──load_image()───►│              │                │                 │
 │<─ rgb[6.2MB]  ───│              │                │                 │
 │                  │              │                │                 │
 │──TLM_WRITE(0x0, rgb) ──────────>│                │                 │
 │                  │              │──TLM_WRITE────>│                 │
 │<─ OK ─────────────────────────  │<─ OK ──────────│                 │
 │                  │              │                │                 │
 │──TLM_WRITE(ACCEL_REG_SRC)──────>│                │                 │
 │──TLM_WRITE(ACCEL_REG_DST)──────>│                │                 │
 │──TLM_WRITE(ACCEL_REG_CNT)──────>│                │                 │
 │──TLM_WRITE(ACCEL_REG_CTRL=1)───>│──────────────────────────────>START
 │                  │              │                │                 │
 │──poll STATUS ───>│(loop)        │                │                 │
 │        (espera DONE)            │                │ <─AXI4_RD────   │
 │                  │              │                │ ──AXI4_WR────>  │
 │<─ STATUS=DONE ──────────────────────────────────────────────────── │
 │                  │              │                │                 │
 │──TLM_READ(0x600000, gray)──────>│                │                 │
 │<─ gray[2MB] ──────────────────  │<─ gray ────────│                 │
 │                  │              │                │                 │
 │──save_image()───>│              │                │                 │
 │<─ OK ─────────── │              │                │                 │
```

### Protocolo AXI4 Full — Escritura burst (N beats)

```
Master (bridge)          Slave (ram_axi4.sv)
       │                        │
  ─────┤ AW: awvalid=1,         │
       │     awaddr, awlen=N-1  │
       │ ──────────────────────>│
       │<── awready=1 ──────────│  handshake AW
       │                        │
  ─────┤ W[0]: wdata, wlast=0   │
       │ ──────────────────────>│
       │<── wready=1 ───────────│  handshake W[0]
       │                        │
      ...   (beats 1..N-2)     ...
       │                        │
  ─────┤ W[N-1]: wdata,wlast=1  │
       │ ──────────────────────>│
       │<── wready=1 ───────────│  handshake W[N-1]
       │                        │
       │<── B: bvalid=1,bresp=0 │  respuesta OKAY
  ─────┤ bready=1               │
       │ ──────────────────────>│  handshake B
```

---

## Instrucciones de Compilación y Ejecución

### SystemC TLM

```bash
# Compilar y correr (genera imagen de prueba + simulacion)
make run

# Solo compilar
make all

# Simular con imagen existente
make exec

# Verificar visualmente
make compare
```

### Verificación RTL (Verilator standalone)

```bash
# Lint del RTL
make lint

# Compilar y correr testbench C++
make run_vl

```

### UVM Testbench (EDA Playground / VCS)

**En EDA Playground (VCS):**
- Design: `rtl/ram_axi4.sv`
- Testbench: `uvm/axi4_*.sv`, `uvm/tb_top.sv`
- Compile flags: `-sverilog -ntb_opts uvm-1.2 +define+UVM_NO_DPI`
- Run flags: `+UVM_TESTNAME=smoke_test`

**Tests disponibles:**
```
+UVM_TESTNAME=smoke_test    # write-read + byte enables
+UVM_TESTNAME=burst_test    # burst de 32 beats
+UVM_TESTNAME=stress_test   # 20 transacciones aleatorias
```

### Co-simulación SystemC + RTL

```bash
# Generar librería Verilator 
make verilate_lib

# Compilar y correr sistema completo 
make run_dpi

# Verificación formal contra golden 
make golden        # generar referencia 
make verify_dpi    # comparar byte a byte
```

### Overrides

```bash
# SystemC en ruta no estándar
make run SYSTEMC_HOME=/ruta/a/systemc

# Ver todos los targets disponibles
make help
```

---

## Resultados Obtenidos

### Verificación funcional por fase

| Fase                      | Test                   | Resultado                |
|---------------------------|------------------------|--------------------------|
| Phase 1 (TLM directo)     | Sistema completo 1080p | Output verificado        |
| Phase 2 (AXI4 behavioral) | Sistema completo 1080p | Idéntico a Phase 1       |
| Phase 3 (RTL Verilator)   | 5 tests dirigidos C++  | 12/12 PASS               |
| Phase 4 (UVM)             | smoke + burst + stress | 0 errores                |
| Phase 5 (co-simulación)   | Sistema completo 1080p | Verificación formal PASS |

### Verificación formal Phase 5 vs Golden Phase 1

```
=== Verificacion formal: Phase 5 vs Golden ===
[1] Tamanio: 2,073,600 bytes          [OK]
[2] Comparacion byte a byte:          [OK] IDENTICO
[3] Estadisticas de luminancia:
        Golden    Output
    Min     14        14
    Max    240       240
    Media  126.616   126.616          [OK]
╔══════════════════════════════════════════════╗
║  VERIFICACION FORMAL: PASS                   ║
║  Phase 5 (RTL) == Phase 1 (golden)           ║
╚══════════════════════════════════════════════╝
```

### Comparación de tiempos de simulación

| Fase                      | Tiempo simulado | Observación             |
|---------------------------|-----------------|-------------------------|
| Phase 1 — TLM directo     | 16,791,570 ns   | RAM como `std::vector`  |
| Phase 2 — AXI4 behavioral | 83,125,990 ns   | Overhead protocolo AXI4 |
| Phase 5 — RTL Verilog     | 83,389,950 ns   | RTL cycle-accurate      |

El overhead de Phase 5 vs Phase 2 (~264K ns, 0.3%) corresponde al costo de evaluar lógica RTL real vs el modelo behavioral en C++.

### Cobertura UVM

| Test        | Cobertura | Transacciones            |
|-------------|-----------|--------------------------|
| smoke_test  | 58%       | 3 WR + 2 RD              |
| burst_test  | 48%       | 1 WR (32 beats) + 1 RD   |
| stress_test | 64%       | 20 WR + 20 RD aleatorios |

Los bins de cobertura no cubiertos corresponden a bursts >64 beats y accesos a la región grayscale (0x600000+), que no son ejercitados por los tests funcionales actuales.

---

## Declaración de Uso de Inteligencia Artificial

De acuerdo con la política del curso, se declara que se utilizó inteligencia artificial (Claude, Anthropic) durante el desarrollo de esta evaluación. A continuación se detallan los usos:

| Área | Tipo de uso |
|---|---|
| Arquitectura del sistema | Consulta de conceptos (TLM 2.0, AXI4 Full, UVM) |
| UVM environment | Generación de estructura base |
| Makefile | Generación de targets Verilator |
| Scripts Python | Generación de script de verificación |
| Depuración | Diagnóstico de errores de compilación y timing |

**Prompts principales utilizados:**
- Consultas sobre el protocolo AXI4 Full (canales, handshake, burst)
- Consultas sobre integración Verilator + SystemC
- Depuración de race conditions en testbench SystemVerilog
- Revisión de errores de linking (VlThreadPool)
- Estructura del ambiente UVM para RAM slave

Todo el código fue revisado, entendido y validado por el equipo antes de su uso.

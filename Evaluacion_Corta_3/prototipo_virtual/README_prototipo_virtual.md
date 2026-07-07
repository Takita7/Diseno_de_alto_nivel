# Prototipo Virtual — Documentación técnica

## 1. Descripción general

Un CPU ARM64 simulado en gem5 (ejecutando un binario bare-metal real,
compilado para AArch64) se comunica mediante TLM 2.0 con el `Accelerator`
de la EC anterior (SystemC), usando el puente oficial de co-simulación
`gem5/util/tlm`. El resultado fue verificado byte a byte contra la salida
del sistema SystemC puro de la EC anterior (`cmp` sin diferencias).

Ver `SETUP_PROTOTIPO_VIRTUAL.md` para instrucciones completas de
instalación y ejecución.

## 2. Arquitectura

```
┌────────────────────── gem5 (ARM64, bare-metal) ──────────────────────┐
│                                                                      │
│   CPU (TimingSimpleCPU)          RAM (SimpleMemory, 512 MiB)         │
│   ejecuta accel_test.elf         0x00000000 - 0x1FFFFFFF             │
│        │                                             ▲               │
│        │ MMIO                                        │ DMA           │
└────────┼─────────────────────────────────────────────┼───────────────┘
         │ ExternalSlave                               │ ExternalMaster
         │ (port_type=tlm_slave)                       │ (port_type=tlm_master)
         ▼                                             │
┌──────────────────── SystemC (harness_top.cc) ─────────────────────────┐
│                                                                       │
│   Gem5SlaveTransactor ──────► Accelerator ◄────── Gem5MasterTransactor│
│      ("regs")              (RGB→Grayscale)             ("dma")        │
│                           
└───────────────────────────────────────────────────────────────────────┘
```

## 3. Organización del repositorio

```
Evaluacion_Corta_3/
├── src/                          # Acelerador SystemC (EC anterior)
│   ├── accelerator.h / .cpp      # Sin cambios de lógica
│   ├── bus.h, ram.h, storage.*   # No se usan en el prototipo virtual
├── guest/                        # Programa bare-metal ARM64
│   ├── accel_test.c
│   ├── startup.S
│   ├── linker.ld
│   └── Makefile
├── gem5_conf/
│   └── tlm_soc.py                # Configuración de gem5
├── systemc/                      # Puente gem5<->SystemC (van dentro de
│   ├── harness_top.cc            # gem5/util/tlm/examples/accel_bridge/)
│   ├── accelerator.cc            # Copia de src/accelerator.cpp + 1 ajuste
│   ├── accelerator.h
│   └── SConscript
├── SETUP_PROTOTIPO_VIRTUAL.md
└── README.md (este documento)
```

## 4. Mapa de memoria

| Rango / Dirección | Contenido |
|---|---|
| `0x00000000 – 0x1FFFFFFF` | RAM (gem5 SimpleMemory, 512 MiB) |
| `0x00100000` | Punto de entrada del ELF bare-metal (`.text`) |
| `0x01000000` | Imagen RGB de entrada (embebida en el ELF vía `.incbin`) |
| `0x02000000` | Imagen de salida en escala de grises |
| `0x30000000 – 0x300000FF` | Registros del `Accelerator` (puerto externo TLM, fuera del rango de RAM) |

### Registros del Accelerator (offset relativo a `0x30000000`)

| Offset | Registro | Descripción |
|---|---|---|
| `+0x00` | `REG_INPUT_ADDR` | Dirección física de la imagen RGB de entrada |
| `+0x04` | `REG_OUTPUT_ADDR` | Dirección física de la imagen de salida |
| `+0x08` | `REG_NUM_PIXELS` | Cantidad de píxeles a procesar |
| `+0x0C` | `REG_CONTROL` | Escribir `1` para iniciar el procesamiento |
| `+0x10` | `REG_STATUS` | `0`=IDLE, `1`=BUSY, `2`=DONE, `3`=ERROR |

## 5. Adaptación realizada sobre el Accelerator

El `Accelerator` es el mismo modelo SystemC/TLM 2.0 de la EC anterior; su
lógica de conversión RGB→escala de grises **no cambió**. El único ajuste
(documentado en `systemc/accelerator.cc`) es la decodificación de
direcciones en `b_transport`: en la EC anterior, el `Bus` propio entregaba
direcciones ya relativas (0x00, 0x04, ...); en el prototipo virtual, las
transacciones llegan directo desde gem5 con la dirección física absoluta
(`0x30000000 + offset`), así que se resta la base una sola vez antes de
decodificar el registro.

## 6. Formato de las transacciones

TLM 2.0 genérico (`tlm::tlm_generic_payload`) vía `b_transport`:
- Comando: `TLM_READ_COMMAND` / `TLM_WRITE_COMMAND`
- Registros: accesos de 4 bytes
- Imagen (DMA): bloques según el tamaño que gem5 negocie por transacción
- Respuesta: `TLM_OK_RESPONSE`

## 7. Instrucciones de compilación y ejecución

Ver `SETUP_PROTOTIPO_VIRTUAL.md` — guía completa paso a paso.

## 8. Resultados obtenidos

La co-simulación completa (CPU ARM64 en gem5 + `Accelerator` en SystemC vía
TLM 2.0) procesó una imagen de 1920×1080 (2,073,600 píxeles):

```
708 ns    : Accelerator Inicio procesamiento | input_base=0x1000000
871457 ns : Accelerator Procesamiento terminado | pixels=2073600 gray_bytes=2073600
```

**Verificación:** el archivo de salida (`output.raw`, generado vía
`m5_write_file` desde el programa bare-metal) es **idéntico byte a byte**
(`cmp` sin diferencias) al generado por el sistema SystemC puro de la EC
anterior con la misma imagen de entrada — confirmando que la lógica de
procesamiento no se alteró por el cambio de plataforma.

## 9. Declaración de uso de IA

Se utilizó IA (Claude, Anthropic) como apoyo durante el desarrollo de esta
sección. A continuación se documentan las categorías de uso:

**Depuración** — diagnóstico de errores reales de compilación y ejecución
(gem5, SystemC, puente `util/tlm`), incluyendo el uso de `gdb` para
obtener evidencia concreta (backtraces) en vez de adivinar soluciones a
ciegas.

**Consulta de conceptos** — para entender el funcionamiento real de las
herramientas usadas (qué es gem5, qué es un archivo ELF, cómo funciona un
linker script, qué es TLM 2.0), no solo copiar comandos sin saber qué
hacen.

**Decisiones de arquitectura** — evaluación de tradeoffs entre distintos
caminos posibles (por ejemplo, CPU ARM64 bare-metal vs. arranque completo
de Linux) antes de decidir, y rechazo de una alternativa más simple
(modelo de comportamiento en SystemC) para insistir en la integración real
con gem5.

**Revisión y adaptación de código** — ajustes puntuales sobre
`accelerator.cc` (decodificación de direcciones), `harness_top.cc` y
`tlm_soc.py`, seguidos de una revisión línea por línea de cada archivo del
entregable para verificar y entender el funcionamiento completo del
sistema antes de darlo por terminado.

**Generación de diagramas** — arquitectura, mapa de memoria y estructura
interna del `Accelerator`.

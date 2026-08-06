# RISC-V GPGPU — Software Stack Overview

**Target:** AMD Kria KV260 (Zynq UltraScale+ MPSoC)  
**Fecha:** 2026-08-06

---

## Vista general

```
┌──────────────────────────────────────────────────────────────┐
│                    Aplicación / Benchmark                    │
│   gpgpuLaunchKernel()   gpgpuMemcpyH2D/D2H()   gpgpuMalloc()│
│                  software/host_api/                          │
└───────────────────────────┬──────────────────────────────────┘
                            │
               ┌────────────┴────────────┐
               ▼                         ▼
┌──────────────────────┐   ┌─────────────────────────────────┐
│    KernelLoader      │   │        PTX Transpiler           │
│  software/kernel_    │   │  driver/src/ptx_transpiler/     │
│  loader/             │   │  PTX (CUDA IR)  →  RV32I        │
│  ELF + manifiesto    │   │  ptx_parser  →  rv_emitter      │
└───────────┬──────────┘   └────────────────┬────────────────┘
            │                               │
            └───────────────┬───────────────┘
                            ▼
           ┌────────────────────────────────┐
           │      LLVM Backend (compilador) │
           │   software/llvm/backend/       │
           │   C / CUDA  →  LLVM IR  →  ELF │
           │   software/llvm/mc/  (assembler)│
           └────────────────┬───────────────┘
                            │  .elf binario RV32I
                            ▼
           ┌────────────────────────────────┐
           │       Runtime / Driver         │
           │  runtime/src/host_runtime.h    │
           │  driver/src/fpga_driver.h      │
           │  · mmap /dev/mem (AXI-Lite)    │
           │  · CMA buffer para DDR         │
           │  · ELF loader → DDR            │
           └────────────────┬───────────────┘
                            │  AXI  (ARM PS ↔ PL)
                            ▼
           ┌────────────────────────────────┐
           │        HLS IP (FPGA PL)        │
           │   gpgpu_scheduler              │
           │   ├─ programLoader  (m_axi×3)  │
           │   ├─ schedulerCore  (×NUM_CUS) │
           │   ├─ compute_pipeline(×NUM_CUS)│
           │   ├─ memory_pipeline (L1+L2)   │
           │   └─ barrierCore               │
           └────────────────────────────────┘
```

---

## Dos caminos para compilar un kernel

| Camino | Input del usuario | Pasos internos |
|--------|-------------------|----------------|
| **CUDA/PTX** | archivo `.cu` o PTX text | LLVM → PTX IR → transpiler → RV32I ELF |
| **Nativo RV32I** | C con intrinsics GPGPU | `clang --target=riscv32` → ELF directo |

---

## Capas en detalle

### 1 · Host API  `software/host_api/`

> **¿Qué es?**  
> La capa más alta. Expone una API tipo CUDA para que la aplicación no tenga que
> saber nada sobre la FPGA, el driver, ni el formato de los kernels.

Funciones principales:

```cpp
gpgpuMalloc(dev_ptr, size)        // reserva memoria en DDR del dispositivo
gpgpuMemcpyH2D(dst, src, size)    // ARM → DDR (host to device)
gpgpuMemcpyD2H(dst, src, size)    // DDR → ARM (device to host)
gpgpuLaunchKernel(name, grid, block, args...)  // lanza el kernel
gpgpuSynchronize()                // espera a que el hardware termine
gpgpuRegisterPtx(name, ptx)       // registra un kernel PTX en tiempo de ejecución
```

> **Concepto clave — API GPGPU:**  
> CUDA fue el primer modelo que separó "la lógica del kernel" de "cómo se ejecuta".
> El programador escribe `kernel<<<grid, block>>>(args)` y el runtime decide cuántos
> hilos lanzar, dónde vive la memoria y cuándo sincronizar. Esta API replica ese
> modelo sobre hardware propio.

---

### 2 · Kernel Loader  `software/kernel_loader/`

> **¿Qué es?**  
> Empaqueta un binario ELF de kernel junto con un manifiesto JSON que describe
> el nombre, el punto de entrada y los argumentos. Permite distribuir kernels como
> archivos `.bundle` y cargarlos en tiempo de ejecución sin recompilar.

```
kernel.bundle
  ├── manifest.json   { "name": "vector_add", "entry": "_start", "args": [...] }
  └── kernel.elf      binario RV32I
```

> **Concepto clave — ELF:**  
> ELF (*Executable and Linkable Format*) es el formato binario estándar en Linux.
> Contiene secciones: `.text` (instrucciones), `.data` (datos inicializados),
> `.bss` (datos en cero). El loader lo parsea para saber dónde copiar cada sección
> en la memoria del dispositivo y cuál es la dirección del punto de entrada.

---

### 3 · PTX Transpiler  `driver/src/ptx_transpiler/`

> **¿Qué es?**  
> Convierte PTX (el IR de CUDA, portable entre GPUs NVIDIA) a instrucciones RV32I
> que entiende nuestro procesador RISC-V. Tiene dos etapas: parser y emitter.

```
PTX source                RV32I assembly
──────────                ─────────────────
.reg .u32 %r1;    →       (registro x1)
add.u32 %r2,%r1,5 →       addi x2, x1, 5
ld.global.u32 ...  →       lw   x3, offset(x4)
```

> **Concepto clave — PTX:**  
> PTX (*Parallel Thread eXecution*) es el assembly virtual de NVIDIA. Es un IR
> (representación intermedia) que el driver de CUDA compila a GPU real en tiempo
> de ejecución. Como es texto plano y está bien documentado, es un buen target
> para transpilar a otras arquitecturas.

---

### 4 · LLVM Backend  `software/llvm/`

> **¿Qué es?**  
> Usa LLVM para compilar C/C++/CUDA a código máquina RV32I. El módulo
> `llvm_backend` genera LLVM IR a partir del source, y `riscv_gpgpu_mc` (machine
> code) lo ensambla a ELF usando el target RISC-V de LLVM.

```
C source  →  clang (frontend)  →  LLVM IR  →  riscv32 codegen  →  .elf
```

> **Concepto clave — LLVM IR:**  
> LLVM IR es un assembly de tres direcciones independiente de la arquitectura:
> `%r = add i32 %a, %b`. Todos los frontends (C, C++, Rust, Swift) compilan a IR
> primero. Luego el backend traduce IR al assembly nativo del target. Esto es lo
> que hace que LLVM soporte tantas arquitecturas reutilizando los mismos
> frontends.

---

### 5 · Driver FPGA  `driver/src/fpga_driver.h`

> **¿Qué es?**  
> Habla directamente con el hardware. En Linux, abre `/dev/mem` y hace `mmap`
> de los registros AXI-Lite del IP. Gestiona buffers CMA (Contiguous Memory
> Allocator) para transferencias DMA.

```cpp
driver.open(config)            // mmap de registros AXI-Lite
driver.copyToDevice(dst, src)  // DMA host → DDR via CMA buffer
driver.start()                 // pone CTRL.START = 1
driver.waitForStatus(DONE)     // poll de STATUS hasta que el IP termine
```

> **Concepto clave — AXI-Lite vs AXI-HP:**  
> AXI-Lite es para registros de control (32-bit, baja velocidad): start/stop/status.
> AXI-HP (*High Performance*) es para transferencias de datos en ráfaga (burst) al
> DDR, con ancho de bus de 128 bits. El ARM tiene 4 puertos HP dedicados al
> acceso del PL a la memoria.

> **Concepto clave — CMA (Contiguous Memory Allocator):**  
> DMA requiere memoria físicamente contigua porque el controlador DMA trabaja con
> direcciones físicas, no virtuales. CMA es el mecanismo de Linux para reservar
> regiones contiguas en el arranque y asignarlas a drivers que las necesiten.

---

### 6 · Runtime  `runtime/src/host_runtime.h`

> **¿Qué es?**  
> Puente entre la Host API y el driver. Gestiona el ciclo de vida de una ejecución:
> carga el ELF en DDR, configura los registros de control (total_warps, program_len,
> warp_id_offset), arranca el IP y espera la señal `done`.

---

### 7 · HLS IP — FPGA PL  `hls/src/`

> **¿Qué es?**  
> El hardware sintetizado a partir de C++ con Vitis HLS. Es el procesador GPGPU
> real. Se ejecuta en la lógica programable (PL) del Zynq mientras el ARM corre
> el software en el PS.

| Módulo | Función |
|--------|---------|
| `programLoader` | Lee programa e registros iniciales desde DDR (m_axi), hace broadcast a cada CU vía streams |
| `schedulerCore` | Un warp scheduler por CU: gestiona slots, despacha warps listos |
| `compute_pipeline` | Ejecuta instrucciones RV32I con SIMT (32 hilos en paralelo por warp) |
| `memory_pipeline` | Cache L1 por CU + L2 compartida + árbitro de acceso a DDR |
| `barrierCore` | Sincroniza warps en instrucciones BARRIER (tipo `__syncthreads()`) |

> **Concepto clave — SIMT:**  
> *Single Instruction Multiple Threads*: todos los hilos de un warp ejecutan la
> misma instrucción al mismo tiempo, cada uno sobre sus propios datos. Si un hilo
> toma una rama diferente, el hardware serializa las dos rutas con una máscara de
> actividad (*divergence stack*). Es la base del modelo de ejecución de todas las
> GPUs modernas.

> **Concepto clave — Vitis HLS (High-Level Synthesis):**  
> Compila C++ a RTL (Verilog/VHDL) sintetizable para FPGA. El programador escribe
> comportamiento; HLS decide registros, multiplexores y pipelines. Los pragmas
> (`#pragma HLS DATAFLOW`, `#pragma HLS PIPELINE`) controlan la microarquitectura
> resultante. La clave de este proyecto es el DATAFLOW: cada función C++ se
> convierte en un módulo hardware independiente que corre en paralelo, comunicándose
> vía streams FIFO.

> **Concepto clave — DATAFLOW y por qué importa:**  
> Sin DATAFLOW, HLS ejecuta las funciones secuencialmente (una termina, empieza la
> siguiente). Con DATAFLOW, HLS las convierte en módulos concurrentes conectados
> por FIFOs. Para un GPGPU, esto es crítico: los 8 `compute_pipeline` deben
> ejecutarse en paralelo, no uno tras otro. El bug que corregimos esta sesión era
> precisamente que HLS estaba fusionando los 8 pipelines en 1 módulo
> time-multiplexado por un problema de aliasing de BRAM.

---

## Estado de implementación (2026-08-06)

| Configuración | HLS (csynth) | Vivado impl | LUT post-impl | BRAM | Timing |
|---------------|-------------|-------------|---------------|------|--------|
| CU=2 baseline | ✅ PASS | ✅ PASS | 21,419 (18.3%) | 44 | ✅ +0.010 ns |
| CU=2 v3 (fix) | ✅ PASS | ✅ PASS | **7,924 (6.8%)** | 11.5 | ✅ +1.532 ns |
| CU=4 v3 (fix) | ✅ PASS | ✅ PASS | **8,178 (7.0%)** | 12.5 | ✅ +1.105 ns |
| CU=8 v3 (fix) | ✅ PASS | ✅ PASS | **43,934 (37.5%)** | 56.5 | ⚠️ −0.283 ns |

> La reducción de 21k → 7.9k LUTs en CU=2 se debe a que el fix eliminó los
> DMA engines por-CU (`m_axi initial_regs_ptr` dentro de cada `compute_pipeline`),
> centralizando el acceso a DDR en `programLoader`. También eliminó 31 DSPs y
> 33 BRAMs de infraestructura AXI.

---

## Flujo completo de un kernel (de código a silicio)

```
1. Usuario escribe kernel.cu
        │
        ▼
2. clang → LLVM IR → RV32I ELF         [LLVM Backend]
        │
        ▼
3. KernelLoader empaqueta ELF + manifest en kernel.bundle
        │
        ▼
4. host_api carga el bundle y reserva buffers DDR
        │
        ▼
5. driver copia programa + datos al DDR via DMA (CMA)
        │
        ▼
6. driver escribe registros AXI-Lite: program_ptr, regs_ptr, total_warps, START
        │
        ▼
7. programLoader (HLS) lee DDR → transmite instrucciones + registros a cada CU
        │
        ▼
8. schedulerCore despacha warps a compute_pipeline
        │
        ▼
9. compute_pipeline ejecuta RV32I SIMT (32 hilos / warp)
   ↕  (loads/stores via streams)
   memory_pipeline (L1 → L2 → DDR)
        │
        ▼
10. barrierCore sincroniza warps en __barrier
        │
        ▼
11. IP pone STATUS = DONE
        │
        ▼
12. driver detecta DONE, host_api copia resultado DDR → host
```

---

## BFS — el benchmark que usamos

### ¿Qué es BFS?

BFS (*Breadth-First Search*, búsqueda en anchura) es uno de los algoritmos
fundamentales sobre grafos. Dado un nodo origen, visita primero todos sus
vecinos directos (nivel 1), luego los vecinos de esos vecinos (nivel 2), y así
sucesivamente hasta haber visitado todo el grafo alcanzable. El resultado es un
arreglo `cost[]` donde `cost[v]` indica a cuántos saltos de distancia está el
nodo `v` del origen.

```
     0
    / \
   1   2        BFS desde nodo 0:
  / \   \       cost[0]=0, cost[1]=1, cost[2]=1
 3   4   5      cost[3]=2, cost[4]=2, cost[5]=2
```

### Por qué es un buen benchmark para GPU

En una CPU, BFS se implementa con una cola: se extrae un nodo, se procesan sus
vecinos, se encolan los nuevos. Es inherentemente secuencial — la cola tiene un
solo frente. En una GPU, el truco es reformular la frontera como una operación
masivamente paralela:

> En cada iteración ("nivel"), cada hilo es responsable de **un nodo**.  
> Si ese nodo está en la frontera actual (`g_graph_mask[tid] == true`),  
> el hilo recorre todos sus vecinos y los marca para el siguiente nivel.

El kernel de Rodinia que usamos hace exactamente eso:

```cuda
// kernel.cu — Rodinia BFS (Harish & Narayanan, HiPC 2007)
__global__ void Kernel(Node* g_graph_nodes, int* g_graph_edges,
                       bool* g_graph_mask, bool* g_updating_graph_mask,
                       bool* g_graph_visited, int* g_cost, int no_of_nodes)
{
    int tid = blockIdx.x * MAX_THREADS_PER_BLOCK + threadIdx.x;
    if (tid < no_of_nodes && g_graph_mask[tid]) {
        g_graph_mask[tid] = false;
        for (int i = g_graph_nodes[tid].starting;
             i < g_graph_nodes[tid].starting + g_graph_nodes[tid].no_of_edges;
             i++) {
            int id = g_graph_edges[i];
            if (!g_graph_visited[id]) {
                g_cost[id] = g_cost[tid] + 1;
                g_updating_graph_mask[id] = true;
            }
        }
    }
}
```

Cada hilo hace: una lectura de la máscara, un bucle sobre los vecinos del nodo,
y escrituras de costo y máscara actualizada. La GPU lanza tantos hilos como nodos
tiene el grafo — el paralelismo escala directamente con el tamaño del problema.

### Estructura de datos en memoria

El grafo se representa en formato CSR (*Compressed Sparse Row*), el estándar
para grafos dispersos:

```
Node[]   — array de structs { starting: int, no_of_edges: int }
             Node[v].starting  = índice en edges[] donde empiezan los vecinos de v
             Node[v].no_of_edges = cuántos vecinos tiene v

edges[]  — array plano de IDs de nodo destino
             edges[Node[v].starting .. Node[v].starting + Node[v].no_of_edges - 1]
             son los vecinos de v
```

Esta representación es cache-friendly para lecturas lineales (los vecinos de un
nodo son contiguos en memoria) y permite a múltiples hilos acceder a regiones
distintas sin conflictos.

### Dos kernels, dos pasadas por nivel

Rodinia divide BFS en dos kernels por nivel para evitar condiciones de carrera:

| Kernel | Qué hace |
|--------|----------|
| `Kernel` (kernel.cu) | Lee `g_graph_mask`, escribe `g_updating_graph_mask` y `g_cost` |
| `Kernel2` (kernel2.cu) | Fusiona `g_updating_graph_mask` en `g_graph_mask`, detecta si hay más trabajo |

La CPU hace un bucle hasta que `Kernel2` reporta que no hubo actualizaciones —
señal de que el grafo ya está totalmente visitado.

### Cómo lo ejecutamos en nuestro hardware

El kernel CUDA original se compila a RV32I usando nuestro LLVM backend:

```
cuda/bfs/kernel.cu
      │
      ▼  clang --target=riscv32
rodinia_bfs_kernel.elf   +   rodinia_bfs_kernel2.elf
      │
      ▼  KernelLoader
rodinia_bfs_kernel.bundle
      │
      ▼  host_api → driver → AXI → FPGA
gpgpu_scheduler ejecuta el ELF con NUM_CUS pipelines SIMT
```

El benchmark carga un grafo, asigna un hilo por nodo, y mide el tiempo del bucle
completo de niveles. Con CU=2 hay 2 `compute_pipeline` corriendo en paralelo,
cada uno procesando un subconjunto de los nodos de la frontera simultáneamente.

---

## Guion para presentación

### Introducción (2 min)

"Este proyecto construye una GPU programable sobre una FPGA. No usamos una GPU
comercial — diseñamos el procesador en C++, lo sintetizamos a hardware con Vitis
HLS, y lo desplegamos en una placa Kria KV260 de AMD. El procesador en la FPGA
ejecuta código CUDA real."

"La arquitectura está inspirada en GPUs modernas: múltiples Compute Units, cada
una con un pipeline SIMT de 32 hilos, memoria compartida y un sistema de cachés.
El software es compatible hacia arriba con CUDA — el mismo kernel que correría
en una GPU NVIDIA se compila a nuestro ISA RISC-V y se ejecuta en la FPGA."

---

### El stack de software (3 min)

"Para que una aplicación llegue al hardware, pasa por varias capas. Arriba está
la Host API — exactamente las mismas llamadas que conoce cualquier programador
CUDA: `gpgpuMalloc`, `gpgpuMemcpy`, `gpgpuLaunchKernel`. La diferencia es que
abajo no hay un driver NVIDIA — hay nuestro propio driver que habla directamente
con la FPGA."

"El kernel se compila con LLVM. LLVM es el compilador que usa Apple, Android, y
Rust — nosotros lo usamos para emitir código máquina RISC-V. El formato del
binario es ELF, el mismo que usa cualquier ejecutable de Linux. El KernelLoader
empaqueta ese ELF con un manifiesto, y el driver lo copia al DDR de la placa
usando DMA."

"Para kernels PTX — el IR de CUDA — tenemos un transpiler que convierte las
instrucciones PTX a RV32I uno a uno. Esto nos permite reutilizar kernels
existentes sin reescribirlos."

---

### El benchmark BFS (2 min)

"Para validar el sistema usamos BFS del benchmark Rodinia — la suite de referencia
para GPUs del Virginia Tech. BFS busca el camino más corto en un grafo asignando
un hilo a cada nodo. En cada nivel de la búsqueda, todos los nodos en la frontera
se procesan en paralelo: cada hilo lee sus vecinos, actualiza el costo, y marca
los nuevos nodos a visitar."

"Este benchmark es interesante porque estresa exactamente lo que diferencia una
GPU de una CPU: accesos irregulares a memoria (los vecinos de cada nodo están en
posiciones arbitrarias del DDR), escrituras dispersas (múltiples hilos escriben
a nodos distintos), y un patrón de ejecución que varía por warp (algunos hilos
tienen muchos vecinos, otros pocos). Es una prueba real del sistema completo, no
un microbenchmark artificial."

"El kernel original es CUDA. Nuestro LLVM backend lo compila a RV32I, el
KernelLoader lo empaqueta, y el driver lo lanza en la FPGA. Sin cambiar una línea
del kernel de Rodinia."

---


### El hardware (3 min)

"En la FPGA hay cuatro módulos corriendo en paralelo, conectados por FIFOs —
esto es lo que Vitis HLS llama DATAFLOW. El `programLoader` lee el programa y
los registros iniciales desde el DDR y los distribuye a cada CU. El
`schedulerCore` decide qué warp ejecutar en cada ciclo. El `compute_pipeline`
es el procesador SIMT: ejecuta una instrucción RV32I sobre 32 hilos en
paralelo. El `memory_pipeline` maneja las cachés."

"Un warp es un grupo de 32 hilos que ejecutan siempre la misma instrucción al
mismo tiempo — como un regimiento que marcha al mismo paso. Si los hilos
divergen en un `if`, el hardware serializa las dos ramas usando una pila de
divergencia, y luego las reunifica. Con 2 CUs tenemos 2 warps ejecutándose en
paralelo. Con 8 CUs, 8 warps."

"Esta sesión corregimos un bug crítico: Vitis HLS estaba fusionando los 8
pipelines en uno solo por un problema de aliasing de BRAM. El hardware resultante
time-multiplexaba los 8 CUs en lugar de ejecutarlos en paralelo — y ocupaba menos
recursos que 2 CUs trabajando bien, que fue la pista que nos llevó al diagnóstico.
El fix fue declarar las memorias de registros como variables explícitamente
separadas para que el sintetizador no pudiera confundirlas."

---

### Resultados y próximos pasos (1 min)

"Con CU=2 tenemos un bitstream funcionando en el KV260: 7,900 LUTs, timing met
con 1.5 ns de margen. Estamos corriendo ahora mismo las implementaciones de CU=4
y CU=8 para ver hasta dónde llegamos con los recursos del dispositivo."

"Los próximos pasos son desplegar el bitstream en la placa física, correr el BFS
real, y medir el throughput de instrucciones contra las estimaciones del
simulador SystemC que tenemos en el repositorio."

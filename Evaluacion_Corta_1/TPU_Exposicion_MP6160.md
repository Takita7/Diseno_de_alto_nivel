# In-Datacenter Performance Analysis of a Tensor Processing Unit (TPU)
## Notas de Exposición — MP6160: Diseño de Alto Nivel
### II Cuatrimestre 2026 | Prof. Luis G. León-Vega, Ph.D.

> **Paper original:** Jouppi, N.P. et al. (2017). *In-Datacenter Performance Analysis of a Tensor Processing Unit*. Proceedings of the 44th International Symposium on Computer Architecture (ISCA), Toronto, Canada. Google, Inc.

---

## Índice

1. [Contexto y Motivación](#1-contexto-y-motivación)
2. [Problema Principal](#2-problema-principal)
3. [Metodología de Diseño: Top-Down, Bottom-Up o Combinación](#3-metodología-de-diseño)
4. [Arquitectura del Sistema](#4-arquitectura-del-sistema)
5. [Restricciones y Tradeoffs de Diseño](#5-restricciones-y-tradeoffs-de-diseño)
6. [Herramientas y Metodologías de Exploración](#6-herramientas-y-metodologías-de-exploración)
7. [Aplicaciones Prácticas y Relevancia Actual](#7-aplicaciones-prácticas-y-relevancia-actual)
8. [Conclusiones del Grupo](#8-conclusiones-del-grupo)
9. [Referencias](#9-referencias)

---

## 1. Contexto y Motivación

### El Auge del Deep Learning en Datacenters (2013)

Hacia 2013, las redes neuronales profundas (DNNs) estaban generando resultados sin precedentes:

- Reducción del 30% en tasas de error de reconocimiento de voz.
- Reducción de la tasa de error en reconocimiento de imágenes: de 26% a 3.5% entre 2011 y 2016.
- AlphaGo derrotó al campeón mundial de Go.

Google proyectó que si sus usuarios empleaban búsqueda por voz 3 minutos al día usando DNNs de reconocimiento de voz, **se necesitaría duplicar la capacidad de sus datacenters** solo para satisfacer esa demanda. Hacerlo con CPUs convencionales resultaría prohibitivamente costoso.

### ¿Por qué no bastaban las GPUs?

- Las GPUs disponibles (como la NVIDIA K80) estaban **optimizadas para throughput**, no para latencia.
- Las aplicaciones de inferencia en producción son típicamente *user-facing*, con límites rígidos de tiempo de respuesta en el percentil 99 (ej. 7 ms).
- La K80, limitada por estos requisitos de latencia, resultaba **apenas 1.1× más rápida** que una CPU Haswell en inferencia.

> **Dato clave:** Las GPUs incluyen características como caché, ejecución fuera de orden, multithreading, y prefetching especulativo. Estas mejoran el throughput promedio, pero no garantizan la latencia en el percentil 99, que es lo que exigen las aplicaciones de producción.

---

## 2. Problema Principal

### Enunciado del Problema

> *"¿Cómo diseñar un acelerador de hardware de bajo costo y bajo consumo energético que pueda ejecutar inferencia de redes neuronales en datacenter con una mejora de 10× en rendimiento por vatio respecto a las GPUs disponibles, sin sacrificar la compatibilidad con el software existente ni incrementar drásticamente la complejidad de despliegue?"*

### Desglose en Subproblemas

| Subproblema | Descripción |
|---|---|
| **Rendimiento-Latencia** | Cumplir requisitos de latencia P99 (percentil 99) en aplicaciones de producción |
| **Eficiencia energética** | Operar con menor consumo que CPU y GPU para mejorar el costo de operación (TCO) |
| **Portabilidad de software** | Ser compatible con TensorFlow sin reescribir las aplicaciones |
| **Velocidad de despliegue** | Diseñar, verificar, fabricar y desplegar en 15 meses |
| **Compatibilidad de infraestructura** | Conectarse a servidores existentes sin rediseñar la infraestructura |

### Carga de Trabajo Objetivo

El TPU fue diseñado para cubrir el 95% de la demanda de inferencia en los datacenters de Google:

| Tipo de Red | Ejemplos | % Workload en Datacenter |
|---|---|---|
| MLP (Multi-Layer Perceptron) | MLP0 (RankBrain), MLP1 | 61% |
| LSTM (Long Short-Term Memory) | LSTM0 (GNM Translate), LSTM1 | 29% |
| CNN (Convolutional Neural Network) | CNN0 (Inception), CNN1 (AlphaGo) | 5% |

> **Observación importante:** A pesar de que la comunidad de arquitectura se enfocaba principalmente en CNNs, estas representaban solo el 5% del workload real en datacenter. El grueso de la carga era MLPs y LSTMs.

---

## 3. Metodología de Diseño

### Análisis de la Metodología: Combinación Top-Down con Reutilización Bottom-Up

El diseño del TPU es predominantemente **top-down**, con elementos **bottom-up** en las capas de implementación. A continuación se detalla cada dimensión.

---

### 3.1 Enfoque Top-Down (dominante)

El enfoque top-down implica partir de los **requisitos de alto nivel** y descomponer el problema hacia componentes más concretos. En el TPU esto se evidencia en múltiples niveles:

#### Nivel 1: Requisito del Negocio → Decisión Tecnológica

```
Necesidad: Reducir costos de inferencia NN en datacenter
    ↓
Análisis: CPUs insuficientes, GPUs no optimizadas para latencia
    ↓
Decisión: Desarrollar ASIC personalizado (TPU)
    ↓
Meta cuantitativa: 10× de mejora en costo-rendimiento sobre GPU
```

#### Nivel 2: Requisitos de Aplicación → ISA

Los tipos de operaciones dominantes en las seis aplicaciones de producción (MLP, CNN, LSTM) dictaron el conjunto de instrucciones del TPU. Solo se necesitaban 5 instrucciones clave:

1. `Read_Host_Memory`
2. `Read_Weights`
3. `MatrixMultiply/Convolve`
4. `Activate`
5. `Write_Host_Memory`

> Esta es una decisión top-down clásica: el software (TensorFlow) define las operaciones necesarias, y el hardware se diseña para ejecutarlas eficientemente.

#### Nivel 3: ISA → Microarquitectura

Los requisitos del ISA determinaron los componentes microarquitecturales:

- `MatrixMultiply/Convolve` → **Matrix Multiply Unit** (256×256 MACs, systolic array)
- Manejo de pesos → **Weight FIFO** + **Weight Memory** (8 GiB off-chip DRAM)
- Resultados intermedios → **Accumulators** (4 MiB)
- Activaciones → **Unified Buffer** (24 MiB on-chip)
- Funciones no lineales → **Activation Unit**

#### Nivel 4: Microarquitectura → Dimensionamiento Cuantitativo

El tamaño de cada componente fue determinado **deductivamente** desde los requisitos:

- **4096 acumuladores**: Se calculó que la intensidad operacional necesaria para alcanzar el pico del roofline es ~1350 ops/byte. Se redondeó a 2048 y se duplicó para permitir *double buffering* al compilador.
- **24 MiB Unified Buffer**: Dimensionado para soportar MLPs con batch sizes hasta 2048, y para coincidir con el pitch del Matrix Unit en el die.
- **Matriz 256×256**: Escogida para maximizar la utilización con los tamaños de capas típicos del workload de 2013-2015.

```
Análisis Roofline → Intensidad operacional necesaria (1350 ops/byte)
    ↓
Número de acumuladores requeridos (~4096)
    ↓
Tamaño de Unified Buffer (24 MiB)
    ↓
Dimensiones de la matriz (256×256)
```

#### Nivel 5: Microarquitectura → Estrategia de Ejecución

La necesidad de garantizar latencia determinística en P99 dictó la elección del modelo de ejecución:

- **Sin caché**: Elimina la variabilidad de latencia del cache miss.
- **Sin predicción de ramas**: El flujo de control es determinístico.
- **Sin ejecución fuera de orden**: Simplifica el modelo de ejecución.
- **Single-threaded**: Predecible.
- **CISC instructions con repeat field**: Amortizan la latencia del PCIe bus lento.

> Cita del paper: *"Minimalism is a virtue of domain-specific processors."* [Jouppi et al., 2017]

---

### 3.2 Elementos Bottom-Up (reutilización de componentes probados)

Paralelamente, el equipo utilizó componentes y estándares existentes para reducir el riesgo y el tiempo de desarrollo:

| Componente reutilizado | Justificación Bottom-Up |
|---|---|
| **Interfaz PCIe Gen3 ×16** | Permite insertar el TPU en servidores existentes como si fuera un disco SATA o GPU, sin rediseñar la infraestructura |
| **DDR3 DRAM (8 GiB)** | Tecnología probada y disponible para Weight Memory |
| **Proceso 28 nm** | Tecnología de fabricación madura y confiable |
| **Arquitectura sistólica** | Patrón clásico de Kung & Leiserson (1980) [R2], demostrado para reducir lecturas de SRAM y ahorrar energía |
| **TensorFlow** | Framework existente; la compatibilidad permitió portar apps sin reescribirlas |
| **Aritmética entera de 8 bits** | Técnica de cuantización ya conocida: 6× menos energía y área que FP16 [Dally, 2016] |

---

### 3.3 Resumen de la Metodología: Diagrama

```
┌─────────────────────────────────────────────────────────────────┐
│                    ENFOQUE TOP-DOWN                             │
│                                                                 │
│  Requisito de Negocio (10× costo-rendimiento sobre GPU)        │
│           ↓                                                     │
│  Análisis de Workload (95% datacenter NN inference)            │
│           ↓                                                     │
│  Decisión: ASIC de dominio específico                          │
│           ↓                                                     │
│  Diseño del ISA (5 instrucciones CISC clave)                   │
│           ↓                                                     │
│  Microarquitectura (Matrix Unit, UB, FIFO, Acc)                │
│           ↓                                                     │
│  Dimensionamiento cuantitativo (roofline-driven)               │
│           ↓                                                     │
│  Implementación física (28 nm, die floorplan)                  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              ELEMENTOS BOTTOM-UP                         │  │
│  │  PCIe + DDR3 + Systolic Array + TensorFlow + INT8        │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. Arquitectura del Sistema

### 4.1 Visión General

El TPU es un **coprocesador PCIe** diseñado exclusivamente para inferencia de redes neuronales. Se conecta al servidor host (CPU Intel Haswell) a través del bus PCIe Gen3 ×16, de forma análoga a como se conecta una GPU, pero su modelo de ejecución lo asemeja más a una **FPU (Floating Point Unit)**: el host le envía instrucciones que el TPU ejecuta.

### 4.2 Componentes Principales

#### Matrix Multiply Unit (Corazón del TPU)
- Arreglo de **256×256 MACs** = 65,536 multiply-accumulate units.
- Opera sobre **enteros de 8 bits** (signed/unsigned).
- Peak throughput: **92 TOPS** (TeraOps/second).
- Utiliza **ejecución sistólica**: los datos fluyen desde la izquierda, los pesos se cargan desde arriba, formando una onda diagonal (*wavefront*). Esto reduce lecturas/escrituras al Unified Buffer, ahorrando energía.
- Capacidad de doble buffering: mantiene un tile de 64 KiB de pesos + uno extra para ocultar los 256 ciclos de carga.
- Diseñado para **matrices densas** (sparse omitido por tiempo).

#### Unified Buffer (UB)
- **24 MiB** de SRAM on-chip.
- Almacena activaciones intermedias.
- Representa el **29% del área del die**.
- Funciona como memoria de entrada y salida para el Matrix Unit.
- Controlado por DMA para transferencias con el host.

#### Weight FIFO (Weight Fetcher)
- Buffer on-chip de **4 tiles de profundidad**.
- Lee pesos desde la Weight Memory off-chip.
- Implementa filosofía *decoupled-access/execute*: puede completar el envío de dirección antes de que el dato llegue.

#### Weight Memory
- **8 GiB DRAM DDR3** off-chip.
- Solo lectura durante inferencia.
- Soporta múltiples modelos activos simultáneamente.
- **Ancho de banda: 30 GB/s**.

#### Accumulators
- **4 MiB** (4096 × 256 elementos de 32 bits).
- Acumulan los productos parciales del Matrix Unit.
- Representan el **6% del die**.

#### Activation Unit
- Ejecuta funciones no lineales: ReLU, Sigmoid, tanh, etc.
- También realiza operaciones de pooling para convoluciones.
- Salida va al Unified Buffer.

#### Normalize/Pool + PCIe Interface
- Manejo de normalización de capas.
- Interfaz PCIe Gen3 ×16 para comunicación con el host (14 GiB/s).

### 4.3 Floorplan del Die

```
┌──────────────────────────────────────────────────────┐
│  Local Unified Buffer (activaciones)   │  Matrix     │
│  96K×256×8b = 24 MiB                  │  Multiply   │
│           29% del die                  │  Unit       │
│                                        │  256×256×8b │
│                                        │  = 64K MAC  │
│                                        │  24% del die│
├────────────────┬───────────────────────┴─────────────┤
│ Host Interf.   │  Accumulators (4K×256×32b = 4 MiB)  │
│     2%         │  Activation Pipeline       6%        │
│ Control  2%    │                                      │
├────────────────┴──────────────────────────────────────┤
│  PCIe Interface 3%  │  DDR3 ports 3%  │  Misc I/O 1% │
└──────────────────────────────────────────────────────┘
  Buffers de datos: 37%  |  Cómputo: 30%  |  I/O: 10%  |  Control: 2%
```

> **Dato llamativo:** El control ocupa solo el 2% del die. En una CPU o GPU, el control es mucho mayor y más complejo.

### 4.4 Conjunto de Instrucciones (ISA)

El TPU sigue la tradición CISC con ~12 instrucciones en total. Las 5 instrucciones clave son:

| Instrucción | Descripción |
|---|---|
| `Read_Host_Memory` | Lee datos de la memoria CPU host hacia el Unified Buffer |
| `Read_Weights` | Lee pesos desde Weight Memory hacia el Weight FIFO |
| `MatrixMultiply/Convolve` | Ejecuta multiplicación matricial o convolución. Toma entrada B×256 del UB, la multiplica por pesos 256×256, produce salida B×256 en B ciclos |
| `Activate` | Ejecuta función no lineal (ReLU, Sigmoid, etc.) sobre los Accumulators, resultado al UB |
| `Write_Host_Memory` | Escribe datos del UB a la memoria del host |

- Instrucción `MatrixMultiply`: 12 bytes (3 bytes dirección UB, 2 bytes dirección acumulador, 4 bytes longitud, resto opcode/flags).
- CPI (Ciclos por instrucción) típico: **10 a 20 ciclos** (instrucciones CISC pueden ocupar miles de ciclos).
- Pipeline de 4 etapas para instrucciones CISC.

### 4.5 Software Stack

```
Aplicación (TensorFlow, 100-1500 líneas de código)
        ↓
  Compilador TensorFlow → API común GPU/TPU
        ↓
  User Space Driver (sets up TPU, formatea datos, compila modelo,
                     traduce API calls → instrucciones TPU)
        ↓
  Kernel Driver (memory management + interrupts, estabilidad a largo plazo)
        ↓
  Hardware TPU
```

- El modelo se compila **la primera vez** que se evalúa; las evaluaciones siguientes corren a máxima velocidad.
- El TPU ejecuta modelos **completos** de entrada a salida, minimizando interacciones con el host.

### 4.6 Plataformas Comparadas

| Parámetro | Haswell CPU | NVIDIA K80 | TPU |
|---|---|---|---|
| Die size | ~662 mm² | ~561 mm² | ≤ mitad de Haswell |
| Proceso | 22 nm | 28 nm | 28 nm |
| Frecuencia | 2300 MHz | 560 MHz | 700 MHz |
| TDP | 145 W | 150 W/die | 75 W |
| Peak TOPS (8-bit) | 2.6 | N/A | **92** |
| On-chip memory | 51 MiB | 8 MiB | **28 MiB** |
| Memory BW | 51 GB/s | 160 GB/s | 34 GB/s |

---

## 5. Restricciones y Tradeoffs de Diseño

### 5.1 Rendimiento

#### Comparativa Principal

| Aplicación | GPU/CPU (ratio relativo) | TPU/CPU (ratio relativo) |
|---|---|---|
| MLP0 | 2.5× | 41.0× |
| MLP1 | 0.3× | 18.5× |
| LSTM0 | 0.4× | 3.5× |
| LSTM1 | 1.2× | 1.2× |
| CNN0 | 1.6× | 40.3× |
| CNN1 | 2.7× | 71.0× |
| **Media Ponderada** | **1.9×** | **29.2×** |

> El TPU es en promedio **~15× más rápido** que la K80 GPU y **~29× más rápido** que la Haswell CPU, usando la media ponderada por la mezcla real del workload.

#### Análisis Roofline

El modelo Roofline, adaptado para enteros con intensidad operacional = ops/byte de peso leído, revela:

- **MLPs y LSTMs**: Memory-bound (limitados por ancho de banda de memoria). Residen en la parte inclinada del roofline.
- **CNNs**: Compute-bound (limitados por capacidad de cómputo). Cerca del techo horizontal.
- **Ridge point del TPU**: ~1350 ops/byte (muy a la derecha, gracias al enorme número de MACs).
- **K80**: Ridge point a 9 ops/byte; **Haswell**: Ridge point a 13 ops/byte.

#### Latencia P99: Ventaja del Modelo Determinístico

| Plataforma | Batch | Latencia P99 | Inf/s | % del Máximo IPS |
|---|---|---|---|---|
| CPU (Haswell) | 16 | 7.2 ms | 5,482 | **42%** |
| GPU (K80) | 16 | 6.7 ms | 13,461 | **37%** |
| **TPU** | **200** | **7.0 ms** | **225,000** | **80%** |

> El TPU opera al 80% de su throughput máximo respetando el límite de 7 ms, mientras que la CPU y la GPU deben usar lotes pequeños (degradándose al 37-42%). Esto demuestra que la ausencia de caché, branch prediction y out-of-order execution no es una desventaja para inferencia: es una **virtud**.

#### Limitaciones de Rendimiento (Tabla 3 del paper)

| Factor | MLP0 | MLP1 | LSTM0 | LSTM1 | CNN0 | CNN1 | Media |
|---|---|---|---|---|---|---|---|
| Array active cycles | 12.7% | 10.6% | 8.2% | 10.5% | 78.2% | 46.2% | 28% |
| Useful MACs (% peak) | 12.5% | 9.4% | 8.2% | 6.3% | 78.2% | 22.5% | 23% |
| Weight stall cycles | 53.9% | 44.2% | 58.1% | 62.1% | 0.0% | 28.1% | **43%** |

> El principal cuello de botella para MLPs y LSTMs es la espera de pesos desde memoria (~43% de los ciclos en stall). Esto confirma que el TPU es **memory-bound para la mayoría del workload**.

---

### 5.2 Consumo Energético

#### Eficiencia Energética

| Comparación | Total Perf/Watt (GM) | Total Perf/Watt (WM) | Incremental Perf/Watt (WM) |
|---|---|---|---|
| GPU vs CPU | 1.2× | 2.1× | 2.9× |
| **TPU vs CPU** | **17×** | **34×** | **83×** |
| TPU vs GPU | 14× | 16× | 29× |

- El TPU usa **solo el 8-bit aritmético entero** vs los 32-bit flotantes del GPU, consiguiendo ~6× menos energía por operación y ~6× menos área.
- Empaqueta **25× más MACs** que la K80 (65,536 8-bit vs 2,496 32-bit) y usa **menos de la mitad de la potencia**.

#### Proporcionalidad Energética (Energy Proportionality)

Un problema clave del TPU es su **pobre proporcionalidad energética**:

- Al **10% de carga**: usa el **88%** de la potencia a plena carga.
- Comparado con Haswell al 10% de carga: usa solo el 56% de la potencia.
- K80 al 10% de carga: usa el 66%.

> **Razón:** La agenda de diseño de 15 meses impidió incluir muchas características de ahorro de energía.

**Implicación práctica:** El costo de electricidad (basado en consumo promedio diario) es subóptimo para el TPU en cargas variables. En instalaciones de datacenter donde los servidores rara vez llegan al 100%, esto importa. [Bar07]

---

### 5.3 Escalabilidad

#### Escalabilidad Vertical (dentro del die)

Se evaluaron variaciones en los parámetros clave (Figura 11 del paper):

| Parámetro (escala 4×) | Impacto en MLPs/LSTMs | Impacto en CNNs |
|---|---|---|
| **Ancho de banda de memoria** | **+3× rendimiento** (mayor impacto) | poco beneficio |
| **Clock rate** | casi ningún beneficio | +2× rendimiento |
| **Tamaño de la matriz** | degradación (fragmentación interna) | degradación |

> **Hallazgo contraintuitivo:** Aumentar el Matrix Unit de 256×256 a 512×512 **degrada** el rendimiento promedio. Un LSTM con capas de 600×600 requiere 9 tiles con la matriz de 256 (18 μs), pero solo 4 tiles con la de 512 × cada tile tarda 4× más = 32 μs. Es análogo a la fragmentación interna en páginas de memoria, pero en 2D.

#### Escalabilidad Horizontal (múltiples dies)

- Hasta **4 TPU dies por servidor** (servidor de benchmark).
- El servidor con 4 TPUs usa menos del 20% de potencia adicional vs Haswell solo, pero ejecuta CNN0 **80 veces más rápido**.
- Los 8 GiB de DRAM por TPU soportan múltiples modelos activos simultáneamente.

#### TPU' (Diseño Hipotético Mejorado)

Con el mismo proceso de 28 nm pero más tiempo de diseño:

- **+50% clock** (síntesis más agresiva): mínimo impacto en rendimiento.
- **GDDR5 en lugar de DDR3** (5× más ancho de banda): mejora 2.6× (media geométrica), 3.9× (media ponderada).
- **Ambos juntos**: mejora 2.9×/3.9×.
- GDDR5 requiere expandir el die ~10%, pero reducir el UB de 24 a 14 MiB recupera ese área.
- **Performance/Watt mejorado**: hasta 196× sobre Haswell (incremental WM).

---

### 5.4 Complejidad

#### Simplicidad como Ventaja de Diseño

| Característica | Decisión de Diseño | Impacto |
|---|---|---|
| Sin caché on-chip | Eliminado deliberadamente | Menor área, latencia determinística |
| Sin branch prediction | No aplica (pocas ramas) | Menor control, menor área |
| Sin out-of-order execution | Modelo CISC secuencial | Menor control (2% del die) |
| Sin multithreading | Single-threaded | Latencia P99 predecible |
| ISA CISC (~12 instrucciones) | Pocas instrucciones complejas | Amortizan latencia PCIe |
| 4-stage pipeline | Simple | Fácil de verificar y depurar |

> **El control ocupa solo el 2% del die**, comparado con porcentajes mucho mayores en CPU y GPU. Esto se tradujo en un die relativamente pequeño (≤ mitad de Haswell) a pesar de tener más MACs y más memoria on-chip.

#### Complejidad del Desarrollo

- **15 meses** de design-to-deployment (diseño, verificación [Ste15], fabricación, despliegue).
- Conectado por PCIe (como GPU) → sin modificar la infraestructura de servidores.
- Host envía instrucciones → sin lógica de fetch compleja en el TPU.
- Omisión deliberada de soporte para matrices sparse (postergado a futuras versiones).

---

### 5.5 Flexibilidad

#### Limitaciones de Flexibilidad

El diseño altamente especializado introduce restricciones importantes:

| Aspecto | Limitación |
|---|---|
| **Solo inferencia** | No soporta entrenamiento (requiere FP32/FP16 y backpropagation) |
| **Solo matrices densas** | Sparse support fue omitido por la agenda de tiempo |
| **ISA fijo** | No reprogramable como un FPGA |
| **Dependencia del host** | Requiere un servidor CPU como host (PCIe coprocessor) |
| **Cuantización necesaria** | Las aplicaciones deben ser cuantizadas a INT8 |
| **Batch sizes grandes** | Necesita lotes suficientemente grandes para amortizar el acceso a pesos |

#### Mecanismos de Flexibilidad Incluidos

A pesar de las restricciones, el TPU incluye flexibilidad en puntos clave:

- **TensorFlow compatible**: las aplicaciones no necesitan reescribirse.
- **Soporta MLP, CNN y LSTM**: los 3 tipos de NN más populares.
- **Flexible en tamaño de lote**: B×256 entrada variable.
- **Funciones de activación configurables**: ReLU, Sigmoid, tanh, pooling.
- El objetivo era ser "flexible enough to match the NN needs of 2015 and beyond" [Jouppi et al., 2017].

---

## 6. Herramientas y Metodologías de Exploración

### 6.1 Modelo Roofline Adaptado

El **Roofline Performance Model** [Williams et al., 2009; R3] fue adaptado específicamente para el TPU:

**Modificaciones respecto al modelo original:**
1. **Eje Y**: FLOPS/sec → **Integer Operations/sec** (TOPS).
2. **Eje X**: Intensidad operacional = **integer ops / byte de peso leído** (en lugar de FLOPS/byte de DRAM accedido).

**Uso del modelo:**
- Identificar si cada aplicación es *compute-bound* o *memory-bound*.
- El ridge point (punto de quiebre) indica la intensidad operacional necesaria para saturar el hardware.
- El gap entre el punto de operación real y el techo del roofline cuantifica el potencial de mejora.
- Permitió identificar que **4 de 6 aplicaciones eran memory-bound** → ancho de banda de memoria es el bottleneck más crítico.

> Los modelos Roofline de CPU (ridge point 13 ops/byte), K80 (9 ops/byte) y TPU (1350 ops/byte) se comparan directamente en el paper, revelando que el TPU tiene un ridge point muy a la derecha gracias a sus 65,536 MACs.

### 6.2 Modelo de Rendimiento del TPU

Se desarrolló un **modelo de rendimiento analítico** para el TPU:

- Error promedio respecto a los contadores de hardware reales: **<10%** (máximo 11.2%, mínimo 5.4%).
- Se usó para evaluar diseños alternativos (TPU') sin necesidad de fabricar nuevos chips.
- Permitió explorar el espacio de diseño variando: ancho de banda de memoria, clock rate, tamaño de acumuladores, dimensiones del Matrix Unit.

### 6.3 Contadores de Rendimiento Hardware

El TPU incluye **106 contadores de rendimiento** instrumentados en el hardware para dar visibilidad operacional:

- Ciclos activos del array, MACs útiles, stalls por pesos, stalls por datos de entrada, etc.
- Permiten diagnosticar cuellos de botella (Tabla 3 del paper).
- Los autores sugieren que se necesitarían más ("if anything we would like a few more").

### 6.4 Benchmarking en Producción

La metodología experimental es notable por su rigurosidad:

- **6 aplicaciones de producción** reales (no benchmarks sintéticos) que representan el 95% del workload.
- Misma infraestructura de datacenter para los 3 procesadores comparados.
- Se midió tiempo de respuesta P99 (no solo throughput pico).
- Se incluyó el overhead del servidor host en las métricas finales.
- Plataformas con requisitos de integridad de datos iguales (SECDED en SRAM y DRAM).

### 6.5 Verificación y Simulación

- **Simulación de chip completo** [Steinberg, 2015; R4] para verificación pre-silicon.
- TensorFlow como plataforma de compilación y portabilidad.

---

## 7. Aplicaciones Prácticas y Relevancia Actual

### 7.1 Aplicaciones en 2015-2017 (Momento del Despliegue)

El TPU fue desplegado en los datacenters de Google desde **2015** para acelerar:

| Aplicación | Modelo | Tipo de Red |
|---|---|---|
| Búsqueda por voz | DNNs de reconocimiento de habla | MLP/RNN |
| Reconocimiento de imágenes | Inception V2 | CNN |
| Go (AlphaGo) | DeepMind AlphaGo | CNN |
| Traducción automática | GNM Translate (Google Neural Machine Translation) | LSTM |
| Ranking de búsqueda | RankBrain | MLP |

> **30% de reducción en tasas de error** en reconocimiento de voz; **error de imagen de 3.5%** (desde 26% en 2011).

### 7.2 Impacto Científico

- El paper fue presentado en **ISCA 2017**, la conferencia más prestigiosa en arquitectura de computadores.
- **Más de 6,000 citaciones** (Google Scholar, 2026), convirtiéndolo en una de las publicaciones más influyentes en la historia de la arquitectura de computadores.
- Los propios autores predijeron: *"We expect that many will build successors that will raise the bar even higher"* — predicción que se cumplió con creces.

### 7.3 Legado: La Era de las Arquitecturas de Dominio Específico

El TPU validó experimentalmente la tesis central del discurso de premiación del Turing Award de Hennessy & Patterson (2019): **"A New Golden Age for Computer Architecture"** [R5]. Este trabajo demostró que las mejoras de orden de magnitud en costo-energía-rendimiento debían venir de hardware de dominio específico (DSA).

#### Impacto en la Industria (2017-2026)

| Empresa | Chip/Acelerador | Inspiración TPU |
|---|---|---|
| Google | TPU v2, v3, v4, v5 (continuación directa) | — |
| NVIDIA | Tensor Cores (A100, H100, H200, B100) | Adopción de matrices enteras |
| Apple | Neural Engine (A-series, M-series) | DSA para inferencia móvil |
| Amazon | AWS Trainium, Inferentia | DSA para inferencia en cloud |
| Microsoft | Azure Maia 100 | Custom ASIC para ML |
| Meta | MTIA (Meta Training and Inference Accelerator) | Custom ASIC para recomendación |
| Intel | Gaudi accelerators | DSA para entrenamiento |

> Virtualmente cada empresa de cloud hoy en día tiene su propio ASIC para ML, siguiendo exactamente la lógica del TPU.

#### Evolución del TPU en Google

| Versión | Año | Novedad Principal |
|---|---|---|
| TPU v1 | 2015 | Solo inferencia, 92 TOPS (8-bit), PCIe |
| TPU v2 | 2017 | Entrenamiento + inferencia, 180 TFLOPS (BF16), HBM |
| TPU v3 | 2018 | 420 TFLOPS, refrigeración líquida, pods de 1024 chips |
| TPU v4 | 2021 | 275 TFLOPS (BF16), interconnect ICI, pods de 4096 chips |
| TPU v5 | 2023 | Múltiples variantes (v5e, v5p), escala masiva |

> **Referencias para TPU evolución:** [Jouppi et al., 2021, "Ten Lessons From Three Generations Shaped Google's TPUv4i", ISCA 2021; R6]

---

## 8. Conclusiones del Grupo

### 8.1 Ventajas del Enfoque Propuesto

1. **Rendimiento excepcional para el dominio**: 15-30× sobre GPU y CPU contemporáneas en el caso de uso objetivo (inferencia NN).

2. **Eficiencia energética superior**: 30-80× mejor TOPS/Watt, con implicaciones directas en el costo total de propiedad (TCO) del datacenter.

3. **Latencia determinística y predecible**: El modelo de ejecución simple (sin caché, sin OoO) garantiza cumplimiento de SLAs en P99, algo que ni CPU ni GPU podían lograr a la misma eficiencia.

4. **Velocidad de desarrollo industrial**: 15 meses de idea a despliegue en producción, gracias a la filosofía de "reutilizar lo existente donde sea posible" (PCIe, DDR3, TensorFlow, 28nm).

5. **Validación empírica rigurosa**: El paper mide sobre workloads de producción real, no benchmarks sintéticos, dándole credibilidad excepcional.

6. **"Cornucopia Corollary" a la Ley de Amdahl**: Demostró que baja utilización de un recurso enorme (65,536 MACs) puede ser suficiente para lograr alto rendimiento costo-efectivo. Una baja utilización de un número grande sigue siendo grande.

### 8.2 Limitaciones del Enfoque

1. **Inflexibilidad fundamental**: El diseño es excelente para matrices densas de 8 bits en inferencia, pero inadecuado para:
   - Entrenamiento (requiere FP32/BF16).
   - Modelos sparse (matrices ralas, cada vez más comunes post-2017).
   - Cuantización más allá de INT8.
   - Nuevas arquitecturas (Transformers, MoE, etc. no fueron considerados en el diseño original).

2. **Pobre proporcionalidad energética**: 88% de potencia al 10% de carga es un problema real en datacenters donde la carga varía durante el día. El TPU desperdicia energía en períodos de baja demanda.

3. **Memory-bound en la mayoría del workload**: 4 de 6 aplicaciones están limitadas por el ancho de banda de memoria (DDR3 a 34 GB/s). Ironicamente, la solución obvia (GDDR5, 5× más ancho de banda) no se implementó por la restricción de tiempo de 15 meses.

4. **Dependencia del host**: Al ser un coprocesador PCIe, el TPU no puede operar de forma autónoma. El tiempo de host CPU puede representar 11-76% del tiempo de ejecución (Tabla 5 del paper), erosionando las ganancias.

5. **Fragmentación para ciertos tamaños de capa**: El Matrix Unit de 256×256 puede ser ineficiente para capas con dimensiones que no sean múltiplos de 256 (ej. 600×600 requiere tiling costoso).

6. **No hay soporte para datos sparse**: Una omisión explícita por tiempo que se volvió relevante con técnicas como pruning y quantization-aware training que generan modelos más sparse.

### 8.3 Sobre la Metodología Top-Down

La metodología **top-down dominante** con **reutilización bottom-up** fue apropiada para este contexto por razones específicas:

- Los **requisitos de la aplicación estaban bien definidos** (workload conocido, límites de latencia claros, meta cuantitativa de 10×).
- El **tiempo era crítico** (15 meses no permiten exploración exhaustiva del espacio de diseño de abajo hacia arriba).
- La **compatibilidad con TensorFlow** era no negociable, lo que forzó el punto de entrada top-down.

Sin embargo, la misma rigidez del enfoque top-down es responsable de algunas limitaciones: al diseñar exactamente para el workload de 2013-2015, el TPU v1 no fue adaptable a los modelos que emergieron después (Transformers en 2017, por ejemplo). Las generaciones posteriores del TPU (v2+) requirieron rediseños significativos precisamente por esta razón.

> **Lección de diseño**: El enfoque top-down es eficiente cuando los requisitos son estables y conocidos. En dominios de rápida evolución como el ML, se necesita mayor flexibilidad (o iteraciones rápidas del hardware).

---

## 9. Referencias

### Referencia Principal
- **[R1]** Jouppi, N.P., Young, C., Patil, N., Patterson, D., et al. (2017). *In-Datacenter Performance Analysis of a Tensor Processing Unit*. Proceedings of the 44th International Symposium on Computer Architecture (ISCA), Toronto, Canada, June 26, 2017. Google, Inc. (**Paper asignado**)

### Referencias Citadas en el Paper (selección relevante)
- **[R2]** Kung, H.T. & Leiserson, C.E. (1980). *Algorithms for VLSI Processor Arrays*. Introduction to VLSI Systems. *(Base teórica del systolic array)*
- **[R3]** Williams, S., Waterman, A., & Patterson, D. (2009). *Roofline: An Insightful Visual Performance Model for Multicore Architectures*. Communications of the ACM, 52(4), 65-76. *(Modelo Roofline original)*
- **[R4]** Steinberg, D. (2015). *Full-Chip Simulations, Keys to Success*. Proceedings of the Synopsys Users Group (SNUG) Silicon Valley 2015. *(Verificación pre-silicon del TPU)*
- **[R5]** Hennessy, J.L. & Patterson, D.A. (2019). *A New Golden Age for Computer Architecture*. Communications of the ACM, 62(2), 48-60. *(Contexto general de DSAs, Turing Award Lecture)*

### Referencias Externas Adicionales
- **[R6]** Jouppi, N.P. et al. (2021). *Ten Lessons From Three Generations Shaped Google's TPUv4i*. Proceedings of the 48th International Symposium on Computer Architecture (ISCA). *(Evolución del TPU)*
- **[R7]** Hennessy, J.L. & Patterson, D.A. (2018). *Computer Architecture: A Quantitative Approach*, 6th Edition. Elsevier. *(Referencia estándar del curso, mencionada como [Hen18] en el paper)*
- **[R8]** Dean, J. (2016). *Large-Scale Deep Learning with TensorFlow for Building Intelligent Systems*. ACM Webinar, July 7, 2016. *(Contexto del impacto del DL en Google)*
- **[R9]** Dally, W. (2016). *High Performance Hardware for Machine Learning*. Cadence ENN Summit, February 9, 2016. *(Referencia sobre eficiencia energética INT8 vs FP16)*

---
